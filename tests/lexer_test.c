// lexer_test.c - checks the lexer on real exam-style code.
// Build and run with: test_lexer.bat  (Windows)  or  make test  (Linux)
//
// Each test tokenises a snippet and compares the token types against what
// we expect. At the end it prints how many passed.

#include "../src/lexer.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

static void PrintTokens(const TokenList *list)
{
    for (int i = 0; i < list->count; i++)
    {
        const Token *t = &list->tokens[i];
        printf("    line %2d col %2d  %-12s '%s'", t->line, t->col, Lexer_TokenName(t->type), t->text);
        if (t->type == TOK_NUMBER || t->type == TOK_CHAR_LIT) printf("  value=%d", t->value);
        printf("\n");
    }
}

// Check that 'src' tokenises into exactly the given types (ending with TOK_EOF)
static void ExpectTypes(const char *name, const char *src, const TokenType *expected, int n)
{
    TokenList list;
    bool ok = Lexer_Tokenize(src, &list);

    bool match = ok && list.count == n;
    for (int i = 0; match && i < n; i++)
    {
        if (list.tokens[i].type != expected[i]) match = false;
    }

    if (match) { passed++; printf("[PASS] %s\n", name); }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
        if (!ok) printf("    error: %s\n", list.errorMsg);
        PrintTokens(&list);
    }
    Lexer_Free(&list);
}

// Check that 'src' produces an error on the given line
static void ExpectError(const char *name, const char *src, int expectedLine)
{
    TokenList list;
    bool ok = Lexer_Tokenize(src, &list);

    if (!ok && list.errorLine == expectedLine)
    {
        passed++;
        printf("[PASS] %s  ->  %s\n", name, list.errorMsg);
    }
    else
    {
        failed++;
        printf("[FAIL] %s (expected an error on line %d)\n", name, expectedLine);
        if (!ok) printf("    got: %s\n", list.errorMsg);
    }
    Lexer_Free(&list);
}

#define TYPES(...) (const TokenType[]){ __VA_ARGS__ }, (int)(sizeof((const TokenType[]){ __VA_ARGS__ }) / sizeof(TokenType))

