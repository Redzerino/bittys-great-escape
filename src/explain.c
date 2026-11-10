// explain.c - what a line of C does, in plain English. See explain.h.
//
// It doesn't understand C fully: it recognises the shapes of line a beginner
// writes (loops, ifs, boxes, moves) and describes them in Bitty's words.

#include "explain.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool IsWord(char c) { return isalnum((unsigned char)c) || c == '_'; }

// Copy line 'line' (1-based) of source into out, without leading spaces
static bool GetLine(const char *src, int line, char *out, int size)
{
    for (int l = 1; l < line; l++)
    {
        const char *nl = strchr(src, '\n');
        if (!nl) return false;
        src = nl + 1;
    }
    while (*src == ' ' || *src == '\t') src++;
    int k = 0;
    while (src[k] && src[k] != '\n' && src[k] != '\r' && k < size - 1) { out[k] = src[k]; k++; }
    out[k] = '\0';
    // cut off a // comment at the end, and trailing spaces
    char *c = strstr(out, "//");
    if (c) *c = '\0';
    k = (int)strlen(out);
    while (k > 0 && (out[k - 1] == ' ' || out[k - 1] == '\t')) out[--k] = '\0';
    return true;
}

static bool StartsWith(const char *s, const char *word)
{
    size_t n = strlen(word);
    return strncmp(s, word, n) == 0 && !IsWord(s[n]);
}

static void Trim(char *s)
{
    char *p = s;
    while (*p == ' ') p++;
    memmove(s, p, strlen(p) + 1);
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '{' || s[n - 1] == ';')) s[--n] = '\0';
}

// Was 'name' declared as a pointer anywhere (int *name, char *name)?
static bool IsPointer(const char *src, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = strchr(src, '*'); p; p = strchr(p + 1, '*'))
    {
        const char *q = p + 1;
        while (*q == ' ') q++;
        if (strncmp(q, name, n) != 0 || IsWord(q[n])) continue;
        const char *b = p - 1;
        while (b > src && *b == ' ') b--;
        if (b >= src && IsWord(*b)) return true;     // "int *name" (not "a * name")
    }
    return false;
}

// "*p" in words
static const char *TextFormatWhere(const char *name)
{
    static char text[64];
    snprintf(text, sizeof text, "the box %s points at", name);
    return text;
}

static const char *DirectionWord(const char *dir)
{
    if (strncmp(dir, "EAST", 4) == 0)  return "EAST (to the right)";
    if (strncmp(dir, "WEST", 4) == 0)  return "WEST (to the left)";
    if (strncmp(dir, "NORTH", 5) == 0) return "NORTH (up)";
    if (strncmp(dir, "SOUTH", 5) == 0) return "SOUTH (down)";
    return dir;
}

static const char *TypeWord(const char *type)
{
    if (strcmp(type, "int") == 0)    return "a whole number";
    if (strcmp(type, "char") == 0)   return "one letter";
    if (strcmp(type, "bool") == 0)   return "true or false";
    if (strcmp(type, "float") == 0 || strcmp(type, "double") == 0) return "a decimal number";
    if (strcmp(type, "void") == 0)   return "nothing";
    return type;
}

// A condition in words: "scan(EAST) != TILE_WALL" -> "the tile to my east is not a wall"
static void Words(const char *cond, char *out, int size)
{
    static const struct { const char *code, *words; } SWAP[] = {
        { "scan(EAST)", "the tile to my east" }, { "scan(WEST)", "the tile to my west" },
        { "scan(NORTH)", "the tile north of me" }, { "scan(SOUTH)", "the tile south of me" },
        { "read()", "the number on my tile" },
        { "TILE_WALL", "a wall" }, { "TILE_EMPTY", "empty floor" }, { "TILE_VOID", "a hole" },
        { "TILE_EXIT", "the exit" }, { "TILE_GATE", "the gate" }, { "TILE_DATA", "a data tile" },
        { "'\\0'", "the end marker \\0" },
        { "!=", "is not" }, { "==", "is" }, { ">=", "is at least" }, { "<=", "is at most" },
        { ">", "is bigger than" }, { "<", "is smaller than" }, { "&&", "and" }, { "||", "or" },
    };
    int n = 0;
    out[0] = '\0';
    for (const char *p = cond; *p && n < size - 40; )
    {
        bool done = false;
        for (size_t i = 0; i < sizeof SWAP / sizeof SWAP[0] && !done; i++)
        {
            size_t len = strlen(SWAP[i].code);
            if (strncmp(p, SWAP[i].code, len) != 0) continue;
            n += snprintf(out + n, (size_t)(size - n), "%s", SWAP[i].words);
            p += len;
            done = true;
        }
        if (done) continue;
        if (*p == '\'')                         // a character: copy it as it is
        {
            const char *q = p + 1;
            while (*q && *q != '\'') { if (*q == '\\' && q[1]) q++; q++; }
            if (*q) q++;
            n += snprintf(out + n, (size_t)(size - n), "%.*s", (int)(q - p), p);
            p = q;
            continue;
        }
        out[n++] = *p++;
        out[n] = '\0';
    }
    if (strchr(cond, '%')) snprintf(out + n, (size_t)(size - n), " (%% is the remainder after dividing)");
}

