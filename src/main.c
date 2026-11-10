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

#include "circuit.h"
#include "startup.h"
#include "talk.h"
#include "friendly.h"
#include "explain.h"
#include "customisation.h"
#include "console.h"
#include "editor.h"
#include "game_api.h"
#include "inspector.h"
#include "levels.h"
#include "repair.h"
#include "save.h"
#include "vm.h"

#include <math.h>
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
#define BUTTON_STRIP  48              // height of the code buttons under the editor (levels with buttons:)
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
    REPAIR_RESULT,        // showing PASS / FAIL on the gate terminal for a moment
    REPAIR_LEAVING        // passed: Bitty is beamed back out of the memory room
} RepairPhase;

typedef struct {
    World      world;
    Editor     editor;
    Vm        *vm;
    GameMode   mode;
    bool       stepping;
    bool       stepRequest;     // repair levels in step mode: STEP was pressed, run one more line
    int        speedIndex;
    int        level;
    bool       showCircuit;
    bool       showHelp;        // the F1 shortcuts card
    float      saveTimer;       // autosave countdown
    bool       unsaved;

    // repair levels
    RepairPhase repairPhase;
    int        repairTest;      // next test to run
    int        repairGates;     // gates in the map at the start
    float      repairTimer;     // small pauses so the player can follow along
    int        hintStep;        // how many hint lines have been shown for this level
    FriendlyError friendly;     // Bitty's explanation of the last error, pointing at the spot
    bool       friendlyOn;
    char       friendlySource[EDITOR_MAX_TEXT];   // the code it was about (it goes away when the code changes)
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
        case VM_BUILTIN_READ:
        case VM_BUILTIN_SCAN:
        {
            // escape levels: Bitty takes a moment to look, so you can see what he checked
            bool scan = (id == VM_BUILTIN_SCAN);
            if (Levels_Get(g->level)->type == LEVEL_ESCAPE)
            {
                Action a = { scan ? ACT_SCAN : ACT_READ, scan ? (Direction)args[0] : NORTH, 0, Vm_CurrentLine(g->vm) };
                if (World_StartAction(&g->world, a)) return true;
            }
            *result = scan ? World_Scan(&g->world, (Direction)args[0]) : World_Read(&g->world);
            return false;
        }
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
    g->world.theme = Circuit_WorldOf(g->level);     // colours of the chip this level is on
    World_LoadMap(&g->world, rows, lv->mapRows);
    g->world.speed = SPEEDS[g->speedIndex];
    g->world.doorCode = lv->doorCode;
    g->world.fogRadius = lv->fog;
    g->world.signCount = 0;
    for (int i = 0; i < lv->signCount && i < WORLD_MAX_SIGNS; i++)
    {
        WorldSign *sg = &g->world.signs[g->world.signCount++];
        sg->col = lv->signs[i].col;
        sg->row = lv->signs[i].row;
        snprintf(sg->text, sizeof sg->text, "%s", lv->signs[i].text);
    }
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
    LoadWorld(g);

    const Level *lv = Levels_Get(index);
    const char *saved = Save_GetCode(lv->file);
    Editor_SetText(&g->editor, saved ? saved : lv->starterCode);
    Editor_SelectNextBlank(&g->editor);     // fill-in-the-blank: the first gold gap is ready to type into
    World_PlayIntro(&g->world);     // Bitty drops into the new level
    g->editor.focused = true;
    Save_SetCurrent(lv->file);
    Save_Write();

    Console_Clear();
    Console_PrintKind(CONSOLE_SUCCESS, TextFormat("%s:  %s\n", Circuit_StageName(g->level), lv->title + strcspn(lv->title, " ") + strspn(lv->title + strcspn(lv->title, " "), " ")));
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
    g->hintStep = 0;
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

    // Bitty explains the problem in plain words and points at the exact spot (see friendly.c)
    const char *source = Editor_GetText(&g->editor);
    g->friendlyOn = false;
    for (int i = 0; i < result.warningCount; i++)
    {
        Console_PrintKind(CONSOLE_WARNING, TextFormat("%s\n", result.warnings[i]));
        if (g->vm && !g->friendlyOn && Friendly_Warning(source, result.warnings[i], result.warningLines[i], &g->friendly))
        {
            g->friendlyOn = true;
            snprintf(g->friendlySource, sizeof g->friendlySource, "%s", source);
        }
    }

    if (!g->vm)
    {
        if (Friendly_Explain(source, result.message, result.line, result.col, &g->friendly))
        {
            g->friendlyOn = true;
            snprintf(g->friendlySource, sizeof g->friendlySource, "%s", source);
            Console_PrintKind(CONSOLE_ERROR, TextFormat("Line %d: %s\n", g->friendly.line, g->friendly.say));
            Talk_Bark(g->friendly.say);
            g->barkCooldown = 2.0f;
        }
        else
        {
            Console_PrintKind(CONSOLE_ERROR, TextFormat("%s\n", result.message));
            Bark(g, "Bzzt. I can't read that. Check the red line?", "Hmm, that line confuses me. Look at the red one.", NULL);
        }
        g->editor.errorLine = g->friendlyOn ? g->friendly.line : result.line;
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
        g->stepping    = step;          // STEP: each line of each test waits for the STEP button
        g->stepRequest = false;
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

// Hints come in steps: a nudge, a bigger nudge, then the answer.
// Each line of the level's [hint] is one step.
static int HintCount(const Level *lv)
{
    int n = 0;
    for (const char *p = lv->hint; *p; )
    {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len > 0) n++;
        if (!nl) break;
        p = nl + 1;
    }
    return n;
}

static void HintLine(const Level *lv, int k, char *out, int size)
{
    out[0] = '\0';
    int n = 0;
    for (const char *p = lv->hint; *p; )
    {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > 0 && n++ == k) { snprintf(out, (size_t)size, "%.*s", len, p); return; }
        if (!nl) break;
        p = nl + 1;
    }
}

