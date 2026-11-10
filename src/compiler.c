// compiler.c - turns the syntax tree into bytecode for the VM. See vm_internal.h.
//
// While it walks the tree it also does the checks a real C compiler does:
//   - every variable is declared before use, and only once per scope
//   - functions get the right number and kind of values
//   - pointers and numbers aren't mixed up
//   - printf placeholders (%d %c %s) match the values given
// Every problem stops with a plain-English message pointing at the line.

#include "vm_internal.h"
#include "game_api.h"

#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VISIBLE     512
#define MAX_SCOPES      64
#define MAX_BREAKABLES  32
#define MAX_PATCHES     64
#define MAX_ARRAY_CELLS 4096

static const char *STRING_OWNER = "a string literal";

typedef struct {
    bool isLoop;                        // false = switch
    int  breakPatches[MAX_PATCHES];
    int  breakCount;
    int  contPatches[MAX_PATCHES];
    int  contCount;
} Breakable;

typedef struct {
    Program         *prog;
    const VmOptions *opt;
    VmBuildResult   *res;
    jmp_buf          jump;

    int  func;                          // function being compiled
    int  frameSize;                     // cells used so far in its frame

    int  visible[MAX_VISIBLE];          // symbols currently in scope (indexes into prog->syms)
    int  visibleCount;
    int  scopeStart[MAX_SCOPES];        // visibleCount when each scope opened
    int  scopeDepth;                    // 0 = global

    Breakable breakables[MAX_BREAKABLES];
    int  breakableCount;
} Compiler;

// ===========================================================================
// Built-in functions and constants
// ===========================================================================
typedef struct {
    const char *name;
    VmBuiltin   id;
    int         argc;           // -1 = any (printf)
    bool        returnsValue;
} BuiltinInfo;

static const BuiltinInfo BUILTINS[] = {
    { "printf", VM_BUILTIN_PRINTF, -1, true  },
    { "move",   VM_BUILTIN_MOVE,    1, false },
    { "write",  VM_BUILTIN_WRITE,   1, false },
    { "read",   VM_BUILTIN_READ,    0, true  },
    { "scan",   VM_BUILTIN_SCAN,    1, true  },
    { "get_x",  VM_BUILTIN_GET_X,   0, true  },
    { "get_y",  VM_BUILTIN_GET_Y,   0, true  },
};
#define BUILTIN_COUNT ((int)(sizeof BUILTINS / sizeof BUILTINS[0]))

typedef struct { const char *name; int value; } Constant;

static const Constant CONSTANTS[] = {
    { "NORTH", NORTH }, { "EAST", EAST }, { "SOUTH", SOUTH }, { "WEST", WEST },
    { "TILE_EMPTY", TILE_EMPTY }, { "TILE_WALL", TILE_WALL }, { "TILE_DATA", TILE_DATA },
    { "TILE_GATE", TILE_GATE },   { "TILE_EXIT", TILE_EXIT }, { "TILE_VOID", TILE_VOID },
};
#define CONSTANT_COUNT ((int)(sizeof CONSTANTS / sizeof CONSTANTS[0]))

static const BuiltinInfo *FindBuiltin(const char *name)
{
    for (int i = 0; i < BUILTIN_COUNT; i++)
        if (strcmp(BUILTINS[i].name, name) == 0) return &BUILTINS[i];
    return NULL;
}

static const Constant *FindConstant(const char *name)
{
    for (int i = 0; i < CONSTANT_COUNT; i++)
        if (strcmp(CONSTANTS[i].name, name) == 0) return &CONSTANTS[i];
    return NULL;
}

static bool IsEndl(const char *name)
{
    return strcmp(name, "endl") == 0 || strcmp(name, "std::endl") == 0;
}

static bool IsCout(const char *name)
{
    return strcmp(name, "cout") == 0 || strcmp(name, "std::cout") == 0;
}

// ===========================================================================
// Errors
// ===========================================================================
static void Fail(Compiler *c, int line, const char *fmt, ...)
{
    char msg[VM_MSG_MAX - 20];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);

    c->res->ok   = false;
    c->res->line = line;
    snprintf(c->res->message, VM_MSG_MAX, "Line %d: %s", line, msg);
    longjmp(c->jump, 1);
}

static void *GrowArray(Compiler *c, void *ptr, int *cap, int needed, size_t elemSize)
{
    if (needed <= *cap) return ptr;
    int newCap = *cap ? *cap : 64;
    while (newCap < needed) newCap *= 2;
    void *grown = realloc(ptr, (size_t)newCap * elemSize);
    if (!grown) Fail(c, 0, "out of memory.");
    *cap = newCap;
    return grown;
}

// ===========================================================================
// Emitting bytecode
// ===========================================================================
static int Here(const Compiler *c) { return c->prog->codeLen; }

static int Emit(Compiler *c, int value)
{
    Program *p = c->prog;
    p->code = GrowArray(c, p->code, &p->codeCap, p->codeLen + 1, sizeof(int));
    p->code[p->codeLen] = value;
    return p->codeLen++;
}

static void Emit2(Compiler *c, OpCode op, int a)        { Emit(c, op); Emit(c, a); }
static void Emit3(Compiler *c, OpCode op, int a, int b) { Emit(c, op); Emit(c, a); Emit(c, b); }

// Emit a jump whose target isn't known yet. Returns where to patch it.
static int EmitJump(Compiler *c, OpCode op)
{
    Emit(c, op);
    return Emit(c, -1);
}

static void PatchHere(Compiler *c, int at) { c->prog->code[at] = Here(c); }

static void EmitJumpTo(Compiler *c, OpCode op, int target) { Emit2(c, op, target); }

static void EmitLine(Compiler *c, int line) { Emit2(c, OP_LINE, line); }

// ===========================================================================
// Types
// ===========================================================================
static TypeInfo MakeType(BaseType base)
{
    TypeInfo t;
    memset(&t, 0, sizeof t);
    t.base = base;
    return t;
}

static bool IsArr(TypeInfo t)   { return t.arrayDims > 0; }
static bool IsPtr(TypeInfo t)   { return t.pointerDepth > 0; }
static bool IsAddr(TypeInfo t)  { return IsArr(t) || IsPtr(t); }
static bool IsVoidT(TypeInfo t) { return t.base == TYPE_VOID && !IsAddr(t); }
static bool IsCharT(TypeInfo t) { return t.base == TYPE_CHAR && !IsAddr(t); }
static bool IsBoolT(TypeInfo t) { return t.base == TYPE_BOOL && !IsAddr(t); }

// The type of one element: int[5] -> int,  int[4][6] -> int[6],  int* -> int
static TypeInfo Element(TypeInfo t)
{
    t.isRef = false;
    if (IsArr(t))
    {
        t.arraySize[0] = t.arraySize[1];
        t.arraySize[1] = 0;
        t.arrayDims--;
    }
    else if (IsPtr(t))
    {
        t.pointerDepth--;
    }
    return t;
}

// An array used as a value becomes a pointer to its first element: int[5] -> int*
static TypeInfo Decay(TypeInfo t)
{
    t.isRef = false;
    if (!IsArr(t)) return t;
    TypeInfo e = Element(t);
    e.arrayDims = 0;            // a 2D array decays to a plain pointer here (rows are handled by [ ])
    e.pointerDepth++;
    return e;
}

