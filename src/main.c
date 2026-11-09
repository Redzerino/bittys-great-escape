// main.c - Bitty's Great Escape: the window, the panels and the game loop.
//
//   CODE (editor)  |  MEMORY (Bitty's world)  |  INSPECTOR (live variables)
//                  |  CONSOLE (output)        |
//
// Press RUN (F5) and the player's code is compiled by the VM.
//   Escape levels: the code runs a few hundred instructions per frame. When it
//                  calls move() the VM waits while Bitty slides to the next tile.
//   Repair levels: the player writes a function. Each test is a gate on the
//                  path: pass it and the gate opens, fail it and Bitty is stuck.

#include "raylib.h"

#include "startup.h"
#include "talk.h"
#include "customisation.h"
#include "console.h"
#include "editor.h"
#include "game_api.h"
#include "inspector.h"
#include "levels.h"
#include "repair.h"
#include "save.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_W 1280      // starting window size (the game then goes fullscreen)
#define SCREEN_H 720
#define MAX_VARS 32

// Panel positions, worked out from the window size every frame (see UpdateLayout)
typedef struct {
    Rectangle editor, world, console, inspect;
} Layout;

static Layout layout;
#define EDITOR_PANEL  layout.editor
#define WORLD_PANEL   layout.world
#define CONSOLE_PANEL layout.console
#define INSPECT_PANEL layout.inspect

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// Code on the left, the world + console in the middle, the Inspector on the right
static void UpdateLayout(void)
{
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float m = 10, top = 50;
    float editorW  = Clamp(W * 0.31f, 360, 640);
    float inspectW = Clamp(W * 0.25f, 300, 520);
    float midX = m + editorW + m;
    float midW = W - midX - inspectW - 2 * m;
    float bodyH = H - top - m;
    float consoleH = Clamp(bodyH * 0.29f, 170, 320);

    layout.editor  = (Rectangle){ m, top, editorW, bodyH };
    layout.world   = (Rectangle){ midX, top, midW, bodyH - consoleH - m };
    layout.console = (Rectangle){ midX, top + bodyH - consoleH, midW, consoleH };
    layout.inspect = (Rectangle){ W - m - inspectW, top, inspectW, bodyH };
}

typedef enum {
    MODE_EDITING = 0,   // player is writing code
    MODE_RUNNING,       // VM running (or paused in step mode)
    MODE_DONE           // finished, crashed or escaped: showing the result
} GameMode;

typedef enum {
    REPAIR_WALKING = 0,   // Bitty walks to the next gate (or the exit)
    REPAIR_RUNNING,       // a test is running, line by line, so the player can watch
    REPAIR_RESULT         // showing PASS / FAIL on the gate terminal for a moment
} RepairPhase;

typedef struct {
    World      world;
    Editor     editor;
    Vm        *vm;
    GameMode   mode;
    bool       stepping;
    int        speedIndex;
    int        level;
    bool       showLevels;      // the level menu
    bool       showHelp;        // the F1 shortcuts card
    float      saveTimer;       // autosave countdown
    bool       unsaved;

    // repair levels
    RepairPhase repairPhase;
    int        repairTest;      // next test to run
    int        repairGates;     // gates in the map at the start
    float      repairTimer;     // small pauses so the player can follow along
    bool       hintShown;
    RepairRun  repairRun;       // where the current test's data lives in memory
    TestOutcome repairOutcome;
    float      lineTimer;       // time left showing the current line
    int        linesShown;      // lines shown at normal pace (then fast-forward)

    // the lesson card ("memory dump") over the world
    bool       showLesson;
    bool       introDone;       // Bitty has arrived in this level (so he doesn't drop in twice)
    float      barkCooldown;    // stops Bitty repeating himself
    bool       showWorkshop;
    VmVariable vars[MAX_VARS];
    int        varCount;
    Font       codeFont;
    bool       ownsFont;
} Game;

static const float SPEEDS[] = { 1.0f, 2.0f, 4.0f, 8.0f };
static const char *SPEED_NAMES[] = { "1x", "2x", "4x", "8x" };

// ===========================================================================
// VM <-> world
// ===========================================================================
static bool HostCallBuiltin(void *user, VmBuiltin id, const int *args, int argc, int *result)
{
    Game *g = user;
    (void)argc;
    *result = 0;

    switch (id)
    {
        case VM_BUILTIN_MOVE:
        {
            Action a = { ACT_MOVE, (Direction)args[0], 0, Vm_CurrentLine(g->vm) };
            return World_StartAction(&g->world, a);
        }
        case VM_BUILTIN_WRITE:
        {
            Action a = { ACT_WRITE, NORTH, args[0], Vm_CurrentLine(g->vm) };
            return World_StartAction(&g->world, a);
        }
        case VM_BUILTIN_READ:  *result = World_Read(&g->world);                      return false;
        case VM_BUILTIN_SCAN:  *result = World_Scan(&g->world, (Direction)args[0]);  return false;
        case VM_BUILTIN_GET_X: *result = World_GetX(&g->world);                      return false;
        case VM_BUILTIN_GET_Y: *result = World_GetY(&g->world);                      return false;
        default:               return false;
    }
}

static void HostPrint(void *user, const char *text)
{
    (void)user;
    Console_Print(text);
}

static void HostWarning(void *user, int line, const char *message)
{
    (void)user;
    Console_PrintKind(CONSOLE_WARNING, TextFormat("Line %d: %s\n", line, message));
}

// ===========================================================================
// Levels
// ===========================================================================
static void LoadWorld(Game *g)
{
    const Level *lv = Levels_Get(g->level);
    const char *rows[LEVEL_MAX_ROWS];
    for (int i = 0; i < lv->mapRows; i++) rows[i] = lv->map[i];
    g->world.theme = (lv->title[0] >= '0' && lv->title[0] <= '9') ? lv->title[0] - '0' : 1;   // "3-1 ..." = area 3
    World_LoadMap(&g->world, rows, lv->mapRows);
    g->world.speed = SPEEDS[g->speedIndex];
}

