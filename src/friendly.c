// friendly.c - plain-English errors with a FIX button. See friendly.h.
//
// The compiler's messages are already in plain English, but a beginner still
// has to work out WHERE the problem is and WHAT to type. This reads the
// message, looks at the code around it, and works out both.

#include "friendly.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Finding things in the source
// ---------------------------------------------------------------------------

// Byte offset of the start of a line (1-based), or -1
static int LineStart(const char *src, int line)
{
    int i = 0;
    for (int l = 1; l < line; l++)
    {
        const char *nl = strchr(src + i, '\n');
        if (!nl) return -1;
        i = (int)(nl - src) + 1;
    }
    return i;
}

static int LineEnd(const char *src, int start)
{
    const char *nl = strchr(src + start, '\n');
    return nl ? (int)(nl - src) : (int)strlen(src);
}

static bool IsWordChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

// Where a token that starts at 'at' ends
static int TokenEnd(const char *src, int at)
{
    char c = src[at];
    if (!c) return at;
    if (IsWordChar(c)) { while (IsWordChar(src[at])) at++; return at; }
    if (c == '"' || c == '\'')
    {
        at++;
        while (src[at] && src[at] != c && src[at] != '\n') { if (src[at] == '\\' && src[at + 1]) at++; at++; }
        return src[at] == c ? at + 1 : at;
    }
    if (strchr("+-=<>!&|", c)) { while (src[at] && strchr("+-=<>!&|", src[at])) at++; return at; }
    return at + 1;
}

// The first place 'word' appears as a whole word on a line (offset), or -1
static int FindWord(const char *src, int line, const char *word, bool call)
{
    int s = LineStart(src, line);
    if (s < 0) return -1;
    int e = LineEnd(src, s), n = (int)strlen(word);
    for (int i = s; i + n <= e; i++)
    {
        if (strncmp(src + i, word, (size_t)n) != 0) continue;
        if (i > s && IsWordChar(src[i - 1])) continue;
        if (IsWordChar(src[i + n])) continue;
        if (call)
        {
            int k = i + n;
            while (src[k] == ' ') k++;
            if (src[k] != '(') continue;
        }
        return i;
    }
    return -1;
}

// Column (1-based) of an offset
static int ColOf(const char *src, int at)
{
    int c = 1;
    for (int i = at - 1; i >= 0 && src[i] != '\n'; i--) c++;
    return c;
}

static int LineOfOffset(const char *src, int at)
{
    int l = 1;
    for (int i = 0; i < at && src[i]; i++) if (src[i] == '\n') l++;
    return l;
}

// The text between two markers in a message: Between("you used 'x' but", "'", "'")
static bool Between(const char *msg, const char *before, const char *after, char *out, int size)
{
    const char *a = strstr(msg, before);
    if (!a) return false;
    a += strlen(before);
    const char *b = strstr(a, after);
    if (!b || b - a >= size) return false;
    snprintf(out, (size_t)size, "%.*s", (int)(b - a), a);
    return true;
}

// ---------------------------------------------------------------------------
// "Did you mean...?"
// ---------------------------------------------------------------------------
static int Distance(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 30 || lb > 30) return 99;
    int d[31][31];
    for (int i = 0; i <= la; i++) d[i][0] = i;
    for (int j = 0; j <= lb; j++) d[0][j] = j;
    for (int i = 1; i <= la; i++)
        for (int j = 1; j <= lb; j++)
        {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int best = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < best) best = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + cost < best) best = d[i - 1][j - 1] + cost;
            d[i][j] = best;
        }
    return d[la][lb];
}

static bool SameIgnoringCase(const char *a, const char *b)
{
    while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; a++; b++; }
    return *a == *b;
}

#define MAX_NAMES 64
typedef struct { char name[32]; } Name;