static const char *TypeText(TypeInfo t, char *buf, int size)
{
    t.isRef = false;
    Ast_TypeToString(&t, buf, size);
    return buf;
}

// "an int", "a char*", "a string"...
static const char *Describe(TypeInfo t, char *buf, int size)
{
    char tmp[40];
    if (IsArr(t) && t.base == TYPE_CHAR && t.arrayDims == 1) { snprintf(buf, size, "a string"); return buf; }
    if (IsArr(t)) { snprintf(buf, size, "an array (%s)", TypeText(t, tmp, sizeof tmp)); return buf; }
    TypeText(t, tmp, sizeof tmp);
    snprintf(buf, size, "%s %s", (tmp[0] == 'i') ? "an" : "a", tmp);
    return buf;
}

static void EmitCast(Compiler *c, TypeInfo target)
{
    if (IsCharT(target)) Emit(c, OP_CAST_CHAR);
    else if (IsBoolT(target)) Emit(c, OP_CAST_BOOL);
}

static bool IsZeroLiteral(const Node *n)
{
    return n && n->kind == NODE_NUMBER && n->intValue == 0;
}

// Can a value of type 'value' be stored somewhere of type 'target'?
static void CheckAssign(Compiler *c, int line, TypeInfo target, TypeInfo value,
                        const Node *valueNode, const char *what)
{
    char a[64], b[64];
    TypeInfo v = Decay(value);
    TypeInfo t = Decay(target);

    if (IsVoidT(value))
        Fail(c, line, "that doesn't give back a value (it's void), so it can't be stored in %s.", what);

    if (IsPtr(t) && !IsPtr(v))
    {
        if (IsZeroLiteral(valueNode)) return;   // p = 0 is the same as p = NULL
        Fail(c, line, "%s is a pointer, so it needs an address (e.g. &x or an array name), "
                      "but you gave it %s.", what, Describe(value, a, sizeof a));
    }
    if (!IsPtr(t) && IsPtr(v))
        Fail(c, line, "%s holds %s, but you gave it an address. "
                      "Use * to get the value a pointer points at.", what, Describe(target, a, sizeof a));

    if (IsPtr(t) && IsPtr(v) && v.base != TYPE_VOID &&
        (v.base != t.base || v.pointerDepth != t.pointerDepth))
        Fail(c, line, "%s is %s but you gave it %s.", what,
             Describe(target, a, sizeof a), Describe(value, b, sizeof b));
}

// ===========================================================================
// Static memory (string literals and globals)
// ===========================================================================
static int AllocStatic(Compiler *c, int count)
{
    Program *p = c->prog;
    int needed = p->staticEnd + count;
    if (needed >= VM_MEM_SIZE - VM_HOST_REGION - 1024)
        Fail(c, 0, "the program's global data is too big.");

    if (needed > p->staticCap)
    {
        int newCap = p->staticCap ? p->staticCap : 256;
        while (newCap < needed) newCap *= 2;

        int  *values = realloc(p->staticValues, (size_t)newCap * sizeof(int));
        if (!values) Fail(c, 0, "out of memory.");
        p->staticValues = values;

        Cell *cells = realloc(p->staticCells, (size_t)newCap * sizeof(Cell));
        if (!cells) Fail(c, 0, "out of memory.");
        p->staticCells = cells;

        for (int i = p->staticCap; i < newCap; i++)
        {
            p->staticValues[i] = 0;
            memset(&p->staticCells[i], 0, sizeof(Cell));
        }
        p->staticCap = newCap;
    }

    int addr = p->staticEnd;
    p->staticEnd = needed;
    return addr;
}

static void SetStaticCell(Compiler *c, int addr, CellState state, int value, bool readonly,
                          const char *owner, int base, bool isArray)
{
    Program *p = c->prog;
    p->staticValues[addr] = value;
    Cell *cell = &p->staticCells[addr];
    cell->state    = (unsigned char)state;
    cell->init     = 1;                 // globals and literals always start with a value (C zeroes globals)
    cell->readonly = readonly;
    cell->isArray  = isArray;
    cell->owner    = owner;
    cell->base     = base;
}

static int AddStringLiteral(Compiler *c, const char *text, int len)
{
    int addr = AllocStatic(c, len + 2);   // chars + '\0' + guard
    for (int i = 0; i < len; i++)
        SetStaticCell(c, addr + i, CELL_USED, (unsigned char)text[i], true, STRING_OWNER, addr, true);
    SetStaticCell(c, addr + len, CELL_USED, 0, true, STRING_OWNER, addr, true);
    SetStaticCell(c, addr + len + 1, CELL_GUARD, 0, true, STRING_OWNER, addr, true);
    return addr;
}

// ===========================================================================
// Symbols and scopes
// ===========================================================================
static void PushScope(Compiler *c)
{
    if (c->scopeDepth + 1 >= MAX_SCOPES) Fail(c, 0, "the code is nested too deeply.");
    c->scopeStart[++c->scopeDepth] = c->visibleCount;
}

static void PopScope(Compiler *c)
{
    int start = c->scopeStart[c->scopeDepth];
    for (int i = start; i < c->visibleCount; i++)
        c->prog->syms[c->visible[i]].pcEnd = Here(c);
    c->visibleCount = start;
    c->scopeDepth--;
}

static int LookupSymbol(const Compiler *c, const char *name)
{
    for (int i = c->visibleCount - 1; i >= 0; i--)
    {
        int s = c->visible[i];
        if (strcmp(c->prog->syms[s].name, name) == 0) return s;
    }
    return -1;
}

static int FindFunction(const Program *p, const char *name)
{
    for (int i = 1; i < p->funcCount; i++)
        if (strcmp(p->funcs[i].name, name) == 0) return i;
    return -1;
}

// Names the player isn't allowed to use for their own variables/functions
static void CheckNameAllowed(Compiler *c, const char *name, int line, bool isFunction)
{
    if (FindBuiltin(name))
        Fail(c, line, "'%s' is a built-in function, so pick a different name.", name);
    if (FindConstant(name))
        Fail(c, line, "'%s' is a built-in name, so pick a different name.", name);
    if (!isFunction && FindFunction(c->prog, name) >= 0)
        Fail(c, line, "'%s' is already the name of a function.", name);
}

static void CheckNotInThisScope(Compiler *c, const char *name, int line)
{
    for (int i = c->scopeStart[c->scopeDepth]; i < c->visibleCount; i++)
    {
        const Symbol *s = &c->prog->syms[c->visible[i]];
        if (strcmp(s->name, name) == 0)
            Fail(c, line, "'%s' is already declared on line %d.", name, s->line);
    }
}