static void StopProgram(Game *g)
{
    Vm_Destroy(g->vm);
    g->vm = NULL;
    g->varCount = 0;
    g->stepping = false;
    g->editor.readOnly = false;
    g->editor.runLine = 0;
}

// Keep the code typed for the current level (in memory; Save_Write puts it on disk)
static void RememberCode(Game *g)
{
    Save_SetCode(Levels_Get(g->level)->file, Editor_GetText(&g->editor));
}

static void SaveNow(Game *g)
{
    RememberCode(g);
    Save_SetCurrent(Levels_Get(g->level)->file);
    Save_Write();
    g->unsaved = false;
}

static void LoadLevel(Game *g, int index, bool keepOldCode)
{
    if (keepOldCode) RememberCode(g);

    StopProgram(g);
    g->level = index;
    g->mode = MODE_EDITING;
    g->showLevels = false;
    LoadWorld(g);

    const Level *lv = Levels_Get(index);
    const char *saved = Save_GetCode(lv->file);
    Editor_SetText(&g->editor, saved ? saved : lv->starterCode);
    World_PlayIntro(&g->world);     // Bitty drops into the new level
    g->editor.focused = true;
    Save_SetCurrent(lv->file);
    Save_Write();

    Console_Clear();
    Console_PrintKind(CONSOLE_SUCCESS, TextFormat("LEVEL %s\n", lv->title));
    Console_PrintKind(CONSOLE_STORY, TextFormat("%s\n", lv->story));
    if (lv->type == LEVEL_REPAIR)
        Console_PrintKind(CONSOLE_STORY, TextFormat("Each gate tests your code (%d tests). Press RUN to try.\n", lv->testCount));

    // Bitty introduces a level the first time you see it; after that the lesson card is enough
    g->introDone = false;
    g->world.talking = false;
    if (lv->talk[0] && !Save_IsDone(lv->file))
    {
        Talk_Start(lv->talk);
        g->showLesson = false;          // the card opens when he has finished talking
        g->introDone = true;
    }
    else
    {
        Talk_Skip();
        g->showLesson = lv->lesson[0] != '\0';
    }
    g->hintShown = false;
}

// A short reaction from Bitty, picked from a few so he doesn't sound like a recording
static void Bark(Game *g, const char *a, const char *b, const char *c)
{
    const char *lines[3] = { a, b, c };
    int n = c ? 3 : b ? 2 : 1;
    Talk_Bark(lines[GetRandomValue(0, n - 1)]);
    g->barkCooldown = 2.0f;
}

// ===========================================================================
// Running the player's code
// ===========================================================================
static void StartProgram(Game *g, bool step)
{
    Talk_Skip();
    g->world.talking = false;
    g->introDone = true;
    StopProgram(g);
    LoadWorld(g);
    g->editor.errorLine = 0;

    Console_Clear();

    const Level *lv = Levels_Get(g->level);
    g->showLesson = false;

    VmHost host = { g, HostCallBuiltin, HostPrint, HostWarning };
    VmOptions options = { lv->banIndexing, 0 };       // escape levels: infinite loops are allowed
    if (lv->type == LEVEL_REPAIR) options.maxInstructions = lv->maxSteps;   // repair: a test must finish
    VmBuildResult result;
    g->vm = Vm_Build(Editor_GetText(&g->editor), &options, &host, &result);

    for (int i = 0; i < result.warningCount; i++)
        Console_PrintKind(CONSOLE_WARNING, TextFormat("%s\n", result.warnings[i]));

    if (!g->vm)
    {
        Console_PrintKind(CONSOLE_ERROR, TextFormat("%s\n", result.message));
        Bark(g, "Bzzt. I can't read that. Check the red line?", "Hmm, that line confuses me. Look at the red one.", NULL);
        g->editor.errorLine = result.line;
        g->world.mood = MOOD_ERROR;
        g->world.shake = 0.4f;
        g->mode = MODE_EDITING;
        return;
    }

    if (lv->type == LEVEL_REPAIR)
    {
        // Tests run one at a time in UpdateRepair
        g->repairPhase = REPAIR_WALKING;
        g->repairTest  = 0;
        g->repairGates = World_CountGates(&g->world);
        g->repairTimer = 0.4f;
        g->stepping    = false;
        g->mode = MODE_RUNNING;
        g->editor.readOnly = true;
        g->world.mood = MOOD_RUNNING;
        Console_PrintKind(CONSOLE_STORY, TextFormat("Testing your code against %d gates...\n", lv->testCount));
        return;
    }

    Vm_Start(g->vm, NULL, NULL, 0);
    if (step) Vm_PauseAtNextLine(g->vm);

    g->stepping = step;
    g->mode = MODE_RUNNING;
    g->editor.readOnly = true;
    g->world.mood = MOOD_RUNNING;
}

static void FinishRun(Game *g)
{
    g->mode = MODE_DONE;
    g->editor.readOnly = false;
    g->stepping = false;
}

static void ShowHint(Game *g)
{
    const Level *lv = Levels_Get(g->level);
    if (!g->hintShown && lv->hint[0])
    {
        Console_PrintKind(CONSOLE_STORY, TextFormat("Hint: %s\n", lv->hint));
        g->hintShown = true;
    }
}

// ---------------------------------------------------------------------------
// Repair levels: Bitty walks to a gate, the gate's test runs line by line
// while the player watches the variables and memory change, then the gate
// opens (pass) or Bitty is stuck (fail).
// ---------------------------------------------------------------------------
#define PACE_FIRST_TEST   0.38f   // seconds per line for the first test
#define PACE_LATER_TESTS  0.16f   // later tests run quicker
#define LINES_BEFORE_FF   32      // after this many lines, fast-forward

