// game_api.h - the contract between the interpreter (VM) and the game world.
//
// PLAYER-FACING FUNCTIONS (what players type in the editor):
//
//   move(NORTH);        // move one tile: NORTH, EAST, SOUTH, WEST   (takes time)
//   int v = read();     // value stored in the tile Bitty is standing on (takes a moment: he looks down)
//   write(42);          // store a value in the current tile          (takes time)
//   int t = scan(EAST); // type of the neighbouring tile (TILE_...)   (takes a moment: he scans it)
//   int x = get_x();    // Bitty's column                             (instant)
//   int y = get_y();    // Bitty's row                                (instant)
//   printf("%d", v);    // print to the console panel                 (instant)
//
// HOW THE GAME USES THIS:
//   - Instant functions: answered straight away.
//   - Timed functions (move, write, and in escape levels scan and read): World_StartAction() starts the animation,
//     the VM waits, and once World_ActionDone() is true the game calls
//     Vm_ActionDone(vm, world.lastResult). That is what makes the code
//     visibly run step by step.

#ifndef GAME_API_H
#define GAME_API_H

#include <stdbool.h>
#include "vm.h"

#define WORLD_MAX_COLS 16
#define WORLD_MAX_ROWS 12

typedef enum {
    NORTH = 0,
    EAST,
    SOUTH,
    WEST
} Direction;

typedef enum {
    TILE_EMPTY = 0,   // walkable memory
    TILE_WALL,        // protected memory, blocks movement
    TILE_DATA,        // holds a value Bitty can read
    TILE_GATE,        // opens when the right value is written nearby
    TILE_EXIT,        // reach this to clear the level
    TILE_VOID         // unallocated memory: stepping here = Segfault
} TileType;

typedef enum {
    ACT_NONE = 0,
    ACT_MOVE,
    ACT_WRITE,
    ACT_SCAN,           // scan(dir): Bitty turns and sweeps the tile next to him with a beam
    ACT_READ            // read(): Bitty looks down at the tile he's standing on
} ActionType;

typedef struct {
    ActionType type;
    Direction  dir;     // used by ACT_MOVE and ACT_SCAN
    int        value;   // used by ACT_WRITE
    int        line;    // source line that caused it (for highlighting)
} Action;

typedef struct {
    TileType type;
    int      value;
} Tile;

typedef enum {
    WORLD_RUNNING = 0,
    WORLD_WON,        // Bitty reached the exit
    WORLD_SEGFAULT    // Bitty stepped into the void
} WorldState;

// Things that just happened to Bitty, so the game can make him react (see World_TakeEvent)
typedef enum {
    EVENT_NONE = 0,
    EVENT_BONK,       // walked into a wall or a gate
    EVENT_FALL,       // stepped into the void
    EVENT_WIN,        // reached the exit
    EVENT_DOOR_OPEN,  // wrote the right door code next to a gate
    EVENT_WRONG_CODE  // wrote the wrong one
} WorldEvent;

typedef enum {
    MOOD_IDLE = 0,    // blue eye
    MOOD_RUNNING,     // green eye: code is running
    MOOD_ERROR,       // red eye: compile error / crash
    MOOD_HAPPY        // escaped!
} BittyMood;

// ---- What a repair test shows while it runs ----
#define TEST_MAX_DATA_ROWS  3
#define TEST_ROW_MAX        24
#define TEST_MAX_REGISTERS  6
#define TEST_MAX_MARKERS    4
#define TEST_MAX_WIRES      4

// An array, string or &value argument, floating as a row of memory blocks
typedef struct {
    char  name[16];                     // "#2" (argument number, matching the test)
    char  label[16];                    // the parameter that holds it, e.g. "arr" (empty if unknown)
    int   count;
    int   values[TEST_ROW_MAX];         // what's really in memory now
    int   shown[TEST_ROW_MAX];          // what the room shows (catches up when Bitty puts the value there)
    bool  isChar;
    int address;
    int previous[TEST_ROW_MAX];
    bool populated;
    float readFlash[TEST_ROW_MAX];      // cyan: the code just read this block
    float writeFlash[TEST_ROW_MAX];     // orange: the code just wrote this block
    int   markerCount;                  // pointers aimed at this row
    int   markerIndex[TEST_MAX_MARKERS];
    char  markerName[TEST_MAX_MARKERS][16];
} TestDataRow;