static bool HasComparison(const char *s)
{
    return strstr(s, "==") || strstr(s, "!=") || strchr(s, '<') || strchr(s, '>') || strstr(s, "&&") || strstr(s, "||");
}

// The text inside the first ( ... ) after 'from'
static bool Inside(const char *from, char *out, int size)
{
    const char *a = strchr(from, '(');
    if (!a) return false;
    int depth = 0;
    for (const char *p = a; *p; p++)
    {
        if (*p == '(') depth++;
        if (*p == ')' && --depth == 0)
        {
            snprintf(out, (size_t)size, "%.*s", (int)(p - a - 1), a + 1);
            return true;
        }
    }
    return false;
}

static bool IsNumber(const char *s)
{
    if (*s == '-') s++;
    if (!*s) return false;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return false;
    return true;
}

static bool ExplainFor(const char *text, char *out, int size)
{
    char inside[160], init[64] = "", cond[64] = "", step[64] = "";
    if (!Inside(text, inside, sizeof inside)) return false;
    char *a = strchr(inside, ';'), *b = a ? strchr(a + 1, ';') : NULL;
    if (!a || !b) return false;
    snprintf(init, sizeof init, "%.*s", (int)(a - inside), inside);
    snprintf(cond, sizeof cond, "%.*s", (int)(b - a - 1), a + 1);
    snprintf(step, sizeof step, "%s", b + 1);
    Trim(init); Trim(cond); Trim(step);

    char var[32], start[32], var2[32], end[32], var3[32];
    char op[3] = "";
    bool simple = sscanf(init, "int %31[A-Za-z0-9_] = %31[^ ;]", var, start) == 2 &&
                  sscanf(cond, "%31[A-Za-z0-9_] %2[<=] %31[^ ;]", var2, op, end) == 3 &&
                  sscanf(step, "%31[A-Za-z0-9_]++", var3) == 1 &&
                  strcmp(var, var2) == 0 && strcmp(var, var3) == 0;
    if (simple && strcmp(op, "<") == 0 && IsNumber(start) && IsNumber(end))
    {
        int s = 0, e = 0;
        sscanf(start, "%d", &s);
        sscanf(end, "%d", &e);
        snprintf(out, (size_t)size, "Repeat the code in the { } %d times. %s counts %d, %d, %d... up to %d.",
                 e - s, var, s, s + 1, s + 2, e - 1);
        return true;
    }
    if (simple && strcmp(op, "<") == 0)
    {
        snprintf(out, (size_t)size, "Repeat the code in the { } once for every %s from %s up to (but not including) %s.", var, start, end);
        return true;
    }
    char words[160];
    Words(cond, words, sizeof words);
    snprintf(out, (size_t)size, "A loop: first %s. Keep going while %s. After each time round, %s.", init, words, step);
    return true;
}

static bool ExplainRaw(const char *source, int line, char *out, int size);

bool Explain_Line(const char *source, int line, char *out, int size)
{
    if (!ExplainRaw(source, line, out, size)) return false;
    // a gap still to fill in reads better in words
    if (strstr(out, "___"))
    {
        char copy[400];
        int n = 0;
        for (const char *p = out; *p && n < (int)sizeof copy - 20; )
        {
            if (p[0] == '_' && p[1] == '_' && p[2] == '_')
            {
                n += snprintf(copy + n, sizeof copy - (size_t)n, "(the gold gap)");
                while (*p == '_') p++;
            }
            else copy[n++] = *p++;
        }
        copy[n] = '\0';
        snprintf(out, (size_t)size, "%s", copy);
    }
    return true;
}