// Copy the function's plain variables and the test's arrays/strings into the
// world's TestDisplay, so they float above the corridor
static void UpdateTestDisplay(Game *g, const LevelTest *t)
{
    TestDisplay *td = &g->world.test;

    // ---- registers: plain variables of the function ----
    int n = 0;
    for (int i = 0; i < g->varCount && n < TEST_MAX_REGISTERS; i++)
    {
        const VmVariable *v = &g->vars[i];
        if (v->isGlobal || v->isArray || v->isPointer || v->isRef) continue;

        TestRegister *r = &td->regs[n];
        bool same = strcmp(r->name, v->name) == 0;
        if (same && r->known && (r->value != v->values[0] || !v->initialized[0])) r->flash = 0.6f;
        if (!same) { memset(r, 0, sizeof *r); snprintf(r->name, sizeof r->name, "%.15s", v->name); r->flash = 0.6f; }
        r->value  = v->values[0];
        r->known  = v->initialized[0];
        r->isChar = v->isChar;
        r->isBool = strcmp(v->type, "bool") == 0;
        n++;
    }
    td->regCount = n;

    // ---- data rows: the test's arrays, strings and &values ----
    int reads[VM_MAX_ACCESSES], writes[VM_MAX_ACCESSES], rc, wc;
    Vm_TakeAccesses(g->vm, reads, &rc, writes, &wc);

    int rows = 0;
    for (int k = 0; k < t->argc && rows < TEST_MAX_DATA_ROWS; k++)
    {
        const LevelValue *arg = &t->args[k];
        if (arg->kind == VALUE_INT) continue;

        TestDataRow *d = &td->rows[rows++];
        int base = g->repairRun.argAddr[k];
        int count = arg->kind == VALUE_ARRAY ? (arg->count > 0 ? arg->count : 1) :
                    arg->kind == VALUE_STRING ? (int)strlen(arg->text) + 1 : 1;
        if (count > TEST_ROW_MAX) count = TEST_ROW_MAX;

        snprintf(d->name, sizeof d->name, "#%d", k + 1);
        d->count  = count;
        d->isChar = arg->kind == VALUE_STRING;
        for (int i = 0; i < count; i++) d->values[i] = Vm_ReadMemory(g->vm, base + i);

        for (int i = 0; i < rc; i++) if (reads[i]  >= base && reads[i]  < base + count) d->readFlash[reads[i] - base]   = 0.5f;
        for (int i = 0; i < wc; i++) if (writes[i] >= base && writes[i] < base + count) d->writeFlash[writes[i] - base] = 0.5f;

        // pointers (and array parameters) aimed into this row
        d->markerCount = 0;
        for (int i = 0; i < g->varCount && d->markerCount < TEST_MAX_MARKERS; i++)
        {
            const VmVariable *v = &g->vars[i];
            if (!(v->isPointer || v->isRef) || v->isGlobal) continue;
            int at = v->pointsTo - base;
            if (at < 0 || at > count) continue;
            d->markerIndex[d->markerCount] = at;
            snprintf(d->markerName[d->markerCount], sizeof d->markerName[0], "%.15s", v->name);
            d->markerCount++;
        }
    }
    td->rowCount = rows;
}

static void StartNextTest(Game *g)
{
    const Level *lv = Levels_Get(g->level);
    const LevelTest *t = &lv->tests[g->repairTest];
    TestDisplay *td = &g->world.test;

    memset(td, 0, sizeof *td);
    td->active = true;
    int gx = g->world.bittyX + 1, gy = g->world.bittyY;
    World_NextGate(&g->world, &gx, &gy);
    td->gateX = gx;
    td->gateY = gy;
    snprintf(td->title, sizeof td->title, "GATE TEST %d / %d", g->repairTest + 1, lv->testCount);
    Repair_Describe(t, td->call, sizeof td->call, td->expect, sizeof td->expect);
    td->state = TEST_RUNNING;
    g->world.focus = 1.0f;

    g->linesShown = 0;
    g->lineTimer = 0;
    g->repairPhase = REPAIR_RUNNING;

    if (!Repair_BeginTest(g->vm, t, &g->repairRun, &g->repairOutcome))
    {
        g->repairPhase = REPAIR_RESULT;   // couldn't even start: show why
        td->state = TEST_FAILED;
        snprintf(td->result, sizeof td->result, "%s", g->repairOutcome.summary);
        g->repairTimer = 0.2f;
        return;
    }
    Vm_PauseAtNextLine(g->vm);   // stop on the first line so the player sees it start
}

static void RunTestFrame(Game *g, float dt)
{
    const Level *lv = Levels_Get(g->level);
    const LevelTest *t = &lv->tests[g->repairTest];
    TestDisplay *td = &g->world.test;
    float speed = SPEEDS[g->speedIndex];
    float pace = (g->repairTest == 0 ? PACE_FIRST_TEST : PACE_LATER_TESTS) / speed;

    VmStatus s = Vm_GetStatus(g->vm);
    if (s == VM_WAITING) { Vm_ActionDone(g->vm, 0); s = Vm_GetStatus(g->vm); }   // move() does nothing here

    bool ff = g->linesShown >= LINES_BEFORE_FF;
    td->fastForward = ff;

    if (s == VM_PAUSED || (s == VM_RUNNING && !ff))
    {
        g->lineTimer -= dt;
        if (g->lineTimer <= 0)
        {
            g->linesShown++;
            if (g->linesShown < LINES_BEFORE_FF) Vm_PauseAtNextLine(g->vm);
            g->lineTimer = pace;
            s = Vm_Run(g->vm, 200000);
        }
    }
    else if (s == VM_RUNNING)
    {
        s = Vm_Run(g->vm, (int)(30000 * speed));   // fast-forward
    }

    g->editor.runLine = Vm_CurrentLine(g->vm);
    g->varCount = Vm_GetVariables(g->vm, g->vars, MAX_VARS);
    if (s != VM_FINISHED) UpdateTestDisplay(g, t);   // after it finishes the frame is gone; keep the last picture
    else
    {
        // refresh the data rows one last time (registers keep their final values)
        int keepRegs = td->regCount;
        TestRegister regs[TEST_MAX_REGISTERS];
        memcpy(regs, td->regs, sizeof regs);
        UpdateTestDisplay(g, t);
        memcpy(td->regs, regs, sizeof regs);
        td->regCount = keepRegs;
    }

    if (s == VM_FINISHED || s == VM_ERROR)
    {
        Repair_FinishTest(g->vm, t, &g->repairRun, &g->repairOutcome);
        td->state = g->repairOutcome.passed ? TEST_PASSED : TEST_FAILED;
        snprintf(td->result, sizeof td->result, "%s", g->repairOutcome.summary);
        g->repairPhase = REPAIR_RESULT;
        g->repairTimer = (g->repairTest == 0 ? 1.2f : 0.7f) / speed;
    }
}

