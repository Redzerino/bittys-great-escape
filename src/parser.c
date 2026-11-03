// parser.c - recursive-descent parser for Bitty's C subset. See parser.h.
//
// How it works:
//   There is one function per grammar rule. Each one looks at the current
//   token, decides what it is looking at, consumes the tokens it needs and
//   returns a tree Node. Rules call each other, which is why it's called
//   "recursive descent".
//
//   Expressions are parsed by precedence, lowest first. Each level calls the
//   next-higher level for its operands, so * binds tighter than +:
//
//     assignment   =  +=  -=  *=  /=  %=      (right to left)
//     ternary      ? :
//     or           ||
//     and          &&
//     equality     ==  !=
//     comparison   <  >  <=  >=
//     shift        <<  >>                     (used by cout <<)
//     additive     +  -
//     multiply     *  /  %
//     unary        -  !  *  &  ++  --         (prefix)
//     postfix      f()  a[i]  x++  x--  .  ->
//     primary      numbers, names, "strings", ( ... )
//
//   Errors use setjmp/longjmp: the first error jumps straight back to
//   Parser_Parse, which frees everything and reports the message.

#include "parser.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_NESTING 150     // stops deeply nested code from overflowing the C stack

typedef struct {
    const Token *tokens;
    int          count;
    int          pos;
    int          depth;
    Ast         *ast;
    ParseResult *res;
    jmp_buf      jump;
} Parser;

typedef Node *(*ParseFn)(Parser *p);

// Forward declarations (the grammar rules call each other)
static Node *ParseStatement(Parser *p);
static Node *ParseExpression(Parser *p);
static Node *ParseAssignment(Parser *p);
static Node *ParseTernary(Parser *p);
static Node *ParseUnary(Parser *p);
static Node *ParseBlock(Parser *p);

// ===========================================================================
// Token helpers
// ===========================================================================
static const Token *Peek(const Parser *p) { return &p->tokens[p->pos]; }

static const Token *PeekAt(const Parser *p, int offset)
{
    int i = p->pos + offset;
    if (i >= p->count) i = p->count - 1;   // the last token is always EOF
    return &p->tokens[i];
}

static const Token *Previous(const Parser *p) { return &p->tokens[p->pos > 0 ? p->pos - 1 : 0]; }

static bool Check(const Parser *p, TokenType type) { return Peek(p)->type == type; }

static const Token *Advance(Parser *p)
{
    const Token *t = Peek(p);
    if (t->type != TOK_EOF) p->pos++;
    return t;
}

static bool Match(Parser *p, TokenType type)
{
    if (!Check(p, type)) return false;
    Advance(p);
    return true;
}

// How to show a token in an error message
static const char *Describe(const Token *t)
{
    static char buf[TOKEN_TEXT_MAX + 16];
    switch (t->type)
    {
        case TOK_EOF:        return "the end of the code";
        case TOK_STRING_LIT: return "a string";
        case TOK_CHAR_LIT:   return "a char";
        case TOK_NUMBER:     snprintf(buf, sizeof buf, "the number %s", t->text); return buf;
        default:             snprintf(buf, sizeof buf, "'%s'", t->text);          return buf;
    }
}

// ===========================================================================
// Errors and warnings
// ===========================================================================
static void ErrorAt(Parser *p, int line, int col, const char *fmt, ...)
{
    char msg[PARSE_MSG_MAX - 20];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);

    p->res->hasError  = true;
    p->res->errorLine = line;
    p->res->errorCol  = col;
    snprintf(p->res->errorMsg, PARSE_MSG_MAX, "Line %d: %s", line, msg);

    longjmp(p->jump, 1);
}

#define ErrorAtToken(p, tok, ...) ErrorAt((p), (tok)->line, (tok)->col, __VA_ARGS__)

static void Warn(Parser *p, int line, const char *fmt, ...)
{
    ParseResult *r = p->res;
    if (r->warningCount >= PARSE_MAX_WARNINGS) return;

    char msg[PARSE_MSG_MAX - 20];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);

    snprintf(r->warnings[r->warningCount], PARSE_MSG_MAX, "Line %d: %s", line, msg);
    r->warningLines[r->warningCount] = line;
    r->warningCount++;
}

// Consume a token of 'type' or stop with "expected <what> but found ...".
static const Token *Expect(Parser *p, TokenType type, const char *what)
{
    if (Check(p, type)) return Advance(p);
    ErrorAtToken(p, Peek(p), "expected %s but found %s.", what, Describe(Peek(p)));
    return NULL;
}

// A missing ; is reported on the line of the statement that needs it,
// not the line after (which is where the compiler would notice).
static void ExpectSemicolon(Parser *p)
{
    if (Match(p, TOK_SEMICOLON)) return;
    ErrorAtToken(p, Previous(p), "missing ; at the end of this statement.");
}

