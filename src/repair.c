// repair.c - runs one repair-level test against the player's code. See repair.h.

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

void Repair_RunTest(Vm *vm, const LevelTest *t, TestOutcome *o)
{
    memset(o, 0, sizeof *o);

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
            return;
        }
        args[k] = addr;
    }

    // ---- Call the function ----
    if (!Vm_HasFunction(vm, t->func))
    {
        snprintf(o->message, sizeof o->message,
                 "FAIL  %s\n      your code needs a function called %s(). Check the spelling.", t->text, t->func);
        return;
    }
    if (!Vm_Start(vm, t->func, args, t->argc))
    {
        o->crashed = true;
        snprintf(o->message, sizeof o->message, "FAIL  %.150s: %.240s", t->text, Vm_ErrorMessage(vm));
        return;
    }

    // Run to the end. Safety cap: even if no instruction limit was set, a test
    // that runs this long is treated as an infinite loop instead of freezing the game.
    VmStatus s;
    int batches = 0;
    for (;;)
    {
        s = Vm_Run(vm, 100000);
        if (s == VM_WAITING) { Vm_ActionDone(vm, 0); continue; }   // move() etc. do nothing here
        if ((s == VM_RUNNING || s == VM_PAUSED) && ++batches < 200) continue;
        break;
    }
    if (s == VM_RUNNING || s == VM_PAUSED)
    {
        o->crashed = true;
        snprintf(o->message, sizeof o->message, "CRASH %s\n      the code ran for too long. Is there an infinite loop?", t->text);
        return;
    }

    if (s == VM_ERROR)
    {
        o->crashed = true;
        snprintf(o->message, sizeof o->message, "CRASH %.150s\n      %.240s", t->text, Vm_ErrorMessage(vm));
        return;
    }

    // ---- Check the return value ----
    char want[128], got[128];
    if (t->hasExpect)
    {
        LevelValue r = t->expect;
        r.value = Vm_ReturnValue(vm);
        if (!Compare(r.value, t->op, t->expect.value))
        {
            Level_FormatValue(&r, got, sizeof got);
            Level_FormatValue(&t->expect, want, sizeof want);
            snprintf(o->message, sizeof o->message, "FAIL  %s\n      returned %s, but it %s %s",
                     t->text, got, OpWords(t->op), want);
            return;
        }
    }

    // ---- Check what the arguments hold now ----
    for (int i = 0; i < t->checkCount; i++)
    {
        const LevelCheck *c = &t->checks[i];
        const LevelValue *arg = &t->args[c->argIndex - 1];
        LevelValue now;
        LevelValue shape = c->value;
        if (shape.kind == VALUE_INT) shape.kind = VALUE_REF;   // "#1 == 10" on an &value argument
        ReadBack(vm, args[c->argIndex - 1], &shape, &now);
        LevelValue wantValue = c->value;
        wantValue.kind = shape.kind;

        if (!SameValue(&now, &wantValue))
        {
            if (now.kind == VALUE_REF) { now.kind = VALUE_INT; wantValue.kind = VALUE_INT; }
            Level_FormatValue(&now, got, sizeof got);
            Level_FormatValue(&wantValue, want, sizeof want);
            snprintf(o->message, sizeof o->message, "FAIL  %s\n      afterwards %s #%d should be %s, but it is %s",
                     t->text, arg->kind == VALUE_STRING ? "string" : "argument", c->argIndex, want, got);
            return;
        }
    }

    o->passed = true;
    if (t->hasExpect)
    {
        LevelValue r = t->expect;
        r.value = Vm_ReturnValue(vm);
        Level_FormatValue(&r, got, sizeof got);
        snprintf(o->message, sizeof o->message, "PASS  %s  ->  %s", t->text, got);
    }
    else
    {
        snprintf(o->message, sizeof o->message, "PASS  %s", t->text);
    }
}
