// levels_test.c - checks every level file, and the test-line parser.
// Build and run with: test_levels.bat
//
// For every repair level it checks that:
//   - the [solution] compiles and passes every test (so the level is solvable
//     and the expected answers are right)
//   - the [code] starter compiles, but does NOT already pass (so there's work to do)

#include "../src/levels.h"
#include "../src/repair.h"
#include "../src/vm.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

static void Report(bool ok, const char *name, const char *detail)
{
    if (ok) { passed++; printf("[PASS] %s\n", name); }
    else    { failed++; printf("[FAIL] %s\n", name); if (detail) printf("    %s\n", detail); }
}

static void ExpectTestParses(const char *line)
{
    LevelTest t;
    char err[256] = "";
    bool ok = Level_ParseTest(line, &t, err, sizeof err);
    Report(ok, line, err);
}

static void ExpectTestError(const char *line, const char *mustContain)
{
    LevelTest t;
    char err[256] = "";
    bool ok = !Level_ParseTest(line, &t, err, sizeof err) && strstr(err, mustContain);
    char name[300];
    snprintf(name, sizeof name, "rejects: %s", line);
    Report(ok, name, err);
}

// Build code for a level; returns NULL (and prints why) if it doesn't compile
static Vm *BuildFor(const Level *lv, const char *code, VmBuildResult *r)
{
    VmOptions opt = { lv->banIndexing, lv->maxSteps };
    return Vm_Build(code, &opt, NULL, r);
}

// Returns how many tests pass. 'firstFail' gets the first failure message.
static int RunAllTests(const Level *lv, const char *code, char *firstFail, int size, bool *compiled)
{
    VmBuildResult r;
    Vm *vm = BuildFor(lv, code, &r);
    firstFail[0] = '\0';
    *compiled = (vm != NULL);
    if (!vm) { snprintf(firstFail, (size_t)size, "doesn't compile: %s", r.message); return 0; }

    int ok = 0;
    for (int i = 0; i < lv->testCount; i++)
    {
        TestOutcome o;
        Repair_RunTest(vm, &lv->tests[i], &o);
        if (o.passed) ok++;
        else if (!firstFail[0]) snprintf(firstFail, (size_t)size, "%s", o.message);
    }
    Vm_Destroy(vm);
    return ok;
}

static void ExpectMessage(const char *name, const char *code, const char *testLine, const char *mustContain)
{
    LevelTest t;
    Level_ParseTest(testLine, &t, NULL, 0);
    VmBuildResult r;
    VmOptions opt = { false, 200000 };
    Vm *vm = Vm_Build(code, &opt, NULL, &r);
    if (!vm) { Report(false, name, r.message); return; }
    TestOutcome o;
    Repair_RunTest(vm, &t, &o);
    bool ok = !o.passed && strstr(o.message, mustContain);
    Report(ok, name, o.message);
    if (ok) printf("      %s\n", o.message);
    Vm_Destroy(vm);
}

int main(void)
{
    printf("=== Level tests ===\n\n--- Test-line parser ---\n");
    ExpectTestParses("isLeapYear(1700) == false");
    ExpectTestParses("find(5, {4, 9, 2, 8, 5}, 8) == 3");
    ExpectTestParses("compare_strings(\"apple\", \"banana\") < 0");
    ExpectTestParses("concat_strings(\"Hello, \"[20], \"World!\") then #1 == \"Hello, World!\"");
    ExpectTestParses("swap_ints(&5, &10) then #1 == 10, #2 == 5");
    ExpectTestParses("count_vowels(\"OpenAI\") == 4");
    ExpectTestParses("toUpper('a') == 'A'");
    ExpectTestError("isLeapYear(1700)", "needs == value");
    ExpectTestError("isLeapYear(1700 == true", "expected , or )");
    ExpectTestError("swap(5, 6) then #1 == 6", "must point at an array");
    ExpectTestError("f(\"hi\"[1]) == 2", "capacity");

    printf("\n--- Messages the player sees ---\n");
    ExpectMessage("wrong answer", "bool isLeapYear(int y) { return true; }", "isLeapYear(1700) == false",
                  "returned true, but it should return false");
    ExpectMessage("wrong array afterwards", "void reverse(int size, int arr[]) { }", "reverse(3, {1, 2, 3}) then #2 == {3, 2, 1}",
                  "should be {3, 2, 1}, but it is {1, 2, 3}");
    ExpectMessage("crash is reported", "int first(int size, int arr[]) { return arr[size]; }", "first(3, {1, 2, 3}) == 1",
                  "past the end of arr");
    ExpectMessage("infinite loop is caught",
                  "int spin(int n) { while (n > 0) { } return n; }", "spin(1) == 1", "infinite loop");
    ExpectMessage("missing function", "int other(int x) { return x; }", "isLeapYear(2000) == true",
                  "needs a function called isLeapYear()");

    printf("\n--- Every level in levels/ ---\n");
    char err[256];
    int count = Levels_LoadFolder("levels", err, sizeof err);
    char detail[300];
    snprintf(detail, sizeof detail, "%s", err);
    Report(count > 0 && !err[0], "levels/levels.txt loads with no problems", detail);

    for (int i = 0; i < Levels_Count(); i++)
    {
        const Level *lv = Levels_Get(i);
        char name[200];

        if (lv->type == LEVEL_ESCAPE)
        {
            snprintf(name, sizeof name, "%s  (escape): has a lesson and starter code", lv->title);
            Report(lv->lesson[0] && lv->starterCode[0], name, "missing [lesson] or [code]");
            continue;
        }

        char fail[400];
        bool compiled;
        int ok = RunAllTests(lv, lv->solution, fail, sizeof fail, &compiled);
        snprintf(name, sizeof name, "%s: solution passes all %d tests", lv->title, lv->testCount);
        Report(lv->solution[0] && ok == lv->testCount, name, lv->solution[0] ? fail : "no [solution] section");

        ok = RunAllTests(lv, lv->starterCode, fail, sizeof fail, &compiled);
        snprintf(name, sizeof name, "%s: starter code compiles but doesn't pass yet", lv->title);
        Report(compiled && ok < lv->testCount, name, compiled ? "the starter code already passes every test" : fail);
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