static void EnterNesting(Parser *p)
{
    if (++p->depth > MAX_NESTING)
        ErrorAtToken(p, Peek(p), "this code is nested too deeply. Try splitting it up.");
}

static void LeaveNesting(Parser *p) { p->depth--; }

// ===========================================================================
// Node helpers
// ===========================================================================
static Node *NewNodeAt(Parser *p, NodeKind kind, int line, int col)
{
    Node *n = Ast_NewNode(p->ast, kind, line, col);
    if (!n) ErrorAt(p, line, col, "out of memory.");
    return n;
}

static Node *NewNode(Parser *p, NodeKind kind, const Token *at)
{
    return NewNodeAt(p, kind, at->line, at->col);
}

static void AddChild(Parser *p, NodeList *list, Node *child)
{
    if (!NodeList_Add(list, child)) ErrorAt(p, child->line, child->col, "out of memory.");
}

static void SetName(Node *n, const char *name)
{
    strncpy(n->name, name, TOKEN_TEXT_MAX - 1);
    n->name[TOKEN_TEXT_MAX - 1] = '\0';
}

static bool IsLValue(const Node *n)
{
    return n->kind == NODE_IDENT || n->kind == NODE_INDEX || n->kind == NODE_MEMBER ||
           (n->kind == NODE_UNARY && n->op == TOK_STAR);
}

// ===========================================================================
// Types and declarations
// ===========================================================================
static bool IsBaseTypeToken(TokenType t)
{
    return t == TOK_INT || t == TOK_CHAR || t == TOK_BOOL || t == TOK_VOID;
}

static bool IsTypeStart(TokenType t)
{
    return IsBaseTypeToken(t) || t == TOK_CONST;
}

// Friendly errors for types that exist in C/C++ but not inside the machine.
static void CheckUnsupportedType(Parser *p)
{
    const Token *t = Peek(p);
    if (t->type != TOK_IDENT) return;

    TokenType next = PeekAt(p, 1)->type;
    if (next != TOK_IDENT && next != TOK_STAR && next != TOK_AMPERSAND) return;

    if (strcmp(t->text, "string") == 0)
        ErrorAtToken(p, t, "'string' isn't available. In C a string is a char array, "
                           "e.g. char name[20] = \"Bitty\";");

    static const char *OTHERS[] = { "float", "double", "long", "short", "unsigned" };
    for (int i = 0; i < (int)(sizeof OTHERS / sizeof OTHERS[0]); i++)
    {
        if (strcmp(t->text, OTHERS[i]) == 0)
            ErrorAtToken(p, t, "'%s' isn't available inside the machine. Use int, char or bool.", t->text);
    }
}

// const? (int | char | bool | void) const?
static TypeInfo ParseBaseType(Parser *p)
{
    TypeInfo t;
    memset(&t, 0, sizeof t);

    if (Match(p, TOK_CONST)) t.isConst = true;

    const Token *tok = Peek(p);
    switch (tok->type)
    {
        case TOK_INT:  t.base = TYPE_INT;  break;
        case TOK_CHAR: t.base = TYPE_CHAR; break;
        case TOK_BOOL: t.base = TYPE_BOOL; break;
        case TOK_VOID: t.base = TYPE_VOID; break;
        default:
            ErrorAtToken(p, tok, "expected a type (int, char, bool or void) but found %s.", Describe(tok));
    }
    Advance(p);

    if (Match(p, TOK_CONST)) t.isConst = true;   // "int const x" is also legal C
    return t;
}

// The *'s and & that come before a name:  int **p   int &r
static void ParsePointerAndRef(Parser *p, TypeInfo *t)
{
    while (Match(p, TOK_STAR)) t->pointerDepth++;
    if (Match(p, TOK_AMPERSAND)) t->isRef = true;
}

// The [5] or [4][6] that come after a name.
// 'allowEmpty': the first [] may be left empty (parameters, or when an initialiser gives the size).
static void ParseArrayDims(Parser *p, TypeInfo *t, bool allowEmpty)
{
    while (Check(p, TOK_LBRACKET))
    {
        const Token *open = Advance(p);

        if (t->arrayDims >= MAX_ARRAY_DIMS)
            ErrorAtToken(p, open, "only 1D and 2D arrays are supported.");

        int size = -1;
        if (Check(p, TOK_NUMBER))
        {
            const Token *num = Advance(p);
            size = num->value;
            if (size <= 0) ErrorAtToken(p, num, "an array must hold at least 1 element.");
        }
        else if (Check(p, TOK_RBRACKET))
        {
            if (!allowEmpty || t->arrayDims > 0)
                ErrorAtToken(p, open, "this [] needs a size. Only the first [] can be left empty, "
                                      "e.g. int grid[][6].");
        }
        else
        {
            ErrorAtToken(p, Peek(p), "an array size must be a number, e.g. int arr[5].");
        }

        Expect(p, TOK_RBRACKET, "] to close the array size");
        t->arraySize[t->arrayDims++] = size;
    }
}

