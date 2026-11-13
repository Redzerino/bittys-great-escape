// circuit.c - the Escape Circuit: the motherboard map where you pick a stage.
//
// Nine chips (worlds) with five or six stages each, the stages of the escape,
// plus the optional BOOTCAMP strip at the top for people who have never
// coded before. Everything is laid out for a 1280x720 screen and scaled to
// fit the window (see View()).
//
// Levels are placed on the map by their file name (see Placement()), so
// saves, which are keyed by file name, keep working when the map changes.

#include "circuit.h"
#include "levels.h"
#include "save.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum { WORLDS = 9, STAGES = 6, BOOTCAMP_STAGES = 7 };   // STAGES = the most a chip can hold

#define BOOTCAMP WORLDS            // "world" number used for the bootcamp strip

static int   stages[WORLDS][STAGES];          // level index in each slot (-1 = not written yet)
static int   stageCount[WORLDS];              // how many stages each chip shows (5, or 6 if a sixth is written)
static int   bootcamp[BOOTCAMP_STAGES];       // level index of each bootcamp stage (-1 = none)
static int   selectedWorld = 0, selectedStage = 0;
static float clockTime = 0;

static const char *worldNames[] = {
    "BOOT SECTOR", "REGISTER BANK", "CPU CONTROL UNIT", "PROCESSING CORE", "SYSTEM LIBRARY",
    "RAM BANKS", "DATA BUS", "ADDRESS NETWORK", "USB GATEWAY", "BOOTCAMP"
};
static const char *topics[] = {
    "Commands / variables", "First functions / maths", "Decisions / logic", "Loops / repetition",
    "Helper functions / scope", "Arrays / searching", "Characters / strings", "Pointers / addresses",
    "Final escape / revision", "Never coded before? Start here"
};
// Names shown for stages that aren't written yet
static const char *planned[WORLDS][STAGES] = {
    { "Wake Up", "Long Corridor", "Mind the Gap", "Memory Boxes", "Fork in the Road" },
    { "First Function", "Data Types", "Register Updates", "Access Code", "Digit Peeler", "Binary Decoder" },
    { "Comparisons", "Choose a Path", "Multiple Routes", "Logical Gates", "Control Terminal" },
    { "Counted Loops", "Sensor Loop", "Try Again", "Emergency Stop", "Prime Lock" },
    { "Helper Functions", "Parameters", "Return Values", "Local Memory", "Repair Toolkit" },
    { "Data Shelf", "Array Traversal", "Power Audit", "Search the Bay", "Mirror Mirror" },
    { "Character Codes", "End of Message", "String Length", "Password Terminal", "Message Repair" },
    { "Memory Addresses", "Follow the Wire", "Swap Wires", "Pointer Arithmetic", "Measure the String" },
    { "Port Controller", "Transfer Buffer", "Final Firewall", "Escape Protocol", "Bitty Breaks Free" }
};
static const Color accents[] = {
    { 100, 245, 175, 255 }, { 255, 198,  95, 255 }, { 255, 135, 125, 255 },
    { 110, 195, 255, 255 }, { 185, 150, 255, 255 }, { 100, 225, 220, 255 },
    { 245, 145, 210, 255 }, { 180, 235, 115, 255 }, { 235, 242, 255, 255 },
    { 150, 215, 255, 255 }      // bootcamp
};

// ===========================================================================
// Where things go
// ===========================================================================

// Scales the 1280x720 layout to fit the window, centred
static Camera2D View(void)
{
    float scale = fminf(GetScreenWidth() / 1280.0f, GetScreenHeight() / 720.0f);
    Camera2D c = { 0 };
    c.zoom = scale;
    c.offset = (Vector2){ (GetScreenWidth() - 1280 * scale) / 2, (GetScreenHeight() - 720 * scale) / 2 };
    return c;
}

