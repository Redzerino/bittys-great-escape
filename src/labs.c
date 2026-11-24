// labs.c - CLASS LABS chooser and question lists (see labs.h)
//
// Everything is drawn with shapes in code (no images), laid out for 1280x720
// and scaled to the window like the circuit map.

#include "labs.h"
#include "circuit.h"
#include "levels.h"
#include "save.h"
#include "sfx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct { const char *q, *what, *level; } LabQuestion;   // level = file name prefix, e.g. "3-4_"

// Lab 1 (Exam 1) in C
static const LabQuestion LAB1[] = {
    { "Q1",  "isLeapYear",                "3-4_" },
    { "Q2",  "Reversed (helper)",         "2-5_" },
    { "Q2",  "isAPalindrome",             "5-6_" },
    { "Q3",  "isPrime",                   "4-5_" },
    { "Q4",  "Binary to decimal",         "2-6_" },
    { "Q6",  "Number staircase",          "4-6_" },
    { "Q7",  "Number pyramid",            "4-7_" },
    { "Q19", "Diamond with no if",        "4-8_" },
    { "Q8",  "Find a value",              "6-4_" },
    { "Q9",  "Second largest",            "6-6_" },
    { "Q10", "Copy an array",             "9-1_" },
    { "Q11", "Insert and shift",          "9-3_" },
    { "Q12", "Delete and shift",          "9-4_" },
    { "Q13", "Frequency count",           "6-7_" },
    { "Q14", "Find duplicates",           "6-8_" },
    { "Q15", "Reverse an array",          "6-5_" },
    { "Q16", "Rotate an array",           "9-2_" },
    { "Q17", "In-flight movies",          "9-5_" },
    { "Q18", "Count the words",           "7-6_" },
    { "Q19", "PrintArray (2D)",           "10-2_" },
    { "Q20", "Clusters in a grid",        "10-3_" },
    { "Q21", "Gravity drop (2D)",         "10-4_" },
    { "Q21", "Find the variables",        "7-7_" },
    { "Q22", "Variable scope",            "7-8_" },
};
// Lab 2 (Exam 2) in C++: pointers, then linked lists
static const LabQuestion LAB2[] = {
    { "1",  "swap_ints",                  "8-3_" },
    { "2",  "swap_chars",                 "8-6_" },
    { "3",  "find_max",                   "6-3_" },
    { "4",  "reverse_array",              "8-4_" },
    { "5",  "strlen_ptr",                 "8-5_" },
    { "6",  "count_vowels",               "8-7_" },
    { "7",  "sum_array",                  "8-2_" },
    { "8",  "copy_string",                "12-2_" },
    { "9",  "compare_strings",            "12-3_" },
    { "10", "concat_strings",             "12-4_" },
    { "11", "Make a Node (notes code)",   "11-1_" },
    { "12", "Walk the list",              "11-2_" },
    { "13", "InsertAtBeginning",          "11-3_" },
    { "14", "InsertAtEnd",                "11-4_" },
    { "15", "InsertAtPosition",           "11-5_" },
    { "16", "Doubly linked list",         "11-6_" },
    { "17", "DLL InsertAtPosition",       "11-7_" },
};
#define LAB1_COUNT ((int)(sizeof LAB1 / sizeof LAB1[0]))
#define LAB2_COUNT ((int)(sizeof LAB2 / sizeof LAB2[0]))

static time_t endTime;
static bool   onList;          // false = the two cards, true = a lab's question list
static int    lab;             // 0 = C (Lab 1), 1 = C++ (Lab 2)
static int    focus;           // card the keyboard is on
static float  clockTime, flash, toList = -1;
static float  lift[2];         // hover animation of each card
static Font   display;         // a bold poster font for the big words (falls back to the code font)
static bool   ownDisplay;