// { 1, 2, 3 }  or  { {1, 2}, {3, 4} }
static Node *ParseInitList(Parser *p)
{
    const Token *open = Expect(p, TOK_LBRACE, "{");
    Node *list = NewNode(p, NODE_INIT_LIST, open);

    EnterNesting(p);
    while (!Check(p, TOK_RBRACE))
    {
        if (Check(p, TOK_LBRACE)) AddChild(p, &list->list, ParseInitList(p));
        else                      AddChild(p, &list->list, ParseAssignment(p));

        if (!Match(p, TOK_COMMA)) break;     // a trailing comma before } is allowed
    }
    LeaveNesting(p);

    if (!Check(p, TOK_RBRACE))
        ErrorAtToken(p, Peek(p), "expected , or } in the { } list but found %s.", Describe(Peek(p)));
    Advance(p);
    return list;
}

// Checks that only make sense once we know both the type and the initial value
static void CheckDeclaration(Parser *p, Node *decl)
{
    TypeInfo *t = &decl->type;
    Node *init = decl->a;

    if (t->isRef && !init)
        ErrorAt(p, decl->line, decl->col,
                "a reference must be linked to a variable when it's declared, e.g. int &r = x;");

    if (t->arrayDims == 0)
    {
        if (init && init->kind == NODE_INIT_LIST)
            ErrorAt(p, init->line, init->col, "{ } can only be used to fill an array.");
        return;
    }

    // ---- Arrays ----
    if (init && init->kind != NODE_INIT_LIST && init->kind != NODE_STRING_LIT)
        ErrorAt(p, init->line, init->col,
                "an array is filled with { }, e.g. int arr[3] = {1, 2, 3};");

    if (init && init->kind == NODE_STRING_LIT && !(t->base == TYPE_CHAR && t->arrayDims == 1))
        ErrorAt(p, init->line, init->col, "only a char array can hold a string.");

    int given;   // how many elements the initialiser provides
    if (!init)                                given = 0;
    else if (init->kind == NODE_STRING_LIT)   given = init->intValue + 1;   // + the hidden '\0'
    else                                      given = init->list.count;

    if (t->arraySize[0] < 0)
    {
        if (!init)
            ErrorAt(p, decl->line, decl->col,
                    "give the array a size, e.g. int %s[5];", decl->name);
        t->arraySize[0] = given;   // int arr[] = {1, 2, 3}  ->  int arr[3]
    }
    else if (given > t->arraySize[0])
    {
        if (init->kind == NODE_STRING_LIT)
            ErrorAt(p, init->line, init->col,
                    "\"%s\" needs %d chars (%d letters + the hidden '\\0') but %s only holds %d.",
                    init->name, given, init->intValue, decl->name, t->arraySize[0]);
        else
            ErrorAt(p, init->line, init->col,
                    "too many values: %s holds %d but you gave %d.",
                    decl->name, t->arraySize[0], given);
    }

    // 2D: each row must fit too
    if (t->arrayDims == 2 && init && init->kind == NODE_INIT_LIST)
    {
        for (int i = 0; i < init->list.count; i++)
        {
            Node *row = init->list.items[i];
            if (row->kind != NODE_INIT_LIST)
                ErrorAt(p, row->line, row->col, "each row of a 2D array needs its own { }.");
            if (row->list.count > t->arraySize[1])
                ErrorAt(p, row->line, row->col,
                        "too many values in row %d: each row holds %d but you gave %d.",
                        i, t->arraySize[1], row->list.count);
        }
    }
}

// One name in a declaration:  *p   arr[5] = {1,2}   &r = x
static Node *ParseDeclarator(Parser *p, TypeInfo base)
{
    TypeInfo t = base;
    ParsePointerAndRef(p, &t);

    const Token *name = Peek(p);
    if (name->type != TOK_IDENT)
    {
        if (name->type >= TOK_INT && name->type <= TOK_THIS)
            ErrorAtToken(p, name, "'%s' is a keyword, so it can't be used as a variable name.", name->text);
        ErrorAtToken(p, name, "expected a variable name but found %s.", Describe(name));
    }
    Advance(p);

    if (t.base == TYPE_VOID && t.pointerDepth == 0)
        ErrorAtToken(p, name, "a variable can't be void. Use int, char or bool.");

    ParseArrayDims(p, &t, true);

    Node *decl = NewNode(p, NODE_VAR_DECL, name);
    decl->type = t;
    SetName(decl, name->text);

    if (Match(p, TOK_ASSIGN))
    {
        if (Check(p, TOK_LBRACE)) decl->a = ParseInitList(p);
        else                      decl->a = ParseAssignment(p);
    }

    CheckDeclaration(p, decl);
    return decl;
}

static const char *FOR_COMMA_MSG =
    "a for loop's parts are separated by ; not , e.g. for (int i = 0; i < 10; i++).";