// The chips go in a snake: left to right, then right to left, then left to right
static Rectangle Chip(int world)
{
    int row = world / 3, col = world % 3;
    if (row == 1) col = 2 - col;
    return (Rectangle){ 32 + col * 293.0f, 150 + row * 158.0f, 268, 132 };
}

static Rectangle BootcampChip(void)
{
    return (Rectangle){ 742, 16, 506, 112 };
}

static Vector2 Node(int world, int stage)
{
    if (world == BOOTCAMP)
    {
        Rectangle r = BootcampChip();
        return (Vector2){ r.x + 40 + stage * 52, r.y + 80 };
    }
    Rectangle r = Chip(world);
    float gap = 200.0f / (stageCount[world] - 1);      // 50 apart for 5 stages, 40 for 6
    return (Vector2){ r.x + 34 + stage * gap, r.y + 89 };
}

static int StageCount(int world)            { return world == BOOTCAMP ? BOOTCAMP_STAGES : stageCount[world]; }
static int LevelAt(int world, int stage)    { return world == BOOTCAMP ? bootcamp[stage] : stages[world][stage]; }

// Which slot a level file goes in: files are named "W-S_name.txt" (world W,
// stage S), and bootcamp files "0-S_name.txt".
static bool Placement(const char *file, int *world, int *slot)
{
    int w, s;
    if (sscanf(file, "%d-%d", &w, &s) != 2) return false;
    if (w == 0 && s >= 1 && s <= BOOTCAMP_STAGES) { *world = BOOTCAMP; *slot = s - 1; return true; }
    if (w < 1 || w > WORLDS || s < 1 || s > STAGES) return false;
    *world = w - 1;
    *slot = s - 1;
    return true;
}

// ===========================================================================
// Setup and questions from the rest of the game
// ===========================================================================
void Circuit_Init(void)
{
    for (int w = 0; w < WORLDS; w++)
        for (int s = 0; s < STAGES; s++) stages[w][s] = -1;
    for (int s = 0; s < BOOTCAMP_STAGES; s++) bootcamp[s] = -1;

    for (int i = 0; i < Levels_Count(); i++)
    {
        int w, s;
        int *slot = NULL;
        if (Placement(Levels_Get(i)->file, &w, &s))
            slot = (w == BOOTCAMP) ? &bootcamp[s] : &stages[w][s];
        if (slot && *slot < 0) *slot = i;
        else TraceLog(LOG_WARNING, "Circuit: no free slot for %s", Levels_Get(i)->file);
    }
    for (int w = 0; w < WORLDS; w++)
    {
        stageCount[w] = 5;
        for (int s = 5; s < STAGES; s++) if (stages[w][s] >= 0) stageCount[w] = s + 1;
    }
}

void Circuit_SelectLevel(int level)
{
    for (int w = 0; w <= BOOTCAMP; w++)
        for (int s = 0; s < StageCount(w); s++)
            if (LevelAt(w, s) == level) { selectedWorld = w; selectedStage = s; return; }
}

int Circuit_WorldOf(int level)
{
    for (int w = 0; w <= BOOTCAMP; w++)
        for (int s = 0; s < StageCount(w); s++)
            if (LevelAt(w, s) == level) return w == BOOTCAMP ? 0 : w + 1;
    return 1;
}

bool Circuit_IsBootcamp(int level)
{
    for (int s = 0; s < BOOTCAMP_STAGES; s++)
        if (bootcamp[s] == level) return true;
    return false;
}

const char *Circuit_StageName(int level)
{
    for (int w = 0; w <= BOOTCAMP; w++)
        for (int s = 0; s < StageCount(w); s++)
            if (LevelAt(w, s) == level)
                return w == BOOTCAMP ? TextFormat("Bootcamp %d", s + 1) : TextFormat("Stage %d.%d", w + 1, s + 1);
    return "";
}

// ===========================================================================
// Drawing
// ===========================================================================
static bool Done(int index) { return index >= 0 && Save_IsDone(Levels_Get(index)->file); }

static void Label(Font font, const char *text, float x, float y, float size, Color c)
{
    DrawTextEx(font, text, (Vector2){ x, y }, size, 0, c);
}