// ===========================================================================
// Dates
// ===========================================================================
void Labs_Init(const char *endDate)
{
    int y = 2026, m = 12, d = 19;
    if (endDate && endDate[0]) sscanf(endDate, "%d-%d-%d", &y, &m, &d);
    struct tm t = { 0 };
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
    t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
    t.tm_isdst = -1;
    endTime = mktime(&t);

    const char *fonts[] = { "C:/Windows/Fonts/impact.ttf", "C:/Windows/Fonts/ariblk.ttf", "C:/Windows/Fonts/arialbd.ttf",
                            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/System/Library/Fonts/Supplemental/Impact.ttf" };
    for (int i = 0; i < (int)(sizeof fonts / sizeof fonts[0]) && !ownDisplay; i++)
        if (FileExists(fonts[i]))
        {
            display = LoadFontEx(fonts[i], 160, NULL, 0);
            if (display.texture.id != 0) { SetTextureFilter(display.texture, TEXTURE_FILTER_BILINEAR); ownDisplay = true; }
        }
}

void Labs_Shutdown(void)
{
    if (ownDisplay) UnloadFont(display);
    ownDisplay = false;
}

bool Labs_Active(void) { return time(NULL) <= endTime; }

int Labs_DaysLeft(void)
{
    double s = difftime(endTime, time(NULL));
    return s <= 0 ? 0 : (int)(s / 86400.0) + 1;
}

void Labs_Open(bool showChooser)
{
    onList = !showChooser;
    flash = 0;
    toList = -1;
    lift[0] = lift[1] = 0;
}

// ===========================================================================
// Little helpers
// ===========================================================================
static Camera2D View(void)
{
    float scale = fminf(GetScreenWidth() / 1280.0f, GetScreenHeight() / 720.0f);
    Camera2D c = { 0 };
    c.zoom = scale;
    c.offset = (Vector2){ (GetScreenWidth() - 1280 * scale) / 2, (GetScreenHeight() - 720 * scale) / 2 };
    return c;
}

static int LevelFor(const char *prefix)
{
    size_t n = strlen(prefix);
    for (int i = 0; i < Levels_Count(); i++)
        if (strncmp(Levels_Get(i)->file, prefix, n) == 0) return i;
    return -1;
}

static void Text(Font f, const char *s, float x, float y, float size, Color c)
{
    DrawTextEx(f, s, (Vector2){ x, y }, size, size / 12.0f, c);
}

// Big poster lettering: a thick black outline, a drop shadow, then the colour
static void Poster(Font f, const char *s, float x, float y, float size, Color c)
{
    if (ownDisplay) f = display;
    float sp = size / 14.0f, o = size / 22.0f;
    DrawTextEx(f, s, (Vector2){ x + o * 2.2f, y + o * 2.2f }, size, sp, Fade(BLACK, 0.55f));
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            if (dx || dy) DrawTextEx(f, s, (Vector2){ x + dx * o, y + dy * o }, size, sp, BLACK);
    DrawTextEx(f, s, (Vector2){ x, y }, size, sp, c);
}

static float Width(Font f, const char *s, float size) { return MeasureTextEx(f, s, size, size / 12.0f).x; }
static float PosterWidth(Font f, const char *s, float size) { return MeasureTextEx(ownDisplay ? display : f, s, size, size / 14.0f).x; }

static unsigned rng;
static float Rand(void) { rng = rng * 1103515245u + 12345u; return ((rng >> 8) & 0xFFFF) / 65535.0f; }

static Color Mix(Color a, Color b, float t)
{
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), 255 };
}

