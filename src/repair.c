// repair.c - runs repair-level tests against the player's code. See repair.h.

#include "repair.h"

#include <stdio.h>
#include <string.h>

static const char *ArgName(const LevelValue *v)
{
    switch (v->kind)
    {
        case VALUE_ARRAY:  return "arr";
        case VALUE_STRING: return "str";
        default:           return "value";
    }
}

static bool Compare(int got, CompareOp op, int want)
{
    switch (op)
    {
        case CMP_NE: return got != want;
        case CMP_LT: return got <  want;
        case CMP_GT: return got >  want;
        default:     return got == want;
    }
}

static const char *OpWords(CompareOp op)
{
    switch (op)
    {
        case CMP_NE: return "should not return";
        case CMP_LT: return "should return less than";
        case CMP_GT: return "should return more than";
        default:     return "should return";
    }
}

// Read back what an argument holds now, as a LevelValue of the same kind
static void ReadBack(const Vm *vm, int addr, const LevelValue *shape, LevelValue *out)
{
    memset(out, 0, sizeof *out);
    out->kind = shape->kind;
    out->display = shape->display;

    if (shape->kind == VALUE_ARRAY)
    {
        out->count = shape->count;
        for (int i = 0; i < shape->count; i++) out->values[i] = Vm_ReadMemory(vm, addr + i);
    }
    else if (shape->kind == VALUE_STRING)
    {
        int i = 0;
        for (; i < (int)sizeof out->text - 1; i++)
        {
            int ch = Vm_ReadMemory(vm, addr + i);
            if (ch == 0) break;
            out->text[i] = (char)ch;
        }
        out->text[i] = '\0';
    }
    else
    {
        out->value = Vm_ReadMemory(vm, addr);
    }
}

static bool SameValue(const LevelValue *a, const LevelValue *b)
{
    if (a->kind == VALUE_ARRAY)
    {
        if (a->count != b->count) return false;
        for (int i = 0; i < a->count; i++) if (a->values[i] != b->values[i]) return false;
        return true;
    }
    if (a->kind == VALUE_STRING) return strcmp(a->text, b->text) == 0;
    return a->value == b->value;
}

// The call part of the test text: everything up to the ) that closes the call
static void CallText(const LevelTest *t, char *out, int size)
{
    const char *s = t->text;
    int depth = 0, i = 0;
    bool inString = false;
    for (; s[i]; i++)
    {
        char c = s[i];
        if (c == '"' && (i == 0 || s[i - 1] != '\\')) inString = !inString;
        if (inString) continue;
        if (c == '(') depth++;
        if (c == ')' && --depth == 0) { i++; break; }
    }
    int n = i < size - 1 ? i : size - 1;
    memcpy(out, s, (size_t)n);
    out[n] = '\0';
}

void Repair_Describe(const LevelTest *t, char *call, int callSize, char *expect, int expectSize)
{
    CallText(t, call, callSize);

    char value[128];
    int len = 0;
    expect[0] = '\0';
    if (t->hasExpect)
    {
        Level_FormatValue(&t->expect, value, sizeof value);
        len += snprintf(expect + len, (size_t)(expectSize - len), "%s %s", OpWords(t->op), value);
    }
    for (int i = 0; i < t->checkCount && len < expectSize - 1; i++)
    {
        LevelValue v = t->checks[i].value;
        Level_FormatValue(&v, value, sizeof value);
        len += snprintf(expect + len, (size_t)(expectSize - len), "%s#%d should be %s",
                        len ? ",  " : "afterwards ", t->checks[i].argIndex, value);
    }
}

bool Repair_BeginTest(Vm *vm, const LevelTest *t, RepairRun *run, TestOutcome *o)
{
    memset(o, 0, sizeof *o);
    memset(run, 0, sizeof *run);
    run->argc = t->argc;

    if (!Vm_HasFunction(vm, t->func))
    {
        snprintf(o->message, sizeof o->message,
                 "FAIL  %s\n      your code needs a function called %s(). Check the spelling.", t->text, t->func);
        snprintf(o->summary, sizeof o->summary, "there's no function called %s()", t->func);
        return false;
    }

    // ---- Put the arguments into the VM's memory ----
    int args[LEVEL_MAX_ARGS];
    for (int k = 0; k < t->argc; k++)
    {
        const LevelValue *v = &t->args[k];
        int addr = 0;
        switch (v->kind)
        {
            case VALUE_INT:
                args[k] = v->value;
                continue;
            case VALUE_ARRAY:
            {
                int zero = 0;
                addr = (v->count > 0) ? Vm_AllocArray(vm, ArgName(v), v->values, v->count)
                                      : Vm_AllocArray(vm, ArgName(v), &zero, 1);
                break;
            }
            case VALUE_STRING:
                addr = Vm_AllocString(vm, ArgName(v), v->text, v->capacity);
                break;
            case VALUE_REF:
                addr = Vm_AllocArray(vm, ArgName(v), &v->value, 1);
                break;
        }
        if (!addr)
        {
            snprintf(o->message, sizeof o->message, "FAIL  %s: the test data didn't fit in memory.", t->text);
            snprintf(o->summary, sizeof o->summary, "the test data didn't fit in memory");
            return false;
        }
        args[k] = addr;
        run->argAddr[k] = addr;
    }

    // ---- Call the function ----
    if (!Vm_Start(vm, t->func, args, t->argc))
    {
        o->crashed = true;
        snprintf(o->message, sizeof o->message, "FAIL  %.150s: %.240s", t->text, Vm_ErrorMessage(vm));
        snprintf(o->summary, sizeof o->summary, "%.190s", Vm_ErrorMessage(vm));
        return false;
    }
    return true;
}

