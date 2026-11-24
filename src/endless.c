// endless.c - ENDLESS MODE challenge generators (see endless.h)
#include "endless.h"
#include "repair.h"
#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===========================================================================
// A challenge before it becomes a Level
// ===========================================================================
enum { RET_NONE = 0, RET_INT, RET_BOOL, RET_CHAR };

typedef struct {
    char call[360];     // e.g.  sumDigits(4821)
    int  ret;           // RET_*: check the return value (shown as int / bool / char)
    int  checks;        // bit k set = check what argument k+1 holds afterwards
    bool prints;        // check what it printed
} TestSpec;

typedef struct {
    char     name[48];          // shown in the title
    char     task[700];         // what to write (a few short lines)
    char     top[600];          // struct/class definitions: go above everything
    char     starter[900];      // the function(s) the player starts with
    char     solution[2400];    // hidden reference answer
    char     rig[1600];         // test rig: below the player's code, in both
    bool     ban;               // ban_indexing (pointer-only challenges)
    TestSpec t[8];
    int      n;
} Spec;

// ---- small helpers ---------------------------------------------------------
static unsigned rng = 1;
static unsigned Next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int R(int lo, int hi) { return lo + (int)(Next() % (unsigned)(hi - lo + 1)); }   // lo..hi
static int Pick(int n) { return (int)(Next() % (unsigned)n); }

static void Cat(char *buf, size_t size, const char *fmt, ...)
{
    size_t len = strlen(buf);
    if (len >= size - 1) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + len, size - len, fmt, ap);
    va_end(ap);
}
#define CAT(field, ...) Cat(field, sizeof field, __VA_ARGS__)

static void Task(Spec *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->task, sizeof s->task, fmt, ap);
    va_end(ap);
}

// The player's starting point: the signature and an empty body
static void Proto(Spec *s, const char *signature, const char *ret)
{
    if (ret && ret[0]) CAT(s->starter, "%s {\n\n    %s\n}\n", signature, ret);
    else               CAT(s->starter, "%s {\n\n}\n", signature);
}

static void T(Spec *s, int ret, int checks, bool prints, const char *fmt, ...)
{
    if (s->n >= 8) return;
    TestSpec *t = &s->t[s->n++];
    t->ret = ret; t->checks = checks; t->prints = prints;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->call, sizeof t->call, fmt, ap);
    va_end(ap);
}

// {4, 9, 2}
static const char *Arr(const int *a, int n)
{
    static char buf[4][200];
    static int which = 0;
    char *b = buf[which++ & 3];
    b[0] = '\0';
    Cat(b, 200, "{");
    for (int i = 0; i < n; i++) Cat(b, 200, "%s%d", i ? ", " : "", a[i]);
    Cat(b, 200, "}");
    return b;
}

static void RandArr(int *a, int n, int lo, int hi) { for (int i = 0; i < n; i++) a[i] = R(lo, hi); }

// n different values from lo..hi
static void DistinctArr(int *a, int n, int lo, int hi)
{
    for (int i = 0; i < n; i++)
    {
        bool again;
        do {
            a[i] = R(lo, hi);
            again = false;
            for (int k = 0; k < i; k++) if (a[k] == a[i]) again = true;
        } while (again);
    }
}

static const char *WORDS[] = {
    "bitty", "escape", "memory", "pointer", "robot", "circuit", "stack", "heap", "array", "string",
    "loop", "chip", "code", "byte", "node", "gate", "port", "data", "signal", "exit", "maze",
    "power", "logic", "binary", "kernel", "cache", "socket", "cable", "laser", "pixel"
};
#define WORD_COUNT (int)(sizeof WORDS / sizeof WORDS[0])

// A few random words with single spaces ("robot cache exit"), maybe with some capitals
static void Sentence(char *out, int size, int words, bool capitals)
{
    out[0] = '\0';
    for (int i = 0; i < words; i++)
    {
        char w[24];
        snprintf(w, sizeof w, "%s", WORDS[Pick(WORD_COUNT)]);
        if (capitals) for (int k = 0; w[k]; k++) if (Pick(4) == 0) w[k] = (char)(w[k] - 32);
        Cat(out, (size_t)size, "%s%s", i ? " " : "", w);
    }
}

// ===========================================================================
// The generators: one per topic on the coverage sheet
// ===========================================================================

// ---- Exam 1: leap years (a made-up calendar rule each time) ----
static void G_Leap(Spec *s)
{
    static const char *names[] = { "isGlitchYear", "isPatchYear", "isBittyYear", "isBonusYear" };
    const char *f = names[Pick(4)];
    int a = (int[]){ 3, 4, 5, 6, 7 }[Pick(5)];
    int b = a * (int[]){ 5, 10, 20, 25 }[Pick(4)];
    int c = b * (int[]){ 2, 3, 4 }[Pick(3)];
    snprintf(s->name, sizeof s->name, "Calendar Glitch");
    Task(s, "A computer calendar has its own leap rule:\n"
            "  a year divisible by %d is special,\n"
            "  EXCEPT years divisible by %d are not,\n"
            "  EXCEPT years divisible by %d are special again.\n"
            "Write %s(year): true if the year is special.", a, b, c, f);
    char sig[80]; snprintf(sig, sizeof sig, "bool %s(int year)", f);
    Proto(s, sig, "return false;");
    CAT(s->solution, "%s {\n    if (year %% %d == 0) return true;\n    if (year %% %d == 0) return false;\n"
                     "    if (year %% %d == 0) return true;\n    return false;\n}\n", sig, c, b, a);
    T(s, RET_BOOL, 0, false, "%s(%d)", f, c * R(1, 4));
    T(s, RET_BOOL, 0, false, "%s(%d)", f, b * R(1, 9));
    T(s, RET_BOOL, 0, false, "%s(%d)", f, b * (c / b * R(1, 5) + 1));
    T(s, RET_BOOL, 0, false, "%s(%d)", f, a * R(30, 400));
    T(s, RET_BOOL, 0, false, "%s(%d)", f, a * R(30, 400) + 1);
    T(s, RET_BOOL, 0, false, "%s(%d)", f, R(1000, 3000));
}

// ---- Exam 1: peeling digits off a number with % 10 and / 10 ----
static void G_Digits(Spec *s)
{
    int v = Pick(6);
    snprintf(s->name, sizeof s->name, "Digit Peeler");
    const char *f = "";
    switch (v)
    {
        case 0: f = "digitTotal";
            Task(s, "Write digitTotal(n): add up the digits of n.\n4821 -> 4 + 8 + 2 + 1 = 15");
            Proto(s, "int digitTotal(int n)", "return 0;");
            CAT(s->solution, "int digitTotal(int n) {\n    int total = 0;\n    while (n > 0) { total += n %% 10; n /= 10; }\n    return total;\n}\n");
            break;
        case 1: f = "biggestDigit";
            Task(s, "Write biggestDigit(n): the largest single digit in n.\n4821 -> 8");
            Proto(s, "int biggestDigit(int n)", "return 0;");
            CAT(s->solution, "int biggestDigit(int n) {\n    int best = 0;\n    while (n > 0) { if (n %% 10 > best) best = n %% 10; n /= 10; }\n    return best;\n}\n");
            break;
        case 2: f = "smallestDigit";
            Task(s, "Write smallestDigit(n): the smallest single digit in n (n > 0).\n4821 -> 1");
            Proto(s, "int smallestDigit(int n)", "return 0;");
            CAT(s->solution, "int smallestDigit(int n) {\n    int best = 9;\n    while (n > 0) { if (n %% 10 < best) best = n %% 10; n /= 10; }\n    return best;\n}\n");
            break;
        case 3: f = "flipNumber";
            Task(s, "Write flipNumber(n): the digits of n backwards.\n4821 -> 1284   (trailing zeros vanish: 120 -> 21)");
            Proto(s, "int flipNumber(int n)", "return 0;");
            CAT(s->solution, "int flipNumber(int n) {\n    int r = 0;\n    while (n > 0) { r = r * 10 + n %% 10; n /= 10; }\n    return r;\n}\n");
            break;
        case 4: f = "evenDigits";
            Task(s, "Write evenDigits(n): how many digits of n are even (0, 2, 4, 6, 8).\n4821 -> 2");
            Proto(s, "int evenDigits(int n)", "return 0;");
            CAT(s->solution, "int evenDigits(int n) {\n    int count = 0;\n    while (n > 0) { if ((n %% 10) %% 2 == 0) count++; n /= 10; }\n    return count;\n}\n");
            break;
        default: f = "digitProduct";
            Task(s, "Write digitProduct(n): multiply the digits of n together, SKIPPING zeros.\n4021 -> 4 * 2 * 1 = 8");
            Proto(s, "int digitProduct(int n)", "return 0;");
            CAT(s->solution, "int digitProduct(int n) {\n    int p = 1;\n    while (n > 0) { if (n %% 10 != 0) p *= n %% 10; n /= 10; }\n    return p;\n}\n");
            break;
    }
    T(s, RET_INT, 0, false, "%s(%d)", f, R(10, 99));
    T(s, RET_INT, 0, false, "%s(%d)", f, R(100, 9999));
    T(s, RET_INT, 0, false, "%s(%d)", f, R(10000, 999999));
    T(s, RET_INT, 0, false, "%s(%d)", f, R(1, 9));
    T(s, RET_INT, 0, false, "%s(%d)", f, R(1000, 99999) * 10);
}

