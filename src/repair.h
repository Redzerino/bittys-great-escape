// repair.h - runs one repair-level test against the player's code.

#ifndef REPAIR_H
#define REPAIR_H

#include <stdbool.h>
#include "levels.h"
#include "vm.h"

typedef struct {
    bool passed;
    bool crashed;           // runtime error (segfault, out of bounds...)
    char message[640];      // e.g. "FAIL  isLeapYear(1700) returned true, but it should return false"
} TestOutcome;

// Puts the test's arrays/strings into the VM, calls the function, and
// checks the return value and any "then #n == ..." checks.
void Repair_RunTest(Vm *vm, const LevelTest *test, TestOutcome *out);

#endif