// Every name the player could have meant: the built-in ones, and anything declared in their code
static int CollectNames(const char *src, bool functions, Name *names)
{
    static const char *FUNCS[] = { "move", "read", "write", "scan", "get_x", "get_y", "printf" };
    static const char *VALUES[] = { "EAST", "WEST", "NORTH", "SOUTH", "TILE_EMPTY", "TILE_WALL", "TILE_DATA",
                                    "TILE_GATE", "TILE_EXIT", "TILE_VOID", "true", "false" };
    int n = 0;
    if (functions) for (size_t i = 0; i < sizeof FUNCS / sizeof FUNCS[0]; i++) snprintf(names[n++].name, 32, "%s", FUNCS[i]);
    else for (size_t i = 0; i < sizeof VALUES / sizeof VALUES[0]; i++) snprintf(names[n++].name, 32, "%s", VALUES[i]);

    // declarations: a type, maybe some stars, then a name (followed by '(' for a function)
    static const char *TYPES[] = { "int", "char", "bool", "void", "float", "double" };
    for (const char *p = src; *p && n < MAX_NAMES; p++)
    {
        if (p != src && IsWordChar(p[-1])) continue;
        for (size_t t = 0; t < sizeof TYPES / sizeof TYPES[0]; t++)
        {
            size_t len = strlen(TYPES[t]);
            if (strncmp(p, TYPES[t], len) != 0 || IsWordChar(p[len])) continue;
            const char *q = p + len;
            while (*q == ' ' || *q == '*' || *q == '&') q++;
            if (!IsWordChar(*q)) continue;
            char word[32];
            int k = 0;
            while (IsWordChar(*q) && k < 31) word[k++] = *q++;
            word[k] = '\0';
            while (*q == ' ') q++;
            bool isFunc = (*q == '(');
            if (isFunc == functions && n < MAX_NAMES) snprintf(names[n++].name, 32, "%s", word);
            // more names on the same line: int a, b, c
            while (!isFunc && n < MAX_NAMES)
            {
                while (*q && *q != ',' && *q != ';' && *q != ')' && *q != '\n') q++;
                if (*q != ',') break;
                q++;
                while (*q == ' ' || *q == '*' || *q == '&') q++;
                k = 0;
                while (IsWordChar(*q) && k < 31) word[k++] = *q++;
                word[k] = '\0';
                if (k > 0 && !functions) snprintf(names[n++].name, 32, "%s", word);
                else break;
            }
        }
    }
    return n;
}