// ---- Exam 1: palindromes (numbers or words, sometimes ignoring capitals) ----
static void G_Palindrome(Spec *s)
{
    int v = Pick(3);
    snprintf(s->name, sizeof s->name, "Mirror Check");
    if (v == 0)
    {
        Task(s, "Write isMirrorNumber(n): true if n reads the same both ways.\n12321 -> true,  1231 -> false");
        Proto(s, "bool isMirrorNumber(int n)", "return false;");
        CAT(s->solution, "bool isMirrorNumber(int n) {\n    int r = 0, m = n;\n    while (m > 0) { r = r * 10 + m %% 10; m /= 10; }\n    return r == n;\n}\n");
        for (int i = 0; i < 6; i++)
        {
            int half = R(1, 999), n = half, h = half / (i % 2 ? 1 : 10);
            while (h > 0) { n = n * 10 + h % 10; h /= 10; }
            if (i >= 4) n = n * 10 + R(1, 9);
            if (n < 0 || n > 99999999) n = 12321;
            T(s, RET_BOOL, 0, false, "isMirrorNumber(%d)", n);
        }
        return;
    }
    bool ignoreCase = (v == 2);
    if (ignoreCase)
    {
        Task(s, "Write isMirrorWord(word): true if it reads the same both ways,\nIGNORING capitals.  \"Level\" -> true,  \"Robot\" -> false");
        Proto(s, "bool isMirrorWord(char word[])", "return false;");
        CAT(s->solution, "char lower(char c) {\n    if (c >= 'A' && c <= 'Z') return c + 32;\n    return c;\n}\n"
                         "bool isMirrorWord(char word[]) {\n    int len = 0;\n    while (word[len] != '\\0') len++;\n"
                         "    for (int i = 0; i < len / 2; i++)\n        if (lower(word[i]) != lower(word[len - 1 - i])) return false;\n    return true;\n}\n");
    }
    else
    {
        Task(s, "Write isMirrorWord(word): true if the word reads the same both ways.\n\"racecar\" -> true,  \"robot\" -> false");
        Proto(s, "bool isMirrorWord(char word[])", "return false;");
        CAT(s->solution, "bool isMirrorWord(char word[]) {\n    int len = 0;\n    while (word[len] != '\\0') len++;\n"
                         "    for (int i = 0; i < len / 2; i++)\n        if (word[i] != word[len - 1 - i]) return false;\n    return true;\n}\n");
    }
    for (int i = 0; i < 6; i++)
    {
        char w[24] = "";
        int half = R(1, 4);
        for (int k = 0; k < half; k++) w[k] = (char)('a' + Pick(26));
        int len = half;
        if (i % 2) w[len++] = (char)('a' + Pick(26));       // odd length
        for (int k = half - 1; k >= 0; k--) w[len++] = w[k];
        w[len] = '\0';
        if (i >= 4) w[len - 1] = (char)(w[len - 1] == 'z' ? 'a' : w[len - 1] + 1);   // break it
        if (ignoreCase) for (int k = 0; k < len; k++) if (Pick(3) == 0) w[k] = (char)(w[k] - 32);
        T(s, RET_BOOL, 0, false, "isMirrorWord(\"%s\")", w);
    }
}

// ---- Exam 1: primes and divisors ----
static void G_Prime(Spec *s)
{
    int v = Pick(5);
    snprintf(s->name, sizeof s->name, "Prime Lock");
    const char *f = ""; int ret = RET_INT;
    switch (v)
    {
        case 0: f = "isPrime"; ret = RET_BOOL;
            Task(s, "Write isPrime(n): true if n is prime (only divisible by 1 and itself).\n1 is NOT prime.");
            Proto(s, "bool isPrime(int n)", "return false;");
            CAT(s->solution, "bool isPrime(int n) {\n    if (n < 2) return false;\n    for (int d = 2; d * d <= n; d++) if (n %% d == 0) return false;\n    return true;\n}\n");
            break;
        case 1: f = "countDivisors";
            Task(s, "Write countDivisors(n): how many numbers from 1 to n divide n exactly.\n12 -> 6  (1, 2, 3, 4, 6, 12)");
            Proto(s, "int countDivisors(int n)", "return 0;");
            CAT(s->solution, "int countDivisors(int n) {\n    int c = 0;\n    for (int d = 1; d <= n; d++) if (n %% d == 0) c++;\n    return c;\n}\n");
            break;
        case 2: f = "primesUpTo";
            Task(s, "Write primesUpTo(n): how many primes there are from 2 up to n.\n10 -> 4  (2, 3, 5, 7)");
            Proto(s, "int primesUpTo(int n)", "return 0;");
            CAT(s->solution, "int primesUpTo(int n) {\n    int c = 0;\n    for (int k = 2; k <= n; k++) {\n        bool prime = true;\n"
                             "        for (int d = 2; d * d <= k; d++) if (k %% d == 0) prime = false;\n        if (prime) c++;\n    }\n    return c;\n}\n");
            break;
        case 3: f = "nextPrime";
            Task(s, "Write nextPrime(n): the smallest prime BIGGER than n.\n7 -> 11,  20 -> 23");
            Proto(s, "int nextPrime(int n)", "return 0;");
            CAT(s->solution, "int nextPrime(int n) {\n    int k = n + 1;\n    while (true) {\n        bool prime = k >= 2;\n"
                             "        for (int d = 2; d * d <= k; d++) if (k %% d == 0) prime = false;\n        if (prime) return k;\n        k++;\n    }\n}\n");
            break;
        default: f = "divisorSum";
            Task(s, "Write divisorSum(n): add up the divisors of n, NOT counting n itself.\n12 -> 1 + 2 + 3 + 4 + 6 = 16");
            Proto(s, "int divisorSum(int n)", "return 0;");
            CAT(s->solution, "int divisorSum(int n) {\n    int t = 0;\n    for (int d = 1; d < n; d++) if (n %% d == 0) t += d;\n    return t;\n}\n");
            break;
    }
    static const int primes[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103 };
    T(s, ret, 0, false, "%s(%d)", f, primes[Pick(27)]);
    T(s, ret, 0, false, "%s(%d)", f, primes[Pick(10)] * primes[Pick(10)]);
    T(s, ret, 0, false, "%s(%d)", f, R(2, 120));
    T(s, ret, 0, false, "%s(%d)", f, R(2, 120));
    T(s, ret, 0, false, "%s(%d)", f, v == 0 ? 1 : R(2, 12));
}

// ---- Exam 1: number bases (another base each time) ----
static void G_Base(Spec *s)
{
    int base = R(2, 9);
    bool toDecimal = Pick(2) == 0;
    snprintf(s->name, sizeof s->name, "Base %d Decoder", base);
    char f[32];
    if (toDecimal)
    {
        snprintf(f, sizeof f, "base%dToDecimal", base);
        Task(s, "The number is written in BASE %d (each digit is 0-%d).\nWrite %s(n): turn it into a normal (base 10) number.\n"
                "e.g. base %d: 11 -> 1 * %d + 1 = %d", base, base - 1, f, base, base, base + 1);
        char sig[64]; snprintf(sig, sizeof sig, "int %s(int n)", f);
        Proto(s, sig, "return 0;");
        CAT(s->solution, "%s {\n    int result = 0, place = 1;\n    while (n > 0) {\n        result += (n %% 10) * place;\n"
                         "        place *= %d;\n        n /= 10;\n    }\n    return result;\n}\n", sig, base);
        for (int i = 0; i < 5; i++)
        {
            int value = R(1, base == 2 ? 255 : 900), digits = 0, place = 1;
            if (i == 0) value = base;            // "10"
            while (value > 0) { digits += (value % base) * place; place *= 10; value /= base; }
            T(s, RET_INT, 0, false, "%s(%d)", f, digits);
        }
    }
    else
    {
        snprintf(f, sizeof f, "decimalToBase%d", base);
        Task(s, "Write %s(n): write n in BASE %d, using decimal digits.\n"
                "e.g. %d -> 10,  %d -> 11", f, base, base, base + 1);
        char sig[64]; snprintf(sig, sizeof sig, "int %s(int n)", f);
        Proto(s, sig, "return 0;");
        CAT(s->solution, "%s {\n    int result = 0, place = 1;\n    while (n > 0) {\n        result += (n %% %d) * place;\n"
                         "        place *= 10;\n        n /= %d;\n    }\n    return result;\n}\n", sig, base, base);
        T(s, RET_INT, 0, false, "%s(%d)", f, base * base);
        for (int i = 0; i < 4; i++) T(s, RET_INT, 0, false, "%s(%d)", f, R(1, base == 2 ? 255 : 700));
    }
}

// ---- Exam 1: patterns with nested loops (printf) ----
static void G_Pattern(Spec *s)
{
    int v = Pick(6);
    char ch = "*#A@+X"[Pick(6)];
    const char *f = "";
    snprintf(s->name, sizeof s->name, "Pattern Printer");
    switch (v)
    {
        case 0: f = "drawStairs";
            Task(s, "Write drawStairs(n): n rows, row r has r '%c's.\nn = 3:\n%c\n%c%c\n%c%c%c", ch, ch, ch, ch, ch, ch, ch);
            CAT(s->solution, "void drawStairs(int n) {\n    for (int r = 1; r <= n; r++) {\n        for (int c = 1; c <= r; c++) printf(\"%c\");\n        printf(\"\\n\");\n    }\n}\n", ch);
            break;
        case 1: f = "drawDrop";
            Task(s, "Write drawDrop(n): an upside-down triangle of '%c'.\nn = 3:\n%c%c%c\n%c%c\n%c", ch, ch, ch, ch, ch, ch, ch);
            CAT(s->solution, "void drawDrop(int n) {\n    for (int r = n; r >= 1; r--) {\n        for (int c = 1; c <= r; c++) printf(\"%c\");\n        printf(\"\\n\");\n    }\n}\n", ch);
            break;
        case 2: f = "drawCount";
            Task(s, "Write drawCount(n): row r prints the numbers 1 to r.\nn = 3:\n1\n12\n123");
            CAT(s->solution, "void drawCount(int n) {\n    for (int r = 1; r <= n; r++) {\n        for (int c = 1; c <= r; c++) printf(\"%%d\", c);\n        printf(\"\\n\");\n    }\n}\n");
            break;
        case 3: f = "drawRightStairs";
            Task(s, "Write drawRightStairs(n): stairs pushed to the RIGHT with spaces.\nn = 3:\n  %c\n %c%c\n%c%c%c", ch, ch, ch, ch, ch, ch);
            CAT(s->solution, "void drawRightStairs(int n) {\n    for (int r = 1; r <= n; r++) {\n        for (int c = 1; c <= n - r; c++) printf(\" \");\n"
                             "        for (int c = 1; c <= r; c++) printf(\"%c\");\n        printf(\"\\n\");\n    }\n}\n", ch);
            break;
        case 4: f = "drawBox";
            Task(s, "Write drawBox(n): an n x n HOLLOW square of '%c' (spaces inside).\nn = 3:\n%c%c%c\n%c %c\n%c%c%c", ch, ch, ch, ch, ch, ch, ch, ch, ch);
            CAT(s->solution, "void drawBox(int n) {\n    for (int r = 1; r <= n; r++) {\n        for (int c = 1; c <= n; c++) {\n"
                             "            if (r == 1 || r == n || c == 1 || c == n) printf(\"%c\");\n            else printf(\" \");\n        }\n        printf(\"\\n\");\n    }\n}\n", ch);
            break;
        default: f = "drawPyramid";
            Task(s, "Write drawPyramid(n): a centred pyramid of '%c'.\nn = 3:\n  %c\n %c%c%c\n%c%c%c%c%c", ch, ch, ch, ch, ch, ch, ch, ch, ch, ch);
            CAT(s->solution, "void drawPyramid(int n) {\n    for (int r = 1; r <= n; r++) {\n        for (int c = 1; c <= n - r; c++) printf(\" \");\n"
                             "        for (int c = 1; c <= 2 * r - 1; c++) printf(\"%c\");\n        printf(\"\\n\");\n    }\n}\n", ch);
            break;
    }
    char sig[64]; snprintf(sig, sizeof sig, "void %s(int n)", f);
    Proto(s, sig, NULL);
    int a = R(2, 3), b = R(4, 5);
    T(s, RET_NONE, 0, true, "%s(%d)", f, a);
    T(s, RET_NONE, 0, true, "%s(%d)", f, b);
    T(s, RET_NONE, 0, true, "%s(1)", f);
}