static bool IsComparisonOp(TokenType t)
{
    return t == TOK_LT || t == TOK_GT || t == TOK_LTE || t == TOK_GTE || t == TOK_EQ || t == TOK_NEQ;
}

// int x = 5;    int x = 5, y = 10;    char *s;
// 'inForLoop': spot the classic  for (int i = 0, i < 10, i++)  mistake.
static Node *ParseDeclarationEx(Parser *p, bool inForLoop)
{
    const Token *start = Peek(p);
    TypeInfo base = ParseBaseType(p);

    Node *first = ParseDeclarator(p, base);
    Node *result = first;

    if (Check(p, TOK_COMMA))
    {
        result = NewNode(p, NODE_DECL_LIST, start);
        AddChild(p, &result->list, first);

        while (Check(p, TOK_COMMA))
        {
            // , i < 10   looks like a condition, not another variable
            if (inForLoop && PeekAt(p, 1)->type == TOK_IDENT && IsComparisonOp(PeekAt(p, 2)->type))
                ErrorAtToken(p, Peek(p), "%s", FOR_COMMA_MSG);

            Advance(p);
            AddChild(p, &result->list, ParseDeclarator(p, base));
        }
    }

    if (inForLoop && !Check(p, TOK_SEMICOLON))
        ErrorAtToken(p, Peek(p), "%s", FOR_COMMA_MSG);

    ExpectSemicolon(p);
    return result;
}

static Node *ParseDeclaration(Parser *p)
{
    return ParseDeclarationEx(p, false);
}

// ===========================================================================
// Functions
// ===========================================================================

// Looks ahead without consuming: [const] type [const] *... [&] name (
static bool LooksLikeFunction(const Parser *p)
{
    int i = 0;
    if (PeekAt(p, i)->type == TOK_CONST) i++;
    if (!IsBaseTypeToken(PeekAt(p, i)->type)) return false;
    i++;
    if (PeekAt(p, i)->type == TOK_CONST) i++;
    while (PeekAt(p, i)->type == TOK_STAR) i++;
    if (PeekAt(p, i)->type == TOK_AMPERSAND) i++;
    return PeekAt(p, i)->type == TOK_IDENT && PeekAt(p, i + 1)->type == TOK_LPAREN;
}

static Node *ParseParam(Parser *p)
{
    const Token *start = Peek(p);
    CheckUnsupportedType(p);
    if (!IsTypeStart(start->type))
        ErrorAtToken(p, start, "each parameter needs a type, e.g. int x. Found %s.", Describe(start));

    TypeInfo t = ParseBaseType(p);
    ParsePointerAndRef(p, &t);

    Node *param = NewNode(p, NODE_PARAM, start);
    if (Check(p, TOK_IDENT)) SetName(param, Advance(p)->text);

    ParseArrayDims(p, &t, true);

    if (t.base == TYPE_VOID && t.pointerDepth == 0)
        ErrorAtToken(p, start, "a parameter can't be void.");

    param->type = t;
    return param;
}

// int add(int a, int b) { ... }     or a prototype:  int add(int, int);
static Node *ParseFunction(Parser *p)
{
    TypeInfo ret = ParseBaseType(p);
    ParsePointerAndRef(p, &ret);

    const Token *name = Expect(p, TOK_IDENT, "a function name");
    Node *fn = NewNode(p, NODE_FUNCTION, name);
    fn->type = ret;
    SetName(fn, name->text);

    Expect(p, TOK_LPAREN, "(");
    if (Check(p, TOK_VOID) && PeekAt(p, 1)->type == TOK_RPAREN)
    {
        Advance(p);                                 // int main(void)
    }
    else if (!Check(p, TOK_RPAREN))
    {
        do { AddChild(p, &fn->list, ParseParam(p)); } while (Match(p, TOK_COMMA));
    }
    Expect(p, TOK_RPAREN, ") to close the parameter list");

    if (Match(p, TOK_SEMICOLON)) return fn;        // prototype, no body

    if (!Check(p, TOK_LBRACE))
        ErrorAtToken(p, Peek(p), "expected { to start the body of %s() but found %s.",
                     fn->name, Describe(Peek(p)));

    fn->a = ParseBlock(p);
    return fn;
}

// ===========================================================================
// Statements
// ===========================================================================
static Node *ParseBlock(Parser *p)
{
    const Token *open = Expect(p, TOK_LBRACE, "{");
    Node *block = NewNode(p, NODE_BLOCK, open);

    while (!Check(p, TOK_RBRACE))
    {
        if (Check(p, TOK_EOF))
            ErrorAtToken(p, open, "this { is never closed. Add a matching }.");
        AddChild(p, &block->list, ParseStatement(p));
    }
    Advance(p);   // }
    return block;
}