// The closest name, or NULL. *caseOnly = it's the same word with different capitals.
static const char *Closest(const char *word, const Name *names, int n, bool *caseOnly)
{
    *caseOnly = false;
    for (int i = 0; i < n; i++)
        if (strcmp(names[i].name, word) != 0 && SameIgnoringCase(names[i].name, word)) { *caseOnly = true; return names[i].name; }
    const char *best = NULL;
    int bestD = 3;
    int limit = strlen(word) <= 3 ? 1 : 2;
    for (int i = 0; i < n; i++)
    {
        if (strcmp(names[i].name, word) == 0) continue;
        int d = Distance(word, names[i].name);
        if (d <= limit && d < bestD) { bestD = d; best = names[i].name; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// The explanations
// ---------------------------------------------------------------------------
static const char *TextFix(const char *from, const char *to)
{
    static char label[40];
    (void)from;
    snprintf(label, sizeof label, "Change it to %s", to);
    return label;
}

static const char *TextFormatFix(int line)
{
    static char label[40];
    snprintf(label, sizeof label, "Add a { on line %d", line);
    return label;
}

static void Fix(FriendlyError *fe, const char *label, int start, int end, const char *text)
{
    fe->hasFix = true;
    snprintf(fe->fixLabel, sizeof fe->fixLabel, "%s", label);
    fe->fixStart = start;
    fe->fixEnd = end;
    snprintf(fe->fixText, sizeof fe->fixText, "%s", text);
}

// The line a statement starts on that's missing its {: "while (...)" with no { at the end
static int LineMissingBrace(const char *src, int beforeLine)
{
    for (int l = beforeLine - 1; l >= 1; l--)
    {
        int s = LineStart(src, l), e = LineEnd(src, s);
        const char *p = src + s;
        while (*p == ' ' || *p == '\t') p++;
        if (p >= src + e) continue;
        bool opener = strncmp(p, "while", 5) == 0 || strncmp(p, "for", 3) == 0 || strncmp(p, "if", 2) == 0 ||
                      strncmp(p, "else", 4) == 0 || strncmp(p, "do", 2) == 0;
        bool hasBrace = memchr(src + s, '{', (size_t)(e - s)) != NULL;
        if (opener && !hasBrace) return l;
        if (hasBrace || memchr(src + s, '}', (size_t)(e - s))) return 0;
    }
    return 0;
}

// The end of the code on a line (before spaces and // comments)
static int CodeEnd(const char *src, int line)
{
    int s = LineStart(src, line), e = LineEnd(src, s);
    for (int i = s; i + 1 < e; i++) if (src[i] == '/' && src[i + 1] == '/') { e = i; break; }
    while (e > s && (src[e - 1] == ' ' || src[e - 1] == '\t' || src[e - 1] == '\r')) e--;
    return e;
}

bool Friendly_Explain(const char *source, const char *message, int line, int col, FriendlyError *fe)
{
    memset(fe, 0, sizeof *fe);
    if (!message || !message[0]) return false;
    fe->line = line;
    fe->col = col;
    char word[64];

    // a gap still waiting to be filled in
    const char *gap = strstr(source, "___");
    if (gap)
    {
        int at = (int)(gap - source);
        fe->line = LineOfOffset(source, at);
        fe->col = ColOf(source, at);
        snprintf(fe->say, sizeof fe->say, "Fill in the gold gap on line %d first. Click it and type over the ___.", fe->line);
        return true;
    }

    if (strstr(message, "missing ;"))
    {
        // the parser points at the last token of the statement: the ; goes straight after it
        int s = LineStart(source, line);
        int at = (s >= 0 && col > 0) ? TokenEnd(source, s + col - 1) : CodeEnd(source, line);
        if (s < 0) at = (int)strlen(source);
        snprintf(fe->say, sizeof fe->say, "Every instruction ends with a ; (like a full stop). Line %d is missing one.",
                 LineOfOffset(source, at));
        fe->line = LineOfOffset(source, at);
        fe->col = ColOf(source, at);
        int k = at;
        while (source[k] == ' ') k++;
        if (source[k] == ':' || source[k] == ',' || source[k] == '.')
        {
            // a : or , typed instead of the ;
            snprintf(fe->say, sizeof fe->say, "That %c should be a ; (the full stop of C).", source[k]);
            fe->col = ColOf(source, k);
            Fix(fe, "Change it to ;", k, k + 1, ";");
            return true;
        }
        Fix(fe, "Add the ;", at, at, ";");
        return true;
    }

    if (Between(message, "no function called ", "()", word, sizeof word))
    {
        Name names[MAX_NAMES];
        int n = CollectNames(source, true, names);
        bool caseOnly;
        const char *best = Closest(word, names, n, &caseOnly);
        int at = FindWord(source, line, word, true);
        if (at >= 0) fe->col = ColOf(source, at);
        if (best && caseOnly)
            snprintf(fe->say, sizeof fe->say, "I don't know %s. Did you mean %s? Capital letters matter in C!", word, best);
        else if (best)
            snprintf(fe->say, sizeof fe->say, "I don't know %s. Did you mean %s? Check the spelling.", word, best);
        else
            snprintf(fe->say, sizeof fe->say, "I don't know a function called %s. Check the spelling, or write it first.", word);
        if (best && at >= 0) Fix(fe, TextFix(word, best), at, at + (int)strlen(word), best);
        return true;
    }

    if (Between(message, "you used '", "'", word, sizeof word))
    {
        Name names[MAX_NAMES];
        int n = CollectNames(source, false, names);
        bool caseOnly;
        const char *best = Closest(word, names, n, &caseOnly);
        int at = FindWord(source, line, word, false);
        if (at >= 0) fe->col = ColOf(source, at);
        bool direction = best && (strcmp(best, "EAST") == 0 || strcmp(best, "WEST") == 0 ||
                                  strcmp(best, "NORTH") == 0 || strcmp(best, "SOUTH") == 0);
        if (best && caseOnly && direction)
            snprintf(fe->say, sizeof fe->say, "Directions are written in CAPITALS: %s, not %s.", best, word);
        else if (best && caseOnly)
            snprintf(fe->say, sizeof fe->say, "There's no %s, but there's %s. Capital letters matter in C!", word, best);
        else if (best)
            snprintf(fe->say, sizeof fe->say, "There's no box called %s. Did you mean %s?", word, best);
        else
            snprintf(fe->say, sizeof fe->say, "There's no box called %s yet. Make it first, like: int %s = 0;", word, word);
        if (best && at >= 0) Fix(fe, TextFix(word, best), at, at + (int)strlen(word), best);
        return true;
    }

    if (strstr(message, "is never closed"))
    {
        int end = (int)strlen(source);
        while (end > 0 && (source[end - 1] == '\n' || source[end - 1] == ' ' || source[end - 1] == '\r')) end--;
        snprintf(fe->say, sizeof fe->say, "Every { needs a } to close it. The { on line %d never gets one.", line);
        Fix(fe, "Add a } at the end", end, end, "\n}");
        return true;
    }

    if (strstr(message, "doesn't match any {"))
    {
        int missing = LineMissingBrace(source, line);
        if (missing > 0)
        {
            int at = CodeEnd(source, missing);
            snprintf(fe->say, sizeof fe->say, "This } has no { to go with it. I think line %d is missing a { at the end.", missing);
            Fix(fe, TextFormatFix(missing), at, at, " {");
        }
        else
        {
            snprintf(fe->say, sizeof fe->say, "There's an extra } here, with no { to go with it.");
            int s = LineStart(source, line);
            if (s >= 0 && col > 0) Fix(fe, "Remove the extra }", s + col - 1, s + col, "");
        }
        return true;
    }

    if (strstr(message, "expected ) to close"))
    {
        int s = LineStart(source, line);
        snprintf(fe->say, sizeof fe->say, "A ( was opened but never closed. Every ( needs a ).");
        if (s >= 0 && col > 0) Fix(fe, "Add the )", s + col - 1, s + col - 1, ")");
        return true;
    }

    if (strstr(message, "missing its closing \""))
    {
        snprintf(fe->say, sizeof fe->say, "Words in quotes need a \" at the start AND at the end.");
        return true;
    }

    // anything else: the compiler's own words, without the "Line N:" at the front
    const char *m = message;
    if (strncmp(m, "Line ", 5) == 0) { const char *c = strchr(m, ':'); if (c) m = c + 2; }
    snprintf(fe->say, sizeof fe->say, "%s", m);
    if (fe->say[0]) fe->say[0] = (char)toupper((unsigned char)fe->say[0]);
    return true;
}

bool Friendly_Warning(const char *source, const char *warning, int line, FriendlyError *fe)
{
    memset(fe, 0, sizeof *fe);
    fe->line = line;
    fe->warning = true;
    if (strstr(warning, "assigns a value"))
    {
        // find the lone = inside the brackets on that line
        int s = LineStart(source, line), e = s >= 0 ? LineEnd(source, s) : -1;
        int at = -1;
        for (int i = s; s >= 0 && i < e && at < 0; i++)
            if (source[i] == '=' && source[i + 1] != '=' && (i == s || !strchr("=<>!+-*/", source[i - 1]))) at = i;
        snprintf(fe->say, sizeof fe->say, "One = PUTS a value in a box. To ASK \"are these the same?\" use two: ==");
        if (at >= 0)
        {
            fe->col = ColOf(source, at);
            Fix(fe, "Change = to ==", at, at + 1, "==");
        }
        return true;
    }
    if (strstr(warning, "the ; straight after"))
    {
        int s = LineStart(source, line), e = s >= 0 ? LineEnd(source, s) : -1;
        int at = -1;
        for (int i = s; s >= 0 && i < e; i++) if (source[i] == ')') at = i + 1;
        while (at >= 0 && source[at] == ' ') at++;
        snprintf(fe->say, sizeof fe->say, "That ; ends the %s straight away, so the code under it isn't part of it.",
                 strstr(warning, "if") ? "if" : "loop");
        if (at >= 0 && source[at] == ';')
        {
            fe->col = ColOf(source, at);
            Fix(fe, "Remove that ;", at, at + 1, "");
        }
        return true;
    }
    if (strstr(warning, "compares with =="))
    {
        snprintf(fe->say, sizeof fe->say, "Two == only ASKS a question. To PUT a value in the box, use one =");
        int s = LineStart(source, line), e = s >= 0 ? LineEnd(source, s) : -1;
        for (int i = s; s >= 0 && i + 1 < e; i++)
            if (source[i] == '=' && source[i + 1] == '=')
            {
                fe->col = ColOf(source, i);
                Fix(fe, "Change == to =", i, i + 2, "=");
                break;
            }
        return true;
    }
    return false;
}

int Friendly_ApplyFix(const char *source, const FriendlyError *fe, char *out, int size)
{
    int len = (int)strlen(source);
    int a = fe->fixStart, b = fe->fixEnd;
    if (a < 0) a = 0;
    if (b > len) b = len;
    if (b < a) b = a;
    snprintf(out, (size_t)size, "%.*s%s%s", a, source, fe->fixText, source + b);
    return a + (int)strlen(fe->fixText);
}