// Shrinks the text until it fits in 'width'
static void FitLabel(Font font, const char *text, float x, float y, float size, float width, Color c)
{
    while (size > 10 && MeasureTextEx(font, text, size, 0).x > width) size -= 1;
    Label(font, text, x, y, size, c);
}

// The stage name without the number at the front ("1.4  Memory Boxes" -> "Memory Boxes")
static const char *ShortTitle(const char *title)
{
    const char *p = title;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    return *p ? p : title;
}

// One stage node. Returns true if it was clicked.
static bool DrawNode(Font font, int world, int stage, Vector2 mouse, bool click)
{
    Vector2 n = Node(world, stage);
    int index = LevelAt(world, stage);
    Color accent = accents[world];
    bool done = Done(index), hover = CheckCollisionPointCircle(mouse, n, 20);
    Color color = done ? (Color){ 110, 245, 165, 255 } : index >= 0 ? accent : (Color){ 55, 72, 80, 255 };

    if (selectedWorld == world && selectedStage == stage)
        DrawCircleLines((int)n.x, (int)n.y, 21 + 2 * sinf(clockTime * 3), accent);
    if (hover) DrawCircleV(n, 21, Fade(accent, 0.17f));
    DrawCircleV(n, 15, done ? color : (Color){ 8, 20, 29, 255 });
    DrawCircleLines((int)n.x, (int)n.y, 15, color);

    // white pointer over the last level played
    if (index >= 0 && strcmp(Levels_Get(index)->file, Save_GetCurrent()) == 0)
        DrawTriangle((Vector2){ n.x - 5, n.y - 27 }, (Vector2){ n.x, n.y - 20 }, (Vector2){ n.x + 5, n.y - 27 }, WHITE);

    if (done)
    {
        // Draw a tick with lines so it works with every font.
        Color tick = { 8, 45, 30, 255 };
        DrawLineEx((Vector2){ n.x - 7, n.y }, (Vector2){ n.x - 2, n.y + 5 }, 3, tick);
        DrawLineEx((Vector2){ n.x - 2, n.y + 5 }, (Vector2){ n.x + 8, n.y - 6 }, 3, tick);
    }
    else Label(font, TextFormat("%d", stage + 1), n.x - 4, n.y - 7, 16, color);
    if (index < 0)          // small padlock: not written yet
    {
        DrawRectangle((int)n.x + 6, (int)n.y + 8, 7, 6, color);
        DrawCircleLines((int)n.x + 9, (int)n.y + 8, 3, color);
    }
    return hover && click;
}