static void UpdateRepair(Game *g, float dt)
{
    World *w = &g->world;
    const Level *lv = Levels_Get(g->level);

    if (w->state == WORLD_WON)
    {
        Console_PrintKind(CONSOLE_SUCCESS, TextFormat("All %d tests passed! Bitty escaped this sector. Press NEXT.\n", lv->testCount));
        Save_SetDone(lv->file);
        SaveNow(g);
        FinishRun(g);
        return;
    }

    if (g->repairPhase == REPAIR_RUNNING)
    {
        RunTestFrame(g, dt);
        return;
    }

    if (g->repairTimer > 0) { g->repairTimer -= dt; return; }
    if (!World_ActionDone(w)) return;

    if (g->repairPhase == REPAIR_RESULT)
    {
        TestOutcome *o = &g->repairOutcome;
        Console_PrintKind(o->passed ? CONSOLE_SUCCESS : CONSOLE_ERROR, TextFormat("%s\n", o->message));

        if (!o->passed)
        {
            Bark(g, "Bzzt. Not quite. Look what it gave back.", "The gate says no. Check what it wanted.", NULL);
            Action a = { ACT_MOVE, EAST, 0, 0 };   // bonk into the gate
            World_StartAction(w, a);
            w->mood = MOOD_ERROR;
            if (o->crashed)
            {
                g->editor.errorLine = Vm_ErrorLine(g->vm);
                VmErrorKind kind = Vm_GetErrorKind(g->vm);
                if (kind == VM_ERR_SEGFAULT || kind == VM_ERR_OUT_OF_BOUNDS) w->glitch = 1.2f;
            }
            ShowHint(g);
            FinishRun(g);   // the gate terminal stays up showing what went wrong
            return;
        }

        // Passed. Gates are shared out over the tests: open one when this test completes a group.
        int n = lv->testCount;
        int done = ++g->repairTest;
        int gates = g->repairGates;
        if (gates > 0 && (done * gates) / n > ((done - 1) * gates) / n)
        {
            int gx, gy;
            if (World_NextGate(w, &gx, &gy)) World_OpenGate(w, gx, gy);
            w->test.active = false;
            w->focus = 0;
            g->repairPhase = REPAIR_WALKING;
            g->repairTimer = 0.45f;
        }
        else
        {
            StartNextTest(g);   // same gate, next test
        }
        return;
    }

    // ---- REPAIR_WALKING: east until the next closed gate, then test it ----
    int nx = w->bittyX + 1;
    bool gateAhead = nx < w->cols && w->tiles[w->bittyY][nx].type == TILE_GATE;
    if (gateAhead && g->repairTest < lv->testCount)
    {
        StartNextTest(g);
        return;
    }
    if (nx >= w->cols) return;
    Action a = { ACT_MOVE, EAST, 0, 0 };
    World_StartAction(w, a);
}

static void UpdateProgram(Game *g, float dt)
{
    World *w = &g->world;
    World_Update(w, dt);

    if (g->mode != MODE_RUNNING || !g->vm) return;

    if (Levels_Get(g->level)->type == LEVEL_REPAIR)
    {
        UpdateRepair(g, dt);
        return;
    }

    // ---- Did that last move win or crash? ----
    if (w->state == WORLD_WON)
    {
        Console_PrintKind(CONSOLE_SUCCESS, "Bitty escaped this sector! Press NEXT for the next level.\n");
        Save_SetDone(Levels_Get(g->level)->file);
        SaveNow(g);
        FinishRun(g);
        return;
    }
    if (w->state == WORLD_SEGFAULT)
    {
        Console_PrintKind(CONSOLE_ERROR, "SEGMENTATION FAULT! Bitty stepped into unallocated memory and vanished.\n");
        Console_PrintKind(CONSOLE_STORY, "Use scan() to check a tile before you move into it.\n");
        g->editor.errorLine = Vm_CurrentLine(g->vm);
        FinishRun(g);
        return;
    }

    VmStatus s = Vm_GetStatus(g->vm);

    // Animation finished: give move()/write() its result and carry on
    if (s == VM_WAITING && World_ActionDone(w))
    {
        Vm_ActionDone(g->vm, w->lastResult);
        if (g->stepping) Vm_PauseAtNextLine(g->vm);
        s = Vm_GetStatus(g->vm);
    }

    if (s == VM_RUNNING)
    {
        // Plain C code runs fast. move()/write() stop the VM until Bitty finishes moving.
        s = Vm_Run(g->vm, (int)(2000 * SPEEDS[g->speedIndex]));
    }

    g->editor.runLine = Vm_CurrentLine(g->vm);
    g->varCount = Vm_GetVariables(g->vm, g->vars, MAX_VARS);

    if (s == VM_FINISHED)
    {
        if (w->state != WORLD_WON)
            Console_PrintKind(CONSOLE_WARNING, "The program finished, but Bitty hasn't reached the EXIT yet.\n");
        w->mood = MOOD_IDLE;
        FinishRun(g);
    }
    else if (s == VM_ERROR)
    {
        Console_PrintKind(CONSOLE_ERROR, TextFormat("%s\n", Vm_ErrorMessage(g->vm)));
        Bark(g, "Ouch! Something broke in memory.", "Zzzt! That line crashed me.", NULL);
        g->editor.errorLine = Vm_ErrorLine(g->vm);
        w->mood = MOOD_ERROR;
        VmErrorKind kind = Vm_GetErrorKind(g->vm);
        if (kind == VM_ERR_SEGFAULT || kind == VM_ERR_OUT_OF_BOUNDS) w->glitch = 1.2f;
        else w->shake = 0.4f;
        FinishRun(g);
    }
}