// ---- Exam 1: loops that count / repeat ----
static void G_Loop(Spec *s)
{
    int v = Pick(5);
    snprintf(s->name, sizeof s->name, "Loop Engine");
    switch (v)
    {
        case 0:
            Task(s, "Write power(base, exp): base multiplied by itself exp times.\npower(2, 5) -> 32,  power(7, 0) -> 1");
            Proto(s, "int power(int base, int exp)", "return 0;");
            CAT(s->solution, "int power(int base, int exp) {\n    int r = 1;\n    for (int i = 0; i < exp; i++) r *= base;\n    return r;\n}\n");
            for (int i = 0; i < 4; i++) T(s, RET_INT, 0, false, "power(%d, %d)", R(2, 9), R(1, 5));
            T(s, RET_INT, 0, false, "power(%d, 0)", R(2, 9));
            break;
        case 1:
            Task(s, "Write gcd(a, b): the biggest number that divides both a and b.\ngcd(12, 18) -> 6");
            Proto(s, "int gcd(int a, int b)", "return 0;");
            CAT(s->solution, "int gcd(int a, int b) {\n    while (b != 0) {\n        int t = a %% b;\n        a = b;\n        b = t;\n    }\n    return a;\n}\n");
            for (int i = 0; i < 5; i++) { int k = R(1, 12); T(s, RET_INT, 0, false, "gcd(%d, %d)", k * R(1, 15), k * R(1, 15)); }
            break;
        case 2:
            Task(s, "Write hailSteps(n): how many steps to reach 1, where each step does\n  even n -> n / 2      odd n -> 3 * n + 1\nhailSteps(6) -> 8   (6 3 10 5 16 8 4 2 1)");
            Proto(s, "int hailSteps(int n)", "return 0;");
            CAT(s->solution, "int hailSteps(int n) {\n    int steps = 0;\n    while (n != 1) {\n        if (n %% 2 == 0) n = n / 2;\n        else n = 3 * n + 1;\n        steps++;\n    }\n    return steps;\n}\n");
            T(s, RET_INT, 0, false, "hailSteps(1)");
            for (int i = 0; i < 4; i++) T(s, RET_INT, 0, false, "hailSteps(%d)", R(2, 40));
            break;
        case 3:
        {
            int k = R(3, 9);
            Task(s, "Write countMultiples(from, to): how many numbers from 'from' to 'to'\n(both included) are multiples of %d.", k);
            Proto(s, "int countMultiples(int from, int to)", "return 0;");
            CAT(s->solution, "int countMultiples(int from, int to) {\n    int c = 0;\n    for (int i = from; i <= to; i++) if (i %% %d == 0) c++;\n    return c;\n}\n", k);
            for (int i = 0; i < 5; i++) { int a = R(1, 60); T(s, RET_INT, 0, false, "countMultiples(%d, %d)", a, a + R(0, 80)); }
            break;
        }
        default:
            Task(s, "Write rangeSum(from, to): add up every whole number from 'from' to 'to'.\nrangeSum(3, 6) -> 3 + 4 + 5 + 6 = 18");
            Proto(s, "int rangeSum(int from, int to)", "return 0;");
            CAT(s->solution, "int rangeSum(int from, int to) {\n    int t = 0;\n    for (int i = from; i <= to; i++) t += i;\n    return t;\n}\n");
            for (int i = 0; i < 4; i++) { int a = R(-10, 40); T(s, RET_INT, 0, false, "rangeSum(%d, %d)", a, a + R(0, 30)); }
            T(s, RET_INT, 0, false, "rangeSum(%d, %d)", 7, 7);
            break;
    }
}

// ---- Exam 1: if / else if chains (a different grading scale each time) ----
static void G_Grade(Spec *s)
{
    int a = R(80, 92), b = a - R(8, 14), c = b - R(8, 14), d = c - R(8, 14);
    snprintf(s->name, sizeof s->name, "Grade Gate");
    Task(s, "Write grade(score) to return a letter:\n  %d and up -> 'A'    %d and up -> 'B'\n  %d and up -> 'C'    %d and up -> 'D'\n  anything lower  -> 'F'", a, b, c, d);
    Proto(s, "char grade(int score)", "return ' ';");
    CAT(s->solution, "char grade(int score) {\n    if (score >= %d) return 'A';\n    else if (score >= %d) return 'B';\n"
                     "    else if (score >= %d) return 'C';\n    else if (score >= %d) return 'D';\n    return 'F';\n}\n", a, b, c, d);
    T(s, RET_CHAR, 0, false, "grade(%d)", a);
    T(s, RET_CHAR, 0, false, "grade(%d)", a - 1);
    T(s, RET_CHAR, 0, false, "grade(%d)", R(c, b - 1));
    T(s, RET_CHAR, 0, false, "grade(%d)", d);
    T(s, RET_CHAR, 0, false, "grade(%d)", R(0, d - 1));
    T(s, RET_CHAR, 0, false, "grade(100)");
}

// ---- Exam 1: walking an array to total / count ----
static void G_ArraySum(Spec *s)
{
    int v = Pick(6), k = R(3, 12);
    snprintf(s->name, sizeof s->name, "Power Audit");
    const char *f = ""; bool withK = false;
    switch (v)
    {
        case 0: f = "sumEvens";
            Task(s, "Write sumEvens(size, arr): add up only the EVEN values.");
            CAT(s->solution, "int sumEvens(int size, int arr[]) {\n    int t = 0;\n    for (int i = 0; i < size; i++) if (arr[i] %% 2 == 0) t += arr[i];\n    return t;\n}\n");
            break;
        case 1: f = "countAbove"; withK = true;
            Task(s, "Write countAbove(size, arr, limit): how many values are BIGGER than limit.");
            CAT(s->solution, "int countAbove(int size, int arr[], int limit) {\n    int c = 0;\n    for (int i = 0; i < size; i++) if (arr[i] > limit) c++;\n    return c;\n}\n");
            break;
        case 2: f = "countBelow"; withK = true;
            Task(s, "Write countBelow(size, arr, limit): how many values are SMALLER than limit.");
            CAT(s->solution, "int countBelow(int size, int arr[], int limit) {\n    int c = 0;\n    for (int i = 0; i < size; i++) if (arr[i] < limit) c++;\n    return c;\n}\n");
            break;
        case 3: f = "spread";
            Task(s, "Write spread(size, arr): the biggest value minus the smallest value.\n{4, 9, 2} -> 9 - 2 = 7");
            CAT(s->solution, "int spread(int size, int arr[]) {\n    int lo = arr[0], hi = arr[0];\n    for (int i = 1; i < size; i++) {\n"
                             "        if (arr[i] < lo) lo = arr[i];\n        if (arr[i] > hi) hi = arr[i];\n    }\n    return hi - lo;\n}\n");
            break;
        case 4: f = "sumOddSlots";
            Task(s, "Write sumOddSlots(size, arr): add up the values at ODD indexes\n(arr[1] + arr[3] + arr[5] ...).");
            CAT(s->solution, "int sumOddSlots(int size, int arr[]) {\n    int t = 0;\n    for (int i = 1; i < size; i += 2) t += arr[i];\n    return t;\n}\n");
            break;
        default: f = "countNegatives";
            Task(s, "Write countNegatives(size, arr): how many values are below zero.");
            CAT(s->solution, "int countNegatives(int size, int arr[]) {\n    int c = 0;\n    for (int i = 0; i < size; i++) if (arr[i] < 0) c++;\n    return c;\n}\n");
            break;
    }
    char sig[80];
    snprintf(sig, sizeof sig, withK ? "int %s(int size, int arr[], int limit)" : "int %s(int size, int arr[])", f);
    Proto(s, sig, "return 0;");
    for (int i = 0; i < 5; i++)
    {
        int n = R(3, 8), a[8];
        RandArr(a, n, v == 5 || v == 3 ? -20 : 0, 30);
        if (withK) T(s, RET_INT, 0, false, "%s(%d, %s, %d)", f, n, Arr(a, n), i == 4 ? 99 : R(k, 25));
        else       T(s, RET_INT, 0, false, "%s(%d, %s)", f, n, Arr(a, n));
    }
}

// ---- Exam 1: searching an array ----
static void G_Find(Spec *s)
{
    int v = Pick(4);
    snprintf(s->name, sizeof s->name, "Search the Bay");
    const char *f = ""; bool withV = true;
    switch (v)
    {
        case 0: f = "findFirst";
            Task(s, "Write findFirst(size, arr, value): the index of the FIRST box holding\nvalue, or -1 if it isn't there.");
            CAT(s->solution, "int findFirst(int size, int arr[], int value) {\n    for (int i = 0; i < size; i++) if (arr[i] == value) return i;\n    return -1;\n}\n");
            break;
        case 1: f = "findLast";
            Task(s, "Write findLast(size, arr, value): the index of the LAST box holding\nvalue, or -1 if it isn't there.");
            CAT(s->solution, "int findLast(int size, int arr[], int value) {\n    for (int i = size - 1; i >= 0; i--) if (arr[i] == value) return i;\n    return -1;\n}\n");
            break;
        case 2: f = "whereIsMax"; withV = false;
            Task(s, "Write whereIsMax(size, arr): the INDEX of the biggest value\n(the first one, if it appears twice).");
            CAT(s->solution, "int whereIsMax(int size, int arr[]) {\n    int best = 0;\n    for (int i = 1; i < size; i++) if (arr[i] > arr[best]) best = i;\n    return best;\n}\n");
            break;
        default: f = "whereIsMin"; withV = false;
            Task(s, "Write whereIsMin(size, arr): the INDEX of the smallest value\n(the first one, if it appears twice).");
            CAT(s->solution, "int whereIsMin(int size, int arr[]) {\n    int best = 0;\n    for (int i = 1; i < size; i++) if (arr[i] < arr[best]) best = i;\n    return best;\n}\n");
            break;
    }
    char sig[80];
    snprintf(sig, sizeof sig, withV ? "int %s(int size, int arr[], int value)" : "int %s(int size, int arr[])", f);
    Proto(s, sig, "return 0;");
    for (int i = 0; i < 5; i++)
    {
        int n = R(4, 8), a[8];
        RandArr(a, n, 0, 9);
        if (withV) T(s, RET_INT, 0, false, "%s(%d, %s, %d)", f, n, Arr(a, n), i == 4 ? 42 : a[R(0, n - 1)]);
        else       T(s, RET_INT, 0, false, "%s(%d, %s)", f, n, Arr(a, n));
    }
}