int Circuit_Draw(Font font, float dt)
{
    clockTime += dt;
    Camera2D camera = View();
    Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), camera);
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    int launch = -1, cleared = 0, available = 0, total = 0;

    for (int w = 0; w < WORLDS; w++)
        for (int s = 0; s < StageCount(w); s++)
        {
            total++;
            if (stages[w][s] >= 0) available++;
            if (Done(stages[w][s])) cleared++;
        }

    ClearBackground((Color){ 4, 10, 16, 255 });
    BeginMode2D(camera);

    // the board
    DrawRectangleGradientV(0, 0, 1280, 720, (Color){ 10, 35, 38, 255 }, (Color){ 5, 15, 24, 255 });
    for (int x = 16; x < 1280; x += 24)
        for (int y = 16; y < 720; y += 24) DrawCircle(x, y, 1, (Color){ 22, 48, 51, 255 });

    Label(font, "BITTY'S ESCAPE CIRCUIT", 32, 28, 32, (Color){ 150, 255, 210, 255 });
    Label(font, "Trace a route through the computer.", 34, 73, 16, LIGHTGRAY);
    Label(font, "Choose a stage to write your escape.", 34, 92, 16, LIGHTGRAY);
    Label(font, TextFormat("%02d / %d CLEARED   |   %d stages playable", cleared, total, available), 34, 120, 17, SKYBLUE);

    // copper tracks between the chips, with a pulse of data running along each
    for (int w = 0; w < WORLDS - 1; w++)
    {
        Rectangle a = Chip(w), b = Chip(w + 1);
        Vector2 from = { a.x + a.width / 2, a.y + a.height / 2 }, to = { b.x + b.width / 2, b.y + b.height / 2 };
        DrawLineEx(from, to, 7, (Color){ 20, 65, 67, 255 });
        DrawLineEx(from, to, 2, Fade(accents[w], 0.4f));
        float p = fmodf(clockTime * 0.2f + w * 0.13f, 1);
        DrawCircleV((Vector2){ from.x + (to.x - from.x) * p, from.y + (to.y - from.y) * p }, 3, accents[w]);
    }

    // the nine chips
    for (int w = 0; w < WORLDS; w++)
    {
        Rectangle chip = Chip(w);
        Color accent = accents[w];
        for (int pin = 0; pin < 10; pin++)
        {
            DrawRectangle((int)chip.x + 15 + pin * 25, (int)chip.y - 6, 6, 6, (Color){ 135, 130, 95, 255 });
            DrawRectangle((int)chip.x + 15 + pin * 25, (int)(chip.y + chip.height), 6, 6, (Color){ 135, 130, 95, 255 });
        }
        bool worldComplete = true;
        for (int s = 0; s < StageCount(w); s++)
            if (!Done(stages[w][s])) worldComplete = false;
        DrawRectangleRounded(chip, 0.08f, 4, worldComplete ? (Color){ 22, 105, 68, 255 } : (Color){ 12, 27, 36, 255 });
        DrawRectangleRoundedLinesEx(chip, 0.08f, 4, selectedWorld == w ? 2 : 1, Fade(accent, selectedWorld == w ? 1 : 0.45f));
        Label(font, TextFormat("%02d", w + 1), chip.x + 13, chip.y + 13, 16, accent);
        FitLabel(font, worldNames[w], chip.x + 44, chip.y + 13, 18, 212, WHITE);
        Label(font, topics[w], chip.x + 14, chip.y + 41, 12, (Color){ 155, 178, 187, 255 });

        int worldDone = 0;
        for (int s = 0; s < StageCount(w); s++) if (Done(stages[w][s])) worldDone++;
        Label(font, TextFormat("%d/%d", worldDone, StageCount(w)), chip.x + 231, chip.y + 114, 11, accent);

        DrawLineEx(Node(w, 0), Node(w, StageCount(w) - 1), 2, (Color){ 45, 78, 86, 255 });
        for (int s = 0; s < StageCount(w); s++)
            if (DrawNode(font, w, s, mouse, click))
            {
                selectedWorld = w; selectedStage = s;
                if (stages[w][s] >= 0) launch = stages[w][s];
            }
    }

    // the optional bootcamp strip
    {
        Rectangle chip = BootcampChip();
        Color accent = accents[BOOTCAMP];
        bool bootComplete = true;
        for (int s = 0; s < BOOTCAMP_STAGES; s++)
            if (!Done(bootcamp[s])) bootComplete = false;
        DrawRectangleRounded(chip, 0.1f, 4, bootComplete ? (Color){ 22, 105, 68, 255 } : (Color){ 12, 24, 38, 255 });
        DrawRectangleRoundedLinesEx(chip, 0.1f, 4, selectedWorld == BOOTCAMP ? 2 : 1, Fade(accent, selectedWorld == BOOTCAMP ? 1 : 0.5f));
        Label(font, "00", chip.x + 13, chip.y + 12, 16, accent);
        Label(font, "BOOTCAMP", chip.x + 44, chip.y + 11, 18, WHITE);
        Label(font, "OPTIONAL", chip.x + 152, chip.y + 14, 13, accent);
        Label(font, topics[BOOTCAMP], chip.x + 14, chip.y + 38, 12, (Color){ 155, 178, 187, 255 });
        int bootDone = 0;
        for (int s = 0; s < BOOTCAMP_STAGES; s++) if (Done(bootcamp[s])) bootDone++;
        Label(font, TextFormat("%d/%d", bootDone, BOOTCAMP_STAGES), chip.x + chip.width - 38, chip.y + 14, 11, accent);

        DrawLineEx(Node(BOOTCAMP, 0), Node(BOOTCAMP, BOOTCAMP_STAGES - 1), 2, (Color){ 45, 78, 86, 255 });
        for (int s = 0; s < BOOTCAMP_STAGES; s++)
            if (DrawNode(font, BOOTCAMP, s, mouse, click))
            {
                selectedWorld = BOOTCAMP; selectedStage = s;
                if (bootcamp[s] >= 0) launch = bootcamp[s];
            }
    }

    // details of the selected stage
    Rectangle details = { 935, 150, 313, 448 };
    Color accent = accents[selectedWorld];
    DrawRectangleRounded(details, 0.06f, 6, (Color){ 12, 26, 37, 255 });
    DrawRectangleRoundedLinesEx(details, 0.06f, 6, 1, Fade(accent, 0.6f));

    int selected = LevelAt(selectedWorld, selectedStage);
    bool boot = (selectedWorld == BOOTCAMP);
    Label(font, boot ? "OPTIONAL TRAINING" : "SELECTED CIRCUIT", 956, 174, 15, accent);
    FitLabel(font, worldNames[selectedWorld], 956, 212, 23, 270, WHITE);
    Label(font, boot ? TextFormat("LESSON %d", selectedStage + 1) : TextFormat("STAGE %d.%d", selectedWorld + 1, selectedStage + 1),
          956, 254, 18, SKYBLUE);
    const char *name = selected >= 0 ? ShortTitle(Levels_Get(selected)->title)
                     : boot ? "Coming soon" : planned[selectedWorld][selectedStage];
    FitLabel(font, name, 956, 295, 20, 270, WHITE);
    Label(font, Done(selected) ? "COMPLETED" : selected >= 0 ? "READY TO CODE" : "COMING SOON", 956, 340, 18,
          selected >= 0 ? GREEN : GRAY);
    if (boot)
    {
        Label(font, "Never written code before?", 956, 383, 15, LIGHTGRAY);
        Label(font, "Bitty explains it from zero.", 956, 404, 15, LIGHTGRAY);
        Label(font, "You can skip this any time.", 956, 436, 13, LIGHTGRAY);
    }
    else
    {
        Label(font, selected >= 0 ? "Click a lit node to enter." : "This stage is being built.", 956, 383, 15, LIGHTGRAY);
        Label(font, "Your code and completed stages", 956, 415, 13, LIGHTGRAY);
        Label(font, "are saved automatically.", 956, 436, 13, LIGHTGRAY);
    }

    Rectangle play = { 956, 489, 270, 48 };
    DrawRectangleRounded(play, 0.15f, 6, selected >= 0 ? (Color){ 30, 100, 92, 255 } : (Color){ 30, 43, 52, 255 });
    Label(font, selected >= 0 ? "ENTER STAGE  >" : "NOT AVAILABLE YET", 976, 505, 18, selected >= 0 ? WHITE : GRAY);
    if (selected >= 0 && ((click && CheckCollisionPointRec(mouse, play)) || IsKeyPressed(KEY_ENTER))) launch = selected;

    Label(font, "USB EXIT", 1140, 634, 16, accents[8]);
    DrawRectangleLinesEx((Rectangle){ 936, 630, 185, 28 }, 2, accents[8]);
    Label(font, TextFormat("%d stages to freedom", total), 946, 638, 12, LIGHTGRAY);
    Label(font, "Tick + filled green = completed    coloured = playable    padlock = coming soon", 34, 660, 15, LIGHTGRAY);
    Label(font, "White pointer = last played   |   Enter opens the selected stage", 34, 689, 13, (Color){ 120, 155, 166, 255 });

    EndMode2D();
    return launch;
}
