// vm.c - runs compiled bytecode a few instructions at a time. See vm.h.
//
// The game calls Vm_Run() every frame with a budget of instructions. The VM
// runs until the budget is used up, the code calls move()/write() (then it
// waits for the animation), it finishes, or it hits a runtime error.
//
// Every memory access is checked, so bugs that crash (or silently corrupt)
// a real C program become clear messages and in-game hazards instead.

#include "vm_internal.h"
#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HOST_NAMES   32
#define MAX_WARNED       32
#define PRINT_MAX        1024

typedef struct {
    int func;
    int returnPc;
    int fp;             // first memory cell of this frame
} Frame;

struct Vm {
    Program   prog;
    VmHost    host;
    VmOptions opt;

    int      *mem;
    Cell     *cells;

    int       stack[VM_STACK_MAX];
    int       sp;

    Frame     frames[VM_CALL_DEPTH_MAX];
    int       frameCount;

    int       pc;
    int       memTop;           // first free cell above the current frame
    int       hostStart, hostTop, stackStart;
    char      hostNames[MAX_HOST_NAMES][VM_VAR_NAME_MAX];
    int       hostNameCount;

    VmStatus  status;
    int       line;
    int       returnValue;
    bool      pauseAtLine;
    bool      scriptDone;
    long long instructionCount;

    int       pendingFunc;      // function to call once the loose statements have run
    int       pendingArgs[VM_MAX_PARAMS];
    int       pendingArgc;

    VmErrorKind errorKind;
    char        errorMsg[VM_MSG_MAX];
    int         errorLine;

    // Warn about each uninitialised cell only once
    int       warnedAddr[MAX_WARNED];
    int       warnedCount;

    // Recent memory accesses, for the game to show
    int       reads[VM_MAX_ACCESSES], readCount;
    int       writes[VM_MAX_ACCESSES], writeCount;
    VmAccess  log[VM_MAX_LOG];      // reads and writes in order
    int       logCount;
};

static void Remember(int *list, int *count, int addr)
{
    if (*count == VM_MAX_ACCESSES)
    {
        memmove(list, list + 1, sizeof(int) * (VM_MAX_ACCESSES - 1));
        (*count)--;
    }
    list[(*count)++] = addr;
}

static void Log(Vm *vm, int addr, bool write)
{
    if (vm->logCount == VM_MAX_LOG)
    {
        memmove(vm->log, vm->log + 1, sizeof(VmAccess) * (VM_MAX_LOG - 1));
        vm->logCount--;
    }
    vm->log[vm->logCount].address = addr;
    vm->log[vm->logCount].write = write;
    vm->log[vm->logCount].condition = false;
    vm->log[vm->logCount].answer = 0;
    vm->logCount++;
}

static void LogCondition(Vm *vm, int answer)
{
    Log(vm, -1, false);
    vm->log[vm->logCount - 1].condition = true;
    vm->log[vm->logCount - 1].answer = answer;
}

// ===========================================================================
// Errors and warnings
// ===========================================================================
static void RuntimeError(Vm *vm, VmErrorKind kind, const char *fmt, ...)
{
    if (vm->status == VM_ERROR) return;

    char msg[VM_MSG_MAX - 20];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);

    vm->status    = VM_ERROR;
    vm->errorKind = kind;
    vm->errorLine = vm->line;
    snprintf(vm->errorMsg, VM_MSG_MAX, "Line %d: %s", vm->line, msg);
}

static void Warn(Vm *vm, const char *fmt, ...)
{
    char msg[VM_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);

    if (vm->host.warning) vm->host.warning(vm->host.user, vm->line, msg);
}

static void Print(Vm *vm, const char *text)
{
    if (vm->host.print) vm->host.print(vm->host.user, text);
}

// ===========================================================================
// Memory
// ===========================================================================

// "arr[2]" or "x" for a cell
static void CellName(const Vm *vm, int addr, char *out, int size)
{
    if (addr == 0)                              { snprintf(out, size, "NULL"); return; }
    if (addr < 0 || addr >= VM_MEM_SIZE)        { snprintf(out, size, "(invalid)"); return; }
    const Cell *c = &vm->cells[addr];
    if (c->state != CELL_USED || !c->owner)     { snprintf(out, size, "(invalid)"); return; }
    if (c->isArray) snprintf(out, size, "%s[%d]", c->owner, addr - c->base);
    else            snprintf(out, size, "%s", c->owner);
}

