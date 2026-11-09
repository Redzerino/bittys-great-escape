// game_api.h - the contract between the interpreter (VM) and the game world.
//
// PLAYER-FACING FUNCTIONS (what players type in the editor):
//
//   move(NORTH);        // move one tile: NORTH, EAST, SOUTH, WEST   (takes time)
//   int v = read();     // value stored in the tile Bitty is standing on (instant)
//   write(42);          // store a value in the current tile          (takes time)
//   int t = scan(EAST); // type of the neighbouring tile (TILE_...)   (instant)
//   int x = get_x();    // Bitty's column                             (instant)
//   int y = get_y();    // Bitty's row                                (instant)
//   printf("%d", v);    // print to the console panel                 (instant)
//
// HOW THE GAME USES THIS:
//   - Instant functions: answered straight away.
//   - Timed functions (move, write): World_StartAction() starts the animation,
//     the VM waits, and once World_ActionDone() is true the game calls
//     Vm_ActionDone(vm, world.lastResult). That is what makes the code
//     visibly run step by step.

#ifndef GAME_API_H
#define GAME_API_H

#include <stdbool.h>

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
    ACT_WRITE
} ActionType;

typedef struct {
    ActionType type;
    Direction  dir;     // used by ACT_MOVE
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
    EVENT_WIN         // reached the exit
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

// An array, string or &value argument, floating as a row of memory blocks
typedef struct {
    char  name[16];                     // "#2" (argument number, matching the test)
    int   count;
    int   values[TEST_ROW_MAX];
    bool  isChar;
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
} TestRegister;

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
} TestDisplay;

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
    WorldEvent event;         // the latest thing that happened (cleared by World_TakeEvent)

    TestDisplay test;         // repair levels: what the current test is doing
    float  focus;             // 0 = normal camera, 1 = zoomed in on a test

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