// Show the next hint step (the HINT button)
static void NextHint(Game *g)
{
    const Level *lv = Levels_Get(g->level);
    int count = HintCount(lv);
    if (count == 0) { Console_PrintKind(CONSOLE_STORY, "No hints for this one. You've got this!\n"); return; }
    int k = g->hintStep < count ? g->hintStep : count - 1;
    char text[400];
    HintLine(lv, k, text, sizeof text);
    Console_PrintKind(CONSOLE_STORY, TextFormat("Hint %d of %d: %s\n", k + 1, count, text));
    if (k < count - 1) Talk_Bark(text);         // the last step is code: it stays in the console
    else Talk_Bark("Here's the answer. Look at the console, then try to see WHY it works!");
    g->barkCooldown = 2.0f;
    if (g->hintStep < count) g->hintStep++;
}

// After a failed try: the first nudge comes by itself, then the HINT button gives more
static void ShowHint(Game *g)
{
    if (g->hintStep == 0) NextHint(g);
    else if (g->hintStep < HintCount(Levels_Get(g->level)))
        Console_PrintKind(CONSOLE_STORY, "Stuck? Press HINT for another clue.\n");
}

// ---------------------------------------------------------------------------
// Repair levels: Bitty walks to a gate, the gate's test runs line by line
// while the player watches the variables and memory change, then the gate
// opens (pass) or Bitty is stuck (fail).
// ---------------------------------------------------------------------------
#define PACE_FIRST_TEST   0.38f   // seconds per line for the first test
#define PACE_LATER_TESTS  0.16f   // later tests run quicker
#define LINES_BEFORE_FF   32      // after this many lines, fast-forward

// Which register or block of test data lives at this memory address (if any)
static TestSlot FindSlot(const Game *g, const LevelTest *t, const TestDisplay *td, const int *regAddress, int address)
{
    TestSlot slot = { SLOT_NONE, 0, 0 };
    for (int i = 0; i < td->regCount; i++)
        if (regAddress[i] == address) { slot.kind = SLOT_REGISTER; slot.index = i; return slot; }

    int row = 0;
    for (int k = 0; k < t->argc && row < td->rowCount; k++)
    {
        if (t->args[k].kind == VALUE_INT) continue;
        int at = address - g->repairRun.argAddr[k];
        if (at >= 0 && at < td->rows[row].count) { slot.kind = SLOT_DATA; slot.row = row; slot.index = at; return slot; }
        row++;
    }
    return slot;
}

// Copy the function's variables and the test's arrays/strings into the world's
// TestDisplay, and hand the memory room the reads and writes the last line made
// (in order), so Bitty can act them out.
// One line of the player's code (1 = the first line), without its newline
static void CodeLine(Game *g, int line, char *out, int size)
{
    const char *p = Editor_GetText(&g->editor);
    for (int n = 1; n < line && *p; p++) if (*p == '\n') n++;
    int k = 0;
    while (p[k] && p[k] != '\n' && p[k] != '\r' && k < size - 1) { out[k] = p[k]; k++; }
    out[k] = '\0';
}

