// vm_test.c - runs real exam solutions and buggy code through the VM.
// Build and run with: test_vm.bat
//
// A fake "host" stands in for the game: it records moves and printed text
// instead of animating anything.

#include "../src/vm.h"
#include "../src/game_api.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

// ===========================================================================
// Fake game
// ===========================================================================
typedef struct {
    char out[8192];
    int  x, y;
    int  moves, writes, lastWrite, waits;
    int  warnings;
    char warning[VM_MSG_MAX];
} FakeHost;

static bool FakeCall(void *user, VmBuiltin id, const int *args, int argc, int *result)
{
    FakeHost *h = user;
    (void)argc;
    *result = 0;
    switch (id)
    {
        case VM_BUILTIN_MOVE:
            h->moves++;
            h->waits++;
            if (args[0] == EAST)  h->x++;
            if (args[0] == WEST)  h->x--;
            if (args[0] == NORTH) h->y--;
            if (args[0] == SOUTH) h->y++;
            return true;                         // takes time
        case VM_BUILTIN_WRITE:
            h->writes++;
            h->waits++;
            h->lastWrite = args[0];
            return true;
        case VM_BUILTIN_READ:  *result = 42;        return false;
        case VM_BUILTIN_SCAN:  *result = TILE_WALL; return false;
        case VM_BUILTIN_GET_X: *result = h->x;      return false;
        case VM_BUILTIN_GET_Y: *result = h->y;      return false;
        default: return false;
    }
}

static void FakePrint(void *user, const char *text)
{
    FakeHost *h = user;
    strncat(h->out, text, sizeof h->out - strlen(h->out) - 1);
}

static void FakeWarning(void *user, int line, const char *msg)
{
    FakeHost *h = user;
    h->warnings++;
    snprintf(h->warning, sizeof h->warning, "Line %d: %s", line, msg);
}

static VmHost MakeHost(FakeHost *h)
{
    memset(h, 0, sizeof *h);
    VmHost host = { h, FakeCall, FakePrint, FakeWarning };
    return host;
}

// Run until finished or error, finishing every action straight away
static VmStatus RunToEnd(Vm *vm)
{
    for (int i = 0; i < 1000000; i++)
    {
        VmStatus s = Vm_Run(vm, 1000);
        if (s == VM_WAITING) { Vm_ActionDone(vm, 0); continue; }
        if (s == VM_RUNNING || s == VM_PAUSED) continue;
        return s;
    }
    return Vm_GetStatus(vm);
}

static void Report(bool ok, const char *name, const char *detail)
{
    if (ok) { passed++; printf("[PASS] %s%s%s\n", name, detail ? "  ->  " : "", detail ? detail : ""); }
    else    { failed++; printf("[FAIL] %s\n", name); if (detail) printf("    %s\n", detail); }
}

// ===========================================================================
// Test helpers
// ===========================================================================
static void ExpectOutput(const char *name, const char *src, const char *expected)
{
    FakeHost h;
    VmHost host = MakeHost(&h);
    VmBuildResult r;
    Vm *vm = Vm_Build(src, NULL, &host, &r);
    if (!vm) { Report(false, name, r.message); return; }

    Vm_Start(vm, NULL, NULL, 0);
    VmStatus s = RunToEnd(vm);

    char detail[600];
    bool ok = (s == VM_FINISHED && strcmp(h.out, expected) == 0);
    if (!ok)
        snprintf(detail, sizeof detail, "status %d, error \"%.250s\", output \"%.250s\"", s, Vm_ErrorMessage(vm), h.out);
    Report(ok, name, ok ? NULL : detail);
    Vm_Destroy(vm);
}

static void ExpectCompileError(const char *name, const char *src, bool banIndexing,
                               int line, const char *mustContain)
{
    VmOptions opt = { banIndexing, 0 };
    VmBuildResult r;
    Vm *vm = Vm_Build(src, &opt, NULL, &r);

    bool ok = !vm && r.line == line && strstr(r.message, mustContain);
    if (ok) Report(true, name, r.message);
    else
    {
        char detail[600];
        snprintf(detail, sizeof detail, "expected line %d containing \"%s\", got: %s",
                 line, mustContain, vm ? "(compiled OK)" : r.message);
        Report(false, name, detail);
    }
    Vm_Destroy(vm);
}

