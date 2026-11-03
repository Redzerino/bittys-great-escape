// parser_test.c - checks the parser on real exam-style code.
// Build and run with: test_parser.bat
//
// Each test parses some code and compares the tree (printed as an
// S-expression) with what we expect. For example
//     x = a + b * 2;
// should give
//     (program (= x (+ a (* b 2))))
// which shows that * was done before +.

#include "../src/parser.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

static void ExpectTree(const char *name, const char *src, const char *expected)
{
    Ast ast;
    ParseResult res;
    char tree[4096];

    if (!Parser_ParseSource(src, &ast, &res))
    {
        failed++;
        printf("[FAIL] %s\n    unexpected error: %s\n", name, res.errorMsg);
        return;
    }

    Ast_Dump(ast.root, tree, sizeof tree);
    if (strcmp(tree, expected) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n    expected: %s\n    got:      %s\n", name, expected, tree);
    }
    Ast_Free(&ast);
}

static void ExpectError(const char *name, const char *src, int line, const char *mustContain)
{
    Ast ast;
    ParseResult res;

    bool ok = Parser_ParseSource(src, &ast, &res);
    if (!ok && res.errorLine == line && strstr(res.errorMsg, mustContain))
    {
        passed++;
        printf("[PASS] %s  ->  %s\n", name, res.errorMsg);
    }
    else
    {
        failed++;
        printf("[FAIL] %s (expected error on line %d containing \"%s\")\n", name, line, mustContain);
        if (!ok) printf("    got: %s\n", res.errorMsg);
        else     Ast_Free(&ast);
    }
}

static void ExpectWarning(const char *name, const char *src, int line, const char *mustContain)
{
    Ast ast;
    ParseResult res;

    bool ok = Parser_ParseSource(src, &ast, &res);
    bool found = false;
    for (int i = 0; ok && i < res.warningCount; i++)
    {
        if (res.warningLines[i] == line && strstr(res.warnings[i], mustContain)) found = true;
    }

    if (found)
    {
        passed++;
        printf("[PASS] %s  ->  %s\n", name, res.warnings[0]);
    }
    else
    {
        failed++;
        printf("[FAIL] %s (expected a warning on line %d containing \"%s\")\n", name, line, mustContain);
        if (!ok) printf("    got error: %s\n", res.errorMsg);
    }
    if (ok) Ast_Free(&ast);
}

