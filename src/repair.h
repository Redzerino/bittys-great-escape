// repair.h - runs repair-level tests against the player's code.
//
// The game runs a test over many frames so the player can watch it:
//     Repair_BeginTest(...)            put the test data in memory, start the call
//     ... Vm_Run() each frame ...      (line by line, at a visible speed)
//     Repair_FinishTest(...)           once the VM has finished or crashed
//
// Repair_RunTest() does all three at once (used by the automatic level tests).

#ifndef REPAIR_H
#define REPAIR_H

#include <stdbool.h>
#include "levels.h"
#include "vm.h"

typedef struct {
    bool passed;
    bool crashed;           // runtime error (segfault, out of bounds, infinite loop...)
    char message[640];      // console: "FAIL  isLeapYear(1700) == false\n      returned true, but..."
    char summary[200];      // gate terminal: "returned true" / "#2 is {1, 2, 3}" / "crashed: Out of bounds!..."
} TestOutcome;

typedef struct {
    int argAddr[LEVEL_MAX_ARGS];   // where each array/string/&value argument lives in VM memory
    int argc;
} RepairRun;

// Returns false if the test couldn't start (e.g. the function doesn't exist); 'out' explains.
bool Repair_BeginTest(Vm *vm, const LevelTest *test, RepairRun *run, TestOutcome *out);

// Call when Vm_GetStatus() is VM_FINISHED or VM_ERROR.
void Repair_FinishTest(Vm *vm, const LevelTest *test, const RepairRun *run, TestOutcome *out);

// Begin + run to the end + finish, all in one go.
void Repair_RunTest(Vm *vm, const LevelTest *test, TestOutcome *out);

// For the gate terminal:  call = "isLeapYear(1700)"   expect = "should return false"
void Repair_Describe(const LevelTest *test, char *call, int callSize, char *expect, int expectSize);

#endif