// Create a symbol and give it storage. It isn't visible until MakeVisible().
static int NewSymbol(Compiler *c, const char *name, TypeInfo type, int line,
                     int cells, bool holdsAddress, bool hidden)
{
    Program *p = c->prog;
    p->syms = GrowArray(c, p->syms, &p->symCap, p->symCount + 1, sizeof(Symbol));

    int index = p->symCount++;
    Symbol *s = &p->syms[index];
    memset(s, 0, sizeof *s);

    s->name = malloc(strlen(name) + 1);
    if (!s->name) Fail(c, line, "out of memory.");
    strcpy(s->name, name);

    s->type         = type;
    s->cells        = cells;
    s->holdsAddress = holdsAddress;
    s->hidden       = hidden;
    s->line         = line;
    s->funcIndex    = c->func;
    s->pcEnd        = INT_MAX;
    s->isGlobal     = (c->scopeDepth == 0 && !hidden);

    if (s->isGlobal)
    {
        int addr = AllocStatic(c, cells + 1);
        s = &p->syms[index];                        // (AllocStatic doesn't move syms, but be safe)
        bool isArray = IsArr(type) && !holdsAddress;
        for (int i = 0; i < cells; i++)
            SetStaticCell(c, addr + i, CELL_USED, 0, false, s->name, addr, isArray);
        SetStaticCell(c, addr + cells, CELL_GUARD, 0, false, s->name, addr, isArray);
        s->offset = addr;
    }
    else
    {
        s->offset = c->frameSize;
        c->frameSize += cells + 1;                  // + guard cell
        if (c->frameSize > 8000) Fail(c, line, "too many local variables in this function.");
    }
    return index;
}

static void MakeVisible(Compiler *c, int symIndex)
{
    if (c->visibleCount >= MAX_VISIBLE) Fail(c, 0, "too many variables.");
    c->prog->syms[symIndex].pcStart = Here(c);
    c->visible[c->visibleCount++] = symIndex;
}

static void EmitSlotAddress(Compiler *c, int symIndex, int extra)
{
    const Symbol *s = &c->prog->syms[symIndex];
    if (s->isGlobal) Emit2(c, OP_ADDR_GLOBAL, s->offset + extra);
    else             Emit2(c, OP_ADDR_LOCAL,  s->offset + extra);
}

// Build the picture of a fresh stack frame for function 'fi'
static void BuildFrameTemplate(Compiler *c, int fi)
{
    Program *p = c->prog;
    Function *f = &p->funcs[fi];
    int size = f->frameSize > 0 ? f->frameSize : 1;

    f->frameTemplate = calloc((size_t)size, sizeof(Cell));
    if (!f->frameTemplate) Fail(c, 0, "out of memory.");

    for (int i = 0; i < p->symCount; i++)
    {
        const Symbol *s = &p->syms[i];
        if (s->isGlobal || s->funcIndex != fi) continue;

        bool isArray = IsArr(s->type) && !s->holdsAddress;
        for (int k = 0; k <= s->cells; k++)
        {
            Cell *cell = &f->frameTemplate[s->offset + k];
            cell->state   = (k < s->cells) ? CELL_USED : CELL_GUARD;
            cell->owner   = s->name;
            cell->base    = s->offset;
            cell->isArray = isArray;
        }
    }
}

// ===========================================================================
// Loops and switches (break / continue)
// ===========================================================================
static void PushBreakable(Compiler *c, bool isLoop)
{
    if (c->breakableCount >= MAX_BREAKABLES) Fail(c, 0, "loops are nested too deeply.");
    Breakable *b = &c->breakables[c->breakableCount++];
    memset(b, 0, sizeof *b);
    b->isLoop = isLoop;
}

static void PatchList(Compiler *c, const int *patches, int count, int target)
{
    for (int i = 0; i < count; i++) c->prog->code[patches[i]] = target;
}

static void PopBreakable(Compiler *c, int breakTarget)
{
    Breakable *b = &c->breakables[--c->breakableCount];
    PatchList(c, b->breakPatches, b->breakCount, breakTarget);
}

static void PatchContinues(Compiler *c, int target)
{
    Breakable *b = &c->breakables[c->breakableCount - 1];
    PatchList(c, b->contPatches, b->contCount, target);
    b->contCount = 0;
}

// ===========================================================================
// Expressions
// ===========================================================================
static TypeInfo CompileValueRaw(Compiler *c, const Node *n);
static TypeInfo CompileAddress(Compiler *c, const Node *n);

static const char *CallName(const Node *n)
{
    if (n->kind == NODE_CALL && n->a->kind == NODE_IDENT) return n->a->name;
    return NULL;
}

// Compile an expression that must produce a value (not a void call)
static TypeInfo CompileValue(Compiler *c, const Node *n)
{
    TypeInfo t = CompileValueRaw(c, n);
    if (IsVoidT(t))
    {
        const char *name = CallName(n);
        if (name) Fail(c, n->line, "%s() doesn't give back a value (it's void), so it can't be used here.", name);
        Fail(c, n->line, "this doesn't give back a value, so it can't be used here.");
    }
    return t;
}

static int RootSymbol(const Compiler *c, const Node *n)
{
    while (n->kind == NODE_INDEX) n = n->a;
    if (n->kind == NODE_IDENT) return LookupSymbol(c, n->name);
    return -1;
}

static const char *TargetName(const Node *n)
{
    while (n->kind == NODE_INDEX) n = n->a;
    if (n->kind == NODE_IDENT) return n->name;
    return "this";
}

static void CheckModifiable(Compiler *c, TypeInfo t, const Node *target, int line)
{
    if (t.isConst && !IsPtr(t))
    {
        if (target->kind == NODE_IDENT)
            Fail(c, line, "%s is const, so it can't be changed.", target->name);
        Fail(c, line, "this is read-only (const or a string literal), so it can't be changed.");
    }
}

static int LookupOrFail(Compiler *c, const Node *ident)
{
    int s = LookupSymbol(c, ident->name);
    if (s >= 0) return s;

    if (FindFunction(c->prog, ident->name) >= 0 || FindBuiltin(ident->name))
        Fail(c, ident->line, "%s is a function. To use it, call it: %s(...)", ident->name, ident->name);
    if (IsEndl(ident->name))
        Fail(c, ident->line, "endl only works with cout, e.g. cout << x << endl;");
    if (IsCout(ident->name))
        Fail(c, ident->line, "cout must be followed by <<, e.g. cout << \"Hello\";");

    Fail(c, ident->line, "you used '%s' but it hasn't been declared. In C every variable needs "
                         "a type first, e.g. int %s = 0;", ident->name, ident->name);
    return -1;
}

// Emit the ADDRESS of something that can be assigned to. Returns its type.
static TypeInfo CompileAddress(Compiler *c, const Node *n)
{
    char a[64];

    switch (n->kind)
    {
        case NODE_IDENT:
        {
            if (FindConstant(n->name))
                Fail(c, n->line, "%s is a fixed built-in value, so it can't be changed.", n->name);
            int s = LookupOrFail(c, n);
            EmitSlotAddress(c, s, 0);
            if (c->prog->syms[s].holdsAddress) Emit(c, OP_LOAD);   // reference / array parameter
            TypeInfo t = c->prog->syms[s].type;
            t.isRef = false;
            return t;
        }

        case NODE_INDEX:
        {
            if (c->opt->banIndexing)
                Fail(c, n->line, "Pointer Wires has no [ ] index operator. "
                                 "Use pointer arithmetic instead, e.g. *(arr + i)");

            TypeInfo bt = CompileValue(c, n->a);
            if (!IsAddr(bt))
                Fail(c, n->line, "%s is %s, not an array or pointer, so [ ] can't be used on it.",
                     TargetName(n->a), Describe(bt, a, sizeof a));

            TypeInfo it = CompileValue(c, n->b);
            if (IsAddr(it)) Fail(c, n->line, "an array index must be a number.");

            int size = -1, stride = 1;
            if (IsArr(bt))
            {
                size   = bt.arraySize[0];
                stride = (bt.arrayDims == 2) ? bt.arraySize[1] : 1;
            }
            Emit(c, OP_INDEX);
            Emit(c, size);
            Emit(c, stride);
            Emit(c, RootSymbol(c, n));
            return Element(bt);
        }

        case NODE_UNARY:
            if (n->op == TOK_STAR)
            {
                TypeInfo t = CompileValue(c, n->a);
                if (!IsAddr(t))
                    Fail(c, n->line, "* (dereference) only works on a pointer, but this is %s.",
                         Describe(t, a, sizeof a));
                TypeInfo e = Element(t);
                if (IsVoidT(e)) Fail(c, n->line, "this pointer has no type (NULL), so * can't be used on it.");
                return e;
            }
            break;

        case NODE_MEMBER:
            Fail(c, n->line, ". and -> are for structs and classes, which arrive in the Rail Yard world.");
            break;

        default:
            break;
    }

    Fail(c, n->line, "this isn't a variable, so it can't be changed or have its address taken.");
    return MakeType(TYPE_INT);
}