// ===========================================================================
// The two cards
// ===========================================================================
// Synthwave sun: a big circle with the bottom half cut into bands
static void Sun(float cx, float cy, float r, Color top, Color bottom, Color sky)
{
    for (int i = 0; i < 40; i++)              // vertical gradient, drawn as slices
    {
        float y0 = -r + 2 * r * i / 40.0f, y1 = -r + 2 * r * (i + 1) / 40.0f;
        float half = sqrtf(fmaxf(0, r * r - ((y0 + y1) / 2) * ((y0 + y1) / 2)));
        DrawRectangleRec((Rectangle){ cx - half, cy + y0, half * 2, y1 - y0 + 1 }, Mix(top, bottom, i / 40.0f));
    }
    for (int b = 0; b < 6; b++)               // the cut-out bands
    {
        float y = cy + r * 0.12f + b * r * 0.16f;
        DrawRectangleRec((Rectangle){ cx - r - 2, y, 2 * r + 4, 2 + b * 1.6f }, sky);
    }
}

static void Palm(float x, float ground, float h, float lean, Color c)
{
    Vector2 prev = { x, ground };
    for (int i = 1; i <= 10; i++)             // a curved trunk
    {
        float t = i / 10.0f;
        Vector2 p = { x + lean * t * t * h, ground - h * t };
        DrawLineEx(prev, p, 7 - 3 * t, c);
        prev = p;
    }
    for (int l = 0; l < 7; l++)               // drooping leaves
    {
        float a = -PI + l * PI / 6.0f;
        Vector2 last = prev;
        for (int s = 1; s <= 6; s++)
        {
            float t = s / 6.0f;
            Vector2 p = { prev.x + cosf(a) * h * 0.42f * t, prev.y + sinf(a) * h * 0.25f * t + h * 0.22f * t * t };
            DrawLineEx(last, p, 5 * (1 - t) + 1, c);
            last = p;
        }
    }
}