static void ExpectRuntimeError(const char *name, const char *src, VmErrorKind kind,
                               int line, const char *mustContain, int maxInstructions)
{
    FakeHost h;
    VmHost host = MakeHost(&h);
    VmOptions opt = { false, maxInstructions };
    VmBuildResult r;
    Vm *vm = Vm_Build(src, &opt, &host, &r);
    if (!vm) { Report(false, name, r.message); return; }

    Vm_Start(vm, NULL, NULL, 0);
    VmStatus s = RunToEnd(vm);

    bool ok = s == VM_ERROR && Vm_GetErrorKind(vm) == kind && Vm_ErrorLine(vm) == line &&
              strstr(Vm_ErrorMessage(vm), mustContain);
    if (ok) Report(true, name, Vm_ErrorMessage(vm));
    else
    {
        char detail[600];
        snprintf(detail, sizeof detail, "expected kind %d on line %d containing \"%s\"; got status %d kind %d: %s",
                 kind, line, mustContain, s, Vm_GetErrorKind(vm), Vm_ErrorMessage(vm));
        Report(false, name, detail);
    }
    Vm_Destroy(vm);
}

// Call one function with int arguments and check what it returns
static void ExpectCall(Vm *vm, const char *name, const char *func, const int *args, int argc, int expected)
{
    Vm_Start(vm, func, args, argc);
    VmStatus s = RunToEnd(vm);
    bool ok = (s == VM_FINISHED && Vm_ReturnValue(vm) == expected);
    char detail[400];
    if (!ok) snprintf(detail, sizeof detail, "expected %d, got %d (status %d) %s",
                      expected, Vm_ReturnValue(vm), s, Vm_ErrorMessage(vm));
    Report(ok, name, ok ? NULL : detail);
}

static void ReadVmString(Vm *vm, int addr, char *out, int size)
{
    int i = 0;
    for (; i < size - 1; i++)
    {
        int ch = Vm_ReadMemory(vm, addr + i);
        if (ch == 0) break;
        out[i] = (char)ch;
    }
    out[i] = '\0';
}

// ===========================================================================
// Exam 1 solutions (repair levels)
// ===========================================================================
static const char *EXAM1 =
    "bool isLeapYear(int year) {\n"
    "    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;\n"
    "}\n"
    "int Reversed(int n) {\n"
    "    int r = 0;\n"
    "    while (n > 0) { r = r * 10 + n % 10; n /= 10; }\n"
    "    return r;\n"
    "}\n"
    "bool isAPalindrome(int n) { return Reversed(n) == n; }\n"
    "bool isAPrimeNumber(int n) {\n"
    "    if (n < 2) return false;\n"
    "    for (int i = 2; i <= n / i; i++) {\n"
    "        if (n % i == 0) return false;\n"
    "    }\n"
    "    return true;\n"
    "}\n"
    "int convertBinarytoDecimal(int b) {\n"
    "    int result = 0, place = 1;\n"
    "    while (b > 0) { result += (b % 10) * place; place *= 2; b /= 10; }\n"
    "    return result;\n"
    "}\n"
    "int find(int size, int arr[], int toFind) {\n"
    "    for (int i = 0; i < size; i++) if (arr[i] == toFind) return i;\n"
    "    return -1;\n"
    "}\n"
    "void reverse(int size, int arr[]) {\n"
    "    for (int i = 0; i < size / 2; i++) {\n"
    "        int t = arr[i]; arr[i] = arr[size - 1 - i]; arr[size - 1 - i] = t;\n"
    "    }\n"
    "}\n"
    "bool insertElement(int& size, int& count, int arr[], int elementToInsert, int insertIndex) {\n"
    "    if (count >= size || insertIndex < 0 || insertIndex > count) return false;\n"
    "    for (int i = count; i > insertIndex; i--) arr[i] = arr[i - 1];\n"
    "    arr[insertIndex] = elementToInsert;\n"
    "    count++;\n"
    "    return true;\n"
    "}\n"
    "int wordCounter(int size, char characters[]) {\n"
    "    int words = 0;\n"
    "    bool inWord = false;\n"
    "    for (int i = 0; i < size; i++) {\n"
    "        char c = characters[i];\n"
    "        bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');\n"
    "        if (letter && !inWord) words++;\n"
    "        inWord = letter;\n"
    "    }\n"
    "    return words;\n"
    "}\n";