// ---- Exam 1: second largest / smallest ----
static void G_Second(Spec *s)
{
    bool largest = Pick(2) == 0;
    snprintf(s->name, sizeof s->name, largest ? "Runner Up" : "Second Lowest");
    if (largest)
    {
        Task(s, "Write secondBiggest(size, arr): the SECOND largest value.\nAll values are different.  {4, 9, 2, 7} -> 7");
        Proto(s, "int secondBiggest(int size, int arr[])", "return 0;");
        CAT(s->solution, "int secondBiggest(int size, int arr[]) {\n    int first = -1000, second = -1000;\n    for (int i = 0; i < size; i++) {\n"
                         "        if (arr[i] > first) { second = first; first = arr[i]; }\n        else if (arr[i] > second) second = arr[i];\n    }\n    return second;\n}\n");
    }
    else
    {
        Task(s, "Write secondSmallest(size, arr): the SECOND smallest value.\nAll values are different.  {4, 9, 2, 7} -> 4");
        Proto(s, "int secondSmallest(int size, int arr[])", "return 0;");
        CAT(s->solution, "int secondSmallest(int size, int arr[]) {\n    int first = 1000, second = 1000;\n    for (int i = 0; i < size; i++) {\n"
                         "        if (arr[i] < first) { second = first; first = arr[i]; }\n        else if (arr[i] < second) second = arr[i];\n    }\n    return second;\n}\n");
    }
    for (int i = 0; i < 5; i++)
    {
        int n = R(3, 8), a[8];
        DistinctArr(a, n, -30, 60);
        T(s, RET_INT, 0, false, "%s(%d, %s)", largest ? "secondBiggest" : "secondSmallest", n, Arr(a, n));
    }
}

// ---- Exam 1: frequency / duplicates ----
static void G_Frequency(Spec *s)
{
    int v = Pick(4);
    snprintf(s->name, sizeof s->name, "Duplicate Hunt");
    const char *f = ""; int ret = RET_INT;
    switch (v)
    {
        case 0: f = "hasRepeat"; ret = RET_BOOL;
            Task(s, "Write hasRepeat(size, arr): true if any value appears more than once.");
            Proto(s, "bool hasRepeat(int size, int arr[])", "return false;");
            CAT(s->solution, "bool hasRepeat(int size, int arr[]) {\n    for (int i = 0; i < size; i++)\n        for (int j = i + 1; j < size; j++)\n"
                             "            if (arr[i] == arr[j]) return true;\n    return false;\n}\n");
            break;
        case 1: f = "countLoners";
            Task(s, "Write countLoners(size, arr): how many values appear EXACTLY once.\n{3, 1, 3, 7} -> 2  (the 1 and the 7)");
            Proto(s, "int countLoners(int size, int arr[])", "return 0;");
            CAT(s->solution, "int countLoners(int size, int arr[]) {\n    int c = 0;\n    for (int i = 0; i < size; i++) {\n        int seen = 0;\n"
                             "        for (int j = 0; j < size; j++) if (arr[j] == arr[i]) seen++;\n        if (seen == 1) c++;\n    }\n    return c;\n}\n");
            break;
        case 2: f = "countDifferent";
            Task(s, "Write countDifferent(size, arr): how many DIFFERENT values there are.\n{3, 1, 3, 7} -> 3");
            Proto(s, "int countDifferent(int size, int arr[])", "return 0;");
            CAT(s->solution, "int countDifferent(int size, int arr[]) {\n    int c = 0;\n    for (int i = 0; i < size; i++) {\n        bool before = false;\n"
                             "        for (int j = 0; j < i; j++) if (arr[j] == arr[i]) before = true;\n        if (!before) c++;\n    }\n    return c;\n}\n");
            break;
        default: f = "mostCommon";
            Task(s, "Write mostCommon(size, arr): the value that appears the most.\nIf there's a tie, the one that shows up FIRST wins.");
            Proto(s, "int mostCommon(int size, int arr[])", "return 0;");
            CAT(s->solution, "int mostCommon(int size, int arr[]) {\n    int best = arr[0], bestCount = 0;\n    for (int i = 0; i < size; i++) {\n        int c = 0;\n"
                             "        for (int j = 0; j < size; j++) if (arr[j] == arr[i]) c++;\n        if (c > bestCount) { best = arr[i]; bestCount = c; }\n    }\n    return best;\n}\n");
            break;
    }
    for (int i = 0; i < 5; i++)
    {
        int n = R(4, 8), a[8];
        if (i == 3) DistinctArr(a, n, 1, 20); else RandArr(a, n, 1, 6);
        T(s, ret, 0, false, "%s(%d, %s)", f, n, Arr(a, n));
    }
}

// ---- Exam 1/2: changing an array in place ----
static void G_Rearrange(Spec *s)
{
    int v = Pick(5);
    snprintf(s->name, sizeof s->name, "Rotate Belt");
    const char *f = "";
    switch (v)
    {
        case 0: f = "flipArray";
            Task(s, "Write flipArray(size, arr): reverse the array IN PLACE.\n{1, 2, 3, 4} becomes {4, 3, 2, 1}");
            CAT(s->solution, "void flipArray(int size, int arr[]) {\n    for (int i = 0; i < size / 2; i++) {\n        int t = arr[i];\n"
                             "        arr[i] = arr[size - 1 - i];\n        arr[size - 1 - i] = t;\n    }\n}\n");
            break;
        case 1: f = "shiftLeft";
            Task(s, "Write shiftLeft(size, arr): every value moves one box LEFT and the\nfirst value wraps round to the end.  {1, 2, 3, 4} -> {2, 3, 4, 1}");
            CAT(s->solution, "void shiftLeft(int size, int arr[]) {\n    int first = arr[0];\n    for (int i = 0; i < size - 1; i++) arr[i] = arr[i + 1];\n    arr[size - 1] = first;\n}\n");
            break;
        case 2: f = "shiftRight";
            Task(s, "Write shiftRight(size, arr): every value moves one box RIGHT and the\nlast value wraps round to the front.  {1, 2, 3, 4} -> {4, 1, 2, 3}");
            CAT(s->solution, "void shiftRight(int size, int arr[]) {\n    int last = arr[size - 1];\n    for (int i = size - 1; i > 0; i--) arr[i] = arr[i - 1];\n    arr[0] = last;\n}\n");
            break;
        case 3: f = "swapPairs";
            Task(s, "Write swapPairs(size, arr): swap box 0 with 1, box 2 with 3, and so on.\nAn odd one out at the end stays put.  {1, 2, 3, 4, 5} -> {2, 1, 4, 3, 5}");
            CAT(s->solution, "void swapPairs(int size, int arr[]) {\n    for (int i = 0; i + 1 < size; i += 2) {\n        int t = arr[i];\n        arr[i] = arr[i + 1];\n        arr[i + 1] = t;\n    }\n}\n");
            break;
        default: f = "zerosToBack";
            Task(s, "Write zerosToBack(size, arr): move every 0 to the END, keeping the\nother values in order.  {0, 3, 0, 5, 1} -> {3, 5, 1, 0, 0}");
            CAT(s->solution, "void zerosToBack(int size, int arr[]) {\n    int w = 0;\n    for (int i = 0; i < size; i++) if (arr[i] != 0) { arr[w] = arr[i]; w++; }\n"
                             "    while (w < size) { arr[w] = 0; w++; }\n}\n");
            break;
    }
    char sig[80]; snprintf(sig, sizeof sig, "void %s(int size, int arr[])", f);
    Proto(s, sig, NULL);
    for (int i = 0; i < 5; i++)
    {
        int n = R(3, 8), a[8];
        DistinctArr(a, n, 1, 40);
        if (v == 4) for (int k = 0; k < n; k++) if (Pick(3) == 0) a[k] = 0;
        T(s, RET_NONE, 2, false, "%s(%d, %s)", f, n, Arr(a, n));
    }
}

// ---- Exam 2: copying from one array to another ----
static void G_Copy(Spec *s)
{
    int v = Pick(3);
    snprintf(s->name, sizeof s->name, "Copy Bay");
    const char *f = ""; int ret = RET_NONE;
    switch (v)
    {
        case 0: f = "copyBackwards";
            Task(s, "Write copyBackwards(size, from, to): copy 'from' into 'to' in REVERSE order.\nfrom {1, 2, 3} -> to {3, 2, 1}");
            Proto(s, "void copyBackwards(int size, int from[], int to[])", NULL);
            CAT(s->solution, "void copyBackwards(int size, int from[], int to[]) {\n    for (int i = 0; i < size; i++) to[i] = from[size - 1 - i];\n}\n");
            break;
        case 1: f = "copyDoubled";
        {
            int k = R(2, 5);
            Task(s, "Write copyTimes(size, from, to): fill 'to' with every value of 'from'\nmultiplied by %d.", k);
            f = "copyTimes";
            Proto(s, "void copyTimes(int size, int from[], int to[])", NULL);
            CAT(s->solution, "void copyTimes(int size, int from[], int to[]) {\n    for (int i = 0; i < size; i++) to[i] = from[i] * %d;\n}\n", k);
            break;
        }
        default: f = "copyOdds"; ret = RET_INT;
            Task(s, "Write copyOdds(size, from, to): copy only the ODD values into the\nfront of 'to', in order, and return how many you copied.\nfrom {4, 7, 1, 8} -> to {7, 1, 0, 0}, returns 2");
            Proto(s, "int copyOdds(int size, int from[], int to[])", "return 0;");
            CAT(s->solution, "int copyOdds(int size, int from[], int to[]) {\n    int c = 0;\n    for (int i = 0; i < size; i++) if (from[i] %% 2 != 0) { to[c] = from[i]; c++; }\n    return c;\n}\n");
            break;
    }
    for (int i = 0; i < 4; i++)
    {
        int n = R(3, 7), a[8], z[8] = { 0 };
        RandArr(a, n, 1, 30);
        T(s, ret, 4, false, "%s(%d, %s, %s)", f, n, Arr(a, n), Arr(z, n));
    }
}

