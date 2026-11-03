// main.c - Bitty's Great Escape: the window, the panels and the game loop.
//
//   CODE (editor)  |  MEMORY (Bitty's world)  |  INSPECTOR (live variables)
//                  |  CONSOLE (output)        |
//
// Press RUN (F5) and the player's code is compiled by the VM, then run a few
// hundred instructions per frame. When it calls move() the VM waits while
// Bitty slides to the next tile, then carries on.

#include "raylib.h"

#include "console.h"
#include "editor.h"
#include "game_api.h"
#include "inspector.h"
#include "levels.h"
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

typedef struct {
    World      world;
    Editor     editor;
    Vm        *vm;
    GameMode   mode;
    bool       stepping;
    int        speedIndex;
    int        level;
    char       savedCode[16][EDITOR_MAX_TEXT];   // each level keeps its own code
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
    World_LoadMap(&g->world, lv->map, lv->mapRows);
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
    Console_PrintKind(CONSOLE_STORY, lv->story);
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

    VmHost host = { g, HostCallBuiltin, HostPrint, HostWarning };
    VmOptions options = { false, 0 };   // escape levels: infinite loops are allowed
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

static void UpdateProgram(Game *g, float dt)
{
    World *w = &g->world;
    World_Update(w, dt);

    if (g->mode != MODE_RUNNING || !g->vm) return;

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
    float areaW = b.width - 40, areaH = b.height - 70;
    float tile = areaW / w->cols;
    if (areaH / w->rows < tile) tile = areaH / w->rows;
    if (tile > 64) tile = 64;
    w->tileSize = tile;
    w->originX = b.x + (b.width - tile * w->cols) / 2;
    w->originY = b.y + 40 + (areaH - tile * w->rows) / 2;

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
        if (running && g->vm && Vm_GetStatus(g->vm) == VM_PAUSED)
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
    g->level = 0;
    LoadLevel(g, 0);
    g->savedCode[0][0] = '\0';

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
