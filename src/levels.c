// levels.c - reads level files. See levels.h for the format.
// No raylib in here, so the level tests can run without a window.

#include "levels.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Level levels[LEVEL_MAX_LEVELS];
static int   levelCount = 0;

// A level that's always available, in case the levels folder is missing
static const char *FALLBACK_LEVEL =
    "title: 0-0  Lost\n"
    "type: escape\n"
    "[story]\n"
    "Bitty: \"I can't find the levels folder... is levels/levels.txt next to the game?\"\n"
    "[map]\n"
    "#####\n"
    "#B.E#\n"
    "#####\n"
    "[code]\n"
    "move(EAST);\n"
    "move(EAST);\n";

// ===========================================================================
// Small string helpers
// ===========================================================================
static void Trim(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    int start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, (size_t)(len - start + 1));
}

static void TrimTrailingNewlines(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == ' ')) s[--len] = '\0';
}

static void AppendLine(char *dest, int size, const char *line)
{
    int len = (int)strlen(dest);
    snprintf(dest + len, (size_t)(size - len), "%s\n", line);
}

static void SetError(char *error, int size, const char *fmt, const char *a, const char *b)
{
    if (error && size > 0) snprintf(error, (size_t)size, fmt, a ? a : "", b ? b : "");
}

// ===========================================================================
// Test lines
//   test   := call [op value] ["then" check ("," check)*]
//   call   := name "(" [value ("," value)*] ")"
//   check  := "#" number "==" value
//   value  := number | true | false | 'c' | "text" ["[" capacity "]"] | "{" values "}" | "&" number
// ===========================================================================
typedef struct {
    const char *s;
    int         pos;
    char       *err;
    int         errSize;
    bool        failed;
} TestParser;

static void SkipSpaces(TestParser *p)
{
    while (p->s[p->pos] == ' ' || p->s[p->pos] == '\t') p->pos++;
}

static void Fail(TestParser *p, const char *what)
{
    if (p->failed) return;
    p->failed = true;
    if (p->err) snprintf(p->err, (size_t)p->errSize, "%s (at position %d in: %s)", what, p->pos + 1, p->s);
}

static bool Accept(TestParser *p, const char *word)
{
    SkipSpaces(p);
    size_t n = strlen(word);
    if (strncmp(p->s + p->pos, word, n) == 0)
    {
        p->pos += (int)n;
        return true;
    }
    return false;
}

static int ParseInt(TestParser *p)
{
    SkipSpaces(p);
    int sign = 1, value = 0, digits = 0;
    if (p->s[p->pos] == '-') { sign = -1; p->pos++; }
    while (isdigit((unsigned char)p->s[p->pos]))
    {
        value = value * 10 + (p->s[p->pos] - '0');
        p->pos++;
        digits++;
    }
    if (!digits) Fail(p, "expected a number");
    return sign * value;
}

static int ParseEscapedChar(TestParser *p)
{
    char c = p->s[p->pos++];
    if (c != '\\') return (unsigned char)c;
    char e = p->s[p->pos++];
    switch (e)
    {
        case 'n':  return '\n';
        case 't':  return '\t';
        case '0':  return 0;
        case '\\': return '\\';
        case '\'': return '\'';
        case '"':  return '"';
        default:   Fail(p, "unknown escape"); return 0;
    }
}

static void ParseValue(TestParser *p, LevelValue *v)
{
    memset(v, 0, sizeof *v);
    SkipSpaces(p);
    char c = p->s[p->pos];

    if (c == '{')
    {
        p->pos++;
        v->kind = VALUE_ARRAY;
        SkipSpaces(p);
        if (p->s[p->pos] == '}') { p->pos++; return; }
        for (;;)
        {
            if (v->count >= LEVEL_VALUE_MAX) { Fail(p, "too many values in { }"); return; }
            LevelValue item;
            ParseValue(p, &item);
            if (p->failed) return;
            if (item.kind != VALUE_INT) { Fail(p, "{ } can only hold numbers or chars"); return; }
            v->values[v->count++] = item.value;
            if (Accept(p, "}")) return;
            if (!Accept(p, ",")) { Fail(p, "expected , or }"); return; }
        }
    }
    else if (c == '"')
    {
        p->pos++;
        v->kind = VALUE_STRING;
        int n = 0;
        while (p->s[p->pos] && p->s[p->pos] != '"')
        {
            int ch = ParseEscapedChar(p);
            if (n < (int)sizeof v->text - 1) v->text[n++] = (char)ch;
        }
        v->text[n] = '\0';
        if (!Accept(p, "\"")) { Fail(p, "missing closing \""); return; }
        v->capacity = n + 1;
        if (p->s[p->pos] == '[')
        {
            p->pos++;
            int cap = ParseInt(p);
            if (!Accept(p, "]")) { Fail(p, "expected ]"); return; }
            if (cap < n + 1) { Fail(p, "the [capacity] is smaller than the string"); return; }
            v->capacity = cap;
        }
    }
    else if (c == '\'')
    {
        p->pos++;
        v->kind = VALUE_INT;
        v->display = SHOW_CHAR;
        v->value = ParseEscapedChar(p);
        if (!Accept(p, "'")) Fail(p, "missing closing '");
    }
    else if (c == '&')
    {
        p->pos++;
        v->kind = VALUE_REF;
        v->value = ParseInt(p);
    }
    else if (strncmp(p->s + p->pos, "true", 4) == 0)  { p->pos += 4; v->display = SHOW_BOOL; v->value = 1; }
    else if (strncmp(p->s + p->pos, "false", 5) == 0) { p->pos += 5; v->display = SHOW_BOOL; v->value = 0; }
    else
    {
        v->kind = VALUE_INT;
        v->value = ParseInt(p);
    }
}