int main(void)
{
    printf("=== Lexer tests ===\n\n");

    ExpectTypes("declaration", "int x = 5;",
        TYPES(TOK_INT, TOK_IDENT, TOK_ASSIGN, TOK_NUMBER, TOK_SEMICOLON, TOK_EOF));

    ExpectTypes("game call", "move(NORTH);",
        TYPES(TOK_IDENT, TOK_LPAREN, TOK_IDENT, TOK_RPAREN, TOK_SEMICOLON, TOK_EOF));

    ExpectTypes("leap year condition",
        "return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;",
        TYPES(TOK_RETURN, TOK_LPAREN, TOK_IDENT, TOK_PERCENT, TOK_NUMBER, TOK_EQ, TOK_NUMBER,
              TOK_AND, TOK_IDENT, TOK_PERCENT, TOK_NUMBER, TOK_NEQ, TOK_NUMBER, TOK_RPAREN,
              TOK_OR, TOK_IDENT, TOK_PERCENT, TOK_NUMBER, TOK_EQ, TOK_NUMBER, TOK_SEMICOLON,
              TOK_EOF));

    ExpectTypes("for loop", "for (int i = 0; i < size; i++) { sum += arr[i]; }",
        TYPES(TOK_FOR, TOK_LPAREN, TOK_INT, TOK_IDENT, TOK_ASSIGN, TOK_NUMBER, TOK_SEMICOLON,
              TOK_IDENT, TOK_LT, TOK_IDENT, TOK_SEMICOLON, TOK_IDENT, TOK_PLUS_PLUS, TOK_RPAREN,
              TOK_LBRACE, TOK_IDENT, TOK_PLUS_ASSIGN, TOK_IDENT, TOK_LBRACKET, TOK_IDENT,
              TOK_RBRACKET, TOK_SEMICOLON, TOK_RBRACE, TOK_EOF));

    ExpectTypes("pointer swap (Exam 2)",
        "void swap_ints(int *a, int *b) { int t = *a; *a = *b; *b = t; }",
        TYPES(TOK_VOID, TOK_IDENT, TOK_LPAREN, TOK_INT, TOK_STAR, TOK_IDENT, TOK_COMMA,
              TOK_INT, TOK_STAR, TOK_IDENT, TOK_RPAREN, TOK_LBRACE,
              TOK_INT, TOK_IDENT, TOK_ASSIGN, TOK_STAR, TOK_IDENT, TOK_SEMICOLON,
              TOK_STAR, TOK_IDENT, TOK_ASSIGN, TOK_STAR, TOK_IDENT, TOK_SEMICOLON,
              TOK_STAR, TOK_IDENT, TOK_ASSIGN, TOK_IDENT, TOK_SEMICOLON, TOK_RBRACE, TOK_EOF));

    ExpectTypes("pointer arithmetic", "*(arr + i)",
        TYPES(TOK_STAR, TOK_LPAREN, TOK_IDENT, TOK_PLUS, TOK_IDENT, TOK_RPAREN, TOK_EOF));

    ExpectTypes("C++ reference param", "bool insertElement(int& size, int &count)",
        TYPES(TOK_BOOL, TOK_IDENT, TOK_LPAREN, TOK_INT, TOK_AMPERSAND, TOK_IDENT, TOK_COMMA,
              TOK_INT, TOK_AMPERSAND, TOK_IDENT, TOK_RPAREN, TOK_EOF));

    ExpectTypes("string terminator", "while (*str != '\\0') str++;",
        TYPES(TOK_WHILE, TOK_LPAREN, TOK_STAR, TOK_IDENT, TOK_NEQ, TOK_CHAR_LIT, TOK_RPAREN,
              TOK_IDENT, TOK_PLUS_PLUS, TOK_SEMICOLON, TOK_EOF));

    ExpectTypes("printf with string", "printf(\"%d\\n\", x);",
        TYPES(TOK_IDENT, TOK_LPAREN, TOK_STRING_LIT, TOK_COMMA, TOK_IDENT, TOK_RPAREN,
              TOK_SEMICOLON, TOK_EOF));

    ExpectTypes("cout", "std::cout << x << std::endl;",
        TYPES(TOK_IDENT, TOK_SCOPE, TOK_IDENT, TOK_SHIFT_LEFT, TOK_IDENT, TOK_SHIFT_LEFT,
              TOK_IDENT, TOK_SCOPE, TOK_IDENT, TOK_SEMICOLON, TOK_EOF));

    ExpectTypes("linked list (Rail Yard)", "Node *n = new Node(); n->next = nullptr;",
        TYPES(TOK_IDENT, TOK_STAR, TOK_IDENT, TOK_ASSIGN, TOK_NEW, TOK_IDENT, TOK_LPAREN,
              TOK_RPAREN, TOK_SEMICOLON, TOK_IDENT, TOK_ARROW, TOK_IDENT, TOK_ASSIGN,
              TOK_NULL, TOK_SEMICOLON, TOK_EOF));

    ExpectTypes("comments and #include are skipped",
        "#include <stdio.h>\n// a comment\n/* block\n comment */ int a;",
        TYPES(TOK_INT, TOK_IDENT, TOK_SEMICOLON, TOK_EOF));

    // ---- Values and line numbers ----
    {
        TokenList list;
        Lexer_Tokenize("int x;\n\n  x = 'A';\nx = 2147483647;", &list);
        bool ok = list.tokens[3].line == 3 && list.tokens[3].col == 3       // x on line 3, col 3
               && list.tokens[5].value == 65                                // 'A' = 65
               && list.tokens[9].value == 2147483647;                       // biggest int
        if (ok) { passed++; printf("[PASS] line/column numbers and values\n"); }
        else    { failed++; printf("[FAIL] line/column numbers and values\n"); PrintTokens(&list); }
        Lexer_Free(&list);
    }

    // ---- Errors with plain-English messages ----
    printf("\n--- Error messages the player would see ---\n");
    ExpectError("unclosed string",       "int a;\nprintf(\"hello);",      2);
    ExpectError("two chars in quotes",   "char c = 'AB';",               1);
    ExpectError("empty char",            "char c = '';",                 1);
    ExpectError("unclosed comment",      "int a;\n/* oops",              2);
    ExpectError("single pipe",           "if (a | b)",                   1);
    ExpectError("bad name",              "int 2fast = 1;",               1);
    ExpectError("float not allowed",     "\n\nint x = 3.5;",             3);
    ExpectError("too big for int",       "int x = 99999999999;",         1);
    ExpectError("unknown symbol",        "int x = 5 @ 3;",               1);

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