// ( condition ) - with a warning for the classic = instead of == mistake
static Node *ParseCondition(Parser *p, const char *keyword)
{
    if (!Check(p, TOK_LPAREN))
        ErrorAtToken(p, Peek(p), "expected ( after %s but found %s.", keyword, Describe(Peek(p)));
    Advance(p);

    Node *cond = ParseExpression(p);
    if (cond->kind == NODE_ASSIGN && cond->op == TOK_ASSIGN)
        Warn(p, cond->line, "= inside an %s assigns a value. Did you mean == (compare)?", keyword);

    Expect(p, TOK_RPAREN, ") to close the condition");
    return cond;
}

static Node *ParseIf(Parser *p)
{
    Node *n = NewNode(p, NODE_IF, Advance(p));
    n->a = ParseCondition(p, "if");

    if (Check(p, TOK_SEMICOLON))
        Warn(p, Peek(p)->line, "the ; straight after if (...) ends the if, so the next line ALWAYS runs.");

    n->b = ParseStatement(p);
    if (Match(p, TOK_ELSE)) n->c = ParseStatement(p);
    return n;
}

static Node *ParseWhile(Parser *p)
{
    Node *n = NewNode(p, NODE_WHILE, Advance(p));
    n->a = ParseCondition(p, "while");

    if (Check(p, TOK_SEMICOLON))
        Warn(p, Peek(p)->line, "the ; straight after while (...) makes an empty loop. "
                               "The body below isn't part of it.");

    n->b = ParseStatement(p);
    return n;
}

static Node *ParseDoWhile(Parser *p)
{
    Node *n = NewNode(p, NODE_DO_WHILE, Advance(p));
    n->a = ParseStatement(p);
    Expect(p, TOK_WHILE, "while (...) after the do { } block");
    n->b = ParseCondition(p, "while");

    if (!Match(p, TOK_SEMICOLON))
        ErrorAtToken(p, Previous(p), "a do-while loop needs a ; after while (...).");
    return n;
}

static Node *ParseFor(Parser *p)
{
    Node *n = NewNode(p, NODE_FOR, Advance(p));
    Expect(p, TOK_LPAREN, "( after for");

    // init
    if (Match(p, TOK_SEMICOLON))
    {
        n->a = NULL;
    }
    else if (IsTypeStart(Peek(p)->type))
    {
        n->a = ParseDeclarationEx(p, true);         // includes the ;
    }
    else
    {
        Node *init = NewNode(p, NODE_EXPR_STMT, Peek(p));
        init->a = ParseExpression(p);
        n->a = init;
        if (!Match(p, TOK_SEMICOLON)) ErrorAtToken(p, Peek(p), "%s", FOR_COMMA_MSG);
    }

    // condition
    if (!Check(p, TOK_SEMICOLON))
    {
        n->b = ParseExpression(p);
        if (n->b->kind == NODE_ASSIGN && n->b->op == TOK_ASSIGN)
            Warn(p, n->b->line, "= in a for loop condition assigns a value. Did you mean == or < ?");
    }
    if (!Match(p, TOK_SEMICOLON)) ErrorAtToken(p, Peek(p), "%s", FOR_COMMA_MSG);

    // step
    if (!Check(p, TOK_RPAREN)) n->c = ParseExpression(p);
    Expect(p, TOK_RPAREN, ") to close the for loop");

    if (Check(p, TOK_SEMICOLON))
        Warn(p, Peek(p)->line, "the ; straight after for (...) makes an empty loop. "
                               "The body below only runs once.");

    n->d = ParseStatement(p);
    return n;
}

static bool IsConstantCaseValue(const Node *v)
{
    return v->kind == NODE_NUMBER || v->kind == NODE_CHAR_LIT || v->kind == NODE_BOOL_LIT;
}

static Node *ParseSwitch(Parser *p)
{
    Node *sw = NewNode(p, NODE_SWITCH, Advance(p));
    sw->a = ParseCondition(p, "switch");

    const Token *open = Expect(p, TOK_LBRACE, "{ after switch (...)");
    Node *current = NULL;
    bool haveDefault = false;

    while (!Check(p, TOK_RBRACE))
    {
        const Token *t = Peek(p);

        if (t->type == TOK_EOF)
        {
            ErrorAtToken(p, open, "this switch's { is never closed. Add a matching }.");
        }
        else if (Match(p, TOK_CASE))
        {
            current = NewNode(p, NODE_CASE, t);
            current->a = ParseTernary(p);
            if (!IsConstantCaseValue(current->a))
                ErrorAtToken(p, t, "a case value must be a fixed number or char, e.g. case 3: or case 'A':");
            Expect(p, TOK_COLON, ": after the case value");
            AddChild(p, &sw->list, current);
        }
        else if (Match(p, TOK_DEFAULT))
        {
            if (haveDefault) ErrorAtToken(p, t, "a switch can only have one default.");
            haveDefault = true;
            current = NewNode(p, NODE_CASE, t);
            current->isDefault = true;
            Expect(p, TOK_COLON, ": after default");
            AddChild(p, &sw->list, current);
        }
        else
        {
            if (!current)
                ErrorAtToken(p, t, "code inside a switch must come after a case label, e.g. case 1:");
            AddChild(p, &current->list, ParseStatement(p));
        }
    }
    Advance(p);   // }
    return sw;
}