bool Level_ParseTest(const char *line, LevelTest *t, char *error, int errorSize)
{
    memset(t, 0, sizeof *t);
    snprintf(t->text, sizeof t->text, "%s", line);
    Trim(t->text);

    TestParser p = { t->text, 0, error, errorSize, false };

    // function name
    SkipSpaces(&p);
    int n = 0;
    while (isalnum((unsigned char)p.s[p.pos]) || p.s[p.pos] == '_')
    {
        if (n < (int)sizeof t->func - 1) t->func[n++] = p.s[p.pos];
        p.pos++;
    }
    t->func[n] = '\0';
    if (!n) Fail(&p, "a test must start with a function name");

    // arguments
    if (!p.failed && !Accept(&p, "(")) Fail(&p, "expected (");
    if (!p.failed && !Accept(&p, ")"))
    {
        for (;;)
        {
            if (t->argc >= LEVEL_MAX_ARGS) { Fail(&p, "too many arguments"); break; }
            ParseValue(&p, &t->args[t->argc++]);
            if (p.failed) break;
            if (Accept(&p, ")")) break;
            if (!Accept(&p, ",")) { Fail(&p, "expected , or )"); break; }
        }
    }

    // expected return value
    if (!p.failed)
    {
        CompareOp op = CMP_EQ;
        bool has = true;
        if      (Accept(&p, "==")) op = CMP_EQ;
        else if (Accept(&p, "!=")) op = CMP_NE;
        else if (Accept(&p, "<"))  op = CMP_LT;
        else if (Accept(&p, ">"))  op = CMP_GT;
        else has = false;

        if (has)
        {
            t->hasExpect = true;
            t->op = op;
            ParseValue(&p, &t->expect);
            if (!p.failed && t->expect.kind != VALUE_INT) Fail(&p, "a return value must be a number, bool or char");
        }
    }

    // checks on the arguments after the call
    if (!p.failed && Accept(&p, "then"))
    {
        for (;;)
        {
            if (t->checkCount >= LEVEL_MAX_CHECKS) { Fail(&p, "too many checks"); break; }
            if (!Accept(&p, "#")) { Fail(&p, "expected #1, #2... after then"); break; }
            LevelCheck *c = &t->checks[t->checkCount++];
            c->argIndex = ParseInt(&p);
            if (!p.failed && (c->argIndex < 1 || c->argIndex > t->argc)) { Fail(&p, "#number is not one of the arguments"); break; }
            if (!p.failed && t->args[c->argIndex - 1].kind == VALUE_INT)
                { Fail(&p, "#number must point at an array, string or &value argument"); break; }
            if (!Accept(&p, "==")) { Fail(&p, "expected =="); break; }
            ParseValue(&p, &c->value);
            if (p.failed || !Accept(&p, ",")) break;
        }
    }

    SkipSpaces(&p);
    if (!p.failed && p.s[p.pos] != '\0') Fail(&p, "unexpected text at the end");
    if (!p.failed && !t->hasExpect && t->checkCount == 0) Fail(&p, "a test needs == value and/or then #n == ...");
    return !p.failed;
}

