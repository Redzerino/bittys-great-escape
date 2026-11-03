// Bitty's Great Escape - starter skeleton
// Opens the window and draws the three main panels plus Bitty on a grid.

#include "raylib.h"
#include <math.h>

#define SCREEN_W 1280
#define SCREEN_H 720

#define GRID_COLS 8
#define GRID_ROWS 6
#define TILE 56

// Panel layout
static const Rectangle EDITOR_PANEL   = { 10,  50, 400, 660 };
static const Rectangle WORLD_PANEL    = { 420, 50, 520, 460 };
static const Rectangle CONSOLE_PANEL  = { 420, 520, 520, 190 };
static const Rectangle INSPECT_PANEL  = { 950, 50, 320, 660 };

static void DrawPanel(Rectangle r, const char *title)
{
    DrawRectangleRec(r, (Color){ 20, 24, 36, 255 });
    DrawRectangleLinesEx(r, 2, (Color){ 60, 200, 255, 255 });
    DrawText(title, (int)r.x + 10, (int)r.y + 8, 18, (Color){ 60, 200, 255, 255 });
}

static void DrawBitty(Vector2 pos, float time)
{
    // Body: small rounded square with a glowing eye
    float bob = sinf(time * 3.0f) * 2.0f;
    Rectangle body = { pos.x + 8, pos.y + 8 + bob, TILE - 16, TILE - 16 };
    DrawRectangleRounded(body, 0.3f, 6, (Color){ 210, 215, 225, 255 });
    DrawRectangleRoundedLinesEx(body, 0.3f, 6, 2, DARKGRAY);

    // Eye blinks every few seconds
    float blink = fmodf(time, 4.0f) < 0.12f ? 1.0f : 6.0f;
    Vector2 eye = { pos.x + TILE / 2.0f, pos.y + TILE / 2.0f + bob };
    DrawCircleV(eye, 9, (Color){ 30, 40, 60, 255 });
    DrawEllipse((int)eye.x, (int)eye.y, 6, blink, (Color){ 80, 180, 255, 255 });
}

int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "Bitty's Great Escape");
    SetTargetFPS(60);

    int bittyCol = 1, bittyRow = 2;
    Vector2 gridOrigin = { WORLD_PANEL.x + 36, WORLD_PANEL.y + 50 };

    while (!WindowShouldClose())
    {
        float t = (float)GetTime();

        BeginDrawing();
        ClearBackground((Color){ 10, 12, 20, 255 });

        DrawText("BITTY'S GREAT ESCAPE", 10, 12, 26, (Color){ 120, 255, 160, 255 });

        DrawPanel(EDITOR_PANEL,  "CODE");
        DrawPanel(WORLD_PANEL,   "MEMORY");
        DrawPanel(CONSOLE_PANEL, "CONSOLE");
        DrawPanel(INSPECT_PANEL, "INSPECTOR");

        // Memory grid: each tile shows its address
        for (int r = 0; r < GRID_ROWS; r++)
        {
            for (int c = 0; c < GRID_COLS; c++)
            {
                int x = (int)gridOrigin.x + c * TILE;
                int y = (int)gridOrigin.y + r * TILE;
                DrawRectangle(x, y, TILE - 2, TILE - 2, (Color){ 30, 36, 54, 255 });
                DrawText(TextFormat("%02X", r * GRID_COLS + c), x + 4, y + 4, 10, GRAY);
            }
        }

        Vector2 bittyPos = { gridOrigin.x + bittyCol * TILE, gridOrigin.y + bittyRow * TILE };
        DrawBitty(bittyPos, t);

        DrawText("// your code goes here", (int)EDITOR_PANEL.x + 12, (int)EDITOR_PANEL.y + 40, 18, GRAY);
        DrawText("> Where... am I?", (int)CONSOLE_PANEL.x + 12, (int)CONSOLE_PANEL.y + 40, 18, RAYWHITE);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