static Node *ParseStatementInner(Parser *p)
{
    const Token *t = Peek(p);
    Node *n;

    switch (t->type)
    {
        case TOK_LBRACE: return ParseBlock(p);
        case TOK_IF:     return ParseIf(p);
        case TOK_WHILE:  return ParseWhile(p);
        case TOK_DO:     return ParseDoWhile(p);
        case TOK_FOR:    return ParseFor(p);
        case TOK_SWITCH: return ParseSwitch(p);

        case TOK_BREAK:
            n = NewNode(p, NODE_BREAK, Advance(p));
            ExpectSemicolon(p);
            return n;

        case TOK_CONTINUE:
            n = NewNode(p, NODE_CONTINUE, Advance(p));
            ExpectSemicolon(p);
            return n;

        case TOK_RETURN:
            n = NewNode(p, NODE_RETURN, Advance(p));
            if (!Check(p, TOK_SEMICOLON)) n->a = ParseExpression(p);
            ExpectSemicolon(p);
            return n;

        case TOK_SEMICOLON:
            return NewNode(p, NODE_EMPTY, Advance(p));

        case TOK_ELSE:
            ErrorAtToken(p, t, "this else has no matching if. Check the { } braces above it.");
            break;

        case TOK_CASE:
        case TOK_DEFAULT:
            ErrorAtToken(p, t, "'%s' can only be used inside a switch.", t->text);
            break;

        case TOK_RBRACE:
            ErrorAtToken(p, t, "this } doesn't match any {. Check your braces.");
            break;

        default:
            break;
    }

    CheckUnsupportedType(p);

    if (IsTypeStart(t->type))
    {
        if (LooksLikeFunction(p))
            ErrorAtToken(p, t, "a function can't be written inside another function. "
                               "Did you forget a } above?");
        return ParseDeclaration(p);
    }

    // Expression statement:  move(NORTH);   x++;   arr[i] = 0;
    n = NewNode(p, NODE_EXPR_STMT, t);
    n->a = ParseExpression(p);

    if (n->a->kind == NODE_BINARY && n->a->op == TOK_EQ)
        Warn(p, t->line, "this compares with == but doesn't change anything. Did you mean = ?");

    ExpectSemicolon(p);
    return n;
}

static Node *ParseStatement(Parser *p)
{
    EnterNesting(p);
    Node *n = ParseStatementInner(p);
    LeaveNesting(p);
    return n;
}

// ===========================================================================
// Expressions (lowest precedence first)
// ===========================================================================
static Node *ParseExpression(Parser *p)
{
    return ParseAssignment(p);
}

static bool IsAssignOp(TokenType t)
{
    return t == TOK_ASSIGN || t == TOK_PLUS_ASSIGN || t == TOK_MINUS_ASSIGN ||
           t == TOK_STAR_ASSIGN || t == TOK_SLASH_ASSIGN || t == TOK_PERCENT_ASSIGN;
}

static Node *ParseAssignment(Parser *p)
{
    EnterNesting(p);
    Node *left = ParseTernary(p);

    if (IsAssignOp(Peek(p)->type))
    {
        const Token *op = Advance(p);
        if (!IsLValue(left))
            ErrorAtToken(p, op, "the left side of %s must be a variable, e.g. x %s 5;", op->text, op->text);

        Node *n = NewNodeAt(p, NODE_ASSIGN, left->line, left->col);
        n->op = op->type;
        n->a  = left;
        n->b  = ParseAssignment(p);      // right to left:  a = b = 0
        left  = n;
    }

    LeaveNesting(p);
    return left;
}

static Node *ParseOr(Parser *p);

static Node *ParseTernary(Parser *p)
{
    Node *cond = ParseOr(p);
    if (!Check(p, TOK_QUESTION)) return cond;

    Advance(p);
    Node *n = NewNodeAt(p, NODE_TERNARY, cond->line, cond->col);
    n->a = cond;
    n->b = ParseExpression(p);
    Expect(p, TOK_COLON, ": in the ? : expression");
    n->c = ParseTernary(p);
    return n;
}

// Shared loop for every left-to-right binary level
static Node *ParseBinaryLevel(Parser *p, ParseFn next, const TokenType *ops, int opCount)
{
    Node *left = next(p);

    for (;;)
    {
        TokenType t = Peek(p)->type;
        bool found = false;
        for (int i = 0; i < opCount; i++)
        {
            if (ops[i] == t) { found = true; break; }
        }
        if (!found) return left;

        const Token *op = Advance(p);
        Node *n = NewNode(p, NODE_BINARY, op);
        n->op = op->type;
        n->a  = left;
        n->b  = next(p);
        n->line = left->line;   // report binary expressions at their left side
        n->col  = left->col;
        left = n;
    }
}