// Is this address OK to read/write? If not, raise a friendly segfault.
static bool CheckAddress(Vm *vm, int addr, bool write)
{
    if (addr == 0)
    {
        RuntimeError(vm, VM_ERR_SEGFAULT,
                     "Segmentation fault! You used a NULL pointer. It doesn't point at anything yet.");
        return false;
    }
    if (addr < 0 || addr >= VM_MEM_SIZE)
    {
        RuntimeError(vm, VM_ERR_SEGFAULT,
                     "Segmentation fault! Address %d is outside the machine's memory.", addr);
        return false;
    }

    const Cell *c = &vm->cells[addr];
    if (c->state == CELL_USED)
    {
        if (write && c->readonly)
        {
            RuntimeError(vm, VM_ERR_READ_ONLY,
                         "you can't change a string literal. Copy it into a char array first, "
                         "e.g. char s[] = \"...\";");
            return false;
        }
        return true;
    }

    if (c->state == CELL_GUARD)
    {
        if (c->isArray)
            RuntimeError(vm, VM_ERR_SEGFAULT,
                         "Segmentation fault! You went past the end of %s: %s[%d] doesn't exist.%s",
                         c->owner, c->owner, addr - c->base,
                         (c->owner && strcmp(c->owner, "a string literal") != 0) ? ""
                         : " Is the string's '\\0' missing?");
        else
            RuntimeError(vm, VM_ERR_SEGFAULT,
                         "Segmentation fault! You went past the end of %s.", c->owner ? c->owner : "a variable");
        return false;
    }

    if (c->owner)
        RuntimeError(vm, VM_ERR_SEGFAULT,
                     "Segmentation fault! That memory belonged to %s, which no longer exists because "
                     "its function has returned (a dangling pointer).", c->owner);
    else
        RuntimeError(vm, VM_ERR_SEGFAULT,
                     "Segmentation fault! Address %d doesn't belong to your program.", addr);
    return false;
}

// What an uninitialised variable "contains": a fixed junk number per address
static int Garbage(int addr)
{
    unsigned h = (unsigned)addr * 2654435761u;
    return (int)((h >> 17) & 0x7FFF) + 1;
}

static int ReadCell(Vm *vm, int addr)
{
    Remember(vm->reads, &vm->readCount, addr);
    Log(vm, addr, false);
    Cell *c = &vm->cells[addr];
    if (c->init) return vm->mem[addr];

    int junk = Garbage(addr);

    bool warned = false;
    for (int i = 0; i < vm->warnedCount; i++)
        if (vm->warnedAddr[i] == addr) warned = true;

    if (!warned)
    {
        if (vm->warnedCount < MAX_WARNED) vm->warnedAddr[vm->warnedCount++] = addr;
        char name[VM_VAR_NAME_MAX];
        CellName(vm, addr, name, sizeof name);
        Warn(vm, "%s is used before it's been given a value, so it holds garbage (%d this time). "
                 "Give it a starting value, e.g. = 0.", name, junk);
    }
    return junk;
}

static void WriteCell(Vm *vm, int addr, int value)
{
    Remember(vm->writes, &vm->writeCount, addr);
    Log(vm, addr, true);
    vm->mem[addr] = value;
    vm->cells[addr].init = 1;
}

// Copy a '\0'-terminated string out of VM memory (checked)
static bool ReadString(Vm *vm, int addr, char *out, int size)
{
    int len = 0;
    for (;;)
    {
        if (!CheckAddress(vm, addr + len, false)) return false;
        int ch = ReadCell(vm, addr + len);
        if (ch == 0) break;
        if (len < size - 1) out[len] = (char)ch;
        len++;
        if (len > 4000)
        {
            RuntimeError(vm, VM_ERR_SEGFAULT, "this string never ends. Is its '\\0' missing?");
            return false;
        }
    }
    out[len < size - 1 ? len : size - 1] = '\0';
    return true;
}