static bool IsLValueNode(const Node *n)
{
    return n->kind == NODE_IDENT || n->kind == NODE_INDEX ||
           (n->kind == NODE_UNARY && n->op == TOK_STAR);
}

// ---- cout << a << b << endl ----
static bool IsCoutChain(const Node *n)
{
    while (n->kind == NODE_BINARY && n->op == TOK_SHIFT_LEFT) n = n->a;
    return n->kind == NODE_IDENT && IsCout(n->name);
}

static void CompileCout(Compiler *c, const Node *n)
{
    if (n->kind == NODE_IDENT) return;      // the stream itself
    CompileCout(c, n->a);

    const Node *item = n->b;
    if (item->kind == NODE_IDENT && IsEndl(item->name))
    {
        Emit2(c, OP_PUSH, '\n');
        Emit(c, OP_PRINT_CHAR);
        return;
    }

    TypeInfo t = CompileValue(c, item);
    TypeInfo d = Decay(t);
    if (IsCharT(t))                                              Emit(c, OP_PRINT_CHAR);
    else if (IsPtr(d) && d.base == TYPE_CHAR && d.pointerDepth == 1) Emit(c, OP_PRINT_STR);
    else                                                         Emit(c, OP_PRINT_INT);
}

static TypeInfo CompileBinary(Compiler *c, const Node *n)
{
    char a[64];

    switch (n->op)
    {
        case TOK_AND:
        {
            CompileValue(c, n->a);
            int j1 = EmitJump(c, OP_JUMP_IF_FALSE);
            CompileValue(c, n->b);
            int j2 = EmitJump(c, OP_JUMP_IF_FALSE);
            Emit2(c, OP_PUSH, 1);
            int j3 = EmitJump(c, OP_JUMP);
            PatchHere(c, j1);
            PatchHere(c, j2);
            Emit2(c, OP_PUSH, 0);
            PatchHere(c, j3);
            return MakeType(TYPE_BOOL);
        }

        case TOK_OR:
        {
            CompileValue(c, n->a);
            int j1 = EmitJump(c, OP_JUMP_IF_TRUE);
            CompileValue(c, n->b);
            int j2 = EmitJump(c, OP_JUMP_IF_TRUE);
            Emit2(c, OP_PUSH, 0);
            int j3 = EmitJump(c, OP_JUMP);
            PatchHere(c, j1);
            PatchHere(c, j2);
            Emit2(c, OP_PUSH, 1);
            PatchHere(c, j3);
            return MakeType(TYPE_BOOL);
        }

        case TOK_SHIFT_LEFT:
            if (IsCoutChain(n))
            {
                CompileCout(c, n);
                Emit2(c, OP_PUSH, 0);
                return MakeType(TYPE_INT);
            }
            Fail(c, n->line, "<< is only used with cout inside the machine, e.g. cout << x;");
            break;

        case TOK_SHIFT_RIGHT:
            Fail(c, n->line, "cin >> isn't available inside the machine. "
                             "Values come in through function parameters or read().");
            break;

        default:
            break;
    }

    TypeInfo lt = CompileValue(c, n->a);
    TypeInfo rt = CompileValue(c, n->b);
    bool lp = IsAddr(lt), rp = IsAddr(rt);

    switch (n->op)
    {
        case TOK_PLUS:
            if (lp && rp) Fail(c, n->line, "you can't add two pointers together.");
            Emit(c, OP_ADD);
            if (lp) return Decay(lt);
            if (rp) return Decay(rt);
            return MakeType(TYPE_INT);

        case TOK_MINUS:
            if (!lp && rp) Fail(c, n->line, "you can't take a pointer away from a number.");
            Emit(c, OP_SUB);
            if (lp && rp) return MakeType(TYPE_INT);   // distance between two pointers
            if (lp) return Decay(lt);
            return MakeType(TYPE_INT);

        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT:
            if (lp || rp)
                Fail(c, n->line, "you can't use %s on a pointer (%s).", Lexer_TokenName(n->op),
                     Describe(lp ? lt : rt, a, sizeof a));
            Emit(c, n->op == TOK_STAR ? OP_MUL : n->op == TOK_SLASH ? OP_DIV : OP_MOD);
            return MakeType(TYPE_INT);

        case TOK_EQ:  Emit(c, OP_EQ); return MakeType(TYPE_BOOL);
        case TOK_NEQ: Emit(c, OP_NE); return MakeType(TYPE_BOOL);
        case TOK_LT:  Emit(c, OP_LT); return MakeType(TYPE_BOOL);
        case TOK_GT:  Emit(c, OP_GT); return MakeType(TYPE_BOOL);
        case TOK_LTE: Emit(c, OP_LE); return MakeType(TYPE_BOOL);
        case TOK_GTE: Emit(c, OP_GE); return MakeType(TYPE_BOOL);

        default:
            Fail(c, n->line, "the operator %s isn't supported.", Lexer_TokenName(n->op));
    }
    return MakeType(TYPE_INT);
}

static TypeInfo CompileAssign(Compiler *c, const Node *n)
{
    TypeInfo t = CompileAddress(c, n->a);
    const char *name = TargetName(n->a);

    if (IsArr(t))
        Fail(c, n->line, "you can't copy a whole array with =. Copy it one element at a time with a loop.");
    CheckModifiable(c, t, n->a, n->line);

    if (n->op == TOK_ASSIGN)
    {
        TypeInfo vt = CompileValue(c, n->b);
        CheckAssign(c, n->line, t, vt, n->b, name);
    }
    else
    {
        Emit(c, OP_DUP);
        Emit(c, OP_LOAD);
        TypeInfo vt = CompileValue(c, n->b);
        if (IsAddr(vt)) Fail(c, n->line, "the right side of %s must be a number.", Lexer_TokenName(n->op));

        switch (n->op)
        {
            case TOK_PLUS_ASSIGN:    Emit(c, OP_ADD); break;
            case TOK_MINUS_ASSIGN:   Emit(c, OP_SUB); break;
            case TOK_STAR_ASSIGN:    Emit(c, OP_MUL); break;
            case TOK_SLASH_ASSIGN:   Emit(c, OP_DIV); break;
            case TOK_PERCENT_ASSIGN: Emit(c, OP_MOD); break;
            default: break;
        }
        if (IsPtr(t) && n->op != TOK_PLUS_ASSIGN && n->op != TOK_MINUS_ASSIGN)
            Fail(c, n->line, "only += and -= can be used on a pointer.");
    }

    EmitCast(c, t);
    Emit(c, OP_STORE);
    return t;
}