// A plain variable (int, char, bool) of the function being tested
typedef struct {
    char  name[16];
    int   value;
    bool  isChar, isBool, known;
    float flash;                        // its value just changed
    int   shown;                        // what the room shows (catches up when Bitty puts the value there)
    bool  shownKnown;
    int   digits;                       // widest it has been (so its box doesn't keep resizing)
    int   address;                      // where it lives in memory
    bool  inScope;                      // false once its { } block has ended (the box stays, greyed out)
    bool  waiting;                      // not in scope because it belongs to a function that called this one
} TestRegister;

// A pointer (or reference) of the function being tested. It is drawn as a
// cable hanging from a socket with the pointer's name, plugged into the
// block of memory it points at. When the pointer changes, the plug moves.
typedef struct {
    char  name[16];
    int   row, index;                   // the block it points at (index == count: one past the end)
    float shownIndex;                   // eased, so the plug slides along the row
    float replug;                       // it just moved: the plug flashes
} TestWire;

// Where a value can be: in one of the function's variables, or in a block of test data
typedef enum { SLOT_NONE = 0, SLOT_REGISTER, SLOT_DATA } TestSlotKind;
typedef struct {
    TestSlotKind kind;
    int row, index;                     // SLOT_DATA: row and block. SLOT_REGISTER: index = which register.
} TestSlot;

// One memory access made by the line that just ran, in order (see World_RoomStep)
typedef struct {
    TestSlot slot;
    bool     write;
    int      value;                     // writes: the value written
    bool     condition;                 // not an access: an if / while / for question was answered...
    int      answer;                    // ...with this (0 = NO)
} TestAccess;

typedef enum { TEST_RUNNING = 0, TEST_PASSED, TEST_FAILED } TestState;

typedef struct {
    bool  active;
    int   gateX, gateY;                 // the gate being tested
    char  title[32];                    // "GATE 2 / 5"
    char  call[128];                    // "isLeapYear(1700)"
    char  expect[160];                  // "should return false"
    char  result[200];                  // "returned true"
    TestState state;
    bool  fastForward;
    TestDataRow  rows[TEST_MAX_DATA_ROWS];
    int          rowCount;
    TestRegister regs[TEST_MAX_REGISTERS];
    int          regCount;
    VmVisualTrace trace;
    TestWire     wires[TEST_MAX_WIRES];
    int          wireCount;
    bool         quick;                 // later tests: Bitty works faster
    bool         waitingForStep;        // step mode: waiting for the player to press STEP
    bool         askGuesses;            // step mode: before Bitty acts out a line, ask the player to guess
} TestDisplay;

// "Guess first": in step mode the player predicts what a line will do, then watches
typedef struct {
    bool  active;
    char  question[100];        // "What will max be after this line?"
    char  detail[100];          // "max = arr[i];"   or   "9 > 3 ?"
    char  options[3][28];
    int   optionCount;
    int   correct;
    int   chosen;               // -1 until the player picks one
    float feedback;             // how long the "right / not quite" stays up
} RoomQuiz;

// ---- The memory room ----
// During a repair test Bitty is beamed INTO the function's memory: the arrays
// and strings become rows of crates he walks along, the variables become
// boxes he carries values to, and every line of code is something he does.
#define ROOM_MAX_JOBS 32

typedef enum {
    JOB_SCAN = 0,     // walk to it and look (the code read it)
    JOB_PICK,         // walk to it and take a copy of its value
    JOB_PUT,          // walk to it and put the value you're carrying in it (the code wrote it)
    JOB_ZAP,          // change it from where you stand (i++, n = n / 10, x = 0)
    JOB_GLANCE,       // a quick look from where you stand (checking a variable in a condition)
    JOB_ASK           // an if / while / for question: show it with the values, and the YES / NO answer
} RoomJobKind;

typedef struct {
    RoomJobKind kind;
    TestSlot    slot;
    int         value;
} RoomJob;

