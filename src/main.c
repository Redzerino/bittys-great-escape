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

#include "console.h"
#include "editor.h"
#include "game_api.h"
#include "inspector.h"
#include "levels.h"
#include "repair.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define MAX_VARS 32

static const Rectangle EDITOR_PANEL  = { 10,  50, 400, 660 };
static const Rectangle WORLD_PANEL   = { 420, 50, 520, 460 };
static const Rectangle CONSOLE_PANEL = { 420, 520, 520, 190 };
static const Rectangle INSPECT_PANEL = { 950, 50, 320, 660 };

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
    char       savedCode[LEVEL_MAX_LEVELS][EDITOR_MAX_TEXT];   // each level keeps its own code

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

static void LoadLevel(Game *g, int index)
{
    // keep the code typed for the level we're leaving
    snprintf(g->savedCode[g->level], EDITOR_MAX_TEXT, "%s", Editor_GetText(&g->editor));

    StopProgram(g);
    g->level = index;
    g->mode = MODE_EDITING;
    LoadWorld(g);

    const Level *lv = Levels_Get(index);
    Editor_SetText(&g->editor, g->savedCode[index][0] ? g->savedCode[index] : lv->starterCode);
    g->editor.focused = true;

    Console_Clear();
    Console_PrintKind(CONSOLE_SUCCESS, TextFormat("LEVEL %s\n", lv->title));
    Console_PrintKind(CONSOLE_STORY, TextFormat("%s\n", lv->story));
    if (lv->type == LEVEL_REPAIR)
        Console_PrintKind(CONSOLE_STORY, TextFormat("Each gate tests your code (%d tests). Press RUN to try.\n", lv->testCount));

    g->showLesson = lv->lesson[0] != '\0';
    g->hintShown = false;
}

// ===========================================================================
// Running the player's code
// ===========================================================================
static void StartProgram(Game *g, bool step)
{
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
        g->showLesson = false;
}

static void DrawToolbar(Game *g)
{
    DrawText("BITTY'S GREAT ESCAPE", 10, 12, 26, (Color){ 120, 255, 160, 255 });

    bool running = (g->mode == MODE_RUNNING);
    float x = 420, y = 10, h = 30;

    if (Button((Rectangle){ x, y, 80, h }, "RUN", (Color){ 40, 150, 80, 255 }, !running) ||
        (!running && IsKeyPressed(KEY_F5)))
        StartProgram(g, false);
    x += 88;

    if (Button((Rectangle){ x, y, 80, h }, "STEP", (Color){ 50, 110, 170, 255 }, true) || IsKeyPressed(KEY_F10))
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
    x += 88;

    if (Button((Rectangle){ x, y, 80, h }, "STOP", (Color){ 170, 60, 60, 255 }, running))
    {
        Console_PrintKind(CONSOLE_WARNING, "Stopped.\n");
        g->world.mood = MOOD_IDLE;
        FinishRun(g);
        g->editor.runLine = 0;
    }
    x += 88;

    if (Button((Rectangle){ x, y, 80, h }, "RESET", (Color){ 90, 90, 110, 255 }, !running))
    {
        StopProgram(g);
        g->mode = MODE_EDITING;
        g->editor.errorLine = 0;
        LoadWorld(g);
        Console_Clear();
        Console_PrintKind(CONSOLE_STORY, Levels_Get(g->level)->story);
    }
    x += 96;

    if (Button((Rectangle){ x, y, 60, h }, SPEED_NAMES[g->speedIndex], (Color){ 70, 70, 100, 255 }, true))
    {
        g->speedIndex = (g->speedIndex + 1) % 4;
        g->world.speed = SPEEDS[g->speedIndex];
    }
    x += 76;

    if (Button((Rectangle){ x, y, 70, h }, "< PREV", (Color){ 70, 70, 100, 255 }, !running && g->level > 0))
        LoadLevel(g, g->level - 1);
    x += 78;

    bool canNext = !running && g->level < Levels_Count() - 1;
    Color nextCol = (g->world.state == WORLD_WON) ? (Color){ 40, 150, 80, 255 } : (Color){ 70, 70, 100, 255 };
    if (Button((Rectangle){ x, y, 70, h }, "NEXT >", nextCol, canNext))
        LoadLevel(g, g->level + 1);

    bool hasLesson = Levels_Get(g->level)->lesson[0] != '\0';
    if (Button((Rectangle){ 1180, y, 90, h }, "LESSON", (Color){ 120, 90, 170, 255 }, hasLesson && !running))
        g->showLesson = !g->showLesson;
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
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Bitty's Great Escape");
    SetExitKey(KEY_NULL);   // Esc shouldn't close the game
    SetTargetFPS(60);

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

    g->level = 0;
    LoadLevel(g, 0);
    g->savedCode[0][0] = '\0';
    if (levelError[0])
        Console_PrintKind(loaded ? CONSOLE_WARNING : CONSOLE_ERROR, TextFormat("Level problem: %s\n", levelError));

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        Editor_Update(&g->editor);
        if (Editor_TakeChanged(&g->editor) && g->mode != MODE_RUNNING)
        {
            g->editor.errorLine = 0;   // editing clears the old error
            if (g->mode == MODE_DONE) { g->mode = MODE_EDITING; g->editor.runLine = 0; }
        }

        UpdateProgram(g, dt);

        BeginDrawing();
        ClearBackground((Color){ 10, 12, 20, 255 });

        DrawToolbar(g);
        Editor_Draw(&g->editor);
        DrawWorldPanel(g);
        DrawLessonCard(g);
        Console_Draw(CONSOLE_PANEL, g->codeFont);
        Inspector_Draw(g->vars, g->varCount, INSPECT_PANEL, g->codeFont,
                       g->mode == MODE_RUNNING ? g->editor.runLine : 0);

        EndDrawing();
    }

    StopProgram(g);
    if (g->ownsFont) UnloadFont(g->codeFont);
    CloseWindow();
    return 0;
}
