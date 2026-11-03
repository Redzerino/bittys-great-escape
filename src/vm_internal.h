// vm_internal.h - shared between compiler.c and vm.c. Not for the game to use.
//
// MEMORY MODEL
//   The VM has its own memory: an array of int "cells". Every value (int,
//   char, bool, pointer) takes exactly one cell, and a pointer is just the
//   index of a cell. Address 0 is NULL.
//
//     [0]         NULL (never valid)
//     [1 ..]      static: string literals and global variables
//     [host]      test data the game puts in (Vm_AllocArray)
//     [stack ..]  one frame per function call, holding its local variables
//
//   After every variable there is one GUARD cell. Touching a guard cell is a
//   segfault: "you went past the end of arr". This is how off-by-one bugs
//   are caught and shown to the player.
//
// BYTECODE
//   The compiler turns the tree into a flat list of ints: an opcode followed
//   by its operands. The VM is a stack machine: most instructions pop their
//   inputs off a value stack and push the result.

#ifndef VM_INTERNAL_H
#define VM_INTERNAL_H

#include "vm.h"
#include "ast.h"

#define VM_MEM_SIZE        16384
#define VM_HOST_REGION     1024
#define VM_STACK_MAX       1024
#define VM_CALL_DEPTH_MAX  200
#define VM_MAX_FUNCS       64
#define VM_MAX_PARAMS      16

typedef enum {
    CELL_FREE = 0,      // not allocated (or belonged to a function that returned)
    CELL_USED,          // part of a variable
    CELL_GUARD          // the cell just after a variable
} CellState;

typedef struct {
    unsigned char state;
    unsigned char init;       // has been given a value
    unsigned char readonly;   // string literal
    unsigned char isArray;    // owner is an array (for names like arr[2])
    const char   *owner;      // name of the variable this cell belongs to / guards / used to belong to
    int           base;       // address of the owner's first cell
} Cell;

typedef enum {
    OP_HALT = 0,
    OP_LINE,            // line             : now running this source line
    OP_PUSH,            // value            : push a constant
    OP_POP,             //                  : discard top
    OP_DUP,             //                  : duplicate top
    OP_ADDR_LOCAL,      // offset           : push address of a local (frame start + offset)
    OP_ADDR_GLOBAL,     // address          : push an absolute address
    OP_LOAD,            //                  : [addr] -> [value]
    OP_STORE,           //                  : [addr value] -> [value]
    OP_INC_PRE,         // delta            : [addr] -> [new value]   ++x
    OP_INC_POST,        // delta            : [addr] -> [old value]   x++
    OP_INDEX,           // size stride sym  : [base i] -> [base + i*stride], bounds-checked if size >= 0

    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_NEG, OP_NOT,
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_CAST_CHAR, OP_CAST_BOOL,

    OP_JUMP,            // target
    OP_JUMP_IF_FALSE,   // target           : pops the condition
    OP_JUMP_IF_TRUE,    // target

    OP_CALL,            // func argc
    OP_CALL_BUILTIN,    // builtin argc
    OP_RET,             //                  : [value] -> return it
    OP_NO_RETURN,       //                  : error, a non-void function reached its }

    OP_PRINT_INT,       //                  : cout << int
    OP_PRINT_CHAR,      //                  : cout << char
    OP_PRINT_STR,       //                  : cout << char*

    OP_COUNT
} OpCode;

typedef struct {
    char    *name;          // malloc'd, so the pointer stays valid (Cell.owner points at it)
    TypeInfo type;
    bool     isGlobal;
    int      offset;        // global: absolute address. local: offset from frame start
    int      cells;         // cells of storage (not counting the guard)
    bool     holdsAddress;  // slot holds the ADDRESS of the real data (reference, array parameter)
    bool     hidden;        // compiler temporaries, not shown in the Inspector
    int      funcIndex;     // which function's frame (locals only)
    int      line;
    int      pcStart;       // visible from this instruction...
    int      pcEnd;         // ...up to (not including) this one
} Symbol;

typedef struct {
    char     name[TOKEN_TEXT_MAX];
    TypeInfo returnType;
    int      paramCount;
    TypeInfo paramTypes[VM_MAX_PARAMS];
    int      paramOffsets[VM_MAX_PARAMS];
    bool     defined;
    int      declLine;
    int      firstCallLine;     // for "declared but never written" errors
    int      entryPc;
    int      frameSize;
    Cell    *frameTemplate;     // how a fresh frame looks; base is relative to the frame start
} Function;

typedef struct {
    int     *code;
    int      codeLen, codeCap;

    Function funcs[VM_MAX_FUNCS];   // funcs[0] is the program's loose statements
    int      funcCount;
    int      mainFunc;              // -1 if there is no main()

    Symbol  *syms;
    int      symCount, symCap;

    // Static memory image: string literals and globals, addresses [0, staticEnd)
    int     *staticValues;
    Cell    *staticCells;
    int      staticEnd, staticCap;
} Program;

bool Compiler_Compile(const Ast *ast, const VmOptions *options, Program *prog, VmBuildResult *result);
void Program_Free(Program *prog);

#endif