static void TestExam1(void)
{
    printf("\n--- Exam 1 (repair levels) ---\n");
    FakeHost h;
    VmHost host = MakeHost(&h);
    VmBuildResult r;
    Vm *vm = Vm_Build(EXAM1, NULL, &host, &r);
    if (!vm) { Report(false, "Exam 1 code compiles", r.message); return; }
    Report(true, "Exam 1 code compiles", NULL);

    int a;
    a = 1700; ExpectCall(vm, "isLeapYear(1700) == false", "isLeapYear", &a, 1, 0);
    a = 1600; ExpectCall(vm, "isLeapYear(1600) == true",  "isLeapYear", &a, 1, 1);
    a = 2016; ExpectCall(vm, "isLeapYear(2016) == true",  "isLeapYear", &a, 1, 1);
    a = 123;  ExpectCall(vm, "Reversed(123) == 321",      "Reversed", &a, 1, 321);
    a = 121;  ExpectCall(vm, "isAPalindrome(121)",        "isAPalindrome", &a, 1, 1);
    a = 1213; ExpectCall(vm, "!isAPalindrome(1213)",      "isAPalindrome", &a, 1, 0);
    a = 2147483647; ExpectCall(vm, "isAPrimeNumber(2147483647)", "isAPrimeNumber", &a, 1, 1);
    a = 4;    ExpectCall(vm, "!isAPrimeNumber(4)",        "isAPrimeNumber", &a, 1, 0);
    a = 110;  ExpectCall(vm, "convertBinarytoDecimal(110) == 6", "convertBinarytoDecimal", &a, 1, 6);

    // Arrays passed in from the "game"
    int data[] = { 4, 9, 2, 8, 5 };
    int arr = Vm_AllocArray(vm, "arr", data, 5);
    int args3[3] = { 5, arr, 8 };
    ExpectCall(vm, "find(5, arr, 8) == 3", "find", args3, 3, 3);
    args3[2] = 7;
    ExpectCall(vm, "find(5, arr, 7) == -1", "find", args3, 3, -1);

    int args2[2] = { 5, arr };
    Vm_Start(vm, "reverse", args2, 2);
    RunToEnd(vm);
    bool rev = Vm_ReadMemory(vm, arr) == 5 && Vm_ReadMemory(vm, arr + 4) == 4 && Vm_ReadMemory(vm, arr + 2) == 2;
    Report(rev, "reverse() changes the array in place", NULL);

    // insertElement uses C++ references for size and count
    int sizeVal = 6, countVal = 3, room[6] = { 10, 20, 30 };
    int size  = Vm_AllocArray(vm, "size", &sizeVal, 1);
    int count = Vm_AllocArray(vm, "count", &countVal, 1);
    int list  = Vm_AllocArray(vm, "list", room, 6);
    int args5[5] = { size, count, list, 15, 1 };
    ExpectCall(vm, "insertElement(..., 15, 1) == true", "insertElement", args5, 5, 1);
    bool shifted = Vm_ReadMemory(vm, count) == 4 && Vm_ReadMemory(vm, list + 1) == 15 &&
                   Vm_ReadMemory(vm, list + 3) == 30;
    Report(shifted, "insertElement shifts right and count++ through the reference", NULL);

    int text = Vm_AllocString(vm, "characters", "  Hi there  Bitty", 0);
    int args[2] = { 17, text };
    ExpectCall(vm, "wordCounter(\"  Hi there  Bitty\") == 3", "wordCounter", args, 2, 3);

    Vm_Destroy(vm);
}

// ===========================================================================
// Exam 2 solutions (Pointer Wires: [ ] is banned)
// ===========================================================================
static const char *EXAM2 =
    "void swap_ints(int *a, int *b) { int t = *a; *a = *b; *b = t; }\n"
    "int find_max(int *arr, int size) {\n"
    "    int max = *arr;\n"
    "    for (int *p = arr + 1; p < arr + size; p++) if (*p > max) max = *p;\n"
    "    return max;\n"
    "}\n"
    "void reverse_array(int *arr, int size) {\n"
    "    int *l = arr;\n"
    "    int *r = arr + size - 1;\n"
    "    while (l < r) { int t = *l; *l = *r; *r = t; l++; r--; }\n"
    "}\n"
    "int strlen_ptr(char *str) { char *p = str; while (*p != '\\0') p++; return p - str; }\n"
    "int count_vowels(char *str) {\n"
    "    int n = 0;\n"
    "    while (*str) {\n"
    "        char c = *str;\n"
    "        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||\n"
    "            c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U') n++;\n"
    "        str++;\n"
    "    }\n"
    "    return n;\n"
    "}\n"
    "void copy_string(char *dest, char *src) {\n"
    "    while (*src) { *dest = *src; dest++; src++; }\n"
    "    *dest = '\\0';\n"
    "}\n"
    "void concat_strings(char *dest, char *src) { while (*dest) dest++; copy_string(dest, src); }\n"
    "int sum_array(int *arr, int size) {\n"
    "    int sum = 0;\n"
    "    for (int *p = arr; p < arr + size; p++) sum += *p;\n"
    "    return sum;\n"
    "}\n"
    "int compare_strings(char *s1, char *s2) {\n"
    "    while (*s1 && *s1 == *s2) { s1++; s2++; }\n"
    "    return *s1 - *s2;\n"
    "}\n";

