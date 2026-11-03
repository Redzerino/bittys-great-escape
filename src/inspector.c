// inspector.c - draws every visible variable as boxes, the way memory really looks.
//
//   x      int      [ 5 ]
//   arr    int[4]   [ 5 | 6 | 7 | 8 ]
//                     0   1   2   3
//   p      int*     [ * ]---------------> arrow to arr[2]
//
// Values that were never set show as "?" in red: garbage.

#include "inspector.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BOX_W      34
#define BOX_H      24
#define MAX_CELLS  256

typedef struct {
    int       address;
    Rectangle rect;
} CellRect;

static CellRect cellRects[MAX_CELLS];
static int cellCount;

static void Remember(int address, Rectangle r)
{
    if (cellCount < MAX_CELLS) cellRects[cellCount++] = (CellRect){ address, r };
}

static const Rectangle *Find(int address)
{
    for (int i = 0; i < cellCount; i++) if (cellRects[i].address == address) return &cellRects[i].rect;
    return NULL;
}

static const char *ValueText(const VmVariable *v, int i)
{
    if (!v->initialized[i]) return "?";
    int value = v->values[i];
    if (v->isChar)
    {
        if (value == 0)  return "\\0";
        if (value == 10) return "\\n";
        if (value >= 32 && value < 127) return TextFormat("'%c'", value);
    }
    if (strcmp(v->type, "bool") == 0) return value ? "true" : "false";
    return TextFormat("%d", value);
}

static void DrawBox(Rectangle r, const char *text, bool initialized, Font font, Color border)
{
    DrawRectangleRec(r, initialized ? (Color){ 34, 44, 70, 255 } : (Color){ 60, 24, 32, 255 });
    DrawRectangleLinesEx(r, 1, border);
    float size = 15;
    Vector2 m = MeasureTextEx(font, text, size, 0);
    if (m.x > r.width - 4) { size = 12; m = MeasureTextEx(font, text, size, 0); }
    DrawTextEx(font, text, (Vector2){ r.x + (r.width - m.x) / 2, r.y + (r.height - m.y) / 2 }, size, 0,
               initialized ? (Color){ 230, 235, 245, 255 } : (Color){ 255, 120, 120, 255 });
}

// Curved arrow, vertical at both ends. 'up' = the target is above the pointer.
static void DrawArrow(Vector2 from, Vector2 to, bool up, Color col)
{
    float bend = up ? -30.0f : 30.0f;
    Vector2 c1 = { from.x, from.y + bend };
    Vector2 c2 = { to.x, to.y - bend };
    Vector2 prev = from;
    for (int i = 1; i <= 20; i++)
    {
        float t = i / 20.0f, u = 1 - t;
        Vector2 p = {
            u * u * u * from.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * to.x,
            u * u * u * from.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * to.y,
        };
        DrawLineEx(prev, p, 2, col);
        prev = p;
    }
    if (up) DrawTriangle(to, (Vector2){ to.x - 5, to.y + 8 }, (Vector2){ to.x + 5, to.y + 8 }, col);
    else    DrawTriangle((Vector2){ to.x - 5, to.y - 8 }, (Vector2){ to.x + 5, to.y - 8 }, to, col);
}