// ===========================================================================
// Value stack
// ===========================================================================
static bool Push(Vm *vm, int v)
{
    if (vm->sp >= VM_STACK_MAX)
    {
        RuntimeError(vm, VM_ERR_OTHER, "this expression is too complicated.");
        return false;
    }
    vm->stack[vm->sp++] = v;
    return true;
}

static int Pop(Vm *vm)
{
    if (vm->sp <= 0) return 0;   // can't happen with correct bytecode
    return vm->stack[--vm->sp];
}

// ===========================================================================
// Function calls
// ===========================================================================
static bool EnterFunction(Vm *vm, int fi, int returnPc, const int *args, int argc)
{
    const Function *f = &vm->prog.funcs[fi];

    if (vm->frameCount >= VM_CALL_DEPTH_MAX || vm->memTop + f->frameSize + 1 >= VM_MEM_SIZE)
    {
        RuntimeError(vm, VM_ERR_STACK_OVERFLOW,
                     "Stack overflow! Too many function calls inside each other (%d deep). "
                     "Does your recursive function have a base case that stops it?", vm->frameCount);
        return false;
    }

    int fp = vm->memTop;
    for (int i = 0; i < f->frameSize; i++)
    {
        Cell *c = &vm->cells[fp + i];
        *c = f->frameTemplate[i];
        c->base += fp;
        c->init = 0;
        vm->mem[fp + i] = 0;
    }

    for (int k = 0; k < argc; k++) WriteCell(vm, fp + f->paramOffsets[k], args[k]);

    Frame *fr = &vm->frames[vm->frameCount++];
    fr->func     = fi;
    fr->returnPc = returnPc;
    fr->fp       = fp;

    vm->memTop = fp + f->frameSize;
    vm->pc     = f->entryPc;
    return true;
}

static void LeaveFunction(Vm *vm, int value)
{
    Frame fr = vm->frames[--vm->frameCount];

    // The frame's memory is freed. Cells keep their owner's name so that a
    // pointer still aimed here gets a "dangling pointer" message.
    for (int i = fr.fp; i < vm->memTop; i++) vm->cells[i].state = CELL_FREE;
    vm->memTop = fr.fp;

    if (vm->frameCount > 0)
    {
        vm->pc = fr.returnPc;
        Push(vm, value);
        return;
    }

    // The outermost call finished
    if (fr.func == 0) vm->scriptDone = true;

    if (vm->pendingFunc >= 0)
    {
        int fi = vm->pendingFunc;
        vm->pendingFunc = -1;
        EnterFunction(vm, fi, -1, vm->pendingArgs, vm->pendingArgc);
        return;
    }

    vm->returnValue = value;
    vm->status = VM_FINISHED;
}

// ===========================================================================
// Built-ins
// ===========================================================================
static void Append(char *out, int *len, const char *text)
{
    for (int i = 0; text[i] && *len < PRINT_MAX - 1; i++) out[(*len)++] = text[i];
    out[*len] = '\0';
}

static void DoPrintf(Vm *vm, const int *args, int argc)
{
    char fmt[PRINT_MAX], out[PRINT_MAX], tmp[PRINT_MAX];
    int len = 0, next = 1;
    out[0] = '\0';

    if (!ReadString(vm, args[0], fmt, sizeof fmt)) return;

    for (int i = 0; fmt[i]; i++)
    {
        if (fmt[i] != '%')
        {
            char one[2] = { fmt[i], '\0' };
            Append(out, &len, one);
            continue;
        }

        char spec = fmt[++i];
        if (spec == '\0') break;
        if (spec == '%') { Append(out, &len, "%"); continue; }

        if (next >= argc)
        {
            RuntimeError(vm, VM_ERR_BAD_ARGUMENT, "printf needs more values for its placeholders.");
            return;
        }
        int v = args[next++];

        switch (spec)
        {
            case 'd': case 'i': snprintf(tmp, sizeof tmp, "%d", v); Append(out, &len, tmp); break;
            case 'c': tmp[0] = (char)v; tmp[1] = '\0'; Append(out, &len, tmp); break;
            case 's':
                if (!ReadString(vm, v, tmp, sizeof tmp)) return;
                Append(out, &len, tmp);
                break;
            default:
                snprintf(tmp, sizeof tmp, "%%%c", spec);
                Append(out, &len, tmp);
                break;
        }
    }
    Print(vm, out);
}