static void CardArt(Rectangle r, int which)
{
    rng = which ? 777u : 4242u;
    Color skyTop  = which ? (Color){ 40, 14, 74, 255 }   : (Color){ 8, 24, 58, 255 };
    Color skyLow  = which ? (Color){ 255, 92, 160, 255 }  : (Color){ 255, 138, 58, 255 };
    Color sunTop  = which ? (Color){ 255, 236, 120, 255 } : (Color){ 255, 240, 150, 255 };
    Color sunLow  = which ? (Color){ 255, 70, 150, 255 }  : (Color){ 255, 90, 60, 255 };
    Color shade   = which ? (Color){ 26, 8, 40, 255 }     : (Color){ 6, 14, 28, 255 };
    float horizon = r.y + r.height * 0.66f;

    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)(horizon - r.y), skyTop, skyLow);
    for (int i = 0; i < 40; i++)             // stars
    {
        float sx = r.x + Rand() * r.width, sy = r.y + Rand() * r.height * 0.35f;
        DrawPixelV((Vector2){ sx, sy }, Fade(WHITE, 0.3f + 0.6f * fabsf(sinf(clockTime * (1 + Rand()) + i))));
    }
    float sunY = horizon - r.height * 0.07f;
    Sun(r.x + r.width / 2, sunY, r.width * 0.27f, sunTop, sunLow, Mix(skyTop, skyLow, (sunY - r.y) / (horizon - r.y) + 0.08f));

    if (!which)
    {
        // C: a downtown skyline with lit windows, some of them blinking like a terminal
        float x = r.x;
        while (x < r.x + r.width)
        {
            float w = 26 + Rand() * 40, h = 50 + Rand() * r.height * 0.32f;
            if (fabsf(x + w / 2 - (r.x + r.width / 2)) < 60) h *= 0.55f;    // keep the sun visible
            if (x + w > r.x + r.width) w = r.x + r.width - x;
            DrawRectangleRec((Rectangle){ x, horizon - h, w, h }, shade);
            if (Rand() < 0.3f) DrawRectangleRec((Rectangle){ x + w / 2 - 1, horizon - h - 14, 2, 14 }, shade);    // antenna
            for (float wy = horizon - h + 8; wy < horizon - 8; wy += 9)
                for (float wx = x + 5; wx < x + w - 6; wx += 8)
                {
                    float roll = Rand();
                    if (roll < 0.35f)
                    {
                        bool blink = roll < 0.03f && fmodf(clockTime + wx, 1.2f) < 0.6f;
                        DrawRectangleRec((Rectangle){ wx, wy, 4, 4 }, blink ? (Color){ 90, 255, 220, 255 } : Fade((Color){ 255, 200, 120, 255 }, 0.75f));
                    }
                }
            x += w + 2;
        }
        // street with light trails
        DrawRectangleRec((Rectangle){ r.x, horizon, r.width, r.y + r.height - horizon }, (Color){ 10, 12, 22, 255 });
        for (int i = 0; i < 6; i++)
        {
            float y = horizon + 10 + i * 9;
            float off = fmodf(clockTime * (60 + i * 25) + i * 50, r.width + 120) - 60;
            DrawRectangleGradientH((int)(r.x + off), (int)y, 60, 2, Fade((Color){ 255, 80, 60, 255 }, 0), (Color){ 255, 120, 80, 255 });
            float off2 = r.width - fmodf(clockTime * (50 + i * 20) + i * 80, r.width + 120) + 60;
            DrawRectangleGradientH((int)(r.x + off2 - 60), (int)y + 4, 60, 2, (Color){ 220, 240, 255, 255 }, Fade(WHITE, 0));
        }
    }
    else
    {
        // C++: ocean, palm trees and a pier
        DrawRectangleGradientV((int)r.x, (int)horizon, (int)r.width, (int)(r.y + r.height - horizon), (Color){ 120, 40, 120, 255 }, (Color){ 20, 6, 36, 255 });
        for (int i = 0; i < 9; i++)          // sun shimmer on the water
        {
            float y = horizon + 6 + i * 9;
            float w = r.width * (0.36f - i * 0.03f) * (0.8f + 0.2f * sinf(clockTime * 2 + i));
            DrawRectangleRec((Rectangle){ r.x + r.width / 2 - w / 2, y, w, 2 }, Fade(sunTop, 0.55f - i * 0.05f));
        }
        DrawRectangleRec((Rectangle){ r.x, horizon - 18, r.width * 0.28f, 18 }, shade);     // far hills
        DrawTriangle((Vector2){ r.x + r.width, horizon }, (Vector2){ r.x + r.width, horizon - 46 }, (Vector2){ r.x + r.width * 0.62f, horizon }, shade);
        DrawRectangleRec((Rectangle){ r.x + r.width * 0.55f, horizon + 30, r.width * 0.45f, 5 }, shade);   // pier
        for (int p = 0; p < 5; p++)
            DrawRectangleRec((Rectangle){ r.x + r.width * 0.58f + p * r.width * 0.09f, horizon + 30, 3, 22 }, shade);
        Palm(r.x + 40, r.y + r.height, r.height * 0.52f, 0.25f, shade);
        Palm(r.x + 92, r.y + r.height, r.height * 0.38f, 0.32f, shade);
        Palm(r.x + r.width - 46, r.y + r.height, r.height * 0.47f, -0.28f, shade);
    }
    // film-grain scanlines
    for (float y = r.y; y < r.y + r.height; y += 3) DrawRectangleRec((Rectangle){ r.x, y, r.width, 1 }, Fade(BLACK, 0.12f));
}

