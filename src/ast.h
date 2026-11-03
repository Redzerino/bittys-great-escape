// ast.h - the Abstract Syntax Tree: the structure of the player's program.
//
// The parser turns tokens into a tree of Nodes. For example:
//
//     x = a + b * 2;
//
// becomes
//
//         =
//        / |
//       x  +
//         / |
//        a  *
//          / |
//         b  2
//
// The VM later walks this tree (or bytecode compiled from it) to run the code.
//
// Every node is a 'Node'. Its 'kind' says what it is, and the meaning of the
// child pointers a/b/c/d depends on the kind (see the table below).

#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include "lexer.h"

// ---------------------------------------------------------------------------
// Types:  int, char*, int&, int[5], int[4][6], const char ...
// ---------------------------------------------------------------------------
typedef enum {
    TYPE_INT = 0,
    TYPE_CHAR,
    TYPE_BOOL,
    TYPE_VOID
} BaseType;

#define MAX_ARRAY_DIMS 2

typedef struct {
    BaseType base;
    bool     isConst;
    int      pointerDepth;                // 0 = not a pointer, 1 = int*, 2 = int**
    bool     isRef;                       // C++ reference: int &x
    int      arrayDims;                   // 0 = not an array, 1 = int a[5], 2 = int a[4][6]
    int      arraySize[MAX_ARRAY_DIMS];   // -1 = size not given, e.g. a parameter int arr[]
} TypeInfo;

// ---------------------------------------------------------------------------
// Node kinds
// ---------------------------------------------------------------------------
typedef enum {
    // ---- Expressions ----          children
    NODE_NUMBER,        // 42               intValue
    NODE_CHAR_LIT,      // 'A'              intValue (character code)
    NODE_STRING_LIT,    // "Hello"          name = text, intValue = length
    NODE_BOOL_LIT,      // true / false     intValue = 1 / 0
    NODE_NULL_LIT,      // NULL / nullptr
    NODE_IDENT,         // x, std::cout     name
    NODE_BINARY,        // a + b            op, a = left, b = right
    NODE_UNARY,         // -x !x *p &x ++x  op, a = operand
    NODE_POSTFIX,       // x++ x--          op, a = operand
    NODE_ASSIGN,        // x = 5, x += 1    op, a = target, b = value
    NODE_TERNARY,       // c ? x : y        a = cond, b = then, c = else
    NODE_CALL,          // f(1, 2)          a = callee (IDENT or MEMBER), list = arguments
    NODE_INDEX,         // arr[i]           a = array, b = index
    NODE_MEMBER,        // n.next n->next   a = object, name = member, isArrow
    NODE_INIT_LIST,     // {1, 2, 3}        list = values (may contain nested lists)

    // ---- Statements ----
    NODE_VAR_DECL,      // int x = 5;       type, name, a = initial value (or NULL)
    NODE_DECL_LIST,     // int x = 5, y;    list = VAR_DECLs (no new scope)
    NODE_BLOCK,         // { ... }          list = statements
    NODE_IF,            // if               a = cond, b = then, c = else (or NULL)
    NODE_WHILE,         // while            a = cond, b = body
    NODE_DO_WHILE,      // do ... while     a = body, b = cond
    NODE_FOR,           // for              a = init, b = cond, c = step, d = body (any may be NULL except d)
    NODE_SWITCH,        // switch           a = value, list = CASE nodes
    NODE_CASE,          // case 1: / default:  a = value (NULL for default), isDefault, list = statements
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_RETURN,        // return x;        a = value (or NULL)
    NODE_EXPR_STMT,     // move(NORTH);     a = expression
    NODE_EMPTY,         // ;

    // ---- Top level ----
    NODE_FUNCTION,      // int f(int a) {}  type = return type, name, list = PARAMs, a = body (NULL = prototype)
    NODE_PARAM,         // int a            type, name (may be empty in a prototype)
    NODE_PROGRAM,       // whole program    list = functions, globals and loose statements, in order

    NODE_KIND_COUNT
} NodeKind;

typedef struct Node Node;

typedef struct {
    Node **items;
    int    count;
    int    capacity;
} NodeList;

struct Node {
    NodeKind  kind;
    int       line, col;            // where it came from in the player's code

    TokenType op;                   // operator (BINARY, UNARY, POSTFIX, ASSIGN)
    int       intValue;             // NUMBER, CHAR_LIT, BOOL_LIT, STRING_LIT length
    char      name[TOKEN_TEXT_MAX]; // identifier / variable / function / member name, or string text
    TypeInfo  type;                 // VAR_DECL, PARAM, FUNCTION return type

    Node     *a, *b, *c, *d;        // children (see table above)
    NodeList  list;                 // child list (see table above)

    bool      isDefault;            // CASE: this is 'default:'
    bool      isArrow;              // MEMBER: '->' rather than '.'

    Node     *allocNext;            // internal: every node is chained here for freeing
};

typedef struct {
    Node *root;         // NODE_PROGRAM
    Node *allNodes;     // internal: linked list of every node, for Ast_Free
} Ast;

// Create a node owned by 'ast'. Returns NULL if out of memory.
Node *Ast_NewNode(Ast *ast, NodeKind kind, int line, int col);

// Append to a node list. Returns false if out of memory.
bool NodeList_Add(NodeList *list, Node *node);

// Free every node in the tree.
void Ast_Free(Ast *ast);

// Write the tree as a one-line S-expression, e.g. (program (var int x 5)).
// Used by the tests, and handy for debugging. Returns the length written.
int Ast_Dump(const Node *node, char *buffer, int size);

// Write a type as text, e.g. "const char*", "int[4][6]", "int&".
void Ast_TypeToString(const TypeInfo *type, char *buffer, int size);

const char *Ast_KindName(NodeKind kind);

#endif