static void CallBuiltin(Vm *vm, VmBuiltin id, const int *args, int argc)
{
    if (id == VM_BUILTIN_PRINTF)
    {
        DoPrintf(vm, args, argc);
        Push(vm, 0);
        return;
    }

    if ((id == VM_BUILTIN_MOVE || id == VM_BUILTIN_SCAN) && (args[0] < 0 || args[0] > 3))
    {
        RuntimeError(vm, VM_ERR_BAD_ARGUMENT, "%s() needs a direction: NORTH, EAST, SOUTH or WEST (you gave %d).",
                     id == VM_BUILTIN_MOVE ? "move" : "scan", args[0]);
        return;
    }

    int result = 0;
    bool waits = false;
    if (vm->host.callBuiltin) waits = vm->host.callBuiltin(vm->host.user, id, args, argc, &result);

    if (waits) vm->status = VM_WAITING;     // the result is pushed in Vm_ActionDone
    else       Push(vm, result);
}

// ===========================================================================
// The main loop
// ===========================================================================
static int Arith(Vm *vm, int op, int a, int b, bool *ok)
{
    *ok = true;
    switch (op)
    {
        case OP_ADD: return (int)((unsigned)a + (unsigned)b);   // wraps like real ints, without UB
        case OP_SUB: return (int)((unsigned)a - (unsigned)b);
        case OP_MUL: return (int)((unsigned)a * (unsigned)b);
        case OP_DIV:
        case OP_MOD:
            if (b == 0)
            {
                RuntimeError(vm, VM_ERR_DIV_ZERO, "Division by zero! The right side of %s is 0.",
                             op == OP_DIV ? "/" : "%");
                *ok = false;
                return 0;
            }
            if (a == (-2147483647 - 1) && b == -1) return op == OP_DIV ? a : 0;
            return op == OP_DIV ? a / b : a % b;
        case OP_EQ: return a == b;
        case OP_NE: return a != b;
        case OP_LT: return a <  b;
        case OP_GT: return a >  b;
        case OP_LE: return a <= b;
        case OP_GE: return a >= b;
    }
    return 0;
}