// ---- Exam 2: insert into / delete from an array that has spare room ----
static void G_InsertDelete(Spec *s)
{
    bool insert = Pick(2) == 0;
    if (insert)
    {
        snprintf(s->name, sizeof s->name, "Make Room");
        Task(s, "Write insertAt(count, arr, value, index): arr holds 'count' values and\nhas room for one more. Shift values right to make room, put value at\n"
                "index, and add 1 to count.  If index > count, do nothing.");
        Proto(s, "void insertAt(int& count, int arr[], int value, int index)", NULL);
        CAT(s->solution, "void insertAt(int& count, int arr[], int value, int index) {\n    if (index < 0 || index > count) return;\n"
                         "    for (int i = count; i > index; i--) arr[i] = arr[i - 1];\n    arr[index] = value;\n    count++;\n}\n");
        for (int i = 0; i < 5; i++)
        {
            int n = R(2, 6), a[8] = { 0 };
            RandArr(a, n, 1, 9);
            int idx = i == 0 ? 0 : i == 1 ? n : i == 4 ? n + 2 : R(1, n - 1);
            T(s, RET_NONE, 3, false, "insertAt(&%d, %s, %d, %d)", n, Arr(a, n + 1), R(10, 99), idx);
        }
    }
    else
    {
        snprintf(s->name, sizeof s->name, "Close the Gap");
        Task(s, "Write removeAt(count, arr, index): take out the value at index by\nshifting everything after it LEFT. Put 0 in the box that's now free\n"
                "and take 1 off count.  Return the value you removed (-1 if index is bad).");
        Proto(s, "int removeAt(int& count, int arr[], int index)", "return 0;");
        CAT(s->solution, "int removeAt(int& count, int arr[], int index) {\n    if (index < 0 || index >= count) return -1;\n    int gone = arr[index];\n"
                         "    for (int i = index; i < count - 1; i++) arr[i] = arr[i + 1];\n    arr[count - 1] = 0;\n    count--;\n    return gone;\n}\n");
        for (int i = 0; i < 5; i++)
        {
            int n = R(3, 7), a[8] = { 0 };
            RandArr(a, n, 1, 50);
            int idx = i == 0 ? 0 : i == 1 ? n - 1 : i == 4 ? n : R(1, n - 2);
            T(s, RET_INT, 3, false, "removeAt(&%d, %s, %d)", n, Arr(a, n), idx);
        }
    }
}

// ---- Exam 1: counting in strings ----
static void G_StringCount(Spec *s)
{
    int v = Pick(5);
    snprintf(s->name, sizeof s->name, "Message Scan");
    const char *f = ""; char extra[16] = "";
    bool capitals = (v == 2 || v == 0);
    switch (v)
    {
        case 0: f = "countVowels";
            Task(s, "Write countVowels(text): how many vowels (a e i o u) - small OR capital.");
            Proto(s, "int countVowels(char text[])", "return 0;");
            CAT(s->solution, "int countVowels(char text[]) {\n    int c = 0;\n    for (int i = 0; text[i] != '\\0'; i++) {\n        char ch = text[i];\n"
                             "        if (ch >= 'A' && ch <= 'Z') ch = ch + 32;\n        if (ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u') c++;\n    }\n    return c;\n}\n");
            break;
        case 1: f = "countLetter";
            Task(s, "Write countLetter(text, letter): how many times letter appears.");
            Proto(s, "int countLetter(char text[], char letter)", "return 0;");
            CAT(s->solution, "int countLetter(char text[], char letter) {\n    int c = 0;\n    for (int i = 0; text[i] != '\\0'; i++) if (text[i] == letter) c++;\n    return c;\n}\n");
            break;
        case 2: f = "countCapitals";
            Task(s, "Write countCapitals(text): how many CAPITAL letters (A-Z).");
            Proto(s, "int countCapitals(char text[])", "return 0;");
            CAT(s->solution, "int countCapitals(char text[]) {\n    int c = 0;\n    for (int i = 0; text[i] != '\\0'; i++) if (text[i] >= 'A' && text[i] <= 'Z') c++;\n    return c;\n}\n");
            break;
        case 3: f = "countConsonants";
            Task(s, "Write countConsonants(text): how many letters that are NOT vowels.\nSpaces aren't letters!  (all small letters)");
            Proto(s, "int countConsonants(char text[])", "return 0;");
            CAT(s->solution, "int countConsonants(char text[]) {\n    int c = 0;\n    for (int i = 0; text[i] != '\\0'; i++) {\n        char ch = text[i];\n"
                             "        if (ch >= 'a' && ch <= 'z' && ch != 'a' && ch != 'e' && ch != 'i' && ch != 'o' && ch != 'u') c++;\n    }\n    return c;\n}\n");
            break;
        default: f = "countSpaces";
            Task(s, "Write countSpaces(text): how many spaces ' ' there are.");
            Proto(s, "int countSpaces(char text[])", "return 0;");
            CAT(s->solution, "int countSpaces(char text[]) {\n    int c = 0;\n    for (int i = 0; text[i] != '\\0'; i++) if (text[i] == ' ') c++;\n    return c;\n}\n");
            break;
    }
    for (int i = 0; i < 5; i++)
    {
        char text[120];
        Sentence(text, sizeof text, R(1, 4), capitals);
        if (v == 1)
        {
            char letter = 'q';
            if (i < 4) do letter = text[Pick((int)strlen(text))]; while (letter == ' ');
            snprintf(extra, sizeof extra, ", '%c'", letter);
        }
        T(s, RET_INT, 0, false, "%s(\"%s\"%s)", f, text, extra);
    }
}

// ---- Exam 1: changing a string in place ----
static void G_StringChange(Spec *s)
{
    int v = Pick(5);
    snprintf(s->name, sizeof s->name, "Message Repair");
    const char *f = ""; char a = 'e', b = '3';
    bool capitals = (v == 1);
    switch (v)
    {
        case 0: f = "shout";
            Task(s, "Write shout(text): change every small letter to a CAPITAL, in place.\n\"hi bitty\" becomes \"HI BITTY\"");
            Proto(s, "void shout(char text[])", NULL);
            CAT(s->solution, "void shout(char text[]) {\n    for (int i = 0; text[i] != '\\0'; i++)\n        if (text[i] >= 'a' && text[i] <= 'z') text[i] = text[i] - 32;\n}\n");
            break;
        case 1: f = "whisper";
            Task(s, "Write whisper(text): change every CAPITAL to a small letter, in place.");
            Proto(s, "void whisper(char text[])", NULL);
            CAT(s->solution, "void whisper(char text[]) {\n    for (int i = 0; text[i] != '\\0'; i++)\n        if (text[i] >= 'A' && text[i] <= 'Z') text[i] = text[i] + 32;\n}\n");
            break;
        case 2:
            a = "aeiost"[Pick(6)]; b = "*#@0+!"[Pick(6)];
            f = "swapChar";
            Task(s, "Write swapChar(text): replace every '%c' with '%c', in place.", a, b);
            Proto(s, "void swapChar(char text[])", NULL);
            CAT(s->solution, "void swapChar(char text[]) {\n    for (int i = 0; text[i] != '\\0'; i++)\n        if (text[i] == '%c') text[i] = '%c';\n}\n", a, b);
            break;
        case 3: f = "backwards";
            Task(s, "Write backwards(text): reverse the string in place.\n\"robot\" becomes \"tobor\"");
            Proto(s, "void backwards(char text[])", NULL);
            CAT(s->solution, "void backwards(char text[]) {\n    int len = 0;\n    while (text[len] != '\\0') len++;\n"
                             "    for (int i = 0; i < len / 2; i++) {\n        char t = text[i];\n        text[i] = text[len - 1 - i];\n        text[len - 1 - i] = t;\n    }\n}\n");
            break;
        default: f = "titleCase";
            Task(s, "Write titleCase(text): make the FIRST letter of every word a capital.\n\"hi bitty\" becomes \"Hi Bitty\"  (words are split by single spaces)");
            Proto(s, "void titleCase(char text[])", NULL);
            CAT(s->solution, "void titleCase(char text[]) {\n    for (int i = 0; text[i] != '\\0'; i++)\n"
                             "        if ((i == 0 || text[i - 1] == ' ') && text[i] >= 'a' && text[i] <= 'z') text[i] = text[i] - 32;\n}\n");
            break;
    }
    for (int i = 0; i < 4; i++)
    {
        char text[120];
        Sentence(text, sizeof text, R(1, 3), capitals);
        T(s, RET_NONE, 1, false, "%s(\"%s\"[40])", f, text);
    }
}

// ---- Exam 1: string length / comparing strings ----
static void G_StringCompare(Spec *s)
{
    int v = Pick(3);
    snprintf(s->name, sizeof s->name, "Password Terminal");
    if (v == 0)
    {
        Task(s, "Write sameText(a, b): true if the two strings are exactly the same.\nNo strcmp allowed - compare them yourself!");
        Proto(s, "bool sameText(char a[], char b[])", "return false;");
        CAT(s->solution, "bool sameText(char a[], char b[]) {\n    int i = 0;\n    while (a[i] != '\\0' && a[i] == b[i]) i++;\n    return a[i] == b[i];\n}\n");
        for (int i = 0; i < 5; i++)
        {
            char x[40], y[40];
            Sentence(x, sizeof x, 1, false);
            snprintf(y, sizeof y, "%s", x);
            if (i == 1) Cat(y, sizeof y, "s");
            if (i == 2) y[0] = (char)(y[0] - 32);
            if (i == 3) Sentence(y, sizeof y, 1, false);
            T(s, RET_BOOL, 0, false, "sameText(\"%s\", \"%s\")", x, y);
        }
    }
    else if (v == 1)
    {
        Task(s, "Write startsWith(text, start): true if text begins with start.\n\"robotics\", \"robot\" -> true");
        Proto(s, "bool startsWith(char text[], char start[])", "return false;");
        CAT(s->solution, "bool startsWith(char text[], char start[]) {\n    for (int i = 0; start[i] != '\\0'; i++)\n        if (text[i] != start[i]) return false;\n    return true;\n}\n");
        for (int i = 0; i < 5; i++)
        {
            char x[40], y[40];
            Sentence(x, sizeof x, 1, false);
            int len = (int)strlen(x);
            snprintf(y, sizeof y, "%.*s", R(1, len), x);
            if (i == 2) Sentence(y, sizeof y, 1, false);
            if (i == 3) snprintf(y, sizeof y, "%.30sx", x);
            T(s, RET_BOOL, 0, false, "startsWith(\"%s\", \"%s\")", x, y);
        }
    }
    else
    {
        Task(s, "Write compareText(a, b) like strcmp: return -1 if a comes first in the\n"
                "alphabet, 1 if b comes first, and 0 if they are the same.\n(A shorter word that matches the start comes first.)");
        Proto(s, "int compareText(char a[], char b[])", "return 0;");
        CAT(s->solution, "int compareText(char a[], char b[]) {\n    int i = 0;\n    while (a[i] != '\\0' && a[i] == b[i]) i++;\n"
                         "    if (a[i] < b[i]) return -1;\n    if (a[i] > b[i]) return 1;\n    return 0;\n}\n");
        for (int i = 0; i < 5; i++)
        {
            char x[40], y[40];
            Sentence(x, sizeof x, 1, false);
            Sentence(y, sizeof y, 1, false);
            if (i == 0) snprintf(y, sizeof y, "%s", x);
            if (i == 1) snprintf(y, sizeof y, "%.30ss", x);
            T(s, RET_INT, 0, false, "compareText(\"%s\", \"%s\")", x, y);
        }
    }
}