void Level_FormatValue(const LevelValue *v, char *out, int size)
{
    int len = 0;
    out[0] = '\0';
    switch (v->kind)
    {
        case VALUE_ARRAY:
            len += snprintf(out + len, (size_t)(size - len), "{");
            for (int i = 0; i < v->count && len < size - 12; i++)
                len += snprintf(out + len, (size_t)(size - len), i ? ", %d" : "%d", v->values[i]);
            snprintf(out + len, (size_t)(size - len), "}");
            break;
        case VALUE_STRING:
            snprintf(out, (size_t)size, "\"%s\"", v->text);
            break;
        case VALUE_REF:
            snprintf(out, (size_t)size, "&%d", v->value);
            break;
        default:
            if (v->display == SHOW_BOOL)      snprintf(out, (size_t)size, "%s", v->value ? "true" : "false");
            else if (v->display == SHOW_CHAR) snprintf(out, (size_t)size, "'%c'", v->value);
            else                              snprintf(out, (size_t)size, "%d", v->value);
            break;
    }
}

// ===========================================================================
// Level files
// ===========================================================================
typedef enum {
    SEC_NONE, SEC_STORY, SEC_TALK, SEC_LESSON, SEC_HINT, SEC_MAP, SEC_CODE, SEC_SOLUTION, SEC_TESTS
} Section;

static Section SectionFromHeader(const char *line)
{
    if (strcmp(line, "[story]") == 0)    return SEC_STORY;
    if (strcmp(line, "[talk]") == 0)     return SEC_TALK;
    if (strcmp(line, "[lesson]") == 0)   return SEC_LESSON;
    if (strcmp(line, "[hint]") == 0)     return SEC_HINT;
    if (strcmp(line, "[map]") == 0)      return SEC_MAP;
    if (strcmp(line, "[code]") == 0)     return SEC_CODE;
    if (strcmp(line, "[solution]") == 0) return SEC_SOLUTION;
    if (strcmp(line, "[tests]") == 0)    return SEC_TESTS;
    return SEC_NONE;
}

// Repair levels without a [map] get a corridor: Bitty, one gate per test (max 5), the exit
static void MakeGateCorridor(Level *lv)
{
    int gates = lv->testCount < 5 ? lv->testCount : 5;
    char row[LEVEL_MAX_COLS + 1];
    int n = 0;
    row[n++] = '#';
    row[n++] = 'B';
    for (int g = 0; g < gates; g++) { row[n++] = '.'; row[n++] = 'G'; }
    row[n++] = '.';
    row[n++] = 'E';
    row[n++] = '#';
    row[n] = '\0';

    lv->mapRows = 3;
    memset(lv->map[0], '#', (size_t)n); lv->map[0][n] = '\0';
    memcpy(lv->map[1], row, (size_t)n + 1);
    memset(lv->map[2], '#', (size_t)n); lv->map[2][n] = '\0';
}