VmStatus Vm_Run(Vm *vm, int maxInstructions)
{
    if (vm->status == VM_PAUSED) vm->status = VM_RUNNING;
    if (vm->status != VM_RUNNING) return vm->status;

    const int *code = vm->prog.code;

    for (int n = 0; n < maxInstructions && vm->status == VM_RUNNING; n++)
    {
        if (vm->opt.maxInstructions > 0 && ++vm->instructionCount > vm->opt.maxInstructions)
        {
            RuntimeError(vm, VM_ERR_TOO_LONG,
                         "the code ran for too long (over %d steps). Is there an infinite loop?",
                         vm->opt.maxInstructions);
            break;
        }

        int op = code[vm->pc++];
        switch (op)
        {
            case OP_HALT:
                vm->status = VM_FINISHED;
                break;

            case OP_LINE:
                vm->line = code[vm->pc++];
                if (vm->pauseAtLine)
                {
                    vm->pauseAtLine = false;
                    vm->status = VM_PAUSED;
                }
                break;

            case OP_PUSH:  Push(vm, code[vm->pc++]); break;
            case OP_POP:   Pop(vm); break;
            case OP_DUP:   Push(vm, vm->stack[vm->sp - 1]); break;

            case OP_ADDR_LOCAL:
                Push(vm, vm->frames[vm->frameCount - 1].fp + code[vm->pc++]);
                break;

            case OP_ADDR_GLOBAL:
                Push(vm, code[vm->pc++]);
                break;

            case OP_LOAD:
            {
                int addr = Pop(vm);
                if (CheckAddress(vm, addr, false)) Push(vm, ReadCell(vm, addr));
                break;
            }

            case OP_STORE:
            {
                int value = Pop(vm);
                int addr  = Pop(vm);
                if (CheckAddress(vm, addr, true))
                {
                    WriteCell(vm, addr, value);
                    Push(vm, value);
                }
                break;
            }

            case OP_INC_PRE:
            case OP_INC_POST:
            {
                int delta = code[vm->pc++];
                int addr  = Pop(vm);
                if (!CheckAddress(vm, addr, true)) break;
                int oldValue = ReadCell(vm, addr);
                int newValue = (int)((unsigned)oldValue + (unsigned)delta);
                WriteCell(vm, addr, newValue);
                Push(vm, op == OP_INC_PRE ? newValue : oldValue);
                break;
            }

            case OP_INDEX:
            {
                int size   = code[vm->pc++];
                int stride = code[vm->pc++];
                int sym    = code[vm->pc++];
                int i      = Pop(vm);
                int base   = Pop(vm);
                if (size >= 0 && (i < 0 || i >= size))
                {
                    const char *name = sym >= 0 ? vm->prog.syms[sym].name : "the array";
                    RuntimeError(vm, VM_ERR_OUT_OF_BOUNDS,
                                 "Out of bounds! Index %d doesn't exist in %s. Valid indexes are 0 to %d.",
                                 i, name, size - 1);
                    break;
                }
                Push(vm, base + i * stride);
                break;
            }

            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
            case OP_EQ:  case OP_NE:  case OP_LT:  case OP_GT:  case OP_LE: case OP_GE:
            {
                int b = Pop(vm);
                int a = Pop(vm);
                bool ok;
                int r = Arith(vm, op, a, b, &ok);
                if (ok) Push(vm, r);
                break;
            }

            case OP_NEG: Push(vm, (int)(0u - (unsigned)Pop(vm))); break;
            case OP_NOT: Push(vm, !Pop(vm)); break;

            case OP_CAST_CHAR:
            {
                int v = Pop(vm);
                Push(vm, ((v + 128) & 255) - 128);
                break;
            }
            case OP_CAST_BOOL: Push(vm, Pop(vm) != 0); break;

            case OP_JUMP:
                vm->pc = code[vm->pc];
                break;

            case OP_NOTE_CONDITION:
                LogCondition(vm, vm->stack[vm->sp - 1] != 0);
                break;

            case OP_JUMP_IF_FALSE:
            {
                int target = code[vm->pc++];
                if (!Pop(vm)) vm->pc = target;
                break;
            }

            case OP_JUMP_IF_TRUE:
            {
                int target = code[vm->pc++];
                if (Pop(vm)) vm->pc = target;
                break;
            }

            case OP_CALL:
            {
                int fi   = code[vm->pc++];
                int argc = code[vm->pc++];
                int args[VM_MAX_PARAMS];
                for (int k = argc - 1; k >= 0; k--) args[k] = Pop(vm);
                EnterFunction(vm, fi, vm->pc, args, argc);
                break;
            }

            case OP_CALL_BUILTIN:
            {
                int id   = code[vm->pc++];
                int argc = code[vm->pc++];
                int args[VM_MAX_PARAMS];
                for (int k = argc - 1; k >= 0; k--) args[k] = Pop(vm);
                CallBuiltin(vm, (VmBuiltin)id, args, argc);
                break;
            }

            case OP_RET:
                LeaveFunction(vm, Pop(vm));
                break;

            case OP_NO_RETURN:
                RuntimeError(vm, VM_ERR_NO_RETURN,
                             "%s() reached its closing } without returning a value. "
                             "Make sure every path ends with a return.",
                             vm->prog.funcs[vm->frames[vm->frameCount - 1].func].name);
                break;

            case OP_PRINT_INT:
            {
                char buf[16];
                snprintf(buf, sizeof buf, "%d", Pop(vm));
                Print(vm, buf);
                break;
            }

            case OP_PRINT_CHAR:
            {
                char buf[2] = { (char)Pop(vm), '\0' };
                Print(vm, buf);
                break;
            }

            case OP_PRINT_STR:
            {
                char buf[PRINT_MAX];
                if (ReadString(vm, Pop(vm), buf, sizeof buf)) Print(vm, buf);
                break;
            }

            default:
                RuntimeError(vm, VM_ERR_OTHER, "internal error: unknown instruction %d.", op);
                break;
        }
    }
    return vm->status;
}