void Inspector_Draw(const VmVariable *vars, int count, Rectangle b, Font font, int currentLine)
{
    DrawRectangleRec(b, (Color){ 20, 24, 36, 255 });
    DrawRectangleLinesEx(b, 2, (Color){ 60, 200, 255, 255 });
    DrawText("INSPECTOR", (int)b.x + 10, (int)b.y + 8, 18, (Color){ 60, 200, 255, 255 });
    if (currentLine > 0)
        DrawTextEx(font, TextFormat("line %d", currentLine), (Vector2){ b.x + b.width - 70, b.y + 9 }, 15, 0,
                   (Color){ 255, 220, 90, 255 });

    if (count == 0)
    {
        DrawTextEx(font, "Variables appear here\nwhile your code runs.", (Vector2){ b.x + 12, b.y + 40 }, 16, 0,
                   (Color){ 100, 110, 135, 255 });
        return;
    }

    cellCount = 0;
    float x0 = b.x + 12;
    float y = b.y + 38;
    float right = b.x + b.width - 12;

    BeginScissorMode((int)b.x + 2, (int)b.y + 30, (int)b.width - 4, (int)b.height - 32);

    // Pointer boxes to draw arrows from, once every target has a position
    Vector2 arrowFrom[64];
    int     arrowTo[64];
    int     arrowCount = 0;

    for (int i = 0; i < count && y < b.y + b.height; i++)
    {
        const VmVariable *v = &vars[i];
        Color nameCol = v->isGlobal ? (Color){ 180, 160, 255, 255 } : (Color){ 230, 235, 245, 255 };

        DrawTextEx(font, v->name, (Vector2){ x0, y }, 17, 0, nameCol);
        float nameW = MeasureTextEx(font, v->name, 17, 0).x;
        DrawTextEx(font, v->type, (Vector2){ x0 + nameW + 8, y + 2 }, 14, 0, (Color){ 100, 180, 255, 255 });
        y += 20;

        if (v->isArray)
        {
            int cols = v->cols > 0 ? v->cols : v->count;
            int rows = v->cols > 0 ? v->rows : 1;
            int perRow = (int)((right - x0) / BOX_W);
            if (perRow < 1) perRow = 1;

            for (int r = 0; r < rows; r++)
            {
                for (int c = 0; c < cols; c++)
                {
                    int idx = r * cols + c;
                    if (idx >= VM_VAR_MAX_VALUES) break;
                    int wrapRow = c / perRow, wrapCol = c % perRow;
                    Rectangle box = { x0 + wrapCol * BOX_W, y + wrapRow * (BOX_H + 14), BOX_W - 2, BOX_H };
                    DrawBox(box, ValueText(v, idx), v->initialized[idx], font, (Color){ 70, 110, 170, 255 });
                    const char *label = (v->cols > 0) ? TextFormat("%d,%d", r, c) : TextFormat("%d", c);
                    DrawTextEx(font, label, (Vector2){ box.x + 2, box.y + BOX_H + 1 }, 11, 0, (Color){ 100, 110, 135, 255 });
                    Remember(v->address + idx, box);
                }
                int wraps = (cols + perRow - 1) / perRow;
                y += wraps * (BOX_H + 14);
            }
            if (v->count > VM_VAR_MAX_VALUES)
            {
                DrawTextEx(font, TextFormat("... %d more", v->count - VM_VAR_MAX_VALUES), (Vector2){ x0, y }, 13, 0, GRAY);
                y += 16;
            }
        }
        else if (v->isPointer || v->isRef)
        {
            Rectangle box = { x0, y, 40, BOX_H };
            bool ok = v->isRef || v->initialized[0] || v->pointsTo > 0;
            DrawBox(box, ok ? "*" : "?", ok, font, (Color){ 255, 170, 90, 255 });

            const char *label = v->isRef ? TextFormat("-> %s = %s", v->pointsToName, ValueText(v, 0))
                                         : TextFormat("-> %s", v->pointsToName);
            Color lc = (strcmp(v->pointsToName, "NULL") == 0 || v->pointsToName[0] == '(')
                     ? (Color){ 255, 120, 120, 255 } : (Color){ 255, 190, 120, 255 };
            DrawTextEx(font, label, (Vector2){ box.x + 48, box.y + 4 }, 15, 0, lc);

            if (arrowCount < 64 && v->pointsTo > 0)
            {
                arrowFrom[arrowCount] = (Vector2){ box.x + box.width / 2, box.y + box.height / 2 };
                arrowTo[arrowCount] = v->pointsTo;
                arrowCount++;
            }
            y += BOX_H + 10;
        }
        else
        {
            Rectangle box = { x0, y, 64, BOX_H };
            DrawBox(box, ValueText(v, 0), v->initialized[0], font, (Color){ 70, 110, 170, 255 });
            if (!v->initialized[0])
                DrawTextEx(font, "garbage!", (Vector2){ box.x + 72, box.y + 4 }, 14, 0, (Color){ 255, 120, 120, 255 });
            Remember(v->address, box);
            y += BOX_H + 10;
        }
        y += 6;
    }

    // Arrows from pointers to whatever they point at (if it's on screen)
    for (int i = 0; i < arrowCount; i++)
    {
        const Rectangle *target = Find(arrowTo[i]);
        if (!target) continue;

        Vector2 from = arrowFrom[i];
        bool up = target->y < from.y;
        Vector2 to;
        if (up) { from.y -= BOX_H / 2; to = (Vector2){ target->x + target->width / 2, target->y + target->height + 13 }; }
        else    { from.y += BOX_H / 2; to = (Vector2){ target->x + target->width / 2, target->y }; }
        DrawArrow(from, to, up, (Color){ 255, 170, 90, 220 });
        DrawRectangleLinesEx(*target, 2, (Color){ 255, 170, 90, 255 });
    }

    EndScissorMode();
}