bool Level_Parse(const char *text, const char *fileName, Level *lv, char *error, int errorSize)
{
    memset(lv, 0, sizeof *lv);
    snprintf(lv->file, sizeof lv->file, "%s", fileName ? fileName : "(level)");
    lv->maxSteps = 2000000;

    Section sec = SEC_NONE;
    const char *p = text;
    char line[512];
    int lineNo = 0;

    while (*p)
    {
        // read one line (handles \n and \r\n)
        int n = 0;
        while (*p && *p != '\n')
        {
            if (*p != '\r' && n < (int)sizeof line - 1) line[n++] = *p;
            p++;
        }
        if (*p == '\n') p++;
        line[n] = '\0';
        lineNo++;

        char trimmed[512];
        snprintf(trimmed, sizeof trimmed, "%s", line);
        Trim(trimmed);

        Section next = SectionFromHeader(trimmed);
        if (next != SEC_NONE) { sec = next; continue; }

        switch (sec)
        {
            case SEC_NONE:
            {
                if (trimmed[0] == '\0' || trimmed[0] == '#') break;   // blank or comment
                char *colon = strchr(trimmed, ':');
                if (!colon)
                {
                    char num[16];
                    snprintf(num, sizeof num, "%d", lineNo);
                    SetError(error, errorSize, "line %s: expected 'key: value' or a [section] (%s)", num, trimmed);
                    return false;
                }
                *colon = '\0';
                char *key = trimmed, *value = colon + 1;
                Trim(key);
                Trim(value);
                if      (strcmp(key, "title") == 0)        snprintf(lv->title, sizeof lv->title, "%s", value);
                else if (strcmp(key, "type") == 0)         lv->type = (strcmp(value, "repair") == 0) ? LEVEL_REPAIR : LEVEL_ESCAPE;
                else if (strcmp(key, "ban_indexing") == 0) lv->banIndexing = (strcmp(value, "yes") == 0 || strcmp(value, "true") == 0);
                else if (strcmp(key, "max_steps") == 0)    lv->maxSteps = atoi(value);
                break;
            }
            case SEC_STORY:    AppendLine(lv->story, sizeof lv->story, line); break;
            case SEC_TALK:     AppendLine(lv->talk, sizeof lv->talk, line); break;
            case SEC_LESSON:   AppendLine(lv->lesson, sizeof lv->lesson, line); break;
            case SEC_HINT:     AppendLine(lv->hint, sizeof lv->hint, line); break;
            case SEC_CODE:     AppendLine(lv->starterCode, sizeof lv->starterCode, line); break;
            case SEC_SOLUTION: AppendLine(lv->solution, sizeof lv->solution, line); break;

            case SEC_MAP:
                if (trimmed[0] == '\0') break;
                if (lv->mapRows >= LEVEL_MAX_ROWS)
                {
                    SetError(error, errorSize, "the [map] has too many rows (max 12)%s%s", NULL, NULL);
                    return false;
                }
                if ((int)strlen(trimmed) > LEVEL_MAX_COLS)
                {
                    SetError(error, errorSize, "a [map] row is too long (max 16): %s%s", trimmed, NULL);
                    return false;
                }
                memcpy(lv->map[lv->mapRows++], trimmed, strlen(trimmed) + 1);   // length checked above
                break;

            case SEC_TESTS:
                if (trimmed[0] == '\0' || trimmed[0] == '#') break;
                if (lv->testCount >= LEVEL_MAX_TESTS)
                {
                    SetError(error, errorSize, "too many tests (max 12)%s%s", NULL, NULL);
                    return false;
                }
                if (!Level_ParseTest(trimmed, &lv->tests[lv->testCount], error, errorSize)) return false;
                lv->testCount++;
                break;
        }
    }

    TrimTrailingNewlines(lv->story);
    TrimTrailingNewlines(lv->lesson);
    TrimTrailingNewlines(lv->hint);

    // ---- Check it makes sense ----
    if (!lv->title[0]) { SetError(error, errorSize, "missing 'title:'%s%s", NULL, NULL); return false; }

    if (lv->type == LEVEL_REPAIR)
    {
        if (lv->testCount == 0) { SetError(error, errorSize, "a repair level needs [tests]%s%s", NULL, NULL); return false; }
        if (lv->mapRows == 0) MakeGateCorridor(lv);
    }

    bool hasStart = false, hasExit = false;
    for (int r = 0; r < lv->mapRows; r++)
    {
        if (strchr(lv->map[r], 'B')) hasStart = true;
        if (strchr(lv->map[r], 'E')) hasExit = true;
    }
    if (!hasStart || !hasExit)
    {
        SetError(error, errorSize, "the [map] needs a B (Bitty's start) and an E (exit)%s%s", NULL, NULL);
        return false;
    }
    return true;
}

// ===========================================================================
// Loading the folder
// ===========================================================================
static char *ReadWholeFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 1000000) { fclose(f); return NULL; }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int Levels_LoadFolder(const char *folder, char *error, int errorSize)
{
    levelCount = 0;
    if (error && errorSize > 0) error[0] = '\0';

    char path[512];
    snprintf(path, sizeof path, "%s/levels.txt", folder);
    char *index = ReadWholeFile(path);

    if (index)
    {
        const char *p = index;
        while (*p)
        {
            // one line of levels.txt = one file name
            char file[128];
            int n = 0;
            while (*p && *p != '\n')
            {
                if (*p != '\r' && n < (int)sizeof file - 1) file[n++] = *p;
                p++;
            }
            if (*p == '\n') p++;
            file[n] = '\0';
            Trim(file);
            if (!file[0] || file[0] == '#') continue;
            if (levelCount >= LEVEL_MAX_LEVELS) break;

            snprintf(path, sizeof path, "%s/%s", folder, file);
            char *text = ReadWholeFile(path);
            char err[256] = "";
            if (!text)
            {
                if (error && !error[0]) snprintf(error, (size_t)errorSize, "%s: file not found", file);
                continue;
            }
            if (Level_Parse(text, file, &levels[levelCount], err, sizeof err)) levelCount++;
            else if (error && !error[0]) snprintf(error, (size_t)errorSize, "%s: %s", file, err);
            free(text);
        }
        free(index);
    }
    else if (error)
    {
        snprintf(error, (size_t)errorSize, "couldn't open %s", path);
    }

    if (levelCount == 0)
    {
        Level_Parse(FALLBACK_LEVEL, "(built in)", &levels[0], NULL, 0);
        levelCount = 1;
        return 0;
    }
    return levelCount;
}

int Levels_Count(void)
{
    return levelCount;
}

const Level *Levels_Get(int index)
{
    if (levelCount == 0)
    {
        Level_Parse(FALLBACK_LEVEL, "(built in)", &levels[0], NULL, 0);
        levelCount = 1;
    }
    if (index < 0 || index >= levelCount) index = 0;
    return &levels[index];
}