// ===========================================================================
// Public API
// ===========================================================================
Vm *Vm_Build(const char *source, const VmOptions *options, const VmHost *host, VmBuildResult *result)
{
    memset(result, 0, sizeof *result);

    Ast ast;
    ParseResult pr;
    bool parsed = Parser_ParseSource(source, &ast, &pr);

    result->warningCount = pr.warningCount;
    for (int i = 0; i < pr.warningCount; i++)
    {
        memcpy(result->warnings[i], pr.warnings[i], VM_MSG_MAX);
        result->warningLines[i] = pr.warningLines[i];
    }

    if (!parsed)
    {
        snprintf(result->message, VM_MSG_MAX, "%s", pr.errorMsg);
        result->line = pr.errorLine;
        result->col = pr.errorCol;
        return NULL;
    }

    Vm *vm = calloc(1, sizeof(Vm));
    if (!vm)
    {
        Ast_Free(&ast);
        snprintf(result->message, VM_MSG_MAX, "out of memory.");
        return NULL;
    }

    if (options) vm->opt = *options;
    if (host)    vm->host = *host;

    bool compiled = Compiler_Compile(&ast, &vm->opt, &vm->prog, result);
    Ast_Free(&ast);
    if (!compiled)
    {
        Program_Free(&vm->prog);
        free(vm);
        return NULL;
    }

    vm->mem   = calloc(VM_MEM_SIZE, sizeof(int));
    vm->cells = calloc(VM_MEM_SIZE, sizeof(Cell));
    if (!vm->mem || !vm->cells)
    {
        Vm_Destroy(vm);
        snprintf(result->message, VM_MSG_MAX, "out of memory.");
        return NULL;
    }

    vm->hostStart  = vm->prog.staticEnd;
    vm->hostTop    = vm->hostStart;
    vm->stackStart = vm->hostStart + VM_HOST_REGION;
    vm->memTop     = vm->stackStart;
    vm->status     = VM_READY;
    vm->pendingFunc = -1;

    memcpy(vm->mem, vm->prog.staticValues, (size_t)vm->prog.staticEnd * sizeof(int));
    memcpy(vm->cells, vm->prog.staticCells, (size_t)vm->prog.staticEnd * sizeof(Cell));

    result->ok = true;
    return vm;
}

void Vm_Destroy(Vm *vm)
{
    if (!vm) return;
    Program_Free(&vm->prog);
    free(vm->mem);
    free(vm->cells);
    free(vm);
}

static int FindFunc(const Vm *vm, const char *name)
{
    for (int i = 1; i < vm->prog.funcCount; i++)
        if (strcmp(vm->prog.funcs[i].name, name) == 0) return i;
    return -1;
}

bool Vm_HasFunction(const Vm *vm, const char *name)
{
    int fi = FindFunc(vm, name);
    return fi >= 0 && vm->prog.funcs[fi].defined;
}

static const char *HostName(Vm *vm, const char *name)
{
    if (vm->hostNameCount >= MAX_HOST_NAMES) return "test data";
    char *slot = vm->hostNames[vm->hostNameCount++];
    snprintf(slot, VM_VAR_NAME_MAX, "%s", name ? name : "test data");
    return slot;
}

int Vm_AllocArray(Vm *vm, const char *name, const int *values, int count)
{
    if (count < 1 || vm->hostTop + count + 1 > vm->stackStart) return 0;

    int addr = vm->hostTop;
    const char *owner = HostName(vm, name);
    for (int i = 0; i <= count; i++)
    {
        Cell *c = &vm->cells[addr + i];
        memset(c, 0, sizeof *c);
        c->state   = (i < count) ? CELL_USED : CELL_GUARD;
        c->init    = 1;
        c->owner   = owner;
        c->base    = addr;
        c->isArray = 1;
        vm->mem[addr + i] = (i < count && values) ? values[i] : 0;
    }
    vm->hostTop += count + 1;
    return addr;
}