typedef enum { ROOM_OUTSIDE = 0, ROOM_BEAM_IN, ROOM_INSIDE, ROOM_BEAM_OUT } RoomMode;

typedef struct {
    RoomMode mode;
    float    beam;              // how far through beaming in or out (0..1)
    float    x;                 // where Bitty stands along the walkway
    float    walked;            // distance walked so far (drives his hops)
    float    facing;            // degrees: 0 = towards the camera, 180 = towards the crates
    float    facingShown;       // eased, so he turns smoothly
    bool     placed;            // has been put at the start of the walkway
    bool     walking;
    RoomJob  jobs[ROOM_MAX_JOBS];
    int      jobCount, jobAt;
    float    jobTime;           // seconds into the current job
    bool     holding;           // carrying a value above his head
    int      held;
    bool     heldChar;
    float    heldPop;           // just picked it up: it pops
    TestSlot raySlot;           // the beam from his eye
    float    ray;               // seconds left on it
    bool     rayZap;            // yellow (changing it) instead of cyan (looking at it)
    float    glance[TEST_MAX_REGISTERS];
    float    digitAnim[TEST_MAX_REGISTERS];   // a digit being dropped off or added
    int      digitKind[TEST_MAX_REGISTERS];   // 1 = last digit dropped off, 2 = digit added on the end
    int      digitOld[TEST_MAX_REGISTERS];    // the value before
    float    takeAnim[TEST_MAX_REGISTERS];    // last digit lifted out (digit = n % 10)
    float    dropIn;            // new test data dropping into place
    float    crash;             // a memory error: Bitty walks off the end of the data and falls
    float    crashX;
    bool     crashFall;
    char     result[48];        // what the function gave back, held up at the end
    float    resultTime;
    char     caption[96];       // what Bitty is saying (what he's doing, in words)
    float    captionTime;
    char     code[100];         // the line of code being acted out
    int      codeLine;
    char     question[96];      // the if / while / for question:  arr[i] > max
    char     questionValues[96];//   ...with the values put in:     9 > 3
    int      answer;            //   ...and its answer (0 = NO)
    int      questionKind;      // 0 = if, 1 = a loop (while / for)
    float    askTime;           // how long the question card has been up (< 0 = no card)
    TestSlot arrowFrom;         // carrying a value: where it came from (an arrow shows the trip)
    bool     arrowOn;
    RoomQuiz quiz;              // step mode: the guess the player is making before Bitty acts
    int      guessesRight, guesses;
} RoomState;

// ---- Signs (escape levels): a board on a wall tile with a short message ----
#define WORLD_MAX_SIGNS 6
typedef struct {
    int  col, row;
    char text[64];              // '|' starts a new line
} WorldSign;