// Returns true when clicked
static bool Card(Font f, Rectangle base, int which, Vector2 mouse, bool click, float dt)
{
    bool hover = CheckCollisionPointRec(mouse, base);
    if (hover && focus != which) { focus = which; Sfx_Play(SFX_HOVER); }
    bool on = focus == which;
    lift[which] += ((on ? 1.0f : 0.0f) - lift[which]) * fminf(1, dt * 10);
    float k = lift[which];
    Rectangle r = { base.x - 8 * k, base.y - 14 * k, base.width + 16 * k, base.height + 16 * k };

    Color accent = which ? (Color){ 255, 110, 200, 255 } : (Color){ 90, 240, 220, 255 };
    DrawRectangleRec((Rectangle){ r.x + 14, r.y + 18, r.width, r.height }, Fade(BLACK, 0.5f));     // shadow
    CardArt(r, which);
    if (!on) DrawRectangleRec(r, Fade((Color){ 4, 6, 14, 255 }, 0.45f));

    // bottom panel with the words
    Rectangle band = { r.x, r.y + r.height - 150, r.width, 150 };
    DrawRectangleGradientV((int)band.x, (int)band.y - 40, (int)band.width, 40, Fade(BLACK, 0), Fade(BLACK, 0.8f));
    DrawRectangleRec(band, Fade(BLACK, 0.8f));

    const char *big = which ? "C++" : "C";
    float size = 150 + 10 * k;
    Poster(f, big, r.x + 26, r.y + 18, size, which ? (Color){ 255, 220, 120, 255 } : (Color){ 255, 255, 255, 255 });
    Text(f, which ? "LAB 2" : "LAB 1", band.x + 26, band.y + 16, 34, accent);
    Text(f, which ? "EXAM 2  |  17 QUESTIONS" : "EXAM 1  |  24 QUESTIONS", band.x + 140, band.y + 26, 17, WHITE);
    Text(f, which ? "Pointers, strings and linked lists" : "Loops, arrays, strings and 2D grids", band.x + 26, band.y + 66, 17, LIGHTGRAY);

    // progress
    const LabQuestion *qs = which ? LAB2 : LAB1;
    int n = which ? LAB2_COUNT : LAB1_COUNT, done = 0;
    for (int i = 0; i < n; i++) { int lv = LevelFor(qs[i].level); if (lv >= 0 && Save_IsDone(Levels_Get(lv)->file)) done++; }
    Rectangle bar = { band.x + 26, band.y + 104, band.width - 52, 10 };
    DrawRectangleRec(bar, (Color){ 40, 44, 58, 255 });
    DrawRectangleRec((Rectangle){ bar.x, bar.y, bar.width * done / n, bar.height }, accent);
    Text(f, TextFormat("%d / %d DONE", done, n), bar.x, bar.y + 16, 13, accent);
    if (on) Text(f, "PRESS ENTER  >", bar.x + bar.width - 118, bar.y + 16, 13, WHITE);

    float pulse = on ? 0.6f + 0.4f * sinf(clockTime * 4) : 0.25f;
    DrawRectangleLinesEx(r, on ? 4 : 2, Fade(accent, pulse));
    return hover && click;
}