// ===========================================================================
// UI
// ===========================================================================
static bool Button(Rectangle r, const char *text, Color base, bool enabled)
{
    Vector2 m = GetMousePosition();
    bool hover = enabled && CheckCollisionPointRec(m, r);
    Color fill = enabled ? base : (Color){ 50, 55, 70, 255 };
    if (hover) fill = (Color){ (unsigned char)(fill.r + (255 - fill.r) / 5), (unsigned char)(fill.g + (255 - fill.g) / 5),
                               (unsigned char)(fill.b + (255 - fill.b) / 5), 255 };

    DrawRectangleRounded(r, 0.3f, 6, fill);
    int fw = MeasureText(text, 16);
    DrawText(text, (int)(r.x + (r.width - fw) / 2), (int)(r.y + (r.height - 16) / 2), 16,
             enabled ? WHITE : (Color){ 110, 115, 130, 255 });
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void DrawWorldPanel(Game *g)
{
    Rectangle b = WORLD_PANEL;
    DrawRectangleRec(b, (Color){ 20, 24, 36, 255 });
    DrawRectangleLinesEx(b, 2, (Color){ 60, 200, 255, 255 });
    DrawText("MEMORY", (int)b.x + 10, (int)b.y + 8, 18, (Color){ 60, 200, 255, 255 });

    const char *title = Levels_Get(g->level)->title;
    int tw = MeasureText(title, 16);
    DrawText(title, (int)(b.x + b.width - tw - 12), (int)b.y + 10, 16, (Color){ 120, 255, 160, 255 });

    World *w = &g->world;
    w->viewX = b.x + 2;
    w->viewY = b.y + 32;
    w->viewW = b.width - 4;
    w->viewH = b.height - 34;

    World_Draw(w);

    // Big banner when the run ends
    if (g->mode == MODE_DONE && (w->state == WORLD_WON || w->state == WORLD_SEGFAULT))
    {
        bool won = w->state == WORLD_WON;
        const char *msg = won ? "SECTOR CLEARED" : "SEGMENTATION FAULT";
        Color col = won ? (Color){ 110, 240, 140, 255 } : (Color){ 255, 90, 110, 255 };
        int fw = MeasureText(msg, 30);
        Rectangle banner = { b.x + 20, b.y + b.height - 62, b.width - 40, 44 };
        DrawRectangleRec(banner, (Color){ 10, 12, 20, 220 });
        DrawRectangleLinesEx(banner, 2, col);
        DrawText(msg, (int)(b.x + (b.width - fw) / 2), (int)banner.y + 8, 30, col);
    }
}

// Draw text word-wrapped inside a width. Returns the height used.
static float DrawWrapped(Font font, const char *text, float x, float y, float width, float size, Color col)
{
    char line[256] = "";
    float lineH = size + 4, cy = y;
    const char *p = text;

    while (*p)
    {
        if (*p == '\n')
        {
            DrawTextEx(font, line, (Vector2){ x, cy }, size, 0, col);
            cy += lineH;
            line[0] = '\0';
            p++;
            continue;
        }

        // next word (keep leading spaces so code examples keep their indent)
        char word[128];
        int n = 0;
        while (*p == ' ' && n < 120) word[n++] = *p++;
        while (*p && *p != ' ' && *p != '\n' && n < 126) word[n++] = *p++;
        word[n] = '\0';

        char trial[384];
        snprintf(trial, sizeof trial, "%s%s", line, word);
        if (line[0] && MeasureTextEx(font, trial, size, 0).x > width)
        {
            DrawTextEx(font, line, (Vector2){ x, cy }, size, 0, col);
            cy += lineH;
            const char *w = word;
            while (*w == ' ') w++;
            snprintf(line, sizeof line, "%s", w);
        }
        else
        {
            memcpy(line, trial, sizeof line - 1);
            line[sizeof line - 1] = '\0';
        }
    }
    if (line[0]) { DrawTextEx(font, line, (Vector2){ x, cy }, size, 0, col); cy += lineH; }
    return cy - y;
}

static void DrawLessonCard(Game *g)
{
    if (!g->showLesson) return;
    const Level *lv = Levels_Get(g->level);

    Rectangle b = WORLD_PANEL;
    Rectangle card = { b.x + 24, b.y + 40, b.width - 48, b.height - 64 };
    DrawRectangleRec(b, (Color){ 5, 6, 14, 170 });
    DrawRectangleRounded(card, 0.04f, 8, (Color){ 16, 20, 36, 245 });
    DrawRectangleRoundedLinesEx(card, 0.04f, 8, 2, (Color){ 120, 200, 255, 255 });

    DrawText("MEMORY DUMP", (int)card.x + 18, (int)card.y + 14, 20, (Color){ 120, 255, 160, 255 });
    DrawTextEx(g->codeFont, lv->title, (Vector2){ card.x + 18, card.y + 40 }, 15, 0, (Color){ 140, 160, 200, 255 });

    BeginScissorMode((int)card.x + 10, (int)card.y + 64, (int)card.width - 20, (int)card.height - 116);
    DrawWrapped(g->codeFont, lv->lesson, card.x + 18, card.y + 68, card.width - 36, 16, (Color){ 225, 232, 245, 255 });
    EndScissorMode();

    if (Button((Rectangle){ card.x + card.width - 130, card.y + card.height - 46, 112, 32 }, "GOT IT",
               (Color){ 40, 150, 80, 255 }, true))
    {
        g->showLesson = false;
        g->introDone = true;
    }
    // hear Bitty explain it again
    if (lv->talk[0] && g->mode != MODE_RUNNING &&
        Button((Rectangle){ card.x + 18, card.y + card.height - 46, 150, 32 }, "ASK BITTY", (Color){ 60, 120, 140, 255 }, true))
    {
        g->showLesson = false;
        g->introDone = true;
        Talk_Start(lv->talk);
    }
}

// The level menu: every level, with a tick for the ones already cleared
static void DrawLevelMenu(Game *g)
{
    if (!g->showLevels) return;
    Rectangle b = WORLD_PANEL;
    Rectangle card = { b.x + 16, b.y + 36, b.width - 32, b.height - 52 };
    DrawRectangleRec(b, (Color){ 5, 6, 14, 190 });
    DrawRectangleRounded(card, 0.03f, 8, (Color){ 16, 20, 36, 248 });
    DrawRectangleRoundedLinesEx(card, 0.03f, 8, 2, (Color){ 90, 200, 220, 255 });
    DrawText("LEVELS", (int)card.x + 16, (int)card.y + 12, 20, (Color){ 120, 255, 160, 255 });

    int done = 0;
    for (int i = 0; i < Levels_Count(); i++) if (Save_IsDone(Levels_Get(i)->file)) done++;
    const char *count = TextFormat("%d / %d cleared", done, Levels_Count());
    DrawTextEx(g->codeFont, count, (Vector2){ card.x + card.width - 16 - MeasureTextEx(g->codeFont, count, 15, 0).x, card.y + 14 },
               15, 0, (Color){ 140, 160, 200, 255 });

    float rowH = 30, x0 = card.x + 16, y0 = card.y + 46;
    int perCol = (int)((card.height - 60) / rowH);
    if (perCol < 1) perCol = 1;
    int cols = (Levels_Count() + perCol - 1) / perCol;
    float colW = (card.width - 32) / (cols > 0 ? cols : 1);

    for (int i = 0; i < Levels_Count(); i++)
    {
        const Level *lv = Levels_Get(i);
        Rectangle r = { x0 + (i / perCol) * colW, y0 + (i % perCol) * rowH, colW - 8, rowH - 4 };
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        bool current = (i == g->level);
        bool cleared = Save_IsDone(lv->file);

        DrawRectangleRounded(r, 0.3f, 6, current ? (Color){ 40, 70, 110, 255 } : hover ? (Color){ 32, 40, 64, 255 } : (Color){ 22, 28, 46, 255 });
        if (cleared)
        {
            // a small tick
            DrawLineEx((Vector2){ r.x + 9, r.y + r.height / 2 }, (Vector2){ r.x + 13, r.y + r.height / 2 + 5 }, 2.5f, (Color){ 110, 240, 150, 255 });
            DrawLineEx((Vector2){ r.x + 13, r.y + r.height / 2 + 5 }, (Vector2){ r.x + 21, r.y + r.height / 2 - 5 }, 2.5f, (Color){ 110, 240, 150, 255 });
        }
        DrawTextEx(g->codeFont, lv->title, (Vector2){ r.x + 28, r.y + 5 }, 16, 0,
                   cleared ? (Color){ 170, 240, 190, 255 } : (Color){ 220, 228, 245, 255 });
        if (lv->type == LEVEL_REPAIR)
            DrawTextEx(g->codeFont, "repair", (Vector2){ r.x + r.width - 56, r.y + 7 }, 13, 0, (Color){ 255, 170, 90, 255 });

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (i != g->level) LoadLevel(g, i, true);
            g->showLevels = false;
        }
    }
}