// ---- printf checking: placeholders must match the values ----
static void CheckPrintf(Compiler *c, const Node *call, const TypeInfo *types, int argc)
{
    char a[64];
    const Node *fmt = call->list.items[0];

    if (fmt->kind != NODE_STRING_LIT)
    {
        TypeInfo d = Decay(types[0]);
        if (!(IsPtr(d) && d.base == TYPE_CHAR))
            Fail(c, call->line, "printf's first value must be a string in double quotes, e.g. printf(\"%%d\\n\", x);");
        return;   // can't check placeholders we can't see
    }

    int value = 1;
    const char *s = fmt->name;
    for (int i = 0; s[i]; i++)
    {
        if (s[i] != '%') continue;
        char spec = s[++i];
        if (spec == '%') continue;
        if (spec == '\0') Fail(c, call->line, "a %% at the end of the printf string needs a letter after it, e.g. %%d.");

        if (spec == 'f')
            Fail(c, call->line, "%%f is for decimals, which aren't available inside the machine. Use %%d.");
        if (spec != 'd' && spec != 'i' && spec != 'c' && spec != 's')
            Fail(c, call->line, "%%%c isn't supported. Use %%d (int), %%c (char) or %%s (string).", spec);

        if (value >= argc)
            Fail(c, call->line, "printf has more placeholders (%%d, %%c, %%s) than values. "
                                "Placeholder %d has no value to print.", value);

        TypeInfo t = types[value];
        TypeInfo d = Decay(t);
        if (spec == 's' && !(IsPtr(d) && d.base == TYPE_CHAR))
            Fail(c, call->line, "%%s prints a string, but value %d is %s. Use %%d for numbers.",
                 value, Describe(t, a, sizeof a));
        if (spec != 's' && IsAddr(t))
            Fail(c, call->line, "%%%c prints a single value, but value %d is %s.%s", spec, value,
                 Describe(t, a, sizeof a),
                 (d.base == TYPE_CHAR) ? " Use %s for strings." : "");
        value++;
    }

    if (value < argc)
        Fail(c, call->line, "printf was given %d value%s but its string only has %d placeholder%s (%%d, %%c, %%s).",
             argc - 1, argc - 1 == 1 ? "" : "s", value - 1, value - 1 == 1 ? "" : "s");
}

static void CheckArgument(Compiler *c, const Node *call, const Function *f, int k,
                          TypeInfo param, TypeInfo arg, const Node *argNode)
{
    char a[64], b[64];
    TypeInfo p = Decay(param);
    TypeInfo v = Decay(arg);

    if (IsPtr(p) && !IsPtr(v))
    {
        if (IsZeroLiteral(argNode)) return;
        Fail(c, call->line, "value %d of %s() should be an array or pointer (%s), but you gave %s.",
             k + 1, f->name, TypeText(param, a, sizeof a), Describe(arg, b, sizeof b));
    }
    if (!IsPtr(p) && IsPtr(v))
        Fail(c, call->line, "value %d of %s() should be %s, but you gave an array or address.",
             k + 1, f->name, Describe(param, a, sizeof a));
    if (IsPtr(p) && IsPtr(v) && v.base != TYPE_VOID &&
        (v.base != p.base || v.pointerDepth != p.pointerDepth))
        Fail(c, call->line, "value %d of %s() should be %s, but you gave %s.",
             k + 1, f->name, TypeText(param, a, sizeof a), Describe(arg, b, sizeof b));
}

static TypeInfo CompileCall(Compiler *c, const Node *n)
{
    char a[64];

    if (n->a->kind != NODE_IDENT)
        Fail(c, n->line, "member functions belong to classes, which arrive in the Rail Yard world.");

    const char *name = n->a->name;
    int argc = n->list.count;
    if (argc > VM_MAX_PARAMS) Fail(c, n->line, "too many values in this call (max %d).", VM_MAX_PARAMS);

    // ---- Built-ins ----
    const BuiltinInfo *bi = FindBuiltin(name);
    if (bi)
    {
        if (bi->id == VM_BUILTIN_PRINTF)
        {
            if (argc == 0) Fail(c, n->line, "printf needs a string, e.g. printf(\"Hello\\n\");");
            TypeInfo types[VM_MAX_PARAMS];
            for (int k = 0; k < argc; k++) types[k] = CompileValue(c, n->list.items[k]);
            CheckPrintf(c, n, types, argc);
        }
        else
        {
            if (argc != bi->argc)
                Fail(c, n->line, "%s() takes %d value%s but you gave %d.",
                     name, bi->argc, bi->argc == 1 ? "" : "s", argc);
            for (int k = 0; k < argc; k++)
            {
                TypeInfo t = CompileValue(c, n->list.items[k]);
                if (IsAddr(t)) Fail(c, n->line, "%s() needs a number, but you gave %s.", name, Describe(t, a, sizeof a));
            }
        }
        Emit3(c, OP_CALL_BUILTIN, bi->id, argc);
        return bi->returnsValue ? MakeType(TYPE_INT) : MakeType(TYPE_VOID);
    }

    // ---- Player's own functions ----
    int fi = FindFunction(c->prog, name);
    if (fi < 0)
    {
        if (LookupSymbol(c, name) >= 0)
            Fail(c, n->line, "%s is a variable, not a function, so it can't be called with ( ).", name);
        Fail(c, n->line, "there's no function called %s(). Check the spelling, or write the function.", name);
    }

    Function *f = &c->prog->funcs[fi];
    if (argc != f->paramCount)
        Fail(c, n->line, "%s() takes %d value%s but you gave %d.",
             name, f->paramCount, f->paramCount == 1 ? "" : "s", argc);

    for (int k = 0; k < argc; k++)
    {
        const Node *arg = n->list.items[k];
        TypeInfo param = f->paramTypes[k];

        if (param.isRef)
        {
            if (!IsLValueNode(arg))
            {
                Ast_TypeToString(&param, a, sizeof a);
                Fail(c, n->line, "value %d of %s() must be a variable, because the parameter is a "
                                 "reference (%s).", k + 1, name, a);
            }
            TypeInfo at = CompileAddress(c, arg);
            if (IsArr(at) || at.base != param.base || at.pointerDepth != param.pointerDepth)
            {
                TypeInfo plain = param;
                plain.isRef = false;
                Fail(c, n->line, "value %d of %s() must be %s variable.", k + 1, name,
                     Describe(plain, a, sizeof a));
            }
        }
        else
        {
            TypeInfo at = CompileValue(c, arg);
            CheckArgument(c, n, f, k, param, at, arg);
        }
    }

    Emit3(c, OP_CALL, fi, argc);
    if (f->firstCallLine == 0) f->firstCallLine = n->line;

    TypeInfo ret = f->returnType;
    ret.isRef = false;
    return ret;
}