static int DrawChooser(Font f, Vector2 mouse, bool click, float dt)
{
    // a dark backdrop with slow diagonal light streaks
    DrawRectangleGradientV(0, 0, 1280, 720, (Color){ 6, 6, 16, 255 }, (Color){ 16, 8, 26, 255 });
    for (int i = 0; i < 7; i++)
    {
        float x = fmodf(clockTime * 30 + i * 230, 1700) - 300;
        DrawTriangle((Vector2){ x, 0 }, (Vector2){ x - 260, 720 }, (Vector2){ x - 180, 720 }, Fade(i % 2 ? (Color){ 255, 90, 170, 255 } : (Color){ 60, 230, 220, 255 }, 0.05f));
    }

    Poster(f, "CHOOSE YOUR LAB", 640 - PosterWidth(f, "CHOOSE YOUR LAB", 50) / 2, 16, 50, WHITE);
    const char *sub = "Advanced Programming revision: the exam lab questions as Bitty levels";
    Text(f, sub, 640 - Width(f, sub, 16) / 2, 80, 16, LIGHTGRAY);

    // LIMITED TIME tag
    {
        int days = Labs_DaysLeft();
        const char *left = days == 1 ? "LAST DAY!" : TextFormat("%d DAYS LEFT", days);
        Rectangle tag = { 1056, 26, 196, 58 };
        float wob = sinf(clockTime * 3) * 2.0f;
        DrawRectanglePro((Rectangle){ tag.x + tag.width / 2, tag.y + tag.height / 2, tag.width, tag.height },
                         (Vector2){ tag.width / 2, tag.height / 2 }, -4 + wob * 0.3f, (Color){ 230, 30, 60, 255 });
        Text(f, "LIMITED TIME", tag.x + 22, tag.y + 6, 22, WHITE);
        Text(f, left, tag.x + 22 + (days < 10 ? 18 : 10), tag.y + 33, 16, (Color){ 255, 230, 120, 255 });
    }

    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) { focus = 0; Sfx_Play(SFX_HOVER); }
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) { focus = 1; Sfx_Play(SFX_HOVER); }

    int chose = -1;
    if (Card(f, (Rectangle){ 150, 128, 460, 500 }, 0, mouse, click && toList < 0, dt)) chose = 0;
    if (Card(f, (Rectangle){ 670, 128, 460, 500 }, 1, mouse, click && toList < 0, dt)) chose = 1;
    if (toList < 0 && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) chose = focus;
    if (chose >= 0)
    {
        lab = chose;
        Sfx_Play(SFX_SELECT);
        flash = 1;
        toList = 0.55f;
    }

    // skip
    Rectangle skip = { 1040, 662, 212, 40 };
    bool hover = CheckCollisionPointRec(mouse, skip);
    DrawRectangleRounded(skip, 0.3f, 6, hover ? (Color){ 60, 60, 80, 255 } : (Color){ 30, 30, 44, 255 });
    Text(f, "SKIP TO THE MAP  >", skip.x + 18, skip.y + 11, 16, hover ? WHITE : LIGHTGRAY);
    Text(f, "Left / Right to pick, Enter to choose", 32, 674, 14, GRAY);
    if ((hover && click && toList < 0) || IsKeyPressed(KEY_ESCAPE)) { Sfx_Play(SFX_BACK); return LABS_TO_MAP; }

    if (toList >= 0)
    {
        toList -= dt;
        if (toList < 0) { onList = true; Sfx_Play(SFX_WHOOSH); }
    }
    return -1;
}