// "Line 4: Out of bounds! ..." -> "Out of bounds! ..."
static const char *WithoutLine(const char *msg)
{
    if (strncmp(msg, "Line ", 5) == 0)
    {
        const char *colon = strchr(msg, ':');
        if (colon && colon[1] == ' ') return colon + 2;
    }
    return msg;
}

void Repair_FinishTest(Vm *vm, const LevelTest *t, const RepairRun *run, TestOutcome *o)
{
    memset(o, 0, sizeof *o);
    VmStatus s = Vm_GetStatus(vm);

    if (s != VM_FINISHED)
    {
        o->crashed = true;
        const char *err = (s == VM_ERROR) ? Vm_ErrorMessage(vm) : "the code ran for too long. Is there an infinite loop?";
        snprintf(o->message, sizeof o->message, "CRASH %.150s\n      %.240s", t->text, err);
        snprintf(o->summary, sizeof o->summary, "CRASHED: %.180s", WithoutLine(err));
        return;
    }

    // ---- Check the return value ----
    char want[128], got[128];
    if (t->hasExpect)
    {
        LevelValue r = t->expect;
        r.value = Vm_ReturnValue(vm);
        Level_FormatValue(&r, got, sizeof got);
        if (!Compare(r.value, t->op, t->expect.value))
        {
            Level_FormatValue(&t->expect, want, sizeof want);
            snprintf(o->message, sizeof o->message, "FAIL  %s\n      returned %s, but it %s %s",
                     t->text, got, OpWords(t->op), want);
            snprintf(o->summary, sizeof o->summary, "returned %s", got);
            return;
        }
        snprintf(o->summary, sizeof o->summary, "returned %s", got);
    }

    // ---- Check what the arguments hold now ----
    for (int i = 0; i < t->checkCount; i++)
    {
        const LevelCheck *c = &t->checks[i];
        const LevelValue *arg = &t->args[c->argIndex - 1];
        LevelValue now;
        LevelValue shape = c->value;
        if (shape.kind == VALUE_INT) shape.kind = VALUE_REF;   // "#1 == 10" on an &value argument
        ReadBack(vm, run->argAddr[c->argIndex - 1], &shape, &now);
        LevelValue wantValue = c->value;
        wantValue.kind = shape.kind;

        if (now.kind == VALUE_REF) { now.kind = VALUE_INT; wantValue.kind = VALUE_INT; }
        Level_FormatValue(&now, got, sizeof got);

        if (!SameValue(&now, &wantValue))
        {
            Level_FormatValue(&wantValue, want, sizeof want);
            snprintf(o->message, sizeof o->message, "FAIL  %s\n      afterwards %s #%d should be %s, but it is %s",
                     t->text, arg->kind == VALUE_STRING ? "string" : "argument", c->argIndex, want, got);
            snprintf(o->summary, sizeof o->summary, "#%d is %s", c->argIndex, got);
            return;
        }
        if (!t->hasExpect)
        {
            size_t len = strlen(o->summary);
            snprintf(o->summary + len, sizeof o->summary - len, "%s#%d is %s", len ? ", " : "", c->argIndex, got);
        }
    }

    o->passed = true;
    snprintf(o->message, sizeof o->message, "PASS  %s  ->  %s", t->text, o->summary);
}

void Repair_RunTest(Vm *vm, const LevelTest *t, TestOutcome *o)
{
    RepairRun run;
    if (!Repair_BeginTest(vm, t, &run, o)) return;

    // Run to the end. Safety cap: even if no instruction limit was set, a test
    // that runs this long is treated as an infinite loop instead of freezing the game.
    int batches = 0;
    for (;;)
    {
        VmStatus s = Vm_Run(vm, 100000);
        if (s == VM_WAITING) { Vm_ActionDone(vm, 0); continue; }   // move() etc. do nothing here
        if ((s == VM_RUNNING || s == VM_PAUSED) && ++batches < 200) continue;
        break;
    }
    Repair_FinishTest(vm, t, &run, o);
}