static void TestExam2(void)
{
    printf("\n--- Exam 2 (pointers, with [ ] banned) ---\n");
    FakeHost h;
    VmHost host = MakeHost(&h);
    VmOptions opt = { true, 0 };
    VmBuildResult r;
    Vm *vm = Vm_Build(EXAM2, &opt, &host, &r);
    if (!vm) { Report(false, "Exam 2 code compiles with [ ] banned", r.message); return; }
    Report(true, "Exam 2 code compiles with [ ] banned", NULL);

    int five = 5, ten = 10;
    int x = Vm_AllocArray(vm, "x", &five, 1);
    int y = Vm_AllocArray(vm, "y", &ten, 1);
    int xy[2] = { x, y };
    Vm_Start(vm, "swap_ints", xy, 2);
    RunToEnd(vm);
    Report(Vm_ReadMemory(vm, x) == 10 && Vm_ReadMemory(vm, y) == 5, "swap_ints(&x, &y) swaps", NULL);

    int vals[5] = { 3, 9, 2, 8, 5 };
    int arr = Vm_AllocArray(vm, "arr", vals, 5);
    int a2[2] = { arr, 5 };
    ExpectCall(vm, "find_max == 9", "find_max", a2, 2, 9);
    ExpectCall(vm, "sum_array == 27", "sum_array", a2, 2, 27);

    Vm_Start(vm, "reverse_array", a2, 2);
    RunToEnd(vm);
    Report(Vm_ReadMemory(vm, arr) == 5 && Vm_ReadMemory(vm, arr + 4) == 3, "reverse_array with pointers", NULL);

    int hello = Vm_AllocString(vm, "str", "Hello", 0);
    ExpectCall(vm, "strlen_ptr(\"Hello\") == 5", "strlen_ptr", &hello, 1, 5);

    // NOTE: the Exam 2 prep sheet says 3 ('O', 'e', 'A') but forgets the final 'I'. It's 4.
    int openai = Vm_AllocString(vm, "str", "OpenAI", 0);
    ExpectCall(vm, "count_vowels(\"OpenAI\") == 4", "count_vowels", &openai, 1, 4);

    int dest = Vm_AllocString(vm, "dest", "Hello, ", 20);
    int src  = Vm_AllocString(vm, "src", "World!", 0);
    int ds[2] = { dest, src };
    Vm_Start(vm, "concat_strings", ds, 2);
    RunToEnd(vm);
    char buf[64];
    ReadVmString(vm, dest, buf, sizeof buf);
    Report(strcmp(buf, "Hello, World!") == 0, "concat_strings -> \"Hello, World!\"", buf);

    int apple  = Vm_AllocString(vm, "str1", "apple", 0);
    int banana = Vm_AllocString(vm, "str2", "banana", 0);
    int ab[2] = { apple, banana };
    Vm_Start(vm, "compare_strings", ab, 2);
    RunToEnd(vm);
    Report(Vm_ReturnValue(vm) < 0, "compare_strings(\"apple\", \"banana\") < 0", NULL);

    Vm_Destroy(vm);
}