int Vm_AllocString(Vm *vm, const char *name, const char *text, int capacity)
{
    int len = (int)strlen(text);
    if (capacity < len + 1) capacity = len + 1;
    if (vm->hostTop + capacity + 1 > vm->stackStart) return 0;

    int *values = calloc((size_t)capacity, sizeof(int));
    if (!values) return 0;
    for (int i = 0; i < len; i++) values[i] = (unsigned char)text[i];
    int addr = Vm_AllocArray(vm, name, values, capacity);
    free(values);
    return addr;
}

int Vm_ReadMemory(const Vm *vm, int address)
{
    if (address <= 0 || address >= VM_MEM_SIZE) return 0;
    return vm->mem[address];
}

bool Vm_Start(Vm *vm, const char *functionName, const int *args, int argc)
{
    // Fresh globals, string literals and stack (test data in the host region is kept)
    memcpy(vm->mem, vm->prog.staticValues, (size_t)vm->prog.staticEnd * sizeof(int));
    memcpy(vm->cells, vm->prog.staticCells, (size_t)vm->prog.staticEnd * sizeof(Cell));
    memset(vm->cells + vm->stackStart, 0, (size_t)(VM_MEM_SIZE - vm->stackStart) * sizeof(Cell));

    vm->sp = 0;
    vm->frameCount = 0;
    vm->memTop = vm->stackStart;
    vm->status = VM_RUNNING;
    vm->line = 0;
    vm->returnValue = 0;
    vm->pauseAtLine = false;
    vm->scriptDone = false;
    vm->instructionCount = 0;
    vm->warnedCount = 0;
    vm->errorKind = VM_ERR_NONE;
    vm->errorMsg[0] = '\0';
    vm->errorLine = 0;
    vm->pendingFunc = -1;
    vm->pendingArgc = 0;

    if (functionName)
    {
        int fi = FindFunc(vm, functionName);
        if (fi < 0 || !vm->prog.funcs[fi].defined)
        {
            RuntimeError(vm, VM_ERR_OTHER, "there's no function called %s(). Check the spelling.", functionName);
            return false;
        }
        if (argc != vm->prog.funcs[fi].paramCount || argc > VM_MAX_PARAMS)
        {
            RuntimeError(vm, VM_ERR_OTHER, "%s() should take %d value%s.", functionName,
                         vm->prog.funcs[fi].paramCount, argc == 1 ? "" : "s");
            return false;
        }
        vm->pendingFunc = fi;
        vm->pendingArgc = argc;
        for (int k = 0; k < argc; k++) vm->pendingArgs[k] = args[k];
    }
    else if (vm->prog.mainFunc >= 0 && vm->prog.funcs[vm->prog.mainFunc].defined &&
             vm->prog.funcs[vm->prog.mainFunc].paramCount == 0)
    {
        vm->pendingFunc = vm->prog.mainFunc;
    }

    // Always run the loose statements first: they set up the global variables
    return EnterFunction(vm, 0, -1, NULL, 0);
}

void Vm_ActionDone(Vm *vm, int result)
{
    if (vm->status != VM_WAITING) return;
    vm->status = VM_RUNNING;
    Push(vm, result);
}

void Vm_PauseAtNextLine(Vm *vm) { vm->pauseAtLine = true; }

void Vm_TakeAccesses(Vm *vm, int *reads, int *readCount, int *writes, int *writeCount)
{
    *readCount = vm->readCount;
    *writeCount = vm->writeCount;
    memcpy(reads, vm->reads, sizeof(int) * (size_t)vm->readCount);
    memcpy(writes, vm->writes, sizeof(int) * (size_t)vm->writeCount);
    vm->readCount = vm->writeCount = 0;
    vm->logCount = 0;
}

int Vm_TakeAccessLog(Vm *vm, VmAccess *out, int max)
{
    int n = vm->logCount < max ? vm->logCount : max;
    int skip = vm->logCount - n;                    // not enough room: keep the newest
    memcpy(out, vm->log + skip, sizeof(VmAccess) * (size_t)n);
    vm->logCount = 0;
    vm->readCount = vm->writeCount = 0;
    return n;
}