// ---- Exam 1: words in a sentence ----
static void G_Words(Spec *s)
{
    int v = Pick(3), k = R(4, 6);
    snprintf(s->name, sizeof s->name, "Word Counter");
    const char *f = "";
    switch (v)
    {
        case 0: f = "countWords";
            Task(s, "Write countWords(text): how many words. Words are split by ONE space,\nand an empty string has 0 words.");
            Proto(s, "int countWords(char text[])", "return 0;");
            CAT(s->solution, "int countWords(char text[]) {\n    int c = 0;\n    for (int i = 0; text[i] != '\\0'; i++)\n"
                             "        if (text[i] != ' ' && (i == 0 || text[i - 1] == ' ')) c++;\n    return c;\n}\n");
            break;
        case 1: f = "longestWord";
            Task(s, "Write longestWord(text): the length of the longest word.\n\"hi bitty\" -> 5");
            Proto(s, "int longestWord(char text[])", "return 0;");
            CAT(s->solution, "int longestWord(char text[]) {\n    int best = 0, run = 0;\n    for (int i = 0; text[i] != '\\0'; i++) {\n"
                             "        if (text[i] == ' ') run = 0;\n        else { run++; if (run > best) best = run; }\n    }\n    return best;\n}\n");
            break;
        default: f = "countLongWords";
            Task(s, "Write countLongWords(text): how many words have %d or more letters.", k);
            Proto(s, "int countLongWords(char text[])", "return 0;");
            CAT(s->solution, "int countLongWords(char text[]) {\n    int c = 0, run = 0;\n    for (int i = 0; ; i++) {\n"
                             "        if (text[i] == ' ' || text[i] == '\\0') {\n            if (run >= %d) c++;\n            run = 0;\n"
                             "            if (text[i] == '\\0') break;\n        } else run++;\n    }\n    return c;\n}\n", k);
            break;
    }
    for (int i = 0; i < 5; i++)
    {
        char text[160];
        Sentence(text, sizeof text, R(1, 6), false);
        T(s, RET_INT, 0, false, "%s(\"%s\")", f, text);
    }
}

// ---- Exam 1/2: pointers only (arr[i] is switched off) ----
static void G_Pointers(Spec *s)
{
    int v = Pick(5);
    s->ban = true;
    snprintf(s->name, sizeof s->name, "Pointer Wires");
    switch (v)
    {
        case 0:
            Task(s, "Write sumAll(p, size) using only the POINTER p: add up the values.\n(p[i] and arr[i] are switched off: use *p and p++)");
            Proto(s, "int sumAll(int *p, int size)", "return 0;");
            CAT(s->solution, "int sumAll(int *p, int size) {\n    int t = 0;\n    for (int i = 0; i < size; i++) { t += *p; p++; }\n    return t;\n}\n");
            for (int i = 0; i < 4; i++) { int n = R(2, 7), a[8]; RandArr(a, n, -9, 30); T(s, RET_INT, 0, false, "sumAll(%s, %d)", Arr(a, n), n); }
            break;
        case 1:
        {
            int k = R(2, 10);
            Task(s, "Write addToAll(p, size) using only the POINTER p: add %d to every value.\n(p[i] is switched off: use *p and p++)", k);
            Proto(s, "void addToAll(int *p, int size)", NULL);
            CAT(s->solution, "void addToAll(int *p, int size) {\n    for (int i = 0; i < size; i++) { *p = *p + %d; p++; }\n}\n", k);
            for (int i = 0; i < 4; i++) { int n = R(2, 7), a[8]; RandArr(a, n, 0, 30); T(s, RET_NONE, 1, false, "addToAll(%s, %d)", Arr(a, n), n); }
            break;
        }
        case 2:
            Task(s, "Write sortTwo(a, b): if *a is bigger than *b, swap them, so that\nafterwards *a <= *b.");
            Proto(s, "void sortTwo(int *a, int *b)", NULL);
            CAT(s->solution, "void sortTwo(int *a, int *b) {\n    if (*a > *b) {\n        int t = *a;\n        *a = *b;\n        *b = t;\n    }\n}\n");
            for (int i = 0; i < 4; i++) T(s, RET_NONE, 3, false, "sortTwo(&%d, &%d)", R(-20, 50), R(-20, 50));
            break;
        case 3:
        {
            char c = (char)('a' + Pick(26));
            const char *vow = "aeiou";
            c = vow[Pick(5)];
            Task(s, "Write countWithPointer(s) using only a POINTER: walk the string with\np++ and count the '%c's.  (s[i] is switched off)", c);
            Proto(s, "int countWithPointer(char *s)", "return 0;");
            CAT(s->solution, "int countWithPointer(char *s) {\n    int c = 0;\n    while (*s != '\\0') { if (*s == '%c') c++; s++; }\n    return c;\n}\n", c);
            for (int i = 0; i < 4; i++) { char t[120]; Sentence(t, sizeof t, R(1, 4), false); T(s, RET_INT, 0, false, "countWithPointer(\"%s\")", t); }
            break;
        }
        default:
            Task(s, "Write lengthWithPointer(s): the length of the string, using a POINTER\nthat walks to the '\\0'.  (s[i] is switched off)");
            Proto(s, "int lengthWithPointer(char *s)", "return 0;");
            CAT(s->solution, "int lengthWithPointer(char *s) {\n    char *p = s;\n    while (*p != '\\0') p++;\n    return p - s;\n}\n");
            for (int i = 0; i < 4; i++) { char t[120]; Sentence(t, sizeof t, R(1, 3), Pick(2)); T(s, RET_INT, 0, false, "lengthWithPointer(\"%s\")", t); }
            break;
    }
}

// ---- Exam 2: 2D arrays (a 4 x 6 grid) ----
static void G_Grid(Spec *s)
{
    int v = Pick(6);
    snprintf(s->name, sizeof s->name, "Video Memory");
    int g[24];
    const char *grid = "";
    switch (v)
    {
        case 0:
            Task(s, "The grid has 4 rows and 6 columns.\nWrite rowTotal(grid, row): add up the 6 values on that row.");
            Proto(s, "int rowTotal(int grid[4][6], int row)", "return 0;");
            CAT(s->solution, "int rowTotal(int grid[4][6], int row) {\n    int t = 0;\n    for (int col = 0; col < 6; col++) t += grid[row][col];\n    return t;\n}\n");
            for (int i = 0; i < 4; i++) { RandArr(g, 24, 0, 9); grid = Arr(g, 24); T(s, RET_INT, 0, false, "rowTotal(%s, %d)", grid, R(0, 3)); }
            break;
        case 1:
            Task(s, "The grid has 4 rows and 6 columns.\nWrite columnTotal(grid, col): add up the 4 values in that column.");
            Proto(s, "int columnTotal(int grid[4][6], int col)", "return 0;");
            CAT(s->solution, "int columnTotal(int grid[4][6], int col) {\n    int t = 0;\n    for (int row = 0; row < 4; row++) t += grid[row][col];\n    return t;\n}\n");
            for (int i = 0; i < 4; i++) { RandArr(g, 24, 0, 9); grid = Arr(g, 24); T(s, RET_INT, 0, false, "columnTotal(%s, %d)", grid, R(0, 5)); }
            break;
        case 2:
            Task(s, "The grid has 4 rows and 6 columns.\nWrite gridMax(grid): the biggest value anywhere in it.");
            Proto(s, "int gridMax(int grid[4][6])", "return 0;");
            CAT(s->solution, "int gridMax(int grid[4][6]) {\n    int best = grid[0][0];\n    for (int row = 0; row < 4; row++)\n"
                             "        for (int col = 0; col < 6; col++) if (grid[row][col] > best) best = grid[row][col];\n    return best;\n}\n");
            for (int i = 0; i < 4; i++) { RandArr(g, 24, -20, 60); grid = Arr(g, 24); T(s, RET_INT, 0, false, "gridMax(%s)", grid); }
            break;
        case 3:
        {
            int k = R(0, 4);
            Task(s, "The grid has 4 rows and 6 columns.\nWrite countPixels(grid): how many cells hold %d.", k);
            Proto(s, "int countPixels(int grid[4][6])", "return 0;");
            CAT(s->solution, "int countPixels(int grid[4][6]) {\n    int c = 0;\n    for (int row = 0; row < 4; row++)\n"
                             "        for (int col = 0; col < 6; col++) if (grid[row][col] == %d) c++;\n    return c;\n}\n", k);
            for (int i = 0; i < 4; i++) { RandArr(g, 24, 0, 4); grid = Arr(g, 24); T(s, RET_INT, 0, false, "countPixels(%s)", grid); }
            break;
        }
        case 4:
        {
            int k = R(1, 9);
            Task(s, "The grid has 4 rows and 6 columns.\nWrite paintBorder(grid): set every cell on the OUTSIDE edge to %d.\nThe middle cells stay as they are.", k);
            Proto(s, "void paintBorder(int grid[4][6])", NULL);
            CAT(s->solution, "void paintBorder(int grid[4][6]) {\n    for (int row = 0; row < 4; row++)\n        for (int col = 0; col < 6; col++)\n"
                             "            if (row == 0 || row == 3 || col == 0 || col == 5) grid[row][col] = %d;\n}\n", k);
            for (int i = 0; i < 3; i++) { RandArr(g, 24, 0, 0); for (int c = 0; c < 24; c++) if (Pick(3) == 0) g[c] = R(1, 5) * 10; grid = Arr(g, 24); T(s, RET_NONE, 1, false, "paintBorder(%s)", grid); }
            break;
        }
        default:
            Task(s, "The grid has 4 rows and 6 columns.\nWrite printColumn(grid, col): print the 4 values in that column,\ntop to bottom, one per line.  (use printf(\"%%d\\n\", ...))");
            Proto(s, "void printColumn(int grid[4][6], int col)", NULL);
            CAT(s->solution, "void printColumn(int grid[4][6], int col) {\n    for (int row = 0; row < 4; row++) printf(\"%%d\\n\", grid[row][col]);\n}\n");
            for (int i = 0; i < 3; i++) { RandArr(g, 24, 0, 9); grid = Arr(g, 24); T(s, RET_NONE, 0, true, "printColumn(%s, %d)", grid, R(0, 5)); }
            break;
    }
}

// ---- Exam 2: singly linked lists (struct + new + ->) ----
static const char *LIST_TOP = "struct Node {\n    int data;\n    Node *next;\n};\n";
static const char *LIST_RIG =
    "// ---- Bitty's test rig: leave this alone ----\n"
    "// Builds a list from a number's digits (3172 -> 3 1 7 2) and reads one back as a number\n"
    "Node* rigList(int number) {\n"
    "    Node *head = nullptr;\n"
    "    while (number > 0) {\n"
    "        Node *n = new Node();\n"
    "        n->data = number % 10;\n"
    "        n->next = head;\n"
    "        head = n;\n"
    "        number = number / 10;\n"
    "    }\n"
    "    return head;\n"
    "}\n"
    "int digits(Node *head) {\n"
    "    int number = 0;\n"
    "    for (Node *p = head; p != nullptr; p = p->next) number = number * 10 + p->data;\n"
    "    return number;\n"
    "}\n";

