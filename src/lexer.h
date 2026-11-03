// lexer.h - turns the player's source code (text) into a list of tokens.
//
// Example:   int x = 5;
// becomes:   TOK_INT  TOK_IDENT(x)  TOK_ASSIGN  TOK_NUMBER(5)  TOK_SEMICOLON  TOK_EOF
//
// Every token remembers the line and column it came from, so the parser and
// the game can point at the exact spot when something goes wrong, and the
// editor can highlight the line that is running.

#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>

#define TOKEN_TEXT_MAX 128
#define LEXER_ERROR_MAX 256

typedef enum {
    // ---- Special ----
    TOK_EOF = 0,        // end of the code
    TOK_ERROR,          // something the lexer could not understand

    // ---- Literals and names ----
    TOK_IDENT,          // x, count, isLeapYear, move
    TOK_NUMBER,         // 42
    TOK_CHAR_LIT,       // 'A'  '\0'  '\n'
    TOK_STRING_LIT,     // "Hello"

    // ---- Type keywords ----
    TOK_INT, TOK_CHAR, TOK_BOOL, TOK_VOID, TOK_CONST,

    // ---- Control-flow keywords ----
    TOK_IF, TOK_ELSE, TOK_SWITCH, TOK_CASE, TOK_DEFAULT,
    TOK_FOR, TOK_WHILE, TOK_DO, TOK_BREAK, TOK_CONTINUE, TOK_RETURN,

    // ---- Value keywords ----
    TOK_TRUE, TOK_FALSE, TOK_NULL,          // true, false, NULL / nullptr

    // ---- C++ / structs (Rail Yard world) ----
    TOK_STRUCT, TOK_CLASS, TOK_PUBLIC, TOK_PRIVATE,
    TOK_NEW, TOK_DELETE, TOK_THIS,

    // ---- Arithmetic ----
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,   // + - * / %
    TOK_PLUS_PLUS, TOK_MINUS_MINUS,                          // ++ --

    // ---- Assignment ----
    TOK_ASSIGN,                                              // =
    TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN,                       // += -=
    TOK_STAR_ASSIGN, TOK_SLASH_ASSIGN, TOK_PERCENT_ASSIGN,   // *= /= %=

    // ---- Comparison ----
    TOK_EQ, TOK_NEQ,                                         // == !=
    TOK_LT, TOK_GT, TOK_LTE, TOK_GTE,                        // < > <= >=

    // ---- Logic ----
    TOK_AND, TOK_OR, TOK_NOT,                                // && || !

    // ---- Pointers / references / members ----
    TOK_AMPERSAND,      // &   address-of, or a C++ reference (int &x)
    TOK_ARROW,          // ->
    TOK_DOT,            // .

    // ---- C++ streams ----
    TOK_SHIFT_LEFT,     // <<  (cout << x)
    TOK_SHIFT_RIGHT,    // >>
    TOK_SCOPE,          // ::  (std::cout)

    // ---- Punctuation ----
    TOK_LPAREN, TOK_RPAREN,        // ( )
    TOK_LBRACE, TOK_RBRACE,        // { }
    TOK_LBRACKET, TOK_RBRACKET,    // [ ]
    TOK_SEMICOLON, TOK_COMMA,      // ; ,
    TOK_COLON, TOK_QUESTION,       // : ?

    TOK_COUNT           // number of token types (keep last)
} TokenType;

typedef struct {
    TokenType type;
    char      text[TOKEN_TEXT_MAX]; // exactly what the player typed (string/char literals: the decoded value)
    int       value;                // number value, or char code for TOK_CHAR_LIT
    int       line;                 // 1-based line number
    int       col;                  // 1-based column number
} Token;

typedef struct {
    Token *tokens;
    int    count;
    int    capacity;

    bool   hasError;
    char   errorMsg[LEXER_ERROR_MAX];   // plain-English message for the console
    int    errorLine;
    int    errorCol;
} TokenList;

// Tokenise 'source'. Returns true on success.
// On failure, list->hasError is set and list->errorMsg explains what went wrong.
// Always call Lexer_Free() afterwards, even if it failed.
bool Lexer_Tokenize(const char *source, TokenList *list);

void Lexer_Free(TokenList *list);

// Readable name of a token type, e.g. "TOK_IDENT" (handy for debugging)
const char *Lexer_TokenName(TokenType type);

#endif