// ===========================================================================
// Escape levels: moving Bitty
// ===========================================================================
static void TestGame(void)
{
    printf("\n--- Escape levels (game actions) ---\n");

    FakeHost h;
    VmHost host = MakeHost(&h);
    VmBuildResult r;
    Vm *vm = Vm_Build("for (int i = 0; i < 3; i++) move(EAST);\nwrite(get_x() + read());", NULL, &host, &r);
    if (!vm) { Report(false, "moves compile", r.message); return; }
    Vm_Start(vm, NULL, NULL, 0);

    // Like the game: the VM stops at each move and waits for the animation
    int waits = 0;
    VmStatus s;
    while ((s = Vm_Run(vm, 1000)) == VM_WAITING || s == VM_RUNNING)
    {
        if (s == VM_WAITING) { waits++; Vm_ActionDone(vm, 0); }
    }
    Report(s == VM_FINISHED && h.moves == 3 && h.x == 3 && waits == 4 && h.lastWrite == 45,
           "3 moves + write(get_x() + read()): VM waits for each animation", NULL);
    Vm_Destroy(vm);

    // Farmer-style forever loop: allowed, keeps going while the game keeps calling Vm_Run
    vm = Vm_Build("while (true) {\n  move(EAST);\n  move(WEST);\n}", NULL, &host, &r);
    Vm_Start(vm, NULL, NULL, 0);
    memset(&h, 0, sizeof h);
    for (int frame = 0; frame < 100; frame++)
    {
        s = Vm_Run(vm, 500);
        if (s == VM_WAITING) Vm_ActionDone(vm, 0);
    }
    Report(Vm_GetStatus(vm) != VM_ERROR && h.moves == 100, "while (true) loop runs forever, one move per frame", NULL);
    Vm_Destroy(vm);

    // Line highlighting / step mode
    vm = Vm_Build("int a = 1;\nint b = 2;\nint c = a + b;", NULL, &host, &r);
    Vm_Start(vm, NULL, NULL, 0);
    int lines[3];
    for (int i = 0; i < 3; i++)
    {
        Vm_PauseAtNextLine(vm);
        Vm_Run(vm, 1000);
        lines[i] = Vm_CurrentLine(vm);
    }
    Report(lines[0] == 1 && lines[1] == 2 && lines[2] == 3, "step mode pauses on lines 1, 2, 3", NULL);
    Vm_Destroy(vm);
}

// ===========================================================================
// Inspector
// ===========================================================================
static const VmVariable *FindVar(const VmVariable *vars, int n, const char *name)
{
    for (int i = 0; i < n; i++) if (strcmp(vars[i].name, name) == 0) return &vars[i];
    return NULL;
}

static void TestInspector(void)
{
    printf("\n--- Inspector ---\n");
    FakeHost h;
    VmHost host = MakeHost(&h);
    VmBuildResult r;
    VmVariable vars[16];

    Vm *vm = Vm_Build("int arr[4] = {5, 6, 7, 8};\nint *p = &arr[2];\nint count;\nmove(EAST);", NULL, &host, &r);
    Vm_Start(vm, NULL, NULL, 0);
    while (Vm_Run(vm, 1000) == VM_RUNNING) {}
    int n = Vm_GetVariables(vm, vars, 16);
    const VmVariable *va = FindVar(vars, n, "arr");
    const VmVariable *vp = FindVar(vars, n, "p");
    const VmVariable *vc = FindVar(vars, n, "count");
    Report(va && va->isArray && va->count == 4 && va->values[3] == 8, "arr shows 4 values", NULL);
    Report(vp && vp->isPointer && strcmp(vp->pointsToName, "arr[2]") == 0, "p points at arr[2]", vp ? vp->pointsToName : "missing");
    Report(vc && vc->values[0] == 0, "global count starts at 0 (C zeroes globals)", NULL);
    Vm_Destroy(vm);

    vm = Vm_Build("void addOne(int &x) {\n  x = x + 1;\n  move(NORTH);\n}\nint score = 41;\naddOne(score);", NULL, &host, &r);
    Vm_Start(vm, NULL, NULL, 0);
    while (Vm_Run(vm, 1000) == VM_RUNNING) {}
    n = Vm_GetVariables(vm, vars, 16);
    const VmVariable *vx = FindVar(vars, n, "x");
    const VmVariable *vs = FindVar(vars, n, "score");
    Report(vx && vx->isRef && strcmp(vx->pointsToName, "score") == 0 && vx->values[0] == 42,
           "reference x is linked to score (42)", vx ? vx->pointsToName : "missing");
    Report(vs != NULL, "globals are visible inside functions", NULL);
    Vm_Destroy(vm);
}