// F1: every shortcut in one place
static void DrawHelp(Game *g)
{
    if (!g->showHelp) return;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    Rectangle card = { W / 2 - 300, H / 2 - 255, 600, 510 };
    DrawRectangle(0, 0, (int)W, (int)H, (Color){ 0, 0, 0, 150 });
    DrawRectangleRounded(card, 0.04f, 8, (Color){ 16, 20, 36, 250 });
    DrawRectangleRoundedLinesEx(card, 0.04f, 8, 2, (Color){ 120, 200, 255, 255 });
    DrawText("SHORTCUTS", (int)card.x + 24, (int)card.y + 18, 22, (Color){ 120, 255, 160, 255 });

    static const char *const KEYS[][2] = {
        { "F5",                 "Run your code" },
        { "F10",                "Step: run one line at a time" },
        { "Esc",                "Stop the program / close a card" },
        { "F11",                "Fullscreen on / off" },
        { "F1",                 "This help" },
        { "Ctrl+Z / Ctrl+Y",    "Undo / redo" },
        { "Ctrl+A",             "Select all" },
        { "Ctrl+C / X / V",     "Copy / cut / paste" },
        { "Shift + arrows",     "Select text (or drag with the mouse)" },
        { "Ctrl + / Ctrl -",    "Bigger / smaller code (or Ctrl + wheel)" },
        { "Mouse wheel",        "Scroll the code or the console" },
    };
    for (int i = 0; i < (int)(sizeof KEYS / sizeof KEYS[0]); i++)
    {
        float y = card.y + 64 + i * 31;
        DrawTextEx(g->codeFont, KEYS[i][0], (Vector2){ card.x + 28, y }, 17, 0, (Color){ 255, 214, 110, 255 });
        DrawTextEx(g->codeFont, KEYS[i][1], (Vector2){ card.x + 230, y }, 17, 0, (Color){ 225, 232, 245, 255 });
    }
    DrawTextEx(g->codeFont, "Your progress and code are saved automatically.", (Vector2){ card.x + 28, card.y + card.height - 64 },
               15, 0, (Color){ 140, 160, 200, 255 });
    if (Button((Rectangle){ card.x + card.width - 130, card.y + card.height - 46, 106, 32 }, "CLOSE", (Color){ 40, 150, 80, 255 }, true))
        g->showHelp = false;
}