static int ListNumber(int lo, int hi)      // digits 1-9 only, so the rig reads it back the same
{
    int len = R(lo, hi), n = 0;
    for (int i = 0; i < len; i++) n = n * 10 + R(1, 9);
    return n;
}

static void G_List(Spec *s)
{
    int v = Pick(7);
    snprintf(s->top, sizeof s->top, "%s", LIST_TOP);
    snprintf(s->rig, sizeof s->rig, "%s", LIST_RIG);
    snprintf(s->name, sizeof s->name, "Follow the Chain");
    switch (v)
    {
        case 0:
            Task(s, "Write countNodes(head): how many nodes are in the list.\n(an empty list is nullptr)");
            Proto(s, "int countNodes(Node *head)", "return 0;");
            CAT(s->solution, "int countNodes(Node *head) {\n    int c = 0;\n    for (Node *p = head; p != nullptr; p = p->next) c++;\n    return c;\n}\n");
            CAT(s->rig, "int test(int number) {\n    return countNodes(rigList(number));\n}\n");
            T(s, RET_INT, 0, false, "test(0)");
            for (int i = 0; i < 3; i++) T(s, RET_INT, 0, false, "test(%d)", ListNumber(1, 7));
            break;
        case 1:
            Task(s, "Write listTotal(head): add up the data in every node.");
            Proto(s, "int listTotal(Node *head)", "return 0;");
            CAT(s->solution, "int listTotal(Node *head) {\n    int t = 0;\n    for (Node *p = head; p != nullptr; p = p->next) t += p->data;\n    return t;\n}\n");
            CAT(s->rig, "int test(int number) {\n    return listTotal(rigList(number));\n}\n");
            for (int i = 0; i < 4; i++) T(s, RET_INT, 0, false, "test(%d)", ListNumber(1, 7));
            break;
        case 2:
        {
            int k = R(1, 9);
            Task(s, "Write countMatches(head, value): how many nodes hold value.");
            Proto(s, "int countMatches(Node *head, int value)", "return 0;");
            CAT(s->solution, "int countMatches(Node *head, int value) {\n    int c = 0;\n    for (Node *p = head; p != nullptr; p = p->next) if (p->data == value) c++;\n    return c;\n}\n");
            CAT(s->rig, "int test(int number, int value) {\n    return countMatches(rigList(number), value);\n}\n");
            for (int i = 0; i < 4; i++) { int n = ListNumber(3, 7); T(s, RET_INT, 0, false, "test(%d, %d)", n, i == 3 ? k : n % 10); }
            break;
        }
        case 3:
            Task(s, "Write pushFront(head, value): put a NEW node holding value at the\nfront of the list and return the new head.");
            Proto(s, "Node* pushFront(Node *head, int value)", "return head;");
            CAT(s->solution, "Node* pushFront(Node *head, int value) {\n    Node *n = new Node();\n    n->data = value;\n    n->next = head;\n    return n;\n}\n");
            CAT(s->rig, "int test(int number, int value) {\n    return digits(pushFront(rigList(number), value));\n}\n");
            T(s, RET_INT, 0, false, "test(0, %d)", R(1, 9));
            for (int i = 0; i < 3; i++) T(s, RET_INT, 0, false, "test(%d, %d)", ListNumber(1, 5), R(1, 9));
            break;
        case 4:
            Task(s, "Write pushBack(head, value): put a NEW node holding value at the END\nof the list and return the head (it changes if the list was empty).");
            Proto(s, "Node* pushBack(Node *head, int value)", "return head;");
            CAT(s->solution, "Node* pushBack(Node *head, int value) {\n    Node *n = new Node();\n    n->data = value;\n    n->next = nullptr;\n"
                             "    if (head == nullptr) return n;\n    Node *p = head;\n    while (p->next != nullptr) p = p->next;\n    p->next = n;\n    return head;\n}\n");
            CAT(s->rig, "int test(int number, int value) {\n    return digits(pushBack(rigList(number), value));\n}\n");
            T(s, RET_INT, 0, false, "test(0, %d)", R(1, 9));
            for (int i = 0; i < 3; i++) T(s, RET_INT, 0, false, "test(%d, %d)", ListNumber(1, 5), R(1, 9));
            break;
        case 5:
            Task(s, "Write dropFirst(head): remove the FIRST node (delete it!) and return\nthe new head. An empty list stays empty.");
            Proto(s, "Node* dropFirst(Node *head)", "return head;");
            CAT(s->solution, "Node* dropFirst(Node *head) {\n    if (head == nullptr) return nullptr;\n    Node *rest = head->next;\n    delete head;\n    return rest;\n}\n");
            CAT(s->rig, "int test(int number) {\n    return digits(dropFirst(rigList(number)));\n}\n");
            T(s, RET_INT, 0, false, "test(0)");
            for (int i = 0; i < 3; i++) T(s, RET_INT, 0, false, "test(%d)", ListNumber(1, 6));
            break;
        default:
            Task(s, "Write insertAfter(head, target, value): put a NEW node holding value\nright AFTER the first node holding target. If target isn't there,\n"
                    "change nothing. Return the head.");
            Proto(s, "Node* insertAfter(Node *head, int target, int value)", "return head;");
            CAT(s->solution, "Node* insertAfter(Node *head, int target, int value) {\n    Node *p = head;\n    while (p != nullptr && p->data != target) p = p->next;\n"
                             "    if (p == nullptr) return head;\n    Node *n = new Node();\n    n->data = value;\n    n->next = p->next;\n    p->next = n;\n    return head;\n}\n");
            CAT(s->rig, "int test(int number, int target, int value) {\n    return digits(insertAfter(rigList(number), target, value));\n}\n");
            for (int i = 0; i < 4; i++)
            {
                int n = ListNumber(2, 5), m = n, pick = R(0, 3);
                while (pick-- > 0 && m >= 10) m /= 10;
                T(s, RET_INT, 0, false, "test(%d, %d, %d)", n, i == 3 ? 0 : m % 10, R(1, 9));
            }
            break;
    }
}

// ---- Exam 2: doubly linked lists (class + head/tail + prev pointers) ----
static void G_DoubleList(Spec *s)
{
    int v = Pick(2);
    snprintf(s->name, sizeof s->name, "Two-Way Street");
    snprintf(s->top, sizeof s->top,
             "class DNode {\npublic:\n    int data = 0;\n    DNode *prev = nullptr;\n    DNode *next = nullptr;\n};\n\n"
             "class DList {\npublic:\n    DNode *head = nullptr;\n    DNode *tail = nullptr;\n};\n");
    const char *f = v == 0 ? "addFront" : "addBack";
    if (v == 0)
    {
        Task(s, "Write addFront(list, value): put a NEW node at the FRONT of a doubly\nlinked list. Fix next AND prev, and the tail if the list was empty.");
        Proto(s, "void addFront(DList *list, int value)", NULL);
        CAT(s->solution, "void addFront(DList *list, int value) {\n    DNode *n = new DNode();\n    n->data = value;\n    n->next = list->head;\n"
                         "    if (list->head != nullptr) list->head->prev = n;\n    else list->tail = n;\n    list->head = n;\n}\n");
    }
    else
    {
        Task(s, "Write addBack(list, value): put a NEW node at the END of a doubly\nlinked list. Fix next AND prev, and the head if the list was empty.");
        Proto(s, "void addBack(DList *list, int value)", NULL);
        CAT(s->solution, "void addBack(DList *list, int value) {\n    DNode *n = new DNode();\n    n->data = value;\n    n->prev = list->tail;\n"
                         "    if (list->tail != nullptr) list->tail->next = n;\n    else list->head = n;\n    list->tail = n;\n}\n");
    }
    snprintf(s->rig, sizeof s->rig,
             "// ---- Bitty's test rig: leave this alone ----\n"
             "// Adds a, b, c with your function, then reads the list front-to-back or back-to-front\n"
             "int forwards(int a, int b, int c) {\n    DList *list = new DList();\n    %s(list, a);\n    %s(list, b);\n    %s(list, c);\n"
             "    int number = 0;\n    for (DNode *p = list->head; p != nullptr; p = p->next) number = number * 10 + p->data;\n    return number;\n}\n"
             "int backwards(int a, int b, int c) {\n    DList *list = new DList();\n    %s(list, a);\n    %s(list, b);\n    %s(list, c);\n"
             "    int number = 0;\n    for (DNode *p = list->tail; p != nullptr; p = p->prev) number = number * 10 + p->data;\n    return number;\n}\n",
             f, f, f, f, f, f);
    for (int i = 0; i < 2; i++)
    {
        int a = R(1, 9), b = R(1, 9), c = R(1, 9);
        while (b == a) b = R(1, 9);
        T(s, RET_INT, 0, false, "forwards(%d, %d, %d)", a, b, c);
        T(s, RET_INT, 0, false, "backwards(%d, %d, %d)", a, b, c);
    }
}

// ===========================================================================
// Topic table
// ===========================================================================
typedef struct { const char *name, *source; void (*make)(Spec *); } Topic;
static const Topic TOPICS[] = {
    { "Leap years",           "Exam 1", G_Leap },
    { "Digits",               "Exam 1", G_Digits },
    { "Palindromes",          "Exam 1", G_Palindrome },
    { "Primes",               "Exam 1", G_Prime },
    { "Number bases",         "Exam 1", G_Base },
    { "Nested-loop patterns", "Exam 1", G_Pattern },
    { "Loops",                "Exam 1", G_Loop },
    { "if / else if",         "Exam 1", G_Grade },
    { "Array totals",         "Exam 1", G_ArraySum },
    { "Array search",         "Exam 1", G_Find },
    { "Second largest",       "Exam 1", G_Second },
    { "Duplicates",           "Exam 1", G_Frequency },
    { "Rearranging arrays",   "Exam 2", G_Rearrange },
    { "Copying arrays",       "Exam 2", G_Copy },
    { "Insert / delete",      "Exam 2", G_InsertDelete },
    { "Counting in strings",  "Exam 1", G_StringCount },
    { "Changing strings",     "Exam 1", G_StringChange },
    { "Comparing strings",    "Exam 1", G_StringCompare },
    { "Words",                "Exam 1", G_Words },
    { "Pointers",             "Exam 2", G_Pointers },
    { "2D arrays",            "Exam 2", G_Grid },
    { "Linked lists",         "Exam 2", G_List },
    { "Doubly linked lists",  "Exam 2", G_DoubleList },
};
#define TOPIC_COUNT (int)(sizeof TOPICS / sizeof TOPICS[0])