// ===========================================================================
int main(void)
{
    printf("=== VM tests ===\n\n--- Programs ---\n");

    ExpectOutput("hello world in main()",
        "#include <stdio.h>\nint main() {\n    printf(\"Hello, World!\\n\");\n    return 0;\n}",
        "Hello, World!\n");

    ExpectOutput("integer division and %", "printf(\"%d %d\", 10 / 3, 10 % 3);", "3 1");

    ExpectOutput("char maths", "char c = 'a';\nc = c - 32;\nprintf(\"%c\", c);", "A");

    ExpectOutput("switch falls through stacked cases",
        "char grade = 'a';\nswitch (grade) {\n case 'A':\n case 'a': printf(\"Excellent\"); break;\n"
        " default: printf(\"Invalid\");\n}", "Excellent");

    ExpectOutput("do-while", "int n = 3;\ndo { printf(\"%d \", n); n--; } while (n != 0);", "3 2 1 ");

    ExpectOutput("break and continue",
        "for (int i = 1; i <= 10; i++) {\n if (i % 2 == 0) continue;\n if (i == 9) break;\n printf(\"%d \", i);\n}",
        "1 3 5 7 ");

    ExpectOutput("E1 Q6 right-angled triangle",
        "void drawRightAngledTriangle() {\n"
        "  for (int i = 1; i <= 4; i++) {\n    for (int j = 0; j < i; j++) printf(\"A\");\n    printf(\"\\n\");\n  }\n}\n"
        "drawRightAngledTriangle();",
        "A\nAA\nAAA\nAAAA\n");

    ExpectOutput("E1 Q19 2D array (sum)",
        "int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};\nint total = 0;\n"
        "for (int r = 0; r < 2; r++)\n  for (int c = 0; c < 3; c++) total += grid[r][c];\nprintf(\"%d\", total);",
        "21");

    ExpectOutput("recursion", "int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }\nprintf(\"%d\", fact(5));", "120");

    ExpectOutput("strings and %s", "char name[] = \"Bitty\";\nprintf(\"Hi %s, %c!\", name, name[0]);", "Hi Bitty, B!");

    ExpectOutput("cout from Exam 2's main",
        "#include <iostream>\nusing namespace std;\nint main() {\n  int x = 5;\n  char c = 'A';\n"
        "  std::cout << \"x = \" << x << std::endl;\n  cout << c << endl;\n  return 0;\n}",
        "x = 5\nA\n");

    ExpectOutput("pass by value doesn't change the caller",
        "void addOne(int x) { x = x + 1; }\nint a = 5;\naddOne(a);\nprintf(\"%d\", a);", "5");

    ExpectOutput("pass by reference does",
        "void addOne(int &x) { x = x + 1; }\nint a = 5;\naddOne(a);\nprintf(\"%d\", a);", "6");

    TestExam1();
    TestExam2();
    TestGame();
    TestInspector();

    // ---- Runtime errors: the bugs become hazards ----
    printf("\n--- Runtime errors ---\n");
    ExpectRuntimeError("off-by-one loop",
        "int arr[5];\nfor (int i = 0; i <= 5; i++) {\n  arr[i] = i;\n}", VM_ERR_OUT_OF_BOUNDS, 3, "Index 5 doesn't exist in arr", 0);
    ExpectRuntimeError("2D column out of bounds",
        "int grid[2][3];\ngrid[0][3] = 1;", VM_ERR_OUT_OF_BOUNDS, 2, "Valid indexes are 0 to 2", 0);
    ExpectRuntimeError("pointer past the end",
        "int a[3] = {1, 2, 3};\nint *p = a;\nint x = *(p + 3);", VM_ERR_SEGFAULT, 3, "past the end of a", 0);
    ExpectRuntimeError("NULL pointer",
        "int *p = NULL;\n*p = 3;", VM_ERR_SEGFAULT, 2, "NULL pointer", 0);
    ExpectRuntimeError("dangling pointer",
        "int *bad() {\n  int local = 5;\n  return &local;\n}\nint *p = bad();\nprintf(\"%d\", *p);",
        VM_ERR_SEGFAULT, 6, "dangling pointer", 0);
    ExpectRuntimeError("missing '\\0' in a string",
        "char s[3] = {'a', 'b', 'c'};\nprintf(\"%s\", s);", VM_ERR_SEGFAULT, 2, "s[3] doesn't exist", 0);
    ExpectRuntimeError("division by zero",
        "int a = 5;\nint b = 10 / (a - 5);", VM_ERR_DIV_ZERO, 2, "Division by zero", 0);
    ExpectRuntimeError("infinite recursion",
        "int f(int n) {\n  return f(n + 1);\n}\nf(0);", VM_ERR_STACK_OVERFLOW, 2, "base case", 0);
    ExpectRuntimeError("missing return",
        "int sign(int x) {\n  if (x > 0) return 1;\n}\nint s = sign(-4);", VM_ERR_NO_RETURN, 2, "without returning", 0);
    ExpectRuntimeError("changing a string literal",
        "char *s = \"hi\";\n*s = 'H';", VM_ERR_READ_ONLY, 2, "string literal", 0);
    ExpectRuntimeError("infinite loop in a repair level",
        "int i = 0;\nwhile (i < 10) {\n  printf(\"%d\", i);\n}", VM_ERR_TOO_LONG, 2, "infinite loop", 20000);
    ExpectRuntimeError("bad direction",
        "move(7);", VM_ERR_BAD_ARGUMENT, 1, "NORTH, EAST, SOUTH or WEST", 0);

    // Garbage values are a warning, not an error
    {
        FakeHost h;
        VmHost host = MakeHost(&h);
        VmBuildResult r;
        Vm *vm = Vm_Build("int f() {\n  int sum;\n  sum += 5;\n  return sum;\n}\nf();", NULL, &host, &r);
        Vm_Start(vm, NULL, NULL, 0);
        VmStatus s = RunToEnd(vm);
        Report(s == VM_FINISHED && h.warnings == 1 && strstr(h.warning, "sum is used before"),
               "uninitialised variable gives a garbage warning", h.warning);
        Vm_Destroy(vm);
    }

    // ---- Compile errors ----
    printf("\n--- Compile errors ---\n");
    ExpectCompileError("undeclared variable",   "int a = 1;\ntotal = a + 1;", false, 2, "hasn't been declared");
    ExpectCompileError("declared twice",        "int x = 1;\nint x = 2;", false, 2, "already declared on line 1");
    ExpectCompileError("copy whole array",      "int a[3];\nint b[3];\na = b;", false, 3, "one element at a time");
    ExpectCompileError("wrong number of values","int add(int a, int b) { return a + b; }\nint x = add(1);", false, 2, "takes 2 values but you gave 1");
    ExpectCompileError("printf placeholders",   "printf(\"%d %d\\n\", 5);", false, 1, "more placeholders");
    ExpectCompileError("%s with an int",        "int x = 5;\nprintf(\"%s\", x);", false, 2, "%s prints a string");
    ExpectCompileError("%f not available",      "printf(\"%f\", 3);", false, 1, "decimals");
    ExpectCompileError("[ ] banned in Pointer Wires", "int first(int *arr) {\n  return arr[0];\n}", true, 2, "*(arr + i)");
    ExpectCompileError("break outside a loop",  "int x = 1;\nbreak;", false, 2, "inside a loop");
    ExpectCompileError("void returning a value","void f() {\n  return 5;\n}", false, 2, "is void");
    ExpectCompileError("missing return value",  "int f() {\n  return;\n}", false, 2, "must return");
    ExpectCompileError("declared, never written","int f(int);\nint x = f(2);", false, 2, "never written");
    ExpectCompileError("reference needs a variable", "void inc(int &x) { x++; }\ninc(5);", false, 2, "must be a variable");
    ExpectCompileError("const can't change",    "const int MAX = 5;\nMAX = 6;", false, 2, "MAX is const");
    ExpectCompileError("void has no value",     "void hi() { }\nint x = hi();", false, 2, "doesn't give back a value");
    ExpectCompileError("int into a pointer",    "int x = 5;\nint *p = x;", false, 2, "needs an address");
    ExpectCompileError("address into an int",   "int x = 5;\nint y = &x;", false, 2, "gave it an address");
    ExpectCompileError("* on a non-pointer",    "int x = 3;\nint y = *x;", false, 2, "only works on a pointer");
    ExpectCompileError("built-in name",         "int move = 3;", false, 1, "built-in function");
    ExpectCompileError("calling a variable",    "int x = 3;\nx();", false, 2, "not a function");
    ExpectCompileError("unknown function",      "int y = sqaure(4);", false, 1, "no function called sqaure");
    ExpectCompileError("changing NORTH",        "NORTH = 2;", false, 1, "fixed built-in value");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
