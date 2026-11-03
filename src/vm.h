// vm.h - runs the player's code, a little at a time, so the game can animate it.
//
// HOW THE GAME USES IT
//
//   1. Build:   Vm *vm = Vm_Build(code, &options, &host, &result);
//               (lexes, parses and compiles; result explains any error)
//   2. Start:   Vm_Start(vm, NULL, NULL, 0);                  // escape level: run the code
//               Vm_Start(vm, "isLeapYear", args, 1);          // repair level: call one function
//   3. Every frame:
//               VmStatus s = Vm_Run(vm, 500);                  // run up to 500 instructions
//      - VM_WAITING : the code called move()/write(). Animate it, then call
//                     Vm_ActionDone(vm, result) and keep calling Vm_Run.
//      - VM_PAUSED  : stopped at a new line because Vm_PauseAtNextLine() was used.
//      - VM_FINISHED: done. Vm_ReturnValue() has the result.
//      - VM_ERROR   : runtime error (segfault, out of bounds...). See Vm_ErrorMessage().
//   4. While it runs, Vm_CurrentLine() and Vm_GetVariables() feed the editor
//      highlight and the Inspector panel.
//
// The VM never calls raylib. It talks to the game only through VmHost.

#ifndef VM_H
#define VM_H

#include <stdbool.h>

#define VM_MSG_MAX          300
#define VM_MAX_WARNINGS     8
#define VM_VAR_MAX_VALUES   64
#define VM_VAR_NAME_MAX     64

// Built-in functions the player can call
typedef enum {
    VM_BUILTIN_PRINTF = 0,
    VM_BUILTIN_MOVE,        // move(dir)      takes time
    VM_BUILTIN_WRITE,       // write(value)   takes time
    VM_BUILTIN_READ,        // read()         instant, returns a value
    VM_BUILTIN_SCAN,        // scan(dir)      instant, returns a tile type
    VM_BUILTIN_GET_X,       // get_x()        instant
    VM_BUILTIN_GET_Y,       // get_y()        instant
    VM_BUILTIN_COUNT
} VmBuiltin;

// How the VM talks to the game. Any callback may be NULL.
typedef struct {
    void *user;   // passed back to every callback (e.g. a World*)

    // Called for move/write/read/scan/get_x/get_y.
    // Put the return value in *result.
    // Return true if the action takes time (move, write): the VM then waits
    // until the game calls Vm_ActionDone(). Return false for instant ones.
    bool (*callBuiltin)(void *user, VmBuiltin id, const int *args, int argc, int *result);

    // printf / cout output, for the console panel
    void (*print)(void *user, const char *text);

    // Runtime warnings, e.g. using a variable before it has a value
    void (*warning)(void *user, int line, const char *message);
} VmHost;

typedef struct {
    bool banIndexing;       // Pointer Wires world: arr[i] is not allowed, use *(arr + i)
    int  maxInstructions;   // stop with "infinite loop?" after this many (0 = no limit)
} VmOptions;

// Result of Vm_Build: either ok, or a compile error in plain English
typedef struct {
    bool ok;
    char message[VM_MSG_MAX];   // e.g. "Line 4: you used 'total' but it hasn't been declared..."
    int  line;

    int  warningCount;          // e.g. "Line 2: = inside an if assigns a value..."
    char warnings[VM_MAX_WARNINGS][VM_MSG_MAX];
    int  warningLines[VM_MAX_WARNINGS];
} VmBuildResult;

typedef enum {
    VM_READY = 0,       // built, not started
    VM_RUNNING,         // call Vm_Run to continue
    VM_WAITING,         // waiting for the game to finish an action (Vm_ActionDone)
    VM_PAUSED,          // paused at a new line (step mode). Vm_Run continues.
    VM_FINISHED,
    VM_ERROR
} VmStatus;

// What kind of runtime error happened (so the game can show the right hazard)
typedef enum {
    VM_ERR_NONE = 0,
    VM_ERR_SEGFAULT,        // bad pointer, NULL, past the end, dangling
    VM_ERR_OUT_OF_BOUNDS,   // arr[i] with i outside 0..size-1
    VM_ERR_DIV_ZERO,
    VM_ERR_STACK_OVERFLOW,  // too much recursion
    VM_ERR_TOO_LONG,        // hit maxInstructions: probably an infinite loop
    VM_ERR_NO_RETURN,       // non-void function reached } without return
    VM_ERR_BAD_ARGUMENT,    // e.g. move(7)
    VM_ERR_READ_ONLY,       // changing a string literal
    VM_ERR_OTHER
} VmErrorKind;

// One variable, as shown in the Inspector
typedef struct {
    char name[VM_VAR_NAME_MAX];
    char type[VM_VAR_NAME_MAX];     // e.g. "int", "char[6]", "int*", "int&"
    int  address;                   // where it lives in memory
    bool isGlobal;
    bool isChar;                    // show values as characters

    // Plain variables: count = 1. Arrays: count = number of elements.
    int  count;
    int  rows, cols;                // 2D arrays (rows = 0 for 1D)
    int  values[VM_VAR_MAX_VALUES];
    bool initialized[VM_VAR_MAX_VALUES];
    bool isArray;

    // Pointers and references: what they point at (draw an arrow!)
    bool isPointer;
    bool isRef;
    int  pointsTo;                  // address, 0 = NULL
    char pointsToName[VM_VAR_NAME_MAX]; // e.g. "arr[2]", "x", "NULL", "(invalid)"
} VmVariable;

typedef struct Vm Vm;

// ---- Building and starting ----
Vm  *Vm_Build(const char *source, const VmOptions *options, const VmHost *host, VmBuildResult *result);
void Vm_Destroy(Vm *vm);

bool Vm_HasFunction(const Vm *vm, const char *name);

// Repair levels: put test data into the VM's memory before Vm_Start.
// Returns the address to pass as an argument (or 0 if there's no room).
int  Vm_AllocArray(Vm *vm, const char *name, const int *values, int count);
int  Vm_AllocString(Vm *vm, const char *name, const char *text, int capacity);
int  Vm_ReadMemory(const Vm *vm, int address);

// functionName = NULL runs the program (loose statements, then main() if there is one).
bool Vm_Start(Vm *vm, const char *functionName, const int *args, int argc);

// ---- Running ----
VmStatus Vm_Run(Vm *vm, int maxInstructions);
void     Vm_ActionDone(Vm *vm, int result);
void     Vm_PauseAtNextLine(Vm *vm);

// ---- State ----
VmStatus    Vm_GetStatus(const Vm *vm);
int         Vm_CurrentLine(const Vm *vm);
int         Vm_ReturnValue(const Vm *vm);
const char *Vm_ErrorMessage(const Vm *vm);
int         Vm_ErrorLine(const Vm *vm);
VmErrorKind Vm_GetErrorKind(const Vm *vm);

// Variables visible right now (current function + globals). Returns how many were written.
int Vm_GetVariables(const Vm *vm, VmVariable *out, int max);

// Memory addresses read and written since the last call (most recent last).
// The game uses these to flash the blocks the code is touching.
#define VM_MAX_ACCESSES 16
void Vm_TakeAccesses(Vm *vm, int *reads, int *readCount, int *writes, int *writeCount);

#endif