static TypeInfo CompileValueRaw(Compiler *c, const Node *n)
{
    char a[64];

    switch (n->kind)
    {
        case NODE_NUMBER:   Emit2(c, OP_PUSH, n->intValue); return MakeType(TYPE_INT);
        case NODE_CHAR_LIT: Emit2(c, OP_PUSH, n->intValue); return MakeType(TYPE_CHAR);
        case NODE_BOOL_LIT: Emit2(c, OP_PUSH, n->intValue); return MakeType(TYPE_BOOL);

        case NODE_NULL_LIT:
        {
            Emit2(c, OP_PUSH, 0);
            TypeInfo t = MakeType(TYPE_VOID);
            t.pointerDepth = 1;
            return t;
        }

        case NODE_STRING_LIT:
        {
            int addr = AddStringLiteral(c, n->name, n->intValue);
            Emit2(c, OP_PUSH, addr);
            TypeInfo t = MakeType(TYPE_CHAR);
            t.arrayDims = 1;
            t.arraySize[0] = n->intValue + 1;
            t.isConst = true;
            return t;
        }

        case NODE_IDENT:
        {
            const Constant *k = FindConstant(n->name);
            if (k && LookupSymbol(c, n->name) < 0)
            {
                Emit2(c, OP_PUSH, k->value);
                return MakeType(TYPE_INT);
            }
            TypeInfo t = CompileAddress(c, n);
            if (!IsArr(t)) Emit(c, OP_LOAD);   // arrays are used by address
            return t;
        }

        case NODE_INDEX:
        {
            TypeInfo t = CompileAddress(c, n);
            if (!IsArr(t)) Emit(c, OP_LOAD);   // grid[r] (a whole row) stays an address
            return t;
        }

        case NODE_UNARY:
            switch (n->op)
            {
                case TOK_STAR:
                {
                    TypeInfo t = CompileAddress(c, n);
                    if (!IsArr(t)) Emit(c, OP_LOAD);
                    return t;
                }
                case TOK_AMPERSAND:
                {
                    TypeInfo t = CompileAddress(c, n->a);
                    if (IsArr(t)) return t;     // &arr is the same address as arr
                    t.pointerDepth++;
                    t.isConst = false;
                    return t;
                }
                case TOK_MINUS:
                {
                    TypeInfo t = CompileValue(c, n->a);
                    if (IsAddr(t)) Fail(c, n->line, "you can't make an address negative.");
                    Emit(c, OP_NEG);
                    return MakeType(TYPE_INT);
                }
                case TOK_NOT:
                    CompileValue(c, n->a);
                    Emit(c, OP_NOT);
                    return MakeType(TYPE_BOOL);

                case TOK_PLUS_PLUS:
                case TOK_MINUS_MINUS:
                {
                    TypeInfo t = CompileAddress(c, n->a);
                    if (IsArr(t)) Fail(c, n->line, "you can't use %s on a whole array.", Lexer_TokenName(n->op));
                    CheckModifiable(c, t, n->a, n->line);
                    Emit2(c, OP_INC_PRE, n->op == TOK_PLUS_PLUS ? 1 : -1);
                    return t;
                }
                default:
                    break;
            }
            break;

        case NODE_POSTFIX:
        {
            TypeInfo t = CompileAddress(c, n->a);
            if (IsArr(t)) Fail(c, n->line, "you can't use %s on a whole array.", Lexer_TokenName(n->op));
            CheckModifiable(c, t, n->a, n->line);
            Emit2(c, OP_INC_POST, n->op == TOK_PLUS_PLUS ? 1 : -1);
            return t;
        }

        case NODE_ASSIGN:  return CompileAssign(c, n);
        case NODE_BINARY:  return CompileBinary(c, n);
        case NODE_CALL:    return CompileCall(c, n);

        case NODE_TERNARY:
        {
            CompileValue(c, n->a);
            int toElse = EmitJump(c, OP_JUMP_IF_FALSE);
            TypeInfo t = CompileValue(c, n->b);
            int toEnd = EmitJump(c, OP_JUMP);
            PatchHere(c, toElse);
            TypeInfo e = CompileValue(c, n->c);
            PatchHere(c, toEnd);
            if (IsAddr(t) != IsAddr(e))
            {
                char b[64];
                Fail(c, n->line, "both sides of ? : must be the same kind of value (%s vs %s).",
                     Describe(t, a, sizeof a), Describe(e, b, sizeof b));
            }
            return t;
        }

        case NODE_MEMBER:
            Fail(c, n->line, ". and -> are for structs and classes, which arrive in the Rail Yard world.");
            break;

        case NODE_INIT_LIST:
            Fail(c, n->line, "{ } lists can only be used when creating an array.");
            break;

        default:
            break;
    }

    Fail(c, n->line, "this can't be used as a value.");
    return MakeType(TYPE_INT);
}

// ===========================================================================
// Declarations
// ===========================================================================
static void CompileArrayInit(Compiler *c, int sym, const Node *init)
{
    TypeInfo t   = c->prog->syms[sym].type;
    int rows     = t.arraySize[0];
    int cols     = (t.arrayDims == 2) ? t.arraySize[1] : 1;
    TypeInfo el  = MakeType(t.base);
    const char *name = c->prog->syms[sym].name;

    if (init->kind == NODE_STRING_LIT)
    {
        for (int k = 0; k < rows; k++)
        {
            EmitSlotAddress(c, sym, k);
            Emit2(c, OP_PUSH, k < init->intValue ? (unsigned char)init->name[k] : 0);
            Emit(c, OP_STORE);
            Emit(c, OP_POP);
        }
        return;
    }

    for (int r = 0; r < rows; r++)
    {
        const Node *row = NULL;
        if (t.arrayDims == 2 && r < init->list.count) row = init->list.items[r];

        for (int col = 0; col < cols; col++)
        {
            const Node *value = NULL;
            if (t.arrayDims == 1 && r < init->list.count) value = init->list.items[r];
            if (t.arrayDims == 2 && row && col < row->list.count) value = row->list.items[col];

            EmitSlotAddress(c, sym, r * cols + col);
            if (value)
            {
                if (value->kind == NODE_INIT_LIST)
                    Fail(c, value->line, "%s is a 1D array, so it can't contain { } lists.", name);
                TypeInfo vt = CompileValue(c, value);
                CheckAssign(c, value->line, el, vt, value, name);
                EmitCast(c, el);
            }
            else
            {
                Emit2(c, OP_PUSH, 0);    // C fills the rest of a { } list with 0
            }
            Emit(c, OP_STORE);
            Emit(c, OP_POP);
        }
    }
}

static void CompileVarDecl(Compiler *c, const Node *d)
{
    TypeInfo t = d->type;

    CheckNameAllowed(c, d->name, d->line, false);
    CheckNotInThisScope(c, d->name, d->line);

    int cells = 1;
    if (IsArr(t))
    {
        cells = t.arraySize[0] * (t.arrayDims == 2 ? t.arraySize[1] : 1);
        if (cells > MAX_ARRAY_CELLS)
            Fail(c, d->line, "%s is too big for the machine (max %d elements).", d->name, MAX_ARRAY_CELLS);
    }

    int sym = NewSymbol(c, d->name, t, d->line, cells, t.isRef, false);

    if (d->a)
    {
        if (t.isRef)
        {
            if (!IsLValueNode(d->a))
                Fail(c, d->line, "a reference must be linked to a variable, e.g. int &r = x;");
            EmitSlotAddress(c, sym, 0);
            TypeInfo at = CompileAddress(c, d->a);
            if (IsArr(at) || at.base != t.base || at.pointerDepth != t.pointerDepth)
                Fail(c, d->line, "%s must be linked to a variable of the same type.", d->name);
            Emit(c, OP_STORE);
            Emit(c, OP_POP);
        }
        else if (IsArr(t))
        {
            CompileArrayInit(c, sym, d->a);
        }
        else
        {
            EmitSlotAddress(c, sym, 0);
            TypeInfo vt = CompileValue(c, d->a);
            CheckAssign(c, d->line, t, vt, d->a, d->name);
            EmitCast(c, t);
            Emit(c, OP_STORE);
            Emit(c, OP_POP);
        }
    }

    MakeVisible(c, sym);
}

