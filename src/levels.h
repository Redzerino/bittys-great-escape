// levels.h - levels are loaded from text files in the levels/ folder.
//
// levels/levels.txt lists the level files in play order, one per line.
// Each level file looks like this (see levels/README.md for the full guide):
//
//   title: 3-1  Leap Year Gate
//   type: repair                 (escape = move Bitty, repair = write a function)
//   ban_indexing: no             (yes = arr[i] not allowed, for Pointer Wires)
//
//   [story]     shown in the console when the level loads
//   [lesson]    the "memory dump": the C concept this level teaches
//   [hint]      shown after the first failed attempt
//   [map]       escape levels: the grid (repair levels get a corridor of gates)
//   [code]      starter code in the editor
//   [solution]  a working answer (hidden; used by the automatic level tests)
//   [tests]     repair levels: one test per line, e.g.  isLeapYear(1700) == false

#ifndef LEVELS_H
#define LEVELS_H

#include <stdbool.h>

#define LEVEL_MAX_LEVELS  64
#define LEVEL_MAX_ROWS    12
#define LEVEL_MAX_COLS    16
#define LEVEL_MAX_TESTS   12
#define LEVEL_MAX_ARGS    8
#define LEVEL_MAX_CHECKS  4
#define LEVEL_VALUE_MAX   24
#define LEVEL_TEXT_MAX    2048
#define LEVEL_CODE_MAX    4096

// ---- Test values ----
typedef enum {
    VALUE_INT = 0,      // 5, -3, true, 'A'
    VALUE_ARRAY,        // {1, 2, 3}
    VALUE_STRING,       // "Hello"  or  "Hello, "[20]  (room for 20 chars)
    VALUE_REF           // &5  (a variable holding 5, passed by pointer or reference)
} ValueKind;

typedef enum { SHOW_INT = 0, SHOW_BOOL, SHOW_CHAR } ValueDisplay;

typedef struct {
    ValueKind    kind;
    ValueDisplay display;
    int          value;                     // INT, REF
    int          values[LEVEL_VALUE_MAX];   // ARRAY
    int          count;
    char         text[96];                  // STRING
    int          capacity;
} LevelValue;

typedef enum { CMP_EQ = 0, CMP_NE, CMP_LT, CMP_GT } CompareOp;

typedef struct {
    int        argIndex;    // 1-based: #1 is the first argument
    LevelValue value;       // what it should contain after the call
} LevelCheck;

// One test, e.g.   reverse(5, {1,2,3,4,5}) then #2 == {5,4,3,2,1}
typedef struct {
    char       text[200];   // the line as written, for messages
    char       func[64];
    LevelValue args[LEVEL_MAX_ARGS];
    int        argc;

    bool       hasExpect;   // == / != / < / > on the return value
    CompareOp  op;
    LevelValue expect;

    LevelCheck checks[LEVEL_MAX_CHECKS];
    int        checkCount;
} LevelTest;

typedef enum { LEVEL_ESCAPE = 0, LEVEL_REPAIR } LevelType;

typedef struct {
    char      file[64];
    char      title[64];
    LevelType type;
    bool      banIndexing;
    int       maxSteps;     // repair levels: instruction limit per test (catches infinite loops)

    char      story[LEVEL_TEXT_MAX];
    char      lesson[LEVEL_TEXT_MAX];
    char      hint[512];

    char      map[LEVEL_MAX_ROWS][LEVEL_MAX_COLS + 1];
    int       mapRows;

    char      starterCode[LEVEL_CODE_MAX];
    char      solution[LEVEL_CODE_MAX];

    LevelTest tests[LEVEL_MAX_TESTS];
    int       testCount;
} Level;

// Parse one level file's text. Returns false and fills 'error' if it's wrong.
bool Level_Parse(const char *text, const char *fileName, Level *out, char *error, int errorSize);

// Parse one test line (used by Level_Parse; exposed for the tests).
bool Level_ParseTest(const char *line, LevelTest *out, char *error, int errorSize);

// "{1, 2, 3}", "\"Hi\"", "true", "'A'", "5"
void Level_FormatValue(const LevelValue *v, char *out, int size);

// Load every level listed in folder/levels.txt. Returns how many loaded.
// Problems with individual files are reported in 'error' (the rest still load).
int Levels_LoadFolder(const char *folder, char *error, int errorSize);

int          Levels_Count(void);
const Level *Levels_Get(int index);

#endif