VmStatus    Vm_GetStatus(const Vm *vm)    { return vm->status; }
int         Vm_CurrentLine(const Vm *vm)  { return vm->line; }
int         Vm_ReturnValue(const Vm *vm)  { return vm->returnValue; }
const char *Vm_ErrorMessage(const Vm *vm) { return vm->errorMsg; }
int         Vm_ErrorLine(const Vm *vm)    { return vm->errorLine; }
VmErrorKind Vm_GetErrorKind(const Vm *vm) { return vm->errorKind; }

// ===========================================================================
// Inspector
// ===========================================================================
static void FillValues(const Vm *vm, VmVariable *v, int addr, int count)
{
    v->count = count;
    for (int i = 0; i < count && i < VM_VAR_MAX_VALUES; i++)
    {
        int a = addr + i;
        bool ok = a > 0 && a < VM_MEM_SIZE && vm->cells[a].state == CELL_USED;
        v->values[i]      = ok ? vm->mem[a] : 0;
        v->initialized[i] = ok && vm->cells[a].init;
    }
}

int Vm_GetVariables(const Vm *vm, VmVariable *out, int max)
{
    int n = 0;
    int curFunc = vm->frameCount > 0 ? vm->frames[vm->frameCount - 1].func : -1;
    int fp      = vm->frameCount > 0 ? vm->frames[vm->frameCount - 1].fp : 0;

    // How far the loose statements have got (globals are visible once declared,
    // including inside functions called from there)
    int scriptPc = -1;
    if (vm->frameCount > 0 && vm->frames[0].func == 0)
        scriptPc = (vm->frameCount == 1) ? vm->pc : vm->frames[1].returnPc;

    for (int i = 0; i < vm->prog.symCount && n < max; i++)
    {
        const Symbol *s = &vm->prog.syms[i];
        if (s->hidden) continue;

        bool visible;
        if (s->isGlobal) visible = vm->scriptDone || scriptPc >= s->pcStart;
        else             visible = (s->funcIndex == curFunc && vm->pc >= s->pcStart && vm->pc < s->pcEnd);
        if (!visible) continue;

        VmVariable *v = &out[n++];
        memset(v, 0, sizeof *v);
        snprintf(v->name, sizeof v->name, "%s", s->name);
        Ast_TypeToString(&s->type, v->type, sizeof v->type);
        v->isGlobal = s->isGlobal;
        v->isChar   = (s->type.base == TYPE_CHAR);

        int slot = s->isGlobal ? s->offset : fp + s->offset;
        v->address = slot;

        int target = slot;
        if (s->holdsAddress)
        {
            target = vm->cells[slot].init ? vm->mem[slot] : 0;
            v->address = target;
        }

        if (s->type.arrayDims > 0)
        {
            v->isArray = true;
            int rows = s->type.arraySize[0];
            int cols = s->type.arrayDims == 2 ? s->type.arraySize[1] : 1;
            if (s->type.arrayDims == 2) { v->rows = rows; v->cols = cols; }

            if (rows < 0)   // int arr[] parameter: size unknown, show as a pointer
            {
                v->isArray = false;
                v->isPointer = true;
                v->pointsTo = target;
                CellName(vm, target, v->pointsToName, sizeof v->pointsToName);
                FillValues(vm, v, target, 0);
            }
            else
            {
                FillValues(vm, v, target, rows * cols);
            }
        }
        else if (s->type.isRef)
        {
            v->isRef = true;
            v->pointsTo = target;
            CellName(vm, target, v->pointsToName, sizeof v->pointsToName);
            FillValues(vm, v, target, 1);
        }
        else
        {
            FillValues(vm, v, slot, 1);
            if (s->type.pointerDepth > 0)
            {
                v->isPointer = true;
                v->pointsTo = v->initialized[0] ? v->values[0] : -1;
                if (v->initialized[0]) CellName(vm, v->pointsTo, v->pointsToName, sizeof v->pointsToName);
                else snprintf(v->pointsToName, sizeof v->pointsToName, "(garbage)");
            }
        }
    }
    return n;
}
