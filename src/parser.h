// parser.h - turns tokens into a syntax tree (see ast.h).
//
// The parser checks the GRAMMAR of the player's code: that every ( has a ),
// statements end with ;, an else has an if, and so on. If something is wrong
// it stops at the first problem and explains it in plain English, pointing at
// the line.
//
// It also gives WARNINGS for code that is valid C but almost certainly a
// mistake, such as  if (x = 5)  (assigns instead of compares).
//
// A program can contain:
//   - functions          int isLeapYear(int year) { ... }      (repair levels)
//   - loose statements   for (int i = 0; i < 3; i++) move(EAST); (escape levels)
//   - global variables   int score = 0;
// all mixed together, in any order.

#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include "ast.h"
#include "lexer.h"

#define PARSE_MSG_MAX       300
#define PARSE_MAX_WARNINGS  8

typedef struct {
    bool hasError;
    char errorMsg[PARSE_MSG_MAX];   // e.g. "Line 3: missing ; at the end of this statement."
    int  errorLine;
    int  errorCol;

    int  warningCount;
    char warnings[PARSE_MAX_WARNINGS][PARSE_MSG_MAX];
    int  warningLines[PARSE_MAX_WARNINGS];
} ParseResult;

// Parse a token list. Returns true on success; ast->root is then the program.
// On failure 'result' explains the error and the ast is left empty.
// Call Ast_Free(ast) when you are finished with the tree.
bool Parser_Parse(const TokenList *tokens, Ast *ast, ParseResult *result);

// Convenience: lex and parse source code in one step. Lexer errors are
// reported in 'result' in the same way as parser errors.
bool Parser_ParseSource(const char *source, Ast *ast, ParseResult *result);

#endif
