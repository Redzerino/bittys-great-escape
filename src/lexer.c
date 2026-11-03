// lexer.c - turns the player's source code into tokens. See lexer.h.
//
// How it works:
//   The lexer walks through the code one character at a time.
//   At each position it looks at the current character and decides what
//   kind of token starts there:
//     - a letter or _      -> a name (identifier) or keyword
//     - a digit            -> a number
//     - '                  -> a char literal
//     - "                  -> a string literal
//     - anything else      -> an operator or punctuation
//   Whitespace, comments and #include lines are skipped.

#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Lexer state: where we are in the source
// ---------------------------------------------------------------------------
typedef struct {
    const char *src;
    int         pos;
    int         line;
    int         col;
    TokenList  *list;
} Lexer;

static char Peek(const Lexer *lx)       { return lx->src[lx->pos]; }
static char PeekNext(const Lexer *lx)   { return lx->src[lx->pos] ? lx->src[lx->pos + 1] : '\0'; }

static char Advance(Lexer *lx)
{
    char c = lx->src[lx->pos++];
    if (c == '\n') { lx->line++; lx->col = 1; }
    else           { lx->col++; }
    return c;
}

// If the next character is 'expected', consume it and return true.
static bool Match(Lexer *lx, char expected)
{
    if (Peek(lx) != expected) return false;
    Advance(lx);
    return true;
}

// ---------------------------------------------------------------------------
// Adding tokens and errors
// ---------------------------------------------------------------------------
static bool AddToken(Lexer *lx, TokenType type, const char *text, int value, int line, int col)
{
    TokenList *list = lx->list;
    if (list->count == list->capacity)
    {
        int newCap = list->capacity ? list->capacity * 2 : 64;
        Token *grown = realloc(list->tokens, (size_t)newCap * sizeof(Token));
        if (!grown) return false;
        list->tokens = grown;
        list->capacity = newCap;
    }

    Token *t = &list->tokens[list->count++];
    t->type = type;
    t->value = value;
    t->line = line;
    t->col = col;
    strncpy(t->text, text, TOKEN_TEXT_MAX - 1);
    t->text[TOKEN_TEXT_MAX - 1] = '\0';
    return true;
}

static void SetError(Lexer *lx, int line, int col, const char *msg)
{
    TokenList *list = lx->list;
    if (list->hasError) return;   // keep the first error only
    list->hasError = true;
    list->errorLine = line;
    list->errorCol = col;
    snprintf(list->errorMsg, LEXER_ERROR_MAX, "Line %d: %s", line, msg);
}

// ---------------------------------------------------------------------------
// Keywords
// ---------------------------------------------------------------------------
typedef struct { const char *word; TokenType type; } Keyword;

static const Keyword KEYWORDS[] = {
    { "int", TOK_INT },       { "char", TOK_CHAR },       { "bool", TOK_BOOL },
    { "void", TOK_VOID },     { "const", TOK_CONST },
    { "if", TOK_IF },         { "else", TOK_ELSE },       { "switch", TOK_SWITCH },
    { "case", TOK_CASE },     { "default", TOK_DEFAULT },
    { "for", TOK_FOR },       { "while", TOK_WHILE },     { "do", TOK_DO },
    { "break", TOK_BREAK },   { "continue", TOK_CONTINUE }, { "return", TOK_RETURN },
    { "true", TOK_TRUE },     { "false", TOK_FALSE },
    { "NULL", TOK_NULL },     { "nullptr", TOK_NULL },
    { "struct", TOK_STRUCT }, { "class", TOK_CLASS },
    { "public", TOK_PUBLIC }, { "private", TOK_PRIVATE },
    { "new", TOK_NEW },       { "delete", TOK_DELETE },   { "this", TOK_THIS },
};

static TokenType KeywordOrIdent(const char *word)
{
    int n = (int)(sizeof(KEYWORDS) / sizeof(KEYWORDS[0]));
    for (int i = 0; i < n; i++)
    {
        if (strcmp(word, KEYWORDS[i].word) == 0) return KEYWORDS[i].type;
    }
    return TOK_IDENT;
}