static bool ExplainRaw(const char *source, int line, char *out, int size)
{
    char text[200];
    out[0] = '\0';
    if (!GetLine(source, line, text, sizeof text)) return false;
    if (!text[0]) return false;

    char cond[160], words[200];
    const char *t = text;

    // "} else ..." and "} while (...);" start with the brace of the block before
    bool closer = false;
    if (*t == '}') { closer = true; t++; while (*t == ' ') t++; }
    if (!*t)
    {
        snprintf(out, (size_t)size, "This } closes the { } block that started above.");
        return true;
    }

    if (StartsWith(t, "for")) return ExplainFor(t, out, size);

    if (StartsWith(t, "while") && Inside(t, cond, sizeof cond))
    {
        Words(cond, words, sizeof words);
        if (closer) snprintf(out, (size_t)size, "Go back up to the do { and run it again if %s.", words);
        else snprintf(out, (size_t)size, "Keep repeating the code in the { } for as long as %s.", words);
        return true;
    }
    if (StartsWith(t, "do"))
    {
        snprintf(out, (size_t)size, "Run the code in the { } once, then the while at the bottom decides whether to go again.");
        return true;
    }
    if (StartsWith(t, "else"))
    {
        const char *rest = t + 4;
        while (*rest == ' ') rest++;
        if (StartsWith(rest, "if") && Inside(rest, cond, sizeof cond))
        {
            Words(cond, words, sizeof words);
            snprintf(out, (size_t)size, "Otherwise: if %s, run this { } instead.", words);
        }
        else snprintf(out, (size_t)size, "Otherwise (when the if's answer was NO), run this { } instead.");
        return true;
    }
    if (StartsWith(t, "if") && Inside(t, cond, sizeof cond))
    {
        Words(cond, words, sizeof words);
        snprintf(out, (size_t)size, "A question: %s? Only if the answer is YES, run the code in the { }.", words);
        return true;
    }
    if (StartsWith(t, "switch") && Inside(t, cond, sizeof cond))
    {
        snprintf(out, (size_t)size, "Jump to the case below that matches %s.", cond);
        return true;
    }
    if (StartsWith(t, "case"))
    {
        snprintf(out, (size_t)size, "Start here when it matches: %s", t + 5);
        return true;
    }
    if (StartsWith(t, "default"))
    {
        snprintf(out, (size_t)size, "Start here when none of the cases matched.");
        return true;
    }
    if (StartsWith(t, "break"))
    {
        snprintf(out, (size_t)size, "Jump straight out of the loop (or switch). Emergency stop!");
        return true;
    }
    if (StartsWith(t, "continue"))
    {
        snprintf(out, (size_t)size, "Skip the rest of this time round, and go round the loop again.");
        return true;
    }
    if (StartsWith(t, "return"))
    {
        char expr[160];
        snprintf(expr, sizeof expr, "%s", t + 6);
        Trim(expr);
        if (!expr[0]) snprintf(out, (size_t)size, "Stop the function here.");
        else if (HasComparison(expr))
        {
            Words(expr, words, sizeof words);
            snprintf(out, (size_t)size, "Give back true or false: %s? Then the function stops.", words);
        }
        else snprintf(out, (size_t)size, "Give back %s as the answer. The function stops here.", expr);
        return true;
    }

    // a function:  int doubleIt(int n) {
    char type[16], name[40], params[120];
    if (sscanf(t, "%15[a-z] %39[A-Za-z0-9_*](%119[^)])", type, name, params) == 3 &&
        (strcmp(type, "int") == 0 || strcmp(type, "void") == 0 || strcmp(type, "bool") == 0 || strcmp(type, "char") == 0) &&
        strchr(t, '{'))
    {
        const char *nm = name[0] == '*' ? name + 1 : name;
        if (strcmp(type, "void") == 0)
            snprintf(out, (size_t)size, "A function called %s. It takes in: %s. It doesn't give anything back.", nm, params);
        else
            snprintf(out, (size_t)size, "A function called %s. It takes in: %s. It gives back %s.", nm, params, TypeWord(type));
        return true;
    }

    // a box:  int steps = read();   char c = s[i];   int *p = arr;   int nums[5];
    char rest[160];
    if (sscanf(t, "%15[a-z] %39[^=;]%159[^\n]", type, name, rest) >= 2 &&
        (strcmp(type, "int") == 0 || strcmp(type, "char") == 0 || strcmp(type, "bool") == 0 ||
         strcmp(type, "float") == 0 || strcmp(type, "double") == 0))
    {
        Trim(name);
        bool ptr = name[0] == '*';
        const char *nm = ptr ? name + 1 : name;
        char value[160] = "";
        const char *eq = strchr(t, '=');
        if (eq) { snprintf(value, sizeof value, "%s", eq + 1); Trim(value); }
        if (ptr)
            snprintf(out, (size_t)size, "Make a pointer called %s: a box that holds an ADDRESS%s%s.", nm,
                     value[0] ? ". It points at " : "", value);
        else if (strchr(nm, '['))
            snprintf(out, (size_t)size, "Make a row of boxes (an array) called %s.", nm);
        else if (strcmp(value, "read()") == 0)
            snprintf(out, (size_t)size, "Make a box called %s and put the number on my tile in it.", nm);
        else if (value[0])
            snprintf(out, (size_t)size, "Make a box called %s for %s, and put %s in it.", nm, TypeWord(type), value);
        else
            snprintf(out, (size_t)size, "Make a box called %s for %s. Careful: it has no value yet!", nm, TypeWord(type));
        return true;
    }

    // things Bitty does
    char arg[80];
    if (StartsWith(t, "move") && Inside(t, arg, sizeof arg))
    {
        snprintf(out, (size_t)size, "I take one step %s.", DirectionWord(arg));
        return true;
    }
    if (StartsWith(t, "write") && Inside(t, arg, sizeof arg))
    {
        snprintf(out, (size_t)size, "I stamp %s onto the floor tile I'm standing on.", arg);
        return true;
    }
    if (StartsWith(t, "printf"))
    {
        snprintf(out, (size_t)size, strstr(t, "%d") ? "Show a message in the console. Each %%d is filled in with a number from after the comma."
                                                    : "Show a message in the console.");
        return true;
    }

    // changing a box
    char target[48] = "", expr[120] = "";
    {
        // the box being changed: a name, maybe with a * in front or [ ] after
        const char *p = t;
        int k = 0;
        if (*p == '*') target[k++] = *p++;
        while (IsWord(*p) && k < 40) target[k++] = *p++;
        if (*p == '[')
        {
            int depth = 0;
            do { if (*p == '[') depth++; if (*p == ']') depth--; if (k < 46) target[k++] = *p; p++; } while (*p && depth > 0);
        }
        target[k] = '\0';
        while (*p == ' ') p++;
        snprintf(expr, sizeof expr, "%.119s", p);
    }
    if (target[0] && (target[0] != '*' || target[1]) && expr[0] && strchr("=+-*/", expr[0]))
    {
        Trim(expr);
        bool deref = target[0] == '*';
        const char *tn = deref ? target + 1 : target;
        const char *where = deref ? TextFormatWhere(tn) : tn;
        bool ptr = !deref && IsPointer(source, tn);
        if (strcmp(expr, "++") == 0)
            snprintf(out, (size_t)size, ptr ? "Move the pointer %s along to the next box." : "Add 1 to %s.", tn);
        else if (strcmp(expr, "--") == 0)
            snprintf(out, (size_t)size, ptr ? "Move the pointer %s back to the box before." : "Take 1 away from %s.", tn);
        else if (strncmp(expr, "+=", 2) == 0) snprintf(out, (size_t)size, "Add %s to %s.", expr + 3, where);
        else if (strncmp(expr, "-=", 2) == 0) snprintf(out, (size_t)size, "Take %s away from %s.", expr + 3, where);
        else if (strncmp(expr, "*=", 2) == 0) snprintf(out, (size_t)size, "Multiply %s by %s.", where, expr + 3);
        else if (strncmp(expr, "/=", 2) == 0) snprintf(out, (size_t)size, "Divide %s by %s (whole numbers drop any remainder).", where, expr + 3);
        else if (expr[0] == '=' && expr[1] != '=')
        {
            char value[120];
            snprintf(value, sizeof value, "%s", expr + 1);
            Trim(value);
            if (ptr) snprintf(out, (size_t)size, "Point %s at %s (it now holds that address).", tn, value);
            else snprintf(out, (size_t)size, "Put %s into %s. Whatever was in it before is replaced.", value, where);
        }
        else return false;
        size_t n = strlen(out);
        if (strchr(expr, '%') && n + 40 < (size_t)size) snprintf(out + n, (size_t)size - n, " (%% is the remainder after dividing)");
        return true;
    }

    // calling a function
    if (Inside(t, arg, sizeof arg))
    {
        char fn[40];
        if (sscanf(t, "%39[A-Za-z0-9_]", fn) == 1)
        {
            snprintf(out, (size_t)size, "Run the function %s.", fn);
            return true;
        }
    }
    return false;
}