int         Endless_TopicCount(void)        { return TOPIC_COUNT; }
const char *Endless_TopicName(int topic)    { return (topic >= 0 && topic < TOPIC_COUNT) ? TOPICS[topic].name : ""; }
const char *Endless_TopicSource(int topic)  { return (topic >= 0 && topic < TOPIC_COUNT) ? TOPICS[topic].source : ""; }

static int recent[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

int Endless_PickTopic(unsigned seed)
{
    rng = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < 6; i++) Next();
    int topic = 0;
    for (int tries = 0; tries < 50; tries++)
    {
        topic = Pick(TOPIC_COUNT);
        bool seen = false;
        for (int k = 0; k < 8; k++) if (recent[k] == topic) seen = true;
        if (!seen) break;
    }
    memmove(recent + 1, recent, sizeof recent - sizeof recent[0]);
    recent[0] = topic;
    return topic;
}

// ===========================================================================
// Turning a Spec into a Level: run the reference to get the answers
// ===========================================================================
static void Escape(const char *in, char *out, int size)
{
    int n = 0;
    for (int i = 0; in[i] && n < size - 3; i++)
    {
        if (in[i] == '\n')      { out[n++] = '\\'; out[n++] = 'n'; }
        else if (in[i] == '"')  { out[n++] = '\\'; out[n++] = '"'; }
        else if (in[i] == '\\') { out[n++] = '\\'; out[n++] = '\\'; }
        else out[n++] = in[i];
    }
    out[n] = '\0';
}

static VmStatus RunToEnd(Vm *vm)
{
    int batches = 0;
    for (;;)
    {
        VmStatus st = Vm_Run(vm, 100000);
        if (st == VM_WAITING) { Vm_ActionDone(vm, 0); continue; }
        if ((st == VM_RUNNING || st == VM_PAUSED) && ++batches < 200) continue;
        return st;
    }
}

// Fill in the expected answers; writes the [tests] lines into 'out'
static bool AnswerTests(const Spec *s, const char *solution, char *out, int size, char *error, int errorSize)
{
    VmOptions opt = { s->ban, 0 };
    VmBuildResult r;
    Vm *vm = Vm_Build(solution, &opt, NULL, &r);
    if (!vm) { snprintf(error, (size_t)errorSize, "reference doesn't compile: %s", r.message); return false; }

    static LevelTest t;
    out[0] = '\0';
    for (int i = 0; i < s->n; i++)
    {
        const TestSpec *ts = &s->t[i];
        char line[2048], err[200];
        snprintf(line, sizeof line, "%s == 0", ts->call);
        if (!Level_ParseTest(line, &t, err, sizeof err))
        {
            snprintf(error, (size_t)errorSize, "bad test %s: %s", ts->call, err);
            Vm_Destroy(vm);
            return false;
        }
        RepairRun run;
        TestOutcome o;
        if (!Repair_BeginTest(vm, &t, &run, &o) || RunToEnd(vm) != VM_FINISHED)
        {
            snprintf(error, (size_t)errorSize, "reference failed on %s: %s", ts->call, Vm_ErrorMessage(vm));
            Vm_Destroy(vm);
            return false;
        }

        snprintf(line, sizeof line, "%s", ts->call);
        char value[600];
        if (ts->ret)
        {
            LevelValue v; memset(&v, 0, sizeof v);
            v.kind = VALUE_INT;
            v.display = ts->ret == RET_BOOL ? SHOW_BOOL : ts->ret == RET_CHAR ? SHOW_CHAR : SHOW_INT;
            v.value = Vm_ReturnValue(vm);
            Level_FormatValue(&v, value, sizeof value);
            Cat(line, sizeof line, " == %s", value);
        }
        if (ts->prints)
        {
            char printed[600];
            Escape(Vm_Output(vm), printed, sizeof printed);
            Cat(line, sizeof line, " prints \"%s\"", printed);
        }
        bool first = true;
        for (int k = 0; k < t.argc; k++)
        {
            if (!(ts->checks & (1 << k))) continue;
            const LevelValue *arg = &t.args[k];
            int addr = run.argAddr[k];
            value[0] = '\0';
            if (arg->kind == VALUE_ARRAY)
            {
                Cat(value, sizeof value, "{");
                for (int j = 0; j < arg->count; j++) Cat(value, sizeof value, "%s%d", j ? ", " : "", Vm_ReadMemory(vm, addr + j));
                Cat(value, sizeof value, "}");
            }
            else if (arg->kind == VALUE_STRING)
            {
                char text[300]; int j = 0;
                for (; j < (int)sizeof text - 1; j++) { int ch = Vm_ReadMemory(vm, addr + j); if (!ch) break; text[j] = (char)ch; }
                text[j] = '\0';
                char esc[580]; Escape(text, esc, sizeof esc);
                snprintf(value, sizeof value, "\"%s\"", esc);
            }
            else snprintf(value, sizeof value, "%d", Vm_ReadMemory(vm, addr));
            Cat(line, sizeof line, "%s#%d == %s", first ? " then " : ", ", k + 1, value);
            first = false;
        }
        Cat(out, (size_t)size, "%s\n", line);
    }
    Vm_Destroy(vm);
    return true;
}

// How many of the level's tests this code passes (-1 = doesn't compile)
static int CountPasses(const Level *lv, const char *code)
{
    VmOptions opt = { lv->banIndexing, lv->maxSteps };
    VmBuildResult r;
    Vm *vm = Vm_Build(code, &opt, NULL, &r);
    if (!vm) return -1;
    int ok = 0;
    for (int i = 0; i < lv->testCount; i++)
    {
        TestOutcome o;
        Repair_RunTest(vm, &lv->tests[i], &o);
        if (o.passed) ok++;
    }
    Vm_Destroy(vm);
    return ok;
}

int attemptsUsed = 0;     // for the tests: how many rolls the last challenge took

bool Endless_Make(Level *out, int topic, unsigned seed, int number, char *error, int errorSize)
{
    if (topic < 0 || topic >= TOPIC_COUNT) topic = 0;
    static Spec s;
    static char solution[LEVEL_CODE_MAX], starter[LEVEL_CODE_MAX], tests[8192], text[20000];
    char err[300] = "no attempts";

    for (int attempt = 0; attempt < 12; attempt++)
    {
        rng = (seed ? seed : 1u) * 2654435761u + (unsigned)attempt * 40503u + 1u;
        for (int i = 0; i < 4; i++) Next();
        memset(&s, 0, sizeof s);
        TOPICS[topic].make(&s);

        snprintf(solution, sizeof solution, "%.590s%s%.2390s%s%.1000s", s.top, s.top[0] ? "\n" : "", s.solution, s.rig[0] ? "\n" : "", s.rig);
        if (!AnswerTests(&s, solution, tests, sizeof tests, err, sizeof err)) continue;

        // The starter: the task as comments, one example, then an empty function
        char example[400];
        const char *nl = strchr(tests, '\n');
        snprintf(example, sizeof example, "%.*s", nl ? (int)(nl - tests) : (int)strlen(tests), tests);
        starter[0] = '\0';
        Cat(starter, sizeof starter, "// ENDLESS #%d: %s\n", number, s.name);
        for (const char *p = s.task; *p; )
        {
            const char *e = strchr(p, '\n');
            int len = e ? (int)(e - p) : (int)strlen(p);
            // wrap long lines so they fit the code panel (about 44 characters)
            const char *q = p;
            while (len > 44)
            {
                int cut = 44;
                while (cut > 20 && q[cut] != ' ') cut--;
                if (q[cut] != ' ') cut = 44;
                Cat(starter, sizeof starter, "// %.*s\n", cut, q);
                if (q[cut] == ' ') cut++;
                q += cut; len -= cut;
            }
            Cat(starter, sizeof starter, "// %.*s\n", len, q);
            if (!e) break;
            p = e + 1;
        }
        Cat(starter, sizeof starter, "//\n// Example:\n//   %s\n// No hints, no gaps: write it from scratch!\n\n", example);
        if (s.top[0]) Cat(starter, sizeof starter, "%s\n", s.top);
        Cat(starter, sizeof starter, "%s", s.starter);
        if (s.rig[0]) Cat(starter, sizeof starter, "\n%s", s.rig);

        // The level file, put together and parsed like any other
        text[0] = '\0';
        Cat(text, sizeof text, "title: E.%d  %s\ntype: repair\nban_indexing: %s\n\n", number, s.name, s.ban ? "yes" : "no");
        Cat(text, sizeof text, "[story]\nENDLESS #%d  (%s, %s)\n%s\n\n", number, TOPICS[topic].name, TOPICS[topic].source, s.task);
        Cat(text, sizeof text, "[lesson]\nENDLESS MODE\n\nTopic: %s  (%s)\n\nNo hints and no gold gaps here.\nRead the task in the comments, write the function,\nthen press RUN.\n\n"
                               "Solve it to grow your streak.\nNEXT skips to a new challenge (the streak resets).\n\n", TOPICS[topic].name, TOPICS[topic].source);
        Cat(text, sizeof text, "[code]\n%s\n[solution]\n%s\n[tests]\n%s", starter, solution, tests);

        char perr[256] = "";
        if (!Level_Parse(text, "endless.txt", out, perr, sizeof perr))
        {
            snprintf(err, sizeof err, "level didn't parse: %s", perr);
            continue;
        }
        // Fair? The answer passes everything and the empty starter doesn't.
        if (CountPasses(out, out->solution) != out->testCount) { snprintf(err, sizeof err, "reference failed its own tests"); continue; }
        int starterPasses = CountPasses(out, out->starterCode);
        if (starterPasses < 0) { snprintf(err, sizeof err, "starter doesn't compile"); continue; }
        if (starterPasses >= out->testCount) { snprintf(err, sizeof err, "starter already passes"); continue; }
        attemptsUsed = attempt + 1;
        if (error && errorSize > 0) error[0] = '\0';
        return true;
    }
    if (error) snprintf(error, (size_t)errorSize, "%s: %s", TOPICS[topic].name, err);
    return false;
}

// ===========================================================================
// Best streak
// ===========================================================================
static int  best = 0;
static char bestPath[512] = "";

void Endless_LoadBest(const char *path)
{
    snprintf(bestPath, sizeof bestPath, "%s", path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fscanf(f, "best %d", &best) != 1) best = 0;
    fclose(f);
}

int Endless_Best(void) { return best; }

void Endless_ReportStreak(int streak)
{
    if (streak <= best) return;
    best = streak;
    if (!bestPath[0]) return;
    FILE *f = fopen(bestPath, "w");
    if (!f) return;
    fprintf(f, "best %d\n", best);
    fclose(f);
}