int main(void)
{
    printf("=== Parser tests ===\n\n");

    // ---- Basics ----
    ExpectTree("declaration", "int x = 5;",
        "(program (var int x 5))");

    ExpectTree("precedence: * before +", "x = a + b * 2;",
        "(program (= x (+ a (* b 2))))");

    ExpectTree("several variables at once", "int x = 5, y = 10, *p = &x;",
        "(program (decls (var int x 5) (var int y 10) (var int* p (addr x))))");

    ExpectTree("ternary, not, negative numbers",
        "int m = a > b ? a : b; bool ok = !done && -x < -1;",
        "(program (var int m (? (> a b) a b)) (var bool ok (&& (! done) (< (neg x) -1))))");

    // ---- Escape levels: loose statements, no main() needed ----
    ExpectTree("escape level script",
        "for (int i = 0; i < 3; i++) move(EAST);\nwrite(read() + 1);",
        "(program (for (var int i 0) (< i 3) (post++ i) (call move EAST)) (call write (+ (call read) 1)))");

    // ---- Exam 1 ----
    ExpectTree("E1 Q1 leap year",
        "bool isLeapYear(int year) {\n return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;\n}",
        "(program (func bool isLeapYear ((param int year)) (block (return "
        "(|| (&& (== (% year 4) 0) (!= (% year 100) 0)) (== (% year 400) 0))))))");

    ExpectTree("E1 Q11 reference + array params (prototype)",
        "bool insertElement(int& size, int& count, int arr[], int e, int idx);",
        "(program (func bool insertElement ((param int& size) (param int& count) "
        "(param int[] arr) (param int e) (param int idx)) ;))");

    ExpectTree("E1 Q19 2D array parameter",
        "void PrintArray(int array[4][6]) { printf(\"%d \", array[r][c]); }",
        "(program (func void PrintArray ((param int[4][6] array)) "
        "(block (call printf \"%d \" (index (index array r) c)))))");

    ExpectTree("2D array with { } rows", "int array[2][3] = { {0,0,3}, {1,1,1} };",
        "(program (var int[2][3] array {{0 0 3} {1 1 1}}))");

    ExpectTree("string sets array size (+1 for \\0)", "char name[] = \"Bitty\";",
        "(program (var char[6] name \"Bitty\"))");

    // ---- Notes: switch, do-while, main ----
    ExpectTree("switch with stacked cases",
        "switch (grade) { case 'A': case 'a': printf(\"Excellent\\n\"); break; "
        "default: printf(\"Invalid\\n\"); }",
        "(program (switch grade (case 'A') (case 'a' (call printf \"Excellent\\n\") (break)) "
        "(default (call printf \"Invalid\\n\"))))");

    ExpectTree("do-while", "do { number--; } while (number != 0);",
        "(program (do (block (post-- number)) (!= number 0)))");

    ExpectTree("int main(void)", "int main(void) { return 0; }",
        "(program (func int main () (block (return 0))))");

    // ---- Exam 2: pointers ----
    ExpectTree("E2 swap_ints",
        "void swap_ints(int *a, int *b) { int t = *a; *a = *b; *b = t; }",
        "(program (func void swap_ints ((param int* a) (param int* b)) "
        "(block (var int t (deref a)) (= (deref a) (deref b)) (= (deref b) t))))");

    ExpectTree("E2 strlen_ptr",
        "int strlen_ptr(char *str) { int n = 0; while (*str != '\\0') { n++; str++; } return n; }",
        "(program (func int strlen_ptr ((param char* str)) (block (var int n 0) "
        "(while (!= (deref str) '\\0') (block (post++ n) (post++ str))) (return n))))");

    ExpectTree("E2 sum_array with pointer loop",
        "int sum_array(int *arr, int size) { int sum = 0; "
        "for (int *p = arr; p < arr + size; p++) sum += *p; return sum; }",
        "(program (func int sum_array ((param int* arr) (param int size)) (block (var int sum 0) "
        "(for (var int* p arr) (< p (+ arr size)) (post++ p) (+= sum (deref p))) (return sum))))");

    ExpectTree("E2 cout", "std::cout << \"x = \" << x << std::endl;",
        "(program (<< (<< (<< std::cout \"x = \") x) std::endl))");

    ExpectTree("E2 linked list members", "n->next = head; list.insertAtEnd(5);",
        "(program (= (-> n next) head) (call (. list insertAtEnd) 5))");

    ExpectTree("using namespace std is ignored", "using namespace std; int a = 1;",
        "(program (var int a 1))");

    // ---- Errors ----
    printf("\n--- Error messages the player would see ---\n");
    ExpectError("missing ; points at the right line", "int x = 5\nint y = 6;", 1, "missing ;");
    ExpectError("unclosed { points at the {",          "int main() {\n  int x = 1;\n", 1, "never closed");
    ExpectError("string too long for array",           "char s[5] = \"Hello\";", 1, "hidden '\\0'");
    ExpectError("too many values for array",           "int a[2] = {1, 2, 3};", 1, "holds 2 but you gave 3");
    ExpectError("array with no size",                  "int a[];", 1, "give the array a size");
    ExpectError("commas in a for loop",                "for (int i = 0, i < 10, i++) {}", 1, "separated by ;");
    ExpectError("else without if",                     "x = 1;\nelse y = 2;", 2, "no matching if");
    ExpectError("function inside function",            "int main() {\n  int helper() { return 1; }\n}", 2, "forget a }");
    ExpectError("assign to a number",                  "5 = x;", 1, "must be a variable");
    ExpectError("float not available",                 "float price = 2;", 1, "Use int");
    ExpectError("string type not available",           "string s;", 1, "char array");
    ExpectError("case outside switch",                 "case 1: x = 2;", 1, "inside a switch");
    ExpectError("case value must be fixed",            "switch (x) { case y: break; }", 1, "fixed number");
    ExpectError("missing )",                           "if (x > 3 {\n}", 1, ")");
    ExpectError("do-while missing ;",                  "do { x--; } while (x > 0)", 1, "needs a ;");
    ExpectError("reference with no variable",          "int &r;", 1, "reference");
    ExpectError("lexer errors come through too",       "char c = 'AB';", 1, "ONE character");

    // ---- Warnings (valid C, but almost certainly a mistake) ----
    printf("\n--- Warnings ---\n");
    ExpectWarning("= instead of ==",        "if (x = 5) y = 1;", 1, "Did you mean ==");
    ExpectWarning("== that does nothing",   "x == 5;", 1, "Did you mean =");
    ExpectWarning("; after if",             "if (x > 3); y = 1;", 1, "ALWAYS runs");
    ExpectWarning("; after for",            "for (int i = 0; i < 3; i++);\nmove(EAST);", 1, "empty loop");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