typedef struct {
    int   cols, rows;
    Tile  tiles[WORLD_MAX_ROWS][WORLD_MAX_COLS];

    int   bittyX, bittyY;     // logical tile position
    float drawX, drawY;       // animated position, in tiles (e.g. 2.5 = half way between 2 and 3)
    float fromX, fromY;       // where the current move started

    Action current;           // action being animated (ACT_NONE when idle)
    float  actionTimer;       // seconds left on the current animation
    float  actionLength;      // total seconds for the current animation
    float  speed;             // 1.0 = normal, higher = faster
    int    lastResult;        // result of the last action (move: 1 = moved, 0 = bumped)

    float  shake;             // bump / error wobble, counts down
    float  flash;             // write flash on Bitty's tile, counts down
    float  glitch;            // segfault: Bitty falling into the void, counts down
    float  facing;            // which way Bitty is turned (degrees, 0 = towards the camera)
    float  gateFlash;         // a gate just opened: burst of light, counts down
    int    gateFlashX, gateFlashY;
    BittyMood mood;

    WorldState state;

    // ---- animation only (none of this changes what the code does) ----
    float  facingShown;       // facing, eased so Bitty turns smoothly instead of snapping
    float  land;              // just landed: squash-and-spring countdown
    float  landLength;
    float  spawn;             // dropping in at the start of a level, counts down
    float  winTime;           // seconds since reaching the exit
    float  idleTime;          // seconds since Bitty last did something (drives the idle fidgets)
    bool   impactDone;        // the bonk sparks for this bump have been thrown
    float  tilePress[WORLD_MAX_ROWS][WORLD_MAX_COLS];   // a tile dips when Bitty lands on it
    float  tileTrail[WORLD_MAX_ROWS][WORLD_MAX_COLS];   // tiles Bitty has stepped on keep a soft glow

    // ---- talking and scenery ----
    bool   hasIntroStart;     // the map has a 'b': where Bitty arrives before walking to his start
    int    introX, introY;
    bool   talking;           // Bitty is giving a talk: the camera comes closer
    bool   speaking;          // letters are appearing right now: his mouth moves
    float  closeUp;           // 0 = normal camera, 1 = close on Bitty (eases towards 'talking')
    int    theme;             // which area of memory this is (the level's world number): sets the colours
    int    doorCode;          // escape levels: write() this next to a gate to open it (-1 = no door)
    WorldEvent event;         // the latest thing that happened (cleared by World_TakeEvent)

    TestDisplay test;         // repair levels: what the current test is doing
    float  focus;             // 0 = normal camera, 1 = zoomed in on a test
    RoomState room;           // repair levels: the memory room Bitty is beamed into
    float  teleport;          // 0 = solid, 1 = stretched into a beam of light
    float  bittyScale;        // drawing size (0 = normal); smaller in the memory room

    WorldSign signs[WORLD_MAX_SIGNS];
    int    signCount;
    float  fogRadius;         // escape levels: only tiles this close to Bitty can be seen (0 = no fog)
    float  scanShow;          // scan()/read(): how long the result label stays up
    int    scanX, scanY;      // the tile that was scanned
    int    scanResult;
    bool   scanWasRead;

    // The screen area to draw the 3D scene in (set by the game every frame before World_Draw)
    float viewX, viewY, viewW, viewH;
} World;

// ---- Setup / per-frame (game side) ----
void World_Init(World *w, int cols, int rows);
void World_LoadMap(World *w, const char *const *rows, int rowCount);   // see levels.c for the map letters
void World_Update(World *w, float dt);   // advances animations
void World_Draw(const World *w);
void World_PlayIntro(World *w);          // Bitty drops into the level (call after loading a new level)
WorldEvent World_TakeEvent(World *w);    // what just happened to Bitty (EVENT_NONE if nothing new)
bool World_BittyOnScreen(float *x, float *y);   // where Bitty's head was drawn last frame, in window pixels
// Draw the existing customised character inside an active 3D camera.
void World_DrawBittyPreview(const World *w, float time);

// ---- The memory room (repair levels, see room.c) ----
void World_EnterRoom(World *w);           // beam Bitty in (call when a gate's first test starts)
void World_LeaveRoom(World *w);           // beam him back out to the corridor
bool World_InRoom(const World *w);        // inside, or on the way in
bool World_RoomBusy(const World *w);      // still beaming, or still acting out the last line
void World_RoomNewTest(World *w);         // new test data: the crates drop into place
void World_RoomStep(World *w, const TestAccess *list, int count, bool animate,
                    int line, const char *code);   // act out one line (its number and text, for the banner)
void World_RoomCrash(World *w, bool memoryError);   // the code crashed
void World_RoomResult(World *w, const char *text);  // hold up what the function returned
void World_RoomGuess(World *w, int option);         // the player picked an answer to the quiz
void Room_Update(World *w, float dt);
void Room_Draw(const World *w, int vw, int vh);     // called by World_Draw while inside

// ---- Gates (repair levels: each passed test opens one) ----
void World_OpenGate(World *w, int x, int y);
bool World_NextGate(const World *w, int *x, int *y);   // first closed gate, false if none
int  World_CountGates(const World *w);

// ---- Timed actions (called for the VM) ----
bool World_StartAction(World *w, Action a);   // false if already busy
bool World_ActionDone(const World *w);

// ---- Instant queries (called for the VM) ----
int  World_Read(const World *w);
int  World_Scan(const World *w, Direction d);
int  World_GetX(const World *w);
int  World_GetY(const World *w);

// ---- Console (printf output) ----
void Console_Print(const char *text);

#endif