static void UpdateTestDisplay(Game *g, const LevelTest *t, int line)
{
    TestDisplay *td = &g->world.test;

    // ---- registers: plain variables of the function ----
    // A variable keeps its box for the whole test. When its { } block ends
    // (like temp inside a loop) the box is greyed out, and lights up again
    // when the block runs again.
    for (int i = 0; i < td->regCount; i++) td->regs[i].inScope = false;
    for (int i = 0; i < g->varCount; i++)
    {
        const VmVariable *v = &g->vars[i];
        if (v->isGlobal || v->isArray || v->isPointer || v->isRef) continue;

        TestRegister *r = NULL;
        for (int k = 0; k < td->regCount && !r; k++)
            if (td->regs[k].address == v->address && strcmp(td->regs[k].name, v->name) == 0) r = &td->regs[k];
        if (!r)
        {
            if (td->regCount >= TEST_MAX_REGISTERS) continue;
            r = &td->regs[td->regCount++];
            memset(r, 0, sizeof *r);
            snprintf(r->name, sizeof r->name, "%.15s", v->name);
            r->address = v->address;
        }
        r->inScope = true;
        r->value  = v->values[0];
        r->known  = v->initialized[0];
        r->isChar = v->isChar;
        r->isBool = strcmp(v->type, "bool") == 0;
    }
    // A variable below the current function's variables belongs to a function
    // further up the call stack (recursion): it's waiting, not gone.
    int lowest = 1 << 30;
    for (int i = 0; i < td->regCount; i++) if (td->regs[i].inScope && td->regs[i].address < lowest) lowest = td->regs[i].address;
    for (int i = 0; i < td->regCount; i++) td->regs[i].waiting = !td->regs[i].inScope && td->regs[i].address < lowest;

    int regAddress[TEST_MAX_REGISTERS];
    for (int i = 0; i < td->regCount; i++) regAddress[i] = td->regs[i].address;

    // ---- data rows: the test's arrays, strings and &values ----
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
        d->markerCount = 0;

        // name the row after the parameter that holds it (arr, str, or *a for a single value)
        if (!d->label[0])
            for (int i = 0; i < g->varCount; i++)
            {
                const VmVariable *v = &g->vars[i];
                if (!(v->isPointer || v->isRef) || v->isGlobal || v->pointsTo != base) continue;
                snprintf(d->label, sizeof d->label, (count == 1 && v->isPointer) ? "*%.14s" : "%.15s", v->name);
                break;
            }
    }
    td->rowCount = rows;

    // ---- wires: every pointer (and reference, and array parameter) aimed into the test data ----
    TestWire wires[TEST_MAX_WIRES];
    int wireCount = 0;
    for (int i = 0; i < g->varCount && wireCount < TEST_MAX_WIRES; i++)
    {
        const VmVariable *v = &g->vars[i];
        if (!(v->isPointer || v->isRef) || v->isGlobal) continue;

        int row = -1, index = 0, rowNo = 0;
        for (int k = 0; k < t->argc && rowNo < rows; k++)
        {
            if (t->args[k].kind == VALUE_INT) continue;
            int at = v->pointsTo - g->repairRun.argAddr[k];
            if (at >= 0 && at <= td->rows[rowNo].count) { row = rowNo; index = at; break; }
            rowNo++;
        }
        if (row < 0) continue;      // pointing somewhere we don't draw

        TestWire *wire = &wires[wireCount++];
        memset(wire, 0, sizeof *wire);
        snprintf(wire->name, sizeof wire->name, "%.15s", v->name);
        wire->row = row;
        wire->index = index;
        wire->shownIndex = (float)index;

        // the same pointer as last time? keep its plug where it was, so it slides to the new block
        for (int j = 0; j < td->wireCount; j++)
        {
            const TestWire *old = &td->wires[j];
            if (strcmp(old->name, wire->name) != 0 || old->row != row) continue;
            wire->shownIndex = old->shownIndex;
            wire->replug = (old->index != index) ? 0.45f : old->replug;
        }
    }
    memcpy(td->wires, wires, sizeof wires);
    td->wireCount = wireCount;

    // ---- what the line just did, in order: Bitty acts it out in the memory room ----
    VmAccess log[VM_MAX_LOG];
    int logCount = Vm_TakeAccessLog(g->vm, log, VM_MAX_LOG);
    TestAccess list[VM_MAX_LOG];
    int listCount = 0;
    for (int i = 0; i < logCount; i++)
    {
        if (log[i].condition)
        {
            TestAccess *a = &list[listCount++];
            memset(a, 0, sizeof *a);
            a->condition = true;
            a->answer = log[i].answer;
            continue;
        }
        TestSlot slot = FindSlot(g, t, td, regAddress, log[i].address);
        if (slot.kind == SLOT_NONE) continue;
        TestAccess *a = &list[listCount++];
        a->slot = slot;
        a->write = log[i].write;
        a->value = Vm_ReadMemory(g->vm, log[i].address);
        a->condition = false;
    }
    char code[128];
    CodeLine(g, line, code, sizeof code);
    World_RoomStep(&g->world, list, listCount, !td->fastForward, line, code);
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
    td->quick = g->repairTest > 0;
    g->world.focus = 1.0f;
    g->world.mood = MOOD_RUNNING;

    // Bitty is beamed into the function's memory (or, if he's already in there, the new test data drops in)
    if (World_InRoom(&g->world)) World_RoomNewTest(&g->world);
    else World_EnterRoom(&g->world);

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

    // show the test data straight away (but not the writes that put it in memory)
    VmAccess skip[VM_MAX_LOG];
    Vm_TakeAccessLog(g->vm, skip, VM_MAX_LOG);
    g->varCount = Vm_GetVariables(g->vm, g->vars, MAX_VARS);
    UpdateTestDisplay(g, t, 0);
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

    bool ff = !g->stepping && g->linesShown >= LINES_BEFORE_FF;
    td->fastForward = ff;
    td->waitingForStep = false;
    td->askGuesses = g->stepping;           // step mode: guess what each line does before Bitty shows you

    // Bitty is still acting out the last line (or still beaming in): wait for him
    if (World_RoomBusy(&g->world) && !ff) return;

    int line = Vm_CurrentLine(g->vm);       // the line about to run
    if (g->stepping && (s == VM_PAUSED || s == VM_RUNNING))
    {
        // step mode: one line each time the player presses STEP
        if (!g->stepRequest) { td->waitingForStep = true; return; }
        g->stepRequest = false;
        g->linesShown++;
        Vm_PauseAtNextLine(g->vm);
        s = Vm_Run(g->vm, 200000);
    }
    else if (s == VM_PAUSED || (s == VM_RUNNING && !ff))
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
    if (s != VM_FINISHED) UpdateTestDisplay(g, t, line);   // after it finishes the frame is gone; keep the last picture
    else
    {
        // refresh the data rows one last time (registers keep their final values)
        int keepRegs = td->regCount, keepWires = td->wireCount;
        TestRegister regs[TEST_MAX_REGISTERS];
        TestWire wires[TEST_MAX_WIRES];
        memcpy(regs, td->regs, sizeof regs);
        memcpy(wires, td->wires, sizeof wires);
        UpdateTestDisplay(g, t, line);
        memcpy(td->regs, regs, sizeof regs);
        memcpy(td->wires, wires, sizeof wires);
        td->regCount = keepRegs;
        td->wireCount = keepWires;
    }

    if (s == VM_FINISHED || s == VM_ERROR)
    {
        Repair_FinishTest(g->vm, t, &g->repairRun, &g->repairOutcome);
        td->state = g->repairOutcome.passed ? TEST_PASSED : TEST_FAILED;
        snprintf(td->result, sizeof td->result, "%s", g->repairOutcome.summary);

        // in the room: a crash sends Bitty off the end of the data; otherwise he holds up the answer
        g->world.mood = g->repairOutcome.passed ? MOOD_HAPPY : MOOD_ERROR;
        if (s == VM_ERROR)
        {
            VmErrorKind kind = Vm_GetErrorKind(g->vm);
            World_RoomCrash(&g->world, kind == VM_ERR_SEGFAULT || kind == VM_ERR_OUT_OF_BOUNDS);
        }
        else if (strncmp(g->repairOutcome.summary, "returned ", 9) == 0)
            World_RoomResult(&g->world, TextFormat("return %s", g->repairOutcome.summary + 9));
        else
            World_RoomResult(&g->world, g->repairOutcome.passed ? "Done!" : "Hmm...");
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
    if (World_RoomBusy(w)) return;          // beaming in/out, or Bitty still busy in the room

    if (g->repairPhase == REPAIR_LEAVING)
    {
        // back in the corridor: the gate opens and he walks on
        int gx, gy;
        if (World_NextGate(w, &gx, &gy)) World_OpenGate(w, gx, gy);
        w->test.active = false;
        w->focus = 0;
        w->mood = MOOD_RUNNING;
        g->repairPhase = REPAIR_WALKING;
        g->repairTimer = 0.35f;
        return;
    }

    if (g->repairPhase == REPAIR_RESULT)
    {
        TestOutcome *o = &g->repairOutcome;
        Console_PrintKind(o->passed ? CONSOLE_SUCCESS : CONSOLE_ERROR, TextFormat("%s\n", o->message));

        if (!o->passed)
        {
            Bark(g, "Bzzt. Not quite. Look what it gave back.", "The gate says no. Check what it wanted.", NULL);
            w->mood = MOOD_ERROR;               // Bitty stays in the room, so you can see what the code left behind
            w->shake = 0.4f;
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
            World_LeaveRoom(w);                 // beam back out to the corridor, then the gate opens
            g->repairPhase = REPAIR_LEAVING;
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
        ShowHint(g);
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
        {
            Console_PrintKind(CONSOLE_WARNING, "The program finished, but Bitty hasn't reached the EXIT yet.\n");
            ShowHint(g);
        }
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

// F1: every shortcut in one place
static void DrawHelp(Game *g)
{
    if (!g->showHelp) return;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    Rectangle card = { W / 2 - 300, H / 2 - 300, 600, 600 };
    DrawRectangle(0, 0, (int)W, (int)H, (Color){ 0, 0, 0, 150 });
    DrawRectangleRounded(card, 0.04f, 8, (Color){ 16, 20, 36, 250 });
    DrawRectangleRoundedLinesEx(card, 0.04f, 8, 2, (Color){ 120, 200, 255, 255 });
    DrawText("SHORTCUTS", (int)card.x + 24, (int)card.y + 18, 22, (Color){ 120, 255, 160, 255 });

    static const char *const KEYS[][2] = {
        { "F5",                 "Run your code" },
        { "F10",                "Step: one line at a time (gate tests too)" },
        { "Esc",                "Stop the program / close a card" },
        { "F11",                "Fullscreen on / off" },
        { "F1",                 "This help" },
        { "Ctrl+Z / Ctrl+Y",    "Undo / redo" },
        { "Ctrl+A",             "Select all" },
        { "Ctrl+C / X / V",     "Copy / cut / paste" },
        { "Shift + arrows",     "Select text (or drag with the mouse)" },
        { "Ctrl + / Ctrl -",    "Bigger / smaller code (or Ctrl + wheel)" },
        { "Mouse wheel",        "Scroll the code or the console" },
        { "Tab",                "Jump to the next gold gap" },
        { "Rest the mouse",     "on a line: Bitty explains it in words" },
        { "1 / 2 / 3",          "Answer a GUESS FIRST question (step mode)" },
    };
    for (int i = 0; i < (int)(sizeof KEYS / sizeof KEYS[0]); i++)
    {
        float y = card.y + 60 + i * 30;
        DrawTextEx(g->codeFont, KEYS[i][0], (Vector2){ card.x + 28, y }, 17, 0, (Color){ 255, 214, 110, 255 });
        DrawTextEx(g->codeFont, KEYS[i][1], (Vector2){ card.x + 230, y }, 17, 0, (Color){ 225, 232, 245, 255 });
    }
    DrawTextEx(g->codeFont, "Your progress and code are saved automatically.", (Vector2){ card.x + 28, card.y + card.height - 64 },
               15, 0, (Color){ 140, 160, 200, 255 });
    if (Button((Rectangle){ card.x + card.width - 130, card.y + card.height - 46, 106, 32 }, "CLOSE", (Color){ 40, 150, 80, 255 }, true))
        g->showHelp = false;
}

// Bitty's explanation of an error, right where it is: a marker under the exact
// spot, what's wrong in plain words, and a FIX button when he's sure of the fix
static void DrawFriendly(Game *g)
{
    if (!g->friendlyOn) return;
    if (strcmp(Editor_GetText(&g->editor), g->friendlySource) != 0) { g->friendlyOn = false; return; }   // they changed it
    const FriendlyError *fe = &g->friendly;
    Vector2 at;
    if (!Editor_CharPos(&g->editor, fe->line, fe->col > 0 ? fe->col : 1, &at)) return;

    Color tone = fe->warning ? (Color){ 255, 200, 80, 255 } : (Color){ 255, 95, 105, 255 };
    float lh = g->editor.lineHeight, t = (float)GetTime();
    // the marker: a pulsing box around the exact spot
    float cw = MeasureTextEx(g->editor.font, "M", g->editor.fontSize, g->editor.spacing).x + 2;
    if (fe->col > 0)
    {
        float pulse = 0.6f + 0.4f * sinf(t * 6);
        // as wide as the word it points at (or one letter)
        const char *src = g->friendlySource;
        int off = 0;
        for (int l = 1; l < fe->line && src[off]; off++) if (src[off] == '\n') l++;
        off += fe->col - 1;
        int len = 0;
        while (src[off + len] == '_' || (src[off + len] >= 'a' && src[off + len] <= 'z') ||
               (src[off + len] >= 'A' && src[off + len] <= 'Z') || (src[off + len] >= '0' && src[off + len] <= '9')) len++;
        if (len == 0) len = 1;
        Rectangle box = { at.x - 2, at.y - 3, cw * len + 3, lh };
        DrawRectangleRec(box, Fade(tone, 0.25f * pulse));
        DrawRectangleLinesEx(box, 2, Fade(tone, pulse));
    }

    // the card, under the line (or above it, near the bottom of the panel)
    Rectangle panel = EDITOR_PANEL;
    float size = 15, pad = 10;
    float w = panel.width - 24;
    char text[240];
    snprintf(text, sizeof text, "%s", fe->say);
    // wrap into lines that fit
    char lines[4][120];
    int n = 0;
    const char *p = text;
    float textW = w - 2 * pad;
    while (*p && n < 4)
    {
        int k = 0, lastSpace = -1;
        while (p[k])
        {
            char tmp[120];
            snprintf(tmp, sizeof tmp, "%.*s", k + 1, p);
            if (MeasureTextEx(g->codeFont, tmp, size, 0).x > textW) break;
            if (p[k] == ' ') lastSpace = k;
            k++;
        }
        if (p[k] && lastSpace > 0) k = lastSpace;
        snprintf(lines[n++], sizeof lines[0], "%.*s", k, p);
        p += k;
        while (*p == ' ') p++;
    }
    float h = pad * 2 + n * (size + 3) + (fe->hasFix ? 34 : 0) + 16;
    // docked at the bottom of the Code panel, so it never hides the code; a dotted line leads up to the spot
    float below = Levels_Get(g->level)->buttons[0] ? BUTTON_STRIP : 0;     // above the code buttons
    Rectangle card = { panel.x + 12, panel.y + panel.height - h - 10 - below, w, h };
    if (fe->col > 0 && at.y + lh < card.y)
        for (float yy = at.y + lh; yy < card.y - 2; yy += 7)
            DrawRectangle((int)(at.x + cw / 2), (int)yy, 1, 4, Fade(tone, 0.45f));
    DrawRectangleRounded(card, 0.12f, 8, (Color){ 24, 22, 36, 245 });
    DrawRectangleRoundedLinesEx(card, 0.12f, 8, 2, tone);
    DrawTextEx(g->codeFont, fe->warning ? "BITTY SAYS: CAREFUL" : "BITTY SAYS: OOPS", (Vector2){ card.x + pad, card.y + 7 }, 13, 0, tone);
    for (int i = 0; i < n; i++)
        DrawTextEx(g->codeFont, lines[i], (Vector2){ card.x + pad, card.y + 24 + i * (size + 3) }, size, 0, WHITE);

    if (fe->hasFix)
    {
        float bw = MeasureTextEx(g->codeFont, TextFormat("FIX: %s", fe->fixLabel), 15, 0).x + 22;
        Rectangle fix = { card.x + pad, card.y + h - 36, bw, 26 };
        bool hover = CheckCollisionPointRec(GetMousePosition(), fix);
        DrawRectangleRounded(fix, 0.4f, 6, hover ? (Color){ 60, 190, 110, 255 } : (Color){ 40, 150, 80, 255 });
        DrawTextEx(g->codeFont, TextFormat("FIX: %s", fe->fixLabel), (Vector2){ fix.x + 10, fix.y + 5 }, 15, 0, WHITE);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g->mode != MODE_RUNNING)
        {
            static char fixed[EDITOR_MAX_TEXT];
            int cursor = Friendly_ApplyFix(g->friendlySource, fe, fixed, sizeof fixed);
            Editor_ReplaceAll(&g->editor, fixed);
            g->editor.cursor = cursor;
            g->editor.anchor = -1;
            g->editor.errorLine = 0;
            g->friendlyOn = false;
            Console_PrintKind(CONSOLE_STORY, "Fixed! Press RUN to try again. (Ctrl+Z undoes the fix.)\n");
            Talk_Bark("Fixed! Press RUN and let's try again.");
        }
        DrawTextEx(g->codeFont, "or fix it yourself", (Vector2){ fix.x + bw + 12, fix.y + 6 }, 13, 0, (Color){ 140, 150, 180, 255 });
    }
}

// Code buttons (early levels): tap a word to type it. With a gold gap
// selected it fills the gap and jumps to the next one.
static void DrawCodeButtons(Game *g)
{
    const char *list = Levels_Get(g->level)->buttons;
    if (!list[0]) return;
    Rectangle panel = EDITOR_PANEL;
    Rectangle strip = { panel.x + 2, panel.y + panel.height - BUTTON_STRIP, panel.width - 4, BUTTON_STRIP - 2 };
    DrawRectangleRec(strip, (Color){ 16, 19, 29, 255 });
    DrawLineEx((Vector2){ strip.x, strip.y }, (Vector2){ strip.x + strip.width, strip.y }, 1, (Color){ 60, 200, 255, 120 });

    bool gap = Editor_InGap(&g->editor);
    float x = strip.x + 8, y = strip.y + 9;
    const char *p = list;
    while (*p)
    {
        while (*p == ' ' || *p == '|') p++;
        if (!*p) break;
        const char *end = strchr(p, '|');
        int len = end ? (int)(end - p) : (int)strlen(p);
        while (len > 0 && p[len - 1] == ' ') len--;
        char word[40];
        snprintf(word, sizeof word, "%.*s", len, p);
        p = end ? end : p + strlen(p);

        float w = MeasureTextEx(g->codeFont, word, 16, 0).x + 18;
        if (x + w > strip.x + strip.width - 4) break;
        Rectangle b = { x, y, w, 28 };
        bool hover = CheckCollisionPointRec(GetMousePosition(), b) && g->mode != MODE_RUNNING;
        Color col = gap ? (Color){ 110, 84, 24, 255 } : (Color){ 40, 52, 84, 255 };
        if (hover) col = gap ? (Color){ 160, 122, 34, 255 } : (Color){ 60, 80, 130, 255 };
        DrawRectangleRounded(b, 0.35f, 6, col);
        DrawTextEx(g->codeFont, word, (Vector2){ b.x + 9, b.y + 6 }, 16, 0, gap ? (Color){ 255, 230, 160, 255 } : WHITE);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            bool wasGap = gap;
            // a whole statement goes on its own line
            const char *text = word;
            char line[48];
            size_t wl = strlen(word);
            if (!wasGap && wl > 0 && word[wl - 1] == ';') { snprintf(line, sizeof line, "%s\n", word); text = line; }
            Editor_InsertText(&g->editor, text);
            if (wasGap) Editor_SelectNextBlank(&g->editor);
        }
        x += w + 6;
    }
}

// "Guess first" (step mode): what will this line do? Tap an answer, then watch Bitty act it out
static void DrawQuiz(Game *g)
{
    const RoomState *rm = &g->world.room;
    const RoomQuiz *q = &rm->quiz;
    Rectangle panel = WORLD_PANEL;
    if (rm->guesses > 0 && g->stepping)
        DrawTextEx(g->codeFont, TextFormat("Guesses right: %d / %d", rm->guessesRight, rm->guesses),
                   (Vector2){ panel.x + 12, panel.y + panel.height - 24 }, 15, 0, (Color){ 255, 220, 140, 255 });
    if (!q->active) return;

    float w = panel.width - 60, h = 150;
    Rectangle card = { panel.x + 30, panel.y + panel.height - h - 64, w, h };
    DrawRectangleRounded(card, 0.12f, 8, (Color){ 16, 20, 42, 242 });
    DrawRectangleRoundedLinesEx(card, 0.12f, 8, 3, (Color){ 255, 214, 110, 255 });
    DrawTextEx(g->codeFont, "GUESS FIRST!", (Vector2){ card.x + 16, card.y + 10 }, 14, 0, (Color){ 255, 214, 110, 255 });
    DrawTextEx(g->codeFont, q->question, (Vector2){ card.x + 16, card.y + 30 }, 20, 0, WHITE);
    DrawTextEx(g->codeFont, q->detail, (Vector2){ card.x + 16, card.y + 56 }, 17, 0, (Color){ 255, 236, 170, 255 });

    float bw = (w - 32 - (q->optionCount - 1) * 12) / q->optionCount;
    for (int i = 0; i < q->optionCount; i++)
    {
        Rectangle b = { card.x + 16 + i * (bw + 12), card.y + h - 56, bw, 42 };
        bool hover = q->chosen < 0 && CheckCollisionPointRec(GetMousePosition(), b);
        Color col = { 50, 70, 120, 255 };
        if (hover) col = (Color){ 70, 100, 170, 255 };
        if (q->chosen >= 0 && i == q->correct) col = (Color){ 50, 170, 95, 255 };
        else if (q->chosen == i) col = (Color){ 200, 70, 80, 255 };
        DrawRectangleRounded(b, 0.3f, 8, col);
        float tw = MeasureTextEx(g->codeFont, q->options[i], 22, 0).x;
        DrawTextEx(g->codeFont, q->options[i], (Vector2){ b.x + (bw - tw) / 2, b.y + 10 }, 22, 0, WHITE);
        bool key = IsKeyPressed(KEY_ONE + i);
        if (q->chosen < 0 && ((hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || key))
        {
            World_RoomGuess(&g->world, i);
            Talk_Bark(i == q->correct ? "Yes! You got it. Watch me do it." : TextFormat("Not quite. It's %s. Watch!", q->options[q->correct]));
            g->barkCooldown = 1.5f;
        }
    }
    if (q->chosen < 0)
        DrawTextEx(g->codeFont, "tap an answer (or press 1, 2, 3)", (Vector2){ card.x + w - 250, card.y + 12 }, 13, 0, (Color){ 140, 150, 190, 255 });
}

// Rest the mouse on a line of code and Bitty says what it does, in plain English (see explain.c)
static void DrawExplainHover(Game *g)
{
    static int lastLine = 0;
    static float still = 0;
    static Vector2 lastMouse = { 0, 0 };
    Vector2 m = GetMousePosition();
    int line = Editor_LineAt(&g->editor, m);
    float moved = fabsf(m.x - lastMouse.x) + fabsf(m.y - lastMouse.y);
    lastMouse = m;
    if (line != lastLine || moved > 3 || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { lastLine = line; still = 0; return; }
    still += GetFrameTime();
    if (line == 0 || still < 0.5f) return;

    char text[300];
    if (!Explain_Line(Editor_GetText(&g->editor), line, text, sizeof text)) return;

    // wrap to fit
    float size = 15, maxW = 340;
    char lines[5][120];
    int n = 0;
    const char *p = text;
    float widest = 0;
    while (*p && n < 5)
    {
        int k = 0, lastSpace = -1;
        while (p[k])
        {
            char tmp[120];
            snprintf(tmp, sizeof tmp, "%.*s", k + 1, p);
            if (MeasureTextEx(g->codeFont, tmp, size, 0).x > maxW) break;
            if (p[k] == ' ') lastSpace = k;
            k++;
        }
        if (p[k] && lastSpace > 0) k = lastSpace;
        snprintf(lines[n], sizeof lines[0], "%.*s", k, p);
        float w = MeasureTextEx(g->codeFont, lines[n], size, 0).x;
        if (w > widest) widest = w;
        n++;
        p += k;
        while (*p == ' ') p++;
    }
    float bw = widest + 24, bh = n * (size + 3) + 30;
    float x = m.x + 16, y = m.y + 18;
    if (x + bw > GetScreenWidth() - 6) x = GetScreenWidth() - 6 - bw;
    if (y + bh > GetScreenHeight() - 6) y = m.y - bh - 8;
    float fade = still < 0.65f ? (still - 0.5f) / 0.15f : 1;
    Rectangle box = { x, y, bw, bh };
    DrawRectangleRounded(box, 0.15f, 8, Fade((Color){ 250, 248, 240, 255 }, fade));
    DrawTextEx(g->codeFont, TextFormat("Line %d, in plain English:", line), (Vector2){ x + 12, y + 7 }, 13, 0,
               Fade((Color){ 110, 116, 150, 255 }, fade));
    for (int i = 0; i < n; i++)
        DrawTextEx(g->codeFont, lines[i], (Vector2){ x + 12, y + 24 + i * (size + 3) }, size, 0, Fade((Color){ 30, 32, 48, 255 }, fade));
}

// A small button in the Code panel's title bar: put the starter code back (Ctrl+Z undoes it)
static void DrawStarterButton(Game *g)
{
    // HINT: a nudge, a bigger nudge, then the answer
    {
        Rectangle b = EDITOR_PANEL;
        int count = HintCount(Levels_Get(g->level));
        const char *label = g->hintStep == 0 ? "HINT" : TextFormat("hint %d/%d", g->hintStep < count ? g->hintStep + 1 : count, count);
        Rectangle r = { b.x + b.width - 118 - 92, b.y + 5, 84, 22 };
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRounded(r, 0.4f, 6, hover ? (Color){ 120, 90, 30, 255 } : (Color){ 82, 62, 24, 255 });
        float tw = MeasureTextEx(g->codeFont, label, 15, 0).x;
        DrawTextEx(g->codeFont, label, (Vector2){ r.x + (r.width - tw) / 2, r.y + 3 }, 15, 0, (Color){ 255, 220, 140, 255 });
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) NextHint(g);
    }

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
        if (g->showWorkshop) g->showLesson = false;
    }
    if (g->showWorkshop) return;

    bool running = (g->mode == MODE_RUNNING);
    float x = WORLD_PANEL.x, y = 10, h = 30;
    float W = (float)GetScreenWidth();
    // On a narrow window the run buttons shrink a little so they don't run into the ones on the right
    float k = Clamp((W - 356 - x) / 584.0f, 0.7f, 1.0f);

    bool canRun = !running || g->stepping;      // while stepping, RUN lets the code carry on by itself
    if (Button((Rectangle){ x, y, 80 * k, h }, "RUN", (Color){ 40, 150, 80, 255 }, canRun) ||
        (canRun && IsKeyPressed(KEY_F5)))
    {
        if (!running) StartProgram(g, false);
        else
        {
            g->stepping = false;
            if (Levels_Get(g->level)->type == LEVEL_ESCAPE && g->vm && Vm_GetStatus(g->vm) == VM_PAUSED)
                Vm_Run(g->vm, 1);       // leave the paused line and keep going
        }
    }
    x += 88 * k;

    if (Button((Rectangle){ x, y, 80 * k, h }, "STEP", (Color){ 50, 110, 170, 255 }, true) || IsKeyPressed(KEY_F10))
    {
        if (Levels_Get(g->level)->type == LEVEL_REPAIR)
        {
            // gate tests: one line at a time, and Bitty acts out each one in the memory room
            if (!running) StartProgram(g, true);
            else
            {
                g->stepping = true;
                g->stepRequest = true;
            }
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
    if (Button((Rectangle){ W - 200, y, 90, h }, "CIRCUIT", (Color){ 60, 120, 140, 255 }, !running))
    {
        SaveNow(g);
        Talk_Skip();
        g->world.talking = false;
        g->showHelp = g->showWorkshop = false;
        Circuit_SelectLevel(g->level);
        g->showCircuit = true;
    }
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
    Circuit_Init();
    int start = -1;
    for (int i = 0; i < Levels_Count(); i++)
        if (strcmp(Levels_Get(i)->file, Save_GetCurrent()) == 0) start = i;
    if (start < 0)          // first time: point at stage 1.1 (the bootcamp is there if they want it)
    {
        start = 0;
        while (start < Levels_Count() - 1 && Circuit_IsBootcamp(start)) start++;
    }
    g->level = start;
    LoadLevel(g, start, false);
    if (levelError[0])
        Console_PrintKind(loaded ? CONSOLE_WARNING : CONSOLE_ERROR, TextFormat("Level problem: %s\n", levelError));

    Circuit_SelectLevel(start);
    g->showCircuit = true;
    Talk_Skip();
    Startup_Init(g->codeFont);
    Talk_Init();
    bool startupActive = true;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        UpdateLayout();
        {
            // leave room under the code for the code buttons, on levels that have them
            Rectangle eb = EDITOR_PANEL;
            if (Levels_Get(g->level)->buttons[0]) eb.height -= BUTTON_STRIP;
            Editor_SetBounds(&g->editor, eb);
        }

        if (startupActive)
        {
            if (Startup_Update(dt))
            {
                startupActive = false;
                g->showCircuit = true;
            }
            BeginDrawing();
            Startup_Draw();
            EndDrawing();
            continue;
        }

        if (g->showCircuit)
        {
            BeginDrawing();
            int stage = Circuit_Draw(g->codeFont, dt);
            EndDrawing();
            if (stage >= 0)
            {
                LoadLevel(g, stage, true);
                g->showCircuit = false;
            }
            continue;
        }

        // Esc closes whatever card is open (and stops a running program, see DrawToolbar)
        if (IsKeyPressed(KEY_ESCAPE) && g->mode != MODE_RUNNING && !Talk_Active())
        {
            if (g->showHelp) g->showHelp = false;
            else if (g->showWorkshop) g->showWorkshop = false;
            else g->showLesson = false;
        }

        bool overlay = g->showHelp || g->showWorkshop || Talk_Active();
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
        if (!g->showWorkshop && !g->showHelp)
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
                if (event == EVENT_DOOR_OPEN)  Bark(g, "Beep! Code accepted.", "Click! The door's open!", NULL);
                if (event == EVENT_WRONG_CODE) Bark(g, "Bzzt. Wrong code.", "The door says no. Check the number?", NULL);
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
            DrawCodeButtons(g);
            DrawFriendly(g);
            DrawStarterButton(g);
            DrawWorldPanel(g);
            DrawQuiz(g);
            Talk_Draw(WORLD_PANEL, g->codeFont);
            DrawLessonCard(g);
        }
        Console_Draw(CONSOLE_PANEL, g->codeFont);
        Inspector_Draw(g->vars, g->varCount, INSPECT_PANEL, g->codeFont,
                       g->mode == MODE_RUNNING ? g->editor.runLine : 0);
        if (!g->showWorkshop && !g->showHelp) DrawExplainHover(g);   // on top of everything
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