// ===========================================================================
// A lab's questions
// ===========================================================================
static int DrawList(Font f, Vector2 mouse, bool click)
{
    const LabQuestion *qs = lab ? LAB2 : LAB1;
    int n = lab ? LAB2_COUNT : LAB1_COUNT;
    Color accent = lab ? (Color){ 255, 110, 200, 255 } : (Color){ 90, 240, 220, 255 };

    DrawRectangleGradientV(0, 0, 1280, 720, lab ? (Color){ 30, 10, 44, 255 } : (Color){ 6, 20, 40, 255 }, (Color){ 6, 6, 14, 255 });
    // banner in the card's sunset colours, with the sun's bands
    DrawRectangleGradientH(0, 0, 1280, 122, lab ? (Color){ 60, 18, 90, 255 } : (Color){ 10, 30, 66, 255 },
                           lab ? (Color){ 255, 92, 160, 255 } : (Color){ 255, 138, 58, 255 });
    for (int b = 0; b < 5; b++) DrawRectangle(0, 92 + b * 6, 1280, 2, Fade(BLACK, 0.25f));
    DrawRectangle(0, 120, 1280, 3, accent);
    Poster(f, lab ? "C++  LAB 2" : "C  LAB 1", 32, 10, 64, lab ? (Color){ 255, 220, 120, 255 } : WHITE);
    Text(f, lab ? "Exam 2: pointers and linked lists" : "Exam 1: the lab sheet questions", 34, 90, 17, WHITE);

    int done = 0;
    int perCol = (n + 1) / 2;
    int launch = -1;
    for (int i = 0; i < n; i++)
    {
        int lv = LevelFor(qs[i].level);
        bool isDone = lv >= 0 && Save_IsDone(Levels_Get(lv)->file);
        if (isDone) done++;
        float x = i < perCol ? 32 : 652, y = 142 + (i % perCol) * 42;
        Rectangle row = { x, y, 596, 36 };
        bool hover = lv >= 0 && CheckCollisionPointRec(mouse, row);
        DrawRectangleRounded(row, 0.2f, 6, hover ? Fade(accent, 0.28f) : (Color){ 18, 22, 34, 235 });
        if (hover) DrawRectangleRoundedLinesEx(row, 0.2f, 6, 2, accent);
        DrawRectangleRounded((Rectangle){ x + 6, y + 6, 54, 24 }, 0.3f, 6, Fade(accent, 0.85f));
        Text(f, qs[i].q, x + 33 - Width(f, qs[i].q, 15) / 2, y + 10, 15, BLACK);
        Text(f, qs[i].what, x + 74, y + 9, 17, WHITE);
        if (lv >= 0) Text(f, Circuit_StageName(lv), x + 410, y + 11, 14, LIGHTGRAY);
        else Text(f, "missing level", x + 410, y + 11, 14, GRAY);
        if (isDone)
        {
            Vector2 c = { x + 572, y + 18 };
            DrawCircleV(c, 11, (Color){ 60, 200, 120, 255 });
            DrawLineEx((Vector2){ c.x - 5, c.y }, (Vector2){ c.x - 1, c.y + 4 }, 3, (Color){ 8, 45, 30, 255 });
            DrawLineEx((Vector2){ c.x - 1, c.y + 4 }, (Vector2){ c.x + 6, c.y - 5 }, 3, (Color){ 8, 45, 30, 255 });
        }
        if (hover && click) { Sfx_Play(SFX_SELECT); launch = lv; }
    }

    DrawRectangleRounded((Rectangle){ 1040, 22, 210, 76 }, 0.2f, 6, Fade(BLACK, 0.55f));
    Text(f, TextFormat("%d / %d DONE", done, n), 1058, 32, 22, WHITE);
    Text(f, Labs_DaysLeft() == 1 ? "Last day of the labs!" : TextFormat("Labs leave in %d days", Labs_DaysLeft()), 1058, 68, 14, (Color){ 255, 220, 150, 255 });

    Rectangle back = { 32, 668, 200, 38 }, map = { 1052, 668, 196, 38 };
    bool hb = CheckCollisionPointRec(mouse, back), hm = CheckCollisionPointRec(mouse, map);
    DrawRectangleRounded(back, 0.3f, 6, hb ? (Color){ 60, 60, 80, 255 } : (Color){ 30, 30, 44, 255 });
    DrawRectangleRounded(map, 0.3f, 6, hm ? (Color){ 30, 110, 100, 255 } : (Color){ 22, 70, 66, 255 });
    Text(f, "<  CHOOSE LAB", back.x + 20, back.y + 10, 17, WHITE);
    Text(f, "MAP  >", map.x + 64, map.y + 10, 17, WHITE);
    Text(f, "Click a question to play it. Finished levels get a tick.", 260, 678, 14, GRAY);
    if ((hb && click) || IsKeyPressed(KEY_BACKSPACE)) { Sfx_Play(SFX_BACK); Labs_Open(true); }
    if ((hm && click) || IsKeyPressed(KEY_ESCAPE)) { Sfx_Play(SFX_BACK); return LABS_TO_MAP; }
    return launch;
}

int Labs_Draw(Font font, float dt)
{
    clockTime += dt;
    Camera2D camera = View();
    Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), camera);
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    ClearBackground(BLACK);
    BeginMode2D(camera);
    int result = onList ? DrawList(font, mouse, click) : DrawChooser(font, mouse, click, dt);
    EndMode2D();

    if (flash > 0)          // white flash when a card is picked
    {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(WHITE, flash * 0.8f));
        flash -= dt * 2.2f;
    }
    return result;
}