static Node *ParseMultiplicative(Parser *p)
{
    static const TokenType ops[] = { TOK_STAR, TOK_SLASH, TOK_PERCENT };
    return ParseBinaryLevel(p, ParseUnary, ops, 3);
}

static Node *ParseAdditive(Parser *p)
{
    static const TokenType ops[] = { TOK_PLUS, TOK_MINUS };
    return ParseBinaryLevel(p, ParseMultiplicative, ops, 2);
}

static Node *ParseShift(Parser *p)
{
    static const TokenType ops[] = { TOK_SHIFT_LEFT, TOK_SHIFT_RIGHT };
    return ParseBinaryLevel(p, ParseAdditive, ops, 2);
}

static Node *ParseComparison(Parser *p)
{
    static const TokenType ops[] = { TOK_LT, TOK_GT, TOK_LTE, TOK_GTE };
    return ParseBinaryLevel(p, ParseShift, ops, 4);
}

static Node *ParseEquality(Parser *p)
{
    static const TokenType ops[] = { TOK_EQ, TOK_NEQ };
    return ParseBinaryLevel(p, ParseComparison, ops, 2);
}

static Node *ParseAnd(Parser *p)
{
    static const TokenType ops[] = { TOK_AND };
    return ParseBinaryLevel(p, ParseEquality, ops, 1);
}

static Node *ParseOr(Parser *p)
{
    static const TokenType ops[] = { TOK_OR };
    return ParseBinaryLevel(p, ParseAnd, ops, 1);
}

static Node *ParsePostfix(Parser *p);

static Node *ParseUnary(Parser *p)
{
    const Token *t = Peek(p);

    switch (t->type)
    {
        case TOK_PLUS:
            Advance(p);
            return ParseUnary(p);            // +x is just x

        case TOK_MINUS:
        case TOK_NOT:
        case TOK_STAR:
        case TOK_AMPERSAND:
        case TOK_PLUS_PLUS:
        case TOK_MINUS_MINUS:
        {
            Advance(p);
            EnterNesting(p);
            Node *operand = ParseUnary(p);
            LeaveNesting(p);

            // -5 is stored as the number -5 (needed for e.g. case -1:)
            if (t->type == TOK_MINUS && operand->kind == NODE_NUMBER)
            {
                operand->intValue = -operand->intValue;
                operand->line = t->line;
                operand->col  = t->col;
                return operand;
            }

            if ((t->type == TOK_PLUS_PLUS || t->type == TOK_MINUS_MINUS) && !IsLValue(operand))
                ErrorAtToken(p, t, "%s only works on a variable, e.g. %sx", t->text, t->text);

            if (t->type == TOK_AMPERSAND && !IsLValue(operand))
                ErrorAtToken(p, t, "& (address of) only works on a variable, e.g. &x");

            Node *n = NewNode(p, NODE_UNARY, t);
            n->op = t->type;
            n->a  = operand;
            return n;
        }

        default:
            return ParsePostfix(p);
    }
}

static Node *ParsePrimary(Parser *p);

static Node *ParsePostfix(Parser *p)
{
    Node *n = ParsePrimary(p);

    for (;;)
    {
        const Token *t = Peek(p);

        if (t->type == TOK_LPAREN)                           // call:  f(a, b)
        {
            if (n->kind != NODE_IDENT && n->kind != NODE_MEMBER)
                ErrorAtToken(p, t, "only a function name can be called with ( ).");

            Advance(p);
            Node *call = NewNodeAt(p, NODE_CALL, n->line, n->col);
            call->a = n;
            if (!Check(p, TOK_RPAREN))
            {
                do { AddChild(p, &call->list, ParseAssignment(p)); } while (Match(p, TOK_COMMA));
            }
            Expect(p, TOK_RPAREN, ") to close the function call");
            n = call;
        }
        else if (t->type == TOK_LBRACKET)                    // index:  arr[i]
        {
            Advance(p);
            Node *idx = NewNodeAt(p, NODE_INDEX, n->line, n->col);
            idx->a = n;
            idx->b = ParseExpression(p);
            Expect(p, TOK_RBRACKET, "] to close the array index");
            n = idx;
        }
        else if (t->type == TOK_PLUS_PLUS || t->type == TOK_MINUS_MINUS)   // x++  x--
        {
            if (!IsLValue(n))
                ErrorAtToken(p, t, "%s only works on a variable, e.g. x%s", t->text, t->text);
            Advance(p);
            Node *post = NewNodeAt(p, NODE_POSTFIX, n->line, n->col);
            post->op = t->type;
            post->a  = n;
            n = post;
        }
        else if (t->type == TOK_DOT || t->type == TOK_ARROW) // member:  n.next  n->next
        {
            Advance(p);
            const Token *member = Expect(p, TOK_IDENT, "a member name");
            Node *m = NewNodeAt(p, NODE_MEMBER, n->line, n->col);
            m->a = n;
            m->isArrow = (t->type == TOK_ARROW);
            SetName(m, member->text);
            n = m;
        }
        else
        {
            return n;
        }
    }
}