// A small button in the Code panel's title bar: put the starter code back (Ctrl+Z undoes it)
static void DrawStarterButton(Game *g)
{
    if (g->mode == MODE_RUNNING) return;
    Rectangle b = EDITOR_PANEL;
    Rectangle r = { b.x + b.width - 118, b.y + 5, 110, 22 };
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRounded(r, 0.4f, 6, hover ? (Color){ 50, 60, 90, 255 } : (Color){ 30, 36, 56, 255 });
    DrawTextEx(g->codeFont, "starter code", (Vector2){ r.x + 9, r.y + 3 }, 15, 0, (Color){ 150, 170, 210, 255 });
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        Editor_ReplaceAll(&g->editor, Levels_Get(g->level)->starterCode);
        Console_PrintKind(CONSOLE_STORY, "Starter code is back. Changed your mind? Ctrl+Z undoes it.\n");
    }
}

static void DrawToolbar(Game *g)
{
    DrawText("BITTY'S GREAT ESCAPE", 10, 12, 26, (Color){ 120, 255, 160, 255 });

    if (Button((Rectangle){ (float)GetScreenWidth() - 346, 10, 96, 30 },
               g->showWorkshop ? "BACK" : "WORKSHOP",
               (Color){90, 65, 145, 255}, g->mode != MODE_RUNNING))
    {
        g->showWorkshop = !g->showWorkshop;
        if (g->showWorkshop) { g->showLesson = false; g->showLevels = false; }
    }
    if (g->showWorkshop) return;

    bool running = (g->mode == MODE_RUNNING);
    float x = WORLD_PANEL.x, y = 10, h = 30;
    float W = (float)GetScreenWidth();
    // On a narrow window the run buttons shrink a little so they don't run into the ones on the right
    float k = Clamp((W - 356 - x) / 584.0f, 0.7f, 1.0f);

    if (Button((Rectangle){ x, y, 80 * k, h }, "RUN", (Color){ 40, 150, 80, 255 }, !running) ||
        (!running && IsKeyPressed(KEY_F5)))
        StartProgram(g, false);
    x += 88 * k;

    if (Button((Rectangle){ x, y, 80 * k, h }, "STEP", (Color){ 50, 110, 170, 255 }, true) || IsKeyPressed(KEY_F10))
    {
        if (Levels_Get(g->level)->type == LEVEL_REPAIR)
        {
            if (!running) StartProgram(g, false);   // repair levels run test by test anyway
        }
        else if (running && g->vm && Vm_GetStatus(g->vm) == VM_PAUSED)
        {
            Vm_PauseAtNextLine(g->vm);
            Vm_Run(g->vm, 1);   // leave the paused line
            g->stepping = true;
        }
        else if (!running)
        {
            StartProgram(g, true);
        }
    }
    x += 88 * k;

    if (Button((Rectangle){ x, y, 80 * k, h }, "STOP", (Color){ 170, 60, 60, 255 }, running) ||
        (running && IsKeyPressed(KEY_ESCAPE)))
    {
        Console_PrintKind(CONSOLE_WARNING, "Stopped.\n");
        g->world.mood = MOOD_IDLE;
        FinishRun(g);
        g->editor.runLine = 0;
    }
    x += 88 * k;

    if (Button((Rectangle){ x, y, 80 * k, h }, "RESET", (Color){ 90, 90, 110, 255 }, !running))
    {
        StopProgram(g);
        g->mode = MODE_EDITING;
        g->editor.errorLine = 0;
        LoadWorld(g);
        Console_Clear();
        Console_PrintKind(CONSOLE_STORY, Levels_Get(g->level)->story);
    }
    x += 96 * k;

    if (Button((Rectangle){ x, y, 60 * k, h }, SPEED_NAMES[g->speedIndex], (Color){ 70, 70, 100, 255 }, true))
    {
        g->speedIndex = (g->speedIndex + 1) % 4;
        g->world.speed = SPEEDS[g->speedIndex];
    }
    x += 76 * k;

    if (Button((Rectangle){ x, y, 70 * k, h }, "< PREV", (Color){ 70, 70, 100, 255 }, !running && g->level > 0))
        LoadLevel(g, g->level - 1, true);
    x += 78 * k;

    bool canNext = !running && g->level < Levels_Count() - 1;
    Color nextCol = (g->world.state == WORLD_WON) ? (Color){ 40, 150, 80, 255 } : (Color){ 70, 70, 100, 255 };
    if (Button((Rectangle){ x, y, 70 * k, h }, "NEXT >", nextCol, canNext))
        LoadLevel(g, g->level + 1, true);

    bool hasLesson = Levels_Get(g->level)->lesson[0] != '\0';
    if (Button((Rectangle){ W - 100, y, 90, h }, "LESSON", (Color){ 120, 90, 170, 255 }, hasLesson && !running))
        g->showLesson = !g->showLesson;
    if (Button((Rectangle){ W - 200, y, 90, h }, "LEVELS", (Color){ 60, 120, 140, 255 }, !running))
        g->showLevels = !g->showLevels;
    if (Button((Rectangle){ W - 240, y, 32, h }, "?", (Color){ 70, 70, 100, 255 }, true) || IsKeyPressed(KEY_F1))
        g->showHelp = !g->showHelp;
}