// ===========================================================================
// Statements
// ===========================================================================
static void CompileStatement(Compiler *c, const Node *n);

static void CompileCondition(Compiler *c, const Node *n)
{
    CompileValue(c, n);
}

static void CompileSwitch(Compiler *c, const Node *n)
{
    EmitLine(c, n->line);

    // Store the value in a hidden temporary, then compare it with each case
    int temp = NewSymbol(c, "(switch)", MakeType(TYPE_INT), n->line, 1, false, true);
    EmitSlotAddress(c, temp, 0);
    TypeInfo vt = CompileValue(c, n->a);
    if (IsAddr(vt)) Fail(c, n->line, "switch needs a number or char, not an address.");
    Emit(c, OP_STORE);
    Emit(c, OP_POP);

    int caseCount = n->list.count;
    int jumps[256];
    if (caseCount > 256) Fail(c, n->line, "too many cases in this switch.");

    int defaultIndex = -1;
    for (int i = 0; i < caseCount; i++)
    {
        const Node *cs = n->list.items[i];
        if (cs->isDefault) { defaultIndex = i; jumps[i] = -1; continue; }

        for (int j = 0; j < i; j++)
        {
            const Node *other = n->list.items[j];
            if (!other->isDefault && other->a->intValue == cs->a->intValue)
                Fail(c, cs->line, "this case value is already used on line %d.", other->line);
        }

        EmitSlotAddress(c, temp, 0);
        Emit(c, OP_LOAD);
        Emit2(c, OP_PUSH, cs->a->intValue);
        Emit(c, OP_EQ);
        jumps[i] = EmitJump(c, OP_JUMP_IF_TRUE);
    }
    int toDefault = EmitJump(c, OP_JUMP);

    PushBreakable(c, false);
    PushScope(c);
    for (int i = 0; i < caseCount; i++)
    {
        const Node *cs = n->list.items[i];
        if (i == defaultIndex) PatchHere(c, toDefault);
        else                   PatchHere(c, jumps[i]);

        for (int k = 0; k < cs->list.count; k++) CompileStatement(c, cs->list.items[k]);
    }
    PopScope(c);

    if (defaultIndex < 0) PatchHere(c, toDefault);   // no default: skip to the end
    PopBreakable(c, Here(c));
}

static void CompileReturn(Compiler *c, const Node *n)
{
    char a[64];
    EmitLine(c, n->line);

    if (c->func == 0)   // return in the loose program code just ends it
    {
        if (n->a) CompileValue(c, n->a);
        else      Emit2(c, OP_PUSH, 0);
        Emit(c, OP_RET);
        return;
    }

    const Function *f = &c->prog->funcs[c->func];
    if (IsVoidT(f->returnType))
    {
        if (n->a) Fail(c, n->line, "%s() is void, so it can't return a value. Use just: return;", f->name);
        Emit2(c, OP_PUSH, 0);
    }
    else
    {
        if (!n->a) Fail(c, n->line, "%s() must return %s.", f->name, Describe(f->returnType, a, sizeof a));
        TypeInfo vt = CompileValue(c, n->a);
        char what[TOKEN_TEXT_MAX + 32];
        snprintf(what, sizeof what, "the return value of %s()", f->name);
        CheckAssign(c, n->line, f->returnType, vt, n->a, what);
        EmitCast(c, f->returnType);
    }
    Emit(c, OP_RET);
}

static void CompileStatement(Compiler *c, const Node *n)
{
    switch (n->kind)
    {
        case NODE_VAR_DECL:
            EmitLine(c, n->line);
            CompileVarDecl(c, n);
            break;

        case NODE_DECL_LIST:
            for (int i = 0; i < n->list.count; i++) CompileStatement(c, n->list.items[i]);
            break;

        case NODE_BLOCK:
            PushScope(c);
            for (int i = 0; i < n->list.count; i++) CompileStatement(c, n->list.items[i]);
            PopScope(c);
            break;

        case NODE_EXPR_STMT:
            EmitLine(c, n->line);
            CompileValueRaw(c, n->a);
            Emit(c, OP_POP);
            break;

        case NODE_IF:
        {
            EmitLine(c, n->line);
            CompileCondition(c, n->a);
            Emit(c, OP_NOTE_CONDITION);
            int toElse = EmitJump(c, OP_JUMP_IF_FALSE);
            CompileStatement(c, n->b);
            if (n->c)
            {
                int toEnd = EmitJump(c, OP_JUMP);
                PatchHere(c, toElse);
                CompileStatement(c, n->c);
                PatchHere(c, toEnd);
            }
            else
            {
                PatchHere(c, toElse);
            }
            break;
        }

        case NODE_WHILE:
        {
            int start = Here(c);
            EmitLine(c, n->line);
            CompileCondition(c, n->a);
            Emit(c, OP_NOTE_CONDITION);
            int exit = EmitJump(c, OP_JUMP_IF_FALSE);

            PushBreakable(c, true);
            CompileStatement(c, n->b);
            PatchContinues(c, start);
            EmitJumpTo(c, OP_JUMP, start);
            PatchHere(c, exit);
            PopBreakable(c, Here(c));
            break;
        }

        case NODE_DO_WHILE:
        {
            int start = Here(c);
            PushBreakable(c, true);
            CompileStatement(c, n->a);
            PatchContinues(c, Here(c));
            EmitLine(c, n->b->line);
            CompileCondition(c, n->b);
            Emit(c, OP_NOTE_CONDITION);
            EmitJumpTo(c, OP_JUMP_IF_TRUE, start);
            PopBreakable(c, Here(c));
            break;
        }

        case NODE_FOR:
        {
            PushScope(c);
            if (n->a) CompileStatement(c, n->a);

            int condStart = Here(c);
            EmitLine(c, n->line);
            int exit = -1;
            if (n->b)
            {
                CompileCondition(c, n->b);
                Emit(c, OP_NOTE_CONDITION);
                exit = EmitJump(c, OP_JUMP_IF_FALSE);
            }

            PushBreakable(c, true);
            CompileStatement(c, n->d);
            PatchContinues(c, Here(c));
            if (n->c)
            {
                EmitLine(c, n->line);       // the i++ is its own step (on the for line)
                CompileValueRaw(c, n->c);
                Emit(c, OP_POP);
            }
            EmitJumpTo(c, OP_JUMP, condStart);
            if (exit >= 0) PatchHere(c, exit);
            PopBreakable(c, Here(c));
            PopScope(c);
            break;
        }

        case NODE_SWITCH:
            CompileSwitch(c, n);
            break;

        case NODE_BREAK:
        {
            EmitLine(c, n->line);
            if (c->breakableCount == 0)
                Fail(c, n->line, "break can only be used inside a loop or a switch.");
            Breakable *b = &c->breakables[c->breakableCount - 1];
            if (b->breakCount >= MAX_PATCHES) Fail(c, n->line, "too many breaks in one loop.");
            b->breakPatches[b->breakCount++] = EmitJump(c, OP_JUMP);
            break;
        }

        case NODE_CONTINUE:
        {
            EmitLine(c, n->line);
            int i = c->breakableCount - 1;
            while (i >= 0 && !c->breakables[i].isLoop) i--;
            if (i < 0) Fail(c, n->line, "continue can only be used inside a loop.");
            Breakable *b = &c->breakables[i];
            if (b->contCount >= MAX_PATCHES) Fail(c, n->line, "too many continues in one loop.");
            b->contPatches[b->contCount++] = EmitJump(c, OP_JUMP);
            break;
        }

        case NODE_RETURN:
            CompileReturn(c, n);
            break;

        case NODE_EMPTY:
            break;

        default:
            Fail(c, n->line, "this can't be used here.");
    }
}