static Node *ParsePrimary(Parser *p)
{
    const Token *t = Peek(p);
    Node *n;

    switch (t->type)
    {
        case TOK_NUMBER:
            n = NewNode(p, NODE_NUMBER, Advance(p));
            n->intValue = t->value;
            return n;

        case TOK_CHAR_LIT:
            n = NewNode(p, NODE_CHAR_LIT, Advance(p));
            n->intValue = t->value;
            return n;

        case TOK_STRING_LIT:
            n = NewNode(p, NODE_STRING_LIT, Advance(p));
            SetName(n, t->text);
            n->intValue = t->value;     // length (the lexer stores it)
            return n;

        case TOK_TRUE:
        case TOK_FALSE:
            n = NewNode(p, NODE_BOOL_LIT, Advance(p));
            n->intValue = (t->type == TOK_TRUE);
            return n;

        case TOK_NULL:
            return NewNode(p, NODE_NULL_LIT, Advance(p));

        case TOK_IDENT:
        {
            n = NewNode(p, NODE_IDENT, Advance(p));
            SetName(n, t->text);

            // std::cout, std::endl
            while (Match(p, TOK_SCOPE))
            {
                const Token *part = Expect(p, TOK_IDENT, "a name after ::");
                size_t used = strlen(n->name);
                snprintf(n->name + used, TOKEN_TEXT_MAX - used, "::%s", part->text);
            }
            return n;
        }

        case TOK_LPAREN:
        {
            Advance(p);
            EnterNesting(p);
            Node *inner = ParseExpression(p);
            LeaveNesting(p);
            Expect(p, TOK_RPAREN, ") to close the (");
            return inner;
        }

        case TOK_LBRACE:
            ErrorAtToken(p, t, "{ } lists can only be used when creating an array, "
                               "e.g. int arr[3] = {1, 2, 3};");
            break;

        case TOK_EOF:
            ErrorAtToken(p, Previous(p), "the code ends unexpectedly here. Is something missing?");
            break;

        default:
            if (IsTypeStart(t->type))
                ErrorAtToken(p, t, "you can't declare a variable here. Declare it on its own line first.");
            ErrorAtToken(p, t, "expected a value or expression but found %s.", Describe(t));
            break;
    }
    return NULL;   // not reached: ErrorAt never returns
}

// ===========================================================================
// Program
// ===========================================================================
static Node *ParseProgram(Parser *p)
{
    Node *program = NewNode(p, NODE_PROGRAM, Peek(p));

    while (!Check(p, TOK_EOF))
    {
        // using namespace std;  is allowed and ignored
        if (Check(p, TOK_IDENT) && strcmp(Peek(p)->text, "using") == 0)
        {
            while (!Check(p, TOK_SEMICOLON) && !Check(p, TOK_EOF)) Advance(p);
            ExpectSemicolon(p);
            continue;
        }

        if (LooksLikeFunction(p)) AddChild(p, &program->list, ParseFunction(p));
        else                      AddChild(p, &program->list, ParseStatement(p));
    }
    return program;
}

// ===========================================================================
// Public API
// ===========================================================================
bool Parser_Parse(const TokenList *tokens, Ast *ast, ParseResult *result)
{
    memset(result, 0, sizeof *result);
    memset(ast, 0, sizeof *ast);

    if (!tokens || tokens->count == 0 || tokens->hasError)
    {
        result->hasError = true;
        if (tokens && tokens->hasError)
        {
            snprintf(result->errorMsg, PARSE_MSG_MAX, "%s", tokens->errorMsg);
            result->errorLine = tokens->errorLine;
            result->errorCol  = tokens->errorCol;
        }
        else
        {
            snprintf(result->errorMsg, PARSE_MSG_MAX, "There is no code to run.");
        }
        return false;
    }

    Parser p;
    memset(&p, 0, sizeof p);
    p.tokens = tokens->tokens;
    p.count  = tokens->count;
    p.ast    = ast;
    p.res    = result;

    if (setjmp(p.jump) != 0)
    {
        // An error jumped back here. Free whatever was built so far.
        Ast_Free(ast);
        return false;
    }

    ast->root = ParseProgram(&p);
    return true;
}

bool Parser_ParseSource(const char *source, Ast *ast, ParseResult *result)
{
    TokenList tokens;
    Lexer_Tokenize(source, &tokens);
    bool ok = Parser_Parse(&tokens, ast, result);
    Lexer_Free(&tokens);
    return ok;
}