// ===========================================================================
static Font LoadCodeFont(bool *owned)
{
    // A monospace font makes code line up. Use one from the system if we can.
    const char *candidates[] = {
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/System/Library/Fonts/Menlo.ttc",
    };
    for (int i = 0; i < (int)(sizeof candidates / sizeof candidates[0]); i++)
    {
        if (FileExists(candidates[i]))
        {
            Font f = LoadFontEx(candidates[i], 36, NULL, 0);
            if (f.texture.id != 0)
            {
                SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
                *owned = true;
                return f;
            }
        }
    }
    *owned = false;
    return GetFontDefault();
}

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "Bitty's Great Escape");
    SetWindowMinSize(1024, 600);
    SetExitKey(KEY_NULL);   // Esc stops the program instead of closing the game
    SetTargetFPS(60);
    ToggleBorderlessWindowed();   // start fullscreen (F11 switches back to a window)
    UpdateLayout();
    Customisation_Init();

    static Game game;       // big (saved code), so not on the stack
    Game *g = &game;
    memset(g, 0, sizeof *g);

    g->codeFont = LoadCodeFont(&g->ownsFont);
    Editor_Init(&g->editor, EDITOR_PANEL, g->codeFont, 16);

    // Levels live in the levels/ folder next to the game (or the current folder)
    char levelError[256];
    const char *folder = TextFormat("%slevels", GetApplicationDirectory());
    if (!FileExists(TextFormat("%s/levels.txt", folder))) folder = "levels";
    char folderCopy[512];
    snprintf(folderCopy, sizeof folderCopy, "%s", folder);
    int loaded = Levels_LoadFolder(folderCopy, levelError, sizeof levelError);

    // Pick up where the player left off
    Save_Load(TextFormat("%sbitty_save.txt", GetApplicationDirectory()));
    int start = 0;
    for (int i = 0; i < Levels_Count(); i++)
        if (strcmp(Levels_Get(i)->file, Save_GetCurrent()) == 0) start = i;
    g->level = start;
    LoadLevel(g, start, false);
    if (levelError[0])
        Console_PrintKind(loaded ? CONSOLE_WARNING : CONSOLE_ERROR, TextFormat("Level problem: %s\n", levelError));

    Startup_Init(g->codeFont);
    Talk_Init();
    bool startupActive = true;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        UpdateLayout();
        Editor_SetBounds(&g->editor, EDITOR_PANEL);

        if (startupActive)
        {
            if (Startup_Update(dt))
            {
                startupActive = false;
                World_PlayIntro(&g->world);
            }
            BeginDrawing();
            Startup_Draw();
            EndDrawing();
            continue;
        }

        // Esc closes whatever card is open (and stops a running program, see DrawToolbar)
        if (IsKeyPressed(KEY_ESCAPE) && g->mode != MODE_RUNNING && !Talk_Active())
        {
            if (g->showHelp) g->showHelp = false;
            else if (g->showLevels) g->showLevels = false;
            else if (g->showWorkshop) g->showWorkshop = false;
            else g->showLesson = false;
        }

        bool overlay = g->showHelp || g->showLevels || g->showWorkshop || Talk_Active();
        if (!overlay) Editor_Update(&g->editor);
        if (Editor_TakeChanged(&g->editor))
        {
            g->unsaved = true;
            g->saveTimer = 3.0f;       // save a few seconds after the typing stops
            if (g->mode != MODE_RUNNING)
            {
                g->editor.errorLine = 0;   // editing clears the old error
                if (g->mode == MODE_DONE) { g->mode = MODE_EDITING; g->editor.runLine = 0; }
            }
        }
        if (g->unsaved && (g->saveTimer -= dt) <= 0) SaveNow(g);

        // Bitty waits above the level while the lesson card is open, then drops in
        if (g->showLesson && g->mode == MODE_EDITING && !g->introDone) World_PlayIntro(&g->world);

        // Bitty talks: the level's intro, and little reactions to what happens to him
        if (!g->showWorkshop && !g->showLevels && !g->showHelp)
        {
            const Level *lv = Levels_Get(g->level);
            if (Talk_Update(&g->world, dt, WORLD_PANEL))
            {
                // the talk is over: put Bitty on his start tile and open his notes
                if (g->mode == MODE_EDITING)
                {
                    if (g->world.hasIntroStart) LoadWorld(g);
                    g->showLesson = lv->lesson[0] != '\0';
                }
            }
            if (g->barkCooldown > 0) g->barkCooldown -= dt;
            WorldEvent event = World_TakeEvent(&g->world);
            if (!Talk_Active())
            {
                if (event == EVENT_BONK && g->barkCooldown <= 0 && g->world.speed < 4)
                    Bark(g, "Bonk! That's a wall.", "Oof. Solid memory.", "Bzzt! Can't go that way.");
                if (event == EVENT_FALL) Bark(g, "Aaaah! Unallocated memoryyy!", "Nooo, there's nothing here!", NULL);
                if (event == EVENT_WIN)  Bark(g, "Yes! We did it!", "Sector cleared! Nice code!", "Beep boop! Freedom!");
            }
        }

        if (!g->showWorkshop)
        {
            UpdateProgram(g, dt);
            if (g->world.state == WORLD_WON)
                Customisation_RecordCompletion(g->level);
        }

        BeginDrawing();
        ClearBackground((Color){ 10, 12, 20, 255 });

        DrawToolbar(g);
        if (g->showWorkshop)
        {
            Customisation_Draw(EDITOR_PANEL);
            // Preview the chosen face even after a successful or failed run.
            BittyMood previousMood = g->world.mood;
            g->world.mood = MOOD_IDLE;
            DrawWorldPanel(g);
            g->world.mood = previousMood;
        }
        else
        {
            Editor_Draw(&g->editor);
            DrawStarterButton(g);
            DrawWorldPanel(g);
            Talk_Draw(WORLD_PANEL, g->codeFont);
            DrawLessonCard(g);
            DrawLevelMenu(g);
        }
        Console_Draw(CONSOLE_PANEL, g->codeFont);
        Inspector_Draw(g->vars, g->varCount, INSPECT_PANEL, g->codeFont,
                       g->mode == MODE_RUNNING ? g->editor.runLine : 0);
        DrawHelp(g);

        EndDrawing();
    }

    SaveNow(g);
    StopProgram(g);
    Talk_Shutdown();
    Startup_Shutdown();
    if (g->ownsFont) UnloadFont(g->codeFont);
    CloseWindow();
    return 0;
}
