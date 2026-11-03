// ast.c - creating, freeing and printing syntax tree nodes. See ast.h.

#include "ast.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Creating and freeing
// ---------------------------------------------------------------------------
Node *Ast_NewNode(Ast *ast, NodeKind kind, int line, int col)
{
    Node *n = calloc(1, sizeof(Node));
    if (!n) return NULL;

    n->kind = kind;
    n->line = line;
    n->col  = col;

    // Chain every node so the whole tree can be freed in one go,
    // even if parsing stopped half way through with an error.
    n->allocNext  = ast->allNodes;
    ast->allNodes = n;
    return n;
}

bool NodeList_Add(NodeList *list, Node *node)
{
    if (list->count == list->capacity)
    {
        int newCap = list->capacity ? list->capacity * 2 : 4;
        Node **grown = realloc(list->items, (size_t)newCap * sizeof(Node *));
        if (!grown) return false;
        list->items = grown;
        list->capacity = newCap;
    }
    list->items[list->count++] = node;
    return true;
}

void Ast_Free(Ast *ast)
{
    Node *n = ast->allNodes;
    while (n)
    {
        Node *next = n->allocNext;
        free(n->list.items);
        free(n);
        n = next;
    }
    ast->allNodes = NULL;
    ast->root = NULL;
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------
typedef struct {
    char *s;
    int   size;
    int   len;
} Buf;

static void Put(Buf *b, const char *fmt, ...)
{
    if (b->size <= 0 || b->len >= b->size - 1) return;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(b->s + b->len, (size_t)(b->size - b->len), fmt, args);
    va_end(args);

    if (n > 0)
    {
        b->len += n;
        if (b->len > b->size - 1) b->len = b->size - 1;
    }
}

void Ast_TypeToString(const TypeInfo *t, char *out, int size)
{
    static const char *BASE[] = { "int", "char", "bool", "void" };
    Buf b = { out, size, 0 };
    if (size > 0) out[0] = '\0';

    Put(&b, "%s%s", t->isConst ? "const " : "", BASE[t->base]);
    for (int i = 0; i < t->pointerDepth; i++) Put(&b, "*");
    if (t->isRef) Put(&b, "&");
    for (int i = 0; i < t->arrayDims; i++)
    {
        if (t->arraySize[i] < 0) Put(&b, "[]");
        else                     Put(&b, "[%d]", t->arraySize[i]);
    }
}

static void PutType(Buf *b, const TypeInfo *t)
{
    char tmp[64];
    Ast_TypeToString(t, tmp, sizeof tmp);
    Put(b, "%s", tmp);
}

static void PutChar(Buf *b, int c)
{
    switch (c)
    {
        case '\0': Put(b, "\\0");  break;
        case '\n': Put(b, "\\n");  break;
        case '\t': Put(b, "\\t");  break;
        case '\\': Put(b, "\\\\"); break;
        default:   Put(b, "%c", c); break;
    }
}

static void DumpNode(Buf *b, const Node *n);

static void DumpList(Buf *b, const NodeList *list)
{
    for (int i = 0; i < list->count; i++)
    {
        Put(b, " ");
        DumpNode(b, list->items[i]);
    }
}

static void DumpOpt(Buf *b, const Node *n)
{
    if (n) DumpNode(b, n);
    else   Put(b, "_");
}

static void DumpNode(Buf *b, const Node *n)
{
    if (!n) { Put(b, "_"); return; }

    switch (n->kind)
    {
        case NODE_NUMBER:     Put(b, "%d", n->intValue); break;
        case NODE_CHAR_LIT:   Put(b, "'"); PutChar(b, n->intValue); Put(b, "'"); break;
        case NODE_STRING_LIT:
            Put(b, "\"");
            for (int i = 0; n->name[i]; i++) PutChar(b, (unsigned char)n->name[i]);
            Put(b, "\"");
            break;
        case NODE_BOOL_LIT:   Put(b, n->intValue ? "true" : "false"); break;
        case NODE_NULL_LIT:   Put(b, "null"); break;
        case NODE_IDENT:      Put(b, "%s", n->name); break;

        case NODE_BINARY:
            Put(b, "(%s ", Lexer_TokenName(n->op));
            DumpNode(b, n->a); Put(b, " "); DumpNode(b, n->b); Put(b, ")");
            break;

        case NODE_UNARY:
        {
            const char *name = Lexer_TokenName(n->op);
            if (n->op == TOK_MINUS)       name = "neg";
            if (n->op == TOK_STAR)        name = "deref";
            if (n->op == TOK_AMPERSAND)   name = "addr";
            if (n->op == TOK_PLUS_PLUS)   name = "pre++";
            if (n->op == TOK_MINUS_MINUS) name = "pre--";
            Put(b, "(%s ", name); DumpNode(b, n->a); Put(b, ")");
            break;
        }
        case NODE_POSTFIX:
            Put(b, "(post%s ", Lexer_TokenName(n->op)); DumpNode(b, n->a); Put(b, ")");
            break;

        case NODE_ASSIGN:
            Put(b, "(%s ", Lexer_TokenName(n->op));
            DumpNode(b, n->a); Put(b, " "); DumpNode(b, n->b); Put(b, ")");
            break;

        case NODE_TERNARY:
            Put(b, "(? "); DumpNode(b, n->a); Put(b, " "); DumpNode(b, n->b);
            Put(b, " "); DumpNode(b, n->c); Put(b, ")");
            break;

        case NODE_CALL:
            Put(b, "(call "); DumpNode(b, n->a); DumpList(b, &n->list); Put(b, ")");
            break;

        case NODE_INDEX:
            Put(b, "(index "); DumpNode(b, n->a); Put(b, " "); DumpNode(b, n->b); Put(b, ")");
            break;

        case NODE_MEMBER:
            Put(b, "(%s ", n->isArrow ? "->" : "."); DumpNode(b, n->a); Put(b, " %s)", n->name);
            break;

        case NODE_INIT_LIST:
            Put(b, "{");
            for (int i = 0; i < n->list.count; i++)
            {
                if (i) Put(b, " ");
                DumpNode(b, n->list.items[i]);
            }
            Put(b, "}");
            break;

        case NODE_VAR_DECL:
            Put(b, "(var "); PutType(b, &n->type); Put(b, " %s", n->name);
            if (n->a) { Put(b, " "); DumpNode(b, n->a); }
            Put(b, ")");
            break;

        case NODE_DECL_LIST: Put(b, "(decls"); DumpList(b, &n->list); Put(b, ")"); break;
        case NODE_BLOCK:     Put(b, "(block"); DumpList(b, &n->list); Put(b, ")"); break;

        case NODE_IF:
            Put(b, "(if "); DumpNode(b, n->a); Put(b, " "); DumpNode(b, n->b);
            if (n->c) { Put(b, " "); DumpNode(b, n->c); }
            Put(b, ")");
            break;

        case NODE_WHILE:
            Put(b, "(while "); DumpNode(b, n->a); Put(b, " "); DumpNode(b, n->b); Put(b, ")");
            break;

        case NODE_DO_WHILE:
            Put(b, "(do "); DumpNode(b, n->a); Put(b, " "); DumpNode(b, n->b); Put(b, ")");
            break;

        case NODE_FOR:
            Put(b, "(for "); DumpOpt(b, n->a); Put(b, " "); DumpOpt(b, n->b);
            Put(b, " "); DumpOpt(b, n->c); Put(b, " "); DumpNode(b, n->d); Put(b, ")");
            break;

        case NODE_SWITCH:
            Put(b, "(switch "); DumpNode(b, n->a); DumpList(b, &n->list); Put(b, ")");
            break;

        case NODE_CASE:
            if (n->isDefault) Put(b, "(default");
            else { Put(b, "(case "); DumpNode(b, n->a); }
            DumpList(b, &n->list);
            Put(b, ")");
            break;

        case NODE_BREAK:    Put(b, "(break)");    break;
        case NODE_CONTINUE: Put(b, "(continue)"); break;

        case NODE_RETURN:
            Put(b, "(return");
            if (n->a) { Put(b, " "); DumpNode(b, n->a); }
            Put(b, ")");
            break;

        case NODE_EXPR_STMT: DumpNode(b, n->a); break;
        case NODE_EMPTY:     Put(b, "(empty)"); break;

        case NODE_FUNCTION:
            Put(b, "(func "); PutType(b, &n->type); Put(b, " %s (", n->name);
            for (int i = 0; i < n->list.count; i++)
            {
                if (i) Put(b, " ");
                DumpNode(b, n->list.items[i]);
            }
            Put(b, ") ");
            if (n->a) DumpNode(b, n->a);
            else      Put(b, ";");
            Put(b, ")");
            break;

        case NODE_PARAM:
            Put(b, "(param "); PutType(b, &n->type);
            if (n->name[0]) Put(b, " %s", n->name);
            Put(b, ")");
            break;

        case NODE_PROGRAM: Put(b, "(program"); DumpList(b, &n->list); Put(b, ")"); break;

        default: Put(b, "(?)"); break;
    }
}

int Ast_Dump(const Node *node, char *buffer, int size)
{
    Buf b = { buffer, size, 0 };
    if (size > 0) buffer[0] = '\0';
    DumpNode(&b, node);
    return b.len;
}

const char *Ast_KindName(NodeKind kind)
{
    static const char *NAMES[NODE_KIND_COUNT] = {
        "NUMBER", "CHAR_LIT", "STRING_LIT", "BOOL_LIT", "NULL_LIT", "IDENT",
        "BINARY", "UNARY", "POSTFIX", "ASSIGN", "TERNARY", "CALL", "INDEX",
        "MEMBER", "INIT_LIST",
        "VAR_DECL", "DECL_LIST", "BLOCK", "IF", "WHILE", "DO_WHILE", "FOR",
        "SWITCH", "CASE", "BREAK", "CONTINUE", "RETURN", "EXPR_STMT", "EMPTY",
        "FUNCTION", "PARAM", "PROGRAM",
    };
    if (kind < 0 || kind >= NODE_KIND_COUNT) return "?";
    return NAMES[kind];
}