// ===========================================================================
// Functions and the whole program
// ===========================================================================
static void RegisterFunctions(Compiler *c, const Node *program)
{
    Program *p = c->prog;

    // funcs[0] is the program's loose statements
    memset(&p->funcs[0], 0, sizeof(Function));
    strcpy(p->funcs[0].name, "(program)");
    p->funcs[0].returnType = MakeType(TYPE_VOID);
    p->funcs[0].defined = true;
    p->funcCount = 1;

    for (int i = 0; i < program->list.count; i++)
    {
        const Node *fn = program->list.items[i];
        if (fn->kind != NODE_FUNCTION) continue;

        CheckNameAllowed(c, fn->name, fn->line, true);
        if (fn->type.isRef)
            Fail(c, fn->line, "%s() can't return a reference here. Return a value or a pointer.", fn->name);
        if (fn->list.count > VM_MAX_PARAMS)
            Fail(c, fn->line, "%s() has too many parameters (max %d).", fn->name, VM_MAX_PARAMS);

        int fi = FindFunction(p, fn->name);
        if (fi >= 0)
        {
            Function *f = &p->funcs[fi];
            if (f->paramCount != fn->list.count)
                Fail(c, fn->line, "%s() is declared twice with different parameters (lines %d and %d).",
                     fn->name, f->declLine, fn->line);
            if (f->defined && fn->a)
                Fail(c, fn->line, "%s() is written twice (lines %d and %d).", fn->name, f->declLine, fn->line);
            if (fn->a)
            {
                f->defined = true;
                f->declLine = fn->line;
                for (int k = 0; k < fn->list.count; k++) f->paramTypes[k] = fn->list.items[k]->type;
            }
            continue;
        }

        if (p->funcCount >= VM_MAX_FUNCS) Fail(c, fn->line, "too many functions (max %d).", VM_MAX_FUNCS - 1);

        Function *f = &p->funcs[p->funcCount++];
        memset(f, 0, sizeof *f);
        snprintf(f->name, sizeof f->name, "%s", fn->name);
        f->returnType = fn->type;
        f->paramCount = fn->list.count;
        f->defined    = (fn->a != NULL);
        f->declLine   = fn->line;
        for (int k = 0; k < fn->list.count; k++) f->paramTypes[k] = fn->list.items[k]->type;
    }
}

static void CompileFunction(Compiler *c, const Node *fn)
{
    int fi = FindFunction(c->prog, fn->name);
    c->func = fi;
    c->frameSize = 0;
    c->prog->funcs[fi].entryPc = Here(c);

    PushScope(c);
    for (int k = 0; k < fn->list.count; k++)
    {
        const Node *param = fn->list.items[k];
        if (!param->name[0])
            Fail(c, fn->line, "parameter %d of %s() needs a name, e.g. int x.", k + 1, fn->name);
        CheckNameAllowed(c, param->name, param->line, false);
        CheckNotInThisScope(c, param->name, param->line);

        TypeInfo t = param->type;
        bool holdsAddress = t.isRef || IsArr(t);    // int &x and int arr[] hold an address
        int sym = NewSymbol(c, param->name, t, param->line, 1, holdsAddress, false);
        c->prog->funcs[fi].paramOffsets[k] = c->prog->syms[sym].offset;
        c->prog->syms[sym].pcStart = c->prog->funcs[fi].entryPc;
        c->visible[c->visibleCount++] = sym;
    }

    // The body shares the parameters' scope (C doesn't allow redeclaring a parameter)
    const Node *body = fn->a;
    for (int i = 0; i < body->list.count; i++) CompileStatement(c, body->list.items[i]);

    if (IsVoidT(c->prog->funcs[fi].returnType))
    {
        Emit2(c, OP_PUSH, 0);
        Emit(c, OP_RET);
    }
    else
    {
        Emit(c, OP_NO_RETURN);
    }
    PopScope(c);

    c->prog->funcs[fi].frameSize = c->frameSize;
    BuildFrameTemplate(c, fi);
}

bool Compiler_Compile(const Ast *ast, const VmOptions *options, Program *prog, VmBuildResult *result)
{
    static const VmOptions DEFAULTS = { false, 0 };

    memset(prog, 0, sizeof *prog);
    prog->mainFunc = -1;

    Compiler c;
    memset(&c, 0, sizeof c);
    c.prog = prog;
    c.opt  = options ? options : &DEFAULTS;
    c.res  = result;

    if (setjmp(c.jump) != 0) return false;

    // Address 0 is NULL
    AllocStatic(&c, 1);

    RegisterFunctions(&c, ast->root);

    // ---- The loose statements and global variables (funcs[0]) ----
    c.func = 0;
    c.frameSize = 0;
    prog->funcs[0].entryPc = Here(&c);
    for (int i = 0; i < ast->root->list.count; i++)
    {
        const Node *item = ast->root->list.items[i];
        if (item->kind != NODE_FUNCTION) CompileStatement(&c, item);
    }
    Emit2(&c, OP_PUSH, 0);
    Emit(&c, OP_RET);
    prog->funcs[0].frameSize = c.frameSize;
    BuildFrameTemplate(&c, 0);

    // ---- Every function with a body ----
    for (int i = 0; i < ast->root->list.count; i++)
    {
        const Node *item = ast->root->list.items[i];
        if (item->kind == NODE_FUNCTION && item->a) CompileFunction(&c, item);
    }

    // ---- Called but never written? ----
    for (int i = 1; i < prog->funcCount; i++)
    {
        const Function *f = &prog->funcs[i];
        if (!f->defined && f->firstCallLine)
            Fail(&c, f->firstCallLine, "%s() is declared on line %d but never written. Give it a body { }.",
                 f->name, f->declLine);
    }

    prog->mainFunc = FindFunction(prog, "main");
    return true;
}

void Program_Free(Program *prog)
{
    free(prog->code);
    for (int i = 0; i < prog->symCount; i++) free(prog->syms[i].name);
    free(prog->syms);
    for (int i = 0; i < prog->funcCount; i++) free(prog->funcs[i].frameTemplate);
    free(prog->staticValues);
    free(prog->staticCells);
    memset(prog, 0, sizeof *prog);
}