// ---------------------------------------------------------------------------
// Skipping whitespace, comments and preprocessor lines
// ---------------------------------------------------------------------------
static void SkipWhitespaceAndComments(Lexer *lx)
{
    for (;;)
    {
        char c = Peek(lx);

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            Advance(lx);
        }
        else if (c == '/' && PeekNext(lx) == '/')           // // line comment
        {
            while (Peek(lx) != '\n' && Peek(lx) != '\0') Advance(lx);
        }
        else if (c == '/' && PeekNext(lx) == '*')           // /* block comment */
        {
            int startLine = lx->line, startCol = lx->col;
            Advance(lx); Advance(lx);
            while (!(Peek(lx) == '*' && PeekNext(lx) == '/'))
            {
                if (Peek(lx) == '\0')
                {
                    SetError(lx, startLine, startCol,
                             "this /* comment is never closed. Add */ to end it.");
                    return;
                }
                Advance(lx);
            }
            Advance(lx); Advance(lx);
        }
        else if (c == '#')                                  // #include <stdio.h> etc.
        {
            // The game provides printf and friends itself, so preprocessor
            // lines are allowed but ignored.
            while (Peek(lx) != '\n' && Peek(lx) != '\0') Advance(lx);
        }
        else
        {
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Escape sequences inside '...' and "..."
// Returns the decoded character, or -1 if the escape is not supported.
// ---------------------------------------------------------------------------
static int ReadEscape(Lexer *lx)
{
    char c = Advance(lx);   // the character after the backslash
    switch (c)
    {
        case 'n':  return '\n';
        case 't':  return '\t';
        case '0':  return '\0';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"':  return '"';
        default:   return -1;
    }
}

// ---------------------------------------------------------------------------
// Individual token readers
// ---------------------------------------------------------------------------
static void ReadIdentifier(Lexer *lx, int line, int col)
{
    char buf[TOKEN_TEXT_MAX];
    int len = 0;

    while (isalnum((unsigned char)Peek(lx)) || Peek(lx) == '_')
    {
        char c = Advance(lx);
        if (len < TOKEN_TEXT_MAX - 1) buf[len++] = c;
    }
    buf[len] = '\0';

    AddToken(lx, KeywordOrIdent(buf), buf, 0, line, col);
}

static void ReadNumber(Lexer *lx, int line, int col)
{
    char buf[TOKEN_TEXT_MAX];
    int len = 0;
    int  value = 0;
    bool overflow = false;

    while (isdigit((unsigned char)Peek(lx)))
    {
        char c = Advance(lx);
        int digit = c - '0';
        if (len < TOKEN_TEXT_MAX - 1) buf[len++] = c;

        // Check BEFORE multiplying, so value never overflows.
        // (long is only 32-bit on Windows, so we can't rely on a bigger type.)
        if (!overflow && value > (2147483647 - digit) / 10) overflow = true;
        if (!overflow) value = value * 10 + digit;
    }
    buf[len] = '\0';

    if (isalpha((unsigned char)Peek(lx)) || Peek(lx) == '_')
    {
        SetError(lx, line, col,
                 "a name can't start with a digit. Names must start with a letter or _.");
        return;
    }
    if (Peek(lx) == '.' && isdigit((unsigned char)PeekNext(lx)))
    {
        SetError(lx, line, col,
                 "decimal numbers (float/double) aren't available inside the machine. Use int.");
        return;
    }
    if (overflow)
    {
        SetError(lx, line, col,
                 "that number is too big for an int (max 2147483647).");
        return;
    }

    AddToken(lx, TOK_NUMBER, buf, value, line, col);
}

static void ReadCharLiteral(Lexer *lx, int line, int col)
{
    Advance(lx);   // opening '

    if (Peek(lx) == '\'' )
    {
        SetError(lx, line, col, "'' is empty. A char holds exactly one character, e.g. 'A'.");
        return;
    }
    if (Peek(lx) == '\n' || Peek(lx) == '\0')
    {
        SetError(lx, line, col, "this char is missing its closing '.");
        return;
    }

    int value;
    if (Peek(lx) == '\\')
    {
        Advance(lx);
        value = ReadEscape(lx);
        if (value < 0)
        {
            SetError(lx, line, col, "unknown escape. Supported: \\n \\t \\0 \\\\ \\' \\\"");
            return;
        }
    }
    else
    {
        value = (unsigned char)Advance(lx);
    }

    if (!Match(lx, '\''))
    {
        SetError(lx, line, col,
                 "a char holds ONE character in single quotes, e.g. 'A'. "
                 "For text use double quotes: \"Hello\".");
        return;
    }

    char text[2] = { (char)value, '\0' };
    AddToken(lx, TOK_CHAR_LIT, text, value, line, col);
}

static void ReadStringLiteral(Lexer *lx, int line, int col)
{
    char buf[TOKEN_TEXT_MAX];
    int len = 0;

    Advance(lx);   // opening "

    while (Peek(lx) != '"')
    {
        if (Peek(lx) == '\n' || Peek(lx) == '\0')
        {
            SetError(lx, line, col, "this string is missing its closing \".");
            return;
        }

        int c;
        if (Peek(lx) == '\\')
        {
            Advance(lx);
            c = ReadEscape(lx);
            if (c < 0)
            {
                SetError(lx, line, col, "unknown escape in string. Supported: \\n \\t \\0 \\\\ \\' \\\"");
                return;
            }
        }
        else
        {
            c = (unsigned char)Advance(lx);
        }

        if (len >= TOKEN_TEXT_MAX - 1)
        {
            SetError(lx, line, col, "this string is too long (max 127 characters).");
            return;
        }
        buf[len++] = (char)c;
    }
    Advance(lx);   // closing "
    buf[len] = '\0';

    AddToken(lx, TOK_STRING_LIT, buf, len, line, col);
}

// Operators and punctuation. Two-character operators are checked first
// so that "==" is not read as "=" followed by "=".
static void ReadSymbol(Lexer *lx, int line, int col)
{
    int  startPos = lx->pos;
    char c = Advance(lx);
    TokenType type;
    char text[3] = { c, '\0', '\0' };

    switch (c)
    {
        case '(': type = TOK_LPAREN;    break;
        case ')': type = TOK_RPAREN;    break;
        case '{': type = TOK_LBRACE;    break;
        case '}': type = TOK_RBRACE;    break;
        case '[': type = TOK_LBRACKET;  break;
        case ']': type = TOK_RBRACKET;  break;
        case ';': type = TOK_SEMICOLON; break;
        case ',': type = TOK_COMMA;     break;
        case '?': type = TOK_QUESTION;  break;
        case '.': type = TOK_DOT;       break;

        case ':': type = Match(lx, ':') ? TOK_SCOPE : TOK_COLON; break;

        case '+':
            if      (Match(lx, '+')) type = TOK_PLUS_PLUS;
            else if (Match(lx, '=')) type = TOK_PLUS_ASSIGN;
            else                     type = TOK_PLUS;
            break;
        case '-':
            if      (Match(lx, '-')) type = TOK_MINUS_MINUS;
            else if (Match(lx, '=')) type = TOK_MINUS_ASSIGN;
            else if (Match(lx, '>')) type = TOK_ARROW;
            else                     type = TOK_MINUS;
            break;
        case '*': type = Match(lx, '=') ? TOK_STAR_ASSIGN    : TOK_STAR;    break;
        case '/': type = Match(lx, '=') ? TOK_SLASH_ASSIGN   : TOK_SLASH;   break;
        case '%': type = Match(lx, '=') ? TOK_PERCENT_ASSIGN : TOK_PERCENT; break;
        case '=': type = Match(lx, '=') ? TOK_EQ             : TOK_ASSIGN;  break;
        case '!': type = Match(lx, '=') ? TOK_NEQ            : TOK_NOT;     break;

        case '<':
            if      (Match(lx, '=')) type = TOK_LTE;
            else if (Match(lx, '<')) type = TOK_SHIFT_LEFT;
            else                     type = TOK_LT;
            break;
        case '>':
            if      (Match(lx, '=')) type = TOK_GTE;
            else if (Match(lx, '>')) type = TOK_SHIFT_RIGHT;
            else                     type = TOK_GT;
            break;

        case '&': type = Match(lx, '&') ? TOK_AND : TOK_AMPERSAND; break;
        case '|':
            if (Match(lx, '|')) { type = TOK_OR; break; }
            SetError(lx, line, col, "a single | isn't supported. Did you mean || (OR)?");
            return;

        default:
        {
            char msg[96];
            if ((unsigned char)c >= 128)
                snprintf(msg, sizeof msg,
                         "unexpected character. Check for smart quotes or symbols pasted from Word.");
            else
                snprintf(msg, sizeof msg, "unexpected character '%c'.", c);
            SetError(lx, line, col, msg);
            return;
        }
    }

    // If Match() consumed a second character, it's a two-char operator
    if (lx->pos - startPos == 2)
    {
        text[1] = lx->src[lx->pos - 1];
    }

    AddToken(lx, type, text, 0, line, col);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool Lexer_Tokenize(const char *source, TokenList *list)
{
    memset(list, 0, sizeof(*list));

    Lexer lx = { source ? source : "", 0, 1, 1, list };

    for (;;)
    {
        SkipWhitespaceAndComments(&lx);
        if (list->hasError) break;

        int line = lx.line, col = lx.col;
        char c = Peek(&lx);

        if (c == '\0')
        {
            AddToken(&lx, TOK_EOF, "", 0, line, col);
            break;
        }

        if (isalpha((unsigned char)c) || c == '_') ReadIdentifier(&lx, line, col);
        else if (isdigit((unsigned char)c))         ReadNumber(&lx, line, col);
        else if (c == '\'')                         ReadCharLiteral(&lx, line, col);
        else if (c == '"')                          ReadStringLiteral(&lx, line, col);
        else                                        ReadSymbol(&lx, line, col);

        if (list->hasError) break;
    }

    if (list->hasError)
    {
        AddToken(&lx, TOK_ERROR, list->errorMsg, 0, list->errorLine, list->errorCol);
        return false;
    }
    return true;
}

void Lexer_Free(TokenList *list)
{
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

const char *Lexer_TokenName(TokenType type)
{
    static const char *NAMES[TOK_COUNT] = {
        [TOK_EOF] = "EOF", [TOK_ERROR] = "ERROR",
        [TOK_IDENT] = "IDENT", [TOK_NUMBER] = "NUMBER",
        [TOK_CHAR_LIT] = "CHAR_LIT", [TOK_STRING_LIT] = "STRING_LIT",
        [TOK_INT] = "INT", [TOK_CHAR] = "CHAR", [TOK_BOOL] = "BOOL",
        [TOK_VOID] = "VOID", [TOK_CONST] = "CONST",
        [TOK_IF] = "IF", [TOK_ELSE] = "ELSE", [TOK_SWITCH] = "SWITCH",
        [TOK_CASE] = "CASE", [TOK_DEFAULT] = "DEFAULT",
        [TOK_FOR] = "FOR", [TOK_WHILE] = "WHILE", [TOK_DO] = "DO",
        [TOK_BREAK] = "BREAK", [TOK_CONTINUE] = "CONTINUE", [TOK_RETURN] = "RETURN",
        [TOK_TRUE] = "TRUE", [TOK_FALSE] = "FALSE", [TOK_NULL] = "NULL",
        [TOK_STRUCT] = "STRUCT", [TOK_CLASS] = "CLASS",
        [TOK_PUBLIC] = "PUBLIC", [TOK_PRIVATE] = "PRIVATE",
        [TOK_NEW] = "NEW", [TOK_DELETE] = "DELETE", [TOK_THIS] = "THIS",
        [TOK_PLUS] = "+", [TOK_MINUS] = "-", [TOK_STAR] = "*",
        [TOK_SLASH] = "/", [TOK_PERCENT] = "%",
        [TOK_PLUS_PLUS] = "++", [TOK_MINUS_MINUS] = "--",
        [TOK_ASSIGN] = "=", [TOK_PLUS_ASSIGN] = "+=", [TOK_MINUS_ASSIGN] = "-=",
        [TOK_STAR_ASSIGN] = "*=", [TOK_SLASH_ASSIGN] = "/=", [TOK_PERCENT_ASSIGN] = "%=",
        [TOK_EQ] = "==", [TOK_NEQ] = "!=", [TOK_LT] = "<", [TOK_GT] = ">",
        [TOK_LTE] = "<=", [TOK_GTE] = ">=",
        [TOK_AND] = "&&", [TOK_OR] = "||", [TOK_NOT] = "!",
        [TOK_AMPERSAND] = "&", [TOK_ARROW] = "->", [TOK_DOT] = ".",
        [TOK_SHIFT_LEFT] = "<<", [TOK_SHIFT_RIGHT] = ">>", [TOK_SCOPE] = "::",
        [TOK_LPAREN] = "(", [TOK_RPAREN] = ")", [TOK_LBRACE] = "{", [TOK_RBRACE] = "}",
        [TOK_LBRACKET] = "[", [TOK_RBRACKET] = "]",
        [TOK_SEMICOLON] = ";", [TOK_COMMA] = ",", [TOK_COLON] = ":", [TOK_QUESTION] = "?",
    };
    if (type < 0 || type >= TOK_COUNT || !NAMES[type]) return "?";
    return NAMES[type];
}
