// world.c - the memory grid Bitty lives in, and Bitty's animations.
// Implements the World part of game_api.h.

#include "game_api.h"
#include "raylib.h"

#include <math.h>
#include <string.h>

#define MOVE_SECONDS  0.35f
#define WRITE_SECONDS 0.30f

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void World_Init(World *w, int cols, int rows)
{
    memset(w, 0, sizeof *w);
    w->cols  = cols > WORLD_MAX_COLS ? WORLD_MAX_COLS : cols;
    w->rows  = rows > WORLD_MAX_ROWS ? WORLD_MAX_ROWS : rows;
    w->speed = 1.0f;
    w->state = WORLD_RUNNING;
    w->mood  = MOOD_IDLE;
}

// Map letters:  # wall   . empty   E exit   x void   B Bitty's start   0-9 data tile with that value
void World_LoadMap(World *w, const char *const *rows, int rowCount)
{
    int cols = 0;
    for (int r = 0; r < rowCount; r++)
    {
        int len = (int)strlen(rows[r]);
        if (len > cols) cols = len;
    }
    float speed = w->speed > 0 ? w->speed : 1.0f;
    World_Init(w, cols, rowCount);
    w->speed = speed;

    for (int r = 0; r < w->rows; r++)
    {
        int len = (int)strlen(rows[r]);
        for (int c = 0; c < w->cols; c++)
        {
            char ch = c < len ? rows[r][c] : 'x';
            Tile *t = &w->tiles[r][c];
            t->value = 0;

            if (ch == '#')                  t->type = TILE_WALL;
            else if (ch == 'E')             t->type = TILE_EXIT;
            else if (ch == 'x' || ch == ' ') t->type = TILE_VOID;
            else if (ch >= '0' && ch <= '9') { t->type = TILE_DATA; t->value = ch - '0'; }
            else                            t->type = TILE_EMPTY;

            if (ch == 'B')
            {
                w->bittyX = c;
                w->bittyY = r;
            }
        }
    }
    w->drawX = w->fromX = (float)w->bittyX;
    w->drawY = w->fromY = (float)w->bittyY;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
static bool InGrid(const World *w, int x, int y)
{
    return x >= 0 && y >= 0 && x < w->cols && y < w->rows;
}

static void Step(Direction d, int *dx, int *dy)
{
    *dx = (d == EAST) - (d == WEST);
    *dy = (d == SOUTH) - (d == NORTH);
}

int World_Read(const World *w)  { return w->tiles[w->bittyY][w->bittyX].value; }
int World_GetX(const World *w)  { return w->bittyX; }
int World_GetY(const World *w)  { return w->bittyY; }

int World_Scan(const World *w, Direction d)
{
    int dx, dy;
    Step(d, &dx, &dy);
    int x = w->bittyX + dx, y = w->bittyY + dy;
    if (!InGrid(w, x, y)) return TILE_VOID;
    return w->tiles[y][x].type;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
bool World_StartAction(World *w, Action a)
{
    if (w->current.type != ACT_NONE || w->state != WORLD_RUNNING) return false;

    w->current = a;
    float speed = w->speed > 0 ? w->speed : 1.0f;

    if (a.type == ACT_MOVE)
    {
        int dx, dy;
        Step(a.dir, &dx, &dy);
        int nx = w->bittyX + dx, ny = w->bittyY + dy;

        w->fromX = (float)w->bittyX;
        w->fromY = (float)w->bittyY;

        bool blocked = InGrid(w, nx, ny) && w->tiles[ny][nx].type == TILE_WALL;
        if (blocked)
        {
            w->lastResult = 0;          // bumped: stays put
            w->shake = 0.3f;
        }
        else
        {
            w->lastResult = 1;
            w->bittyX = nx;             // may be outside the grid = void
            w->bittyY = ny;
        }
        w->actionLength = MOVE_SECONDS / speed;
    }
    else if (a.type == ACT_WRITE)
    {
        Tile *t = &w->tiles[w->bittyY][w->bittyX];
        if (t->type == TILE_EMPTY) t->type = TILE_DATA;
        if (t->type == TILE_DATA)  t->value = a.value;
        w->lastResult = 1;
        w->flash = 0.4f;
        w->actionLength = WRITE_SECONDS / speed;
    }

    w->actionTimer = w->actionLength;
    return true;
}

bool World_ActionDone(const World *w)
{
    return w->current.type == ACT_NONE;
}

static float EaseInOut(float t)
{
    return t < 0.5f ? 2 * t * t : 1 - powf(-2 * t + 2, 2) / 2;
}

void World_Update(World *w, float dt)
{
    if (w->shake  > 0) w->shake  -= dt;
    if (w->flash  > 0) w->flash  -= dt;
    if (w->glitch > 0) w->glitch -= dt;

    if (w->current.type == ACT_NONE) return;

    w->actionTimer -= dt;
    float p = 1.0f - (w->actionTimer / w->actionLength);
    if (p > 1) p = 1;

    if (w->current.type == ACT_MOVE)
    {
        float e = EaseInOut(p);
        if (w->lastResult)
        {
            w->drawX = w->fromX + (w->bittyX - w->fromX) * e;
            w->drawY = w->fromY + (w->bittyY - w->fromY) * e;
        }
        else
        {
            // Bump: lean a little towards the wall and back
            int dx, dy;
            Step(w->current.dir, &dx, &dy);
            float lean = sinf(p * 3.14159f) * 0.2f;
            w->drawX = w->fromX + dx * lean;
            w->drawY = w->fromY + dy * lean;
        }
    }

    if (w->actionTimer <= 0)
    {
        w->drawX = (float)w->bittyX;
        w->drawY = (float)w->bittyY;
        w->current.type = ACT_NONE;

        // Where did Bitty end up?
        if (!InGrid(w, w->bittyX, w->bittyY) || w->tiles[w->bittyY][w->bittyX].type == TILE_VOID)
        {
            w->state  = WORLD_SEGFAULT;
            w->glitch = 1.5f;
            w->mood   = MOOD_ERROR;
        }
        else if (w->tiles[w->bittyY][w->bittyX].type == TILE_EXIT)
        {
            w->state = WORLD_WON;
            w->mood  = MOOD_HAPPY;
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static const Color COL_EMPTY   = { 30, 36, 54, 255 };
static const Color COL_WALL    = { 62, 70, 98, 255 };
static const Color COL_WALL_HI = { 88, 98, 134, 255 };
static const Color COL_DATA    = { 38, 58, 92, 255 };
static const Color COL_EXIT    = { 40, 160, 90, 255 };
static const Color COL_VOID    = { 8, 8, 12, 255 };
static const Color COL_ADDR    = { 110, 120, 150, 255 };

static void DrawTile(const World *w, int c, int r, float t)
{
    float s = w->tileSize;
    Rectangle rect = { w->originX + c * s + 1, w->originY + r * s + 1, s - 2, s - 2 };
    const Tile *tile = &w->tiles[r][c];
    int addr = r * w->cols + c;

    switch (tile->type)
    {
        case TILE_WALL:
            DrawRectangleRec(rect, COL_WALL);
            DrawRectangle((int)rect.x, (int)rect.y, (int)rect.width, 4, COL_WALL_HI);
            DrawRectangleLinesEx(rect, 1, (Color){ 46, 52, 74, 255 });
            return;   // protected memory: no address shown

        case TILE_VOID:
        {
            DrawRectangleRec(rect, COL_VOID);
            // static noise
            for (int i = 0; i < 6; i++)
            {
                int seed = (addr * 7 + i * 13 + (int)(t * 12)) % 97;
                float nx = rect.x + (seed * 37 % 100) / 100.0f * (rect.width - 4);
                float ny = rect.y + (seed * 61 % 100) / 100.0f * (rect.height - 2);
                DrawRectangle((int)nx, (int)ny, 4, 1, (Color){ 70, 30, 60, 255 });
            }
            DrawText("??", (int)(rect.x + 4), (int)(rect.y + 3), 10, (Color){ 90, 40, 70, 255 });
            return;
        }

        case TILE_EXIT:
        {
            float pulse = 0.5f + 0.5f * sinf(t * 3.0f);
            DrawRectangleRec(rect, COL_EXIT);
            DrawRectangleLinesEx(rect, 2 + pulse * 2, (Color){ 150, 255, 180, (unsigned char)(120 + pulse * 120) });
            int fw = MeasureText("EXIT", 12);
            DrawText("EXIT", (int)(rect.x + (rect.width - fw) / 2), (int)(rect.y + rect.height / 2 - 6), 12, WHITE);
            break;
        }

        case TILE_DATA:
        {
            DrawRectangleRec(rect, COL_DATA);
            DrawRectangleLinesEx(rect, 1, (Color){ 80, 130, 200, 255 });
            const char *v = TextFormat("%d", tile->value);
            int fw = MeasureText(v, 20);
            DrawText(v, (int)(rect.x + (rect.width - fw) / 2), (int)(rect.y + rect.height / 2 - 8), 20,
                     (Color){ 150, 200, 255, 255 });
            break;
        }

        default:
            DrawRectangleRec(rect, COL_EMPTY);
            break;
    }

    DrawText(TextFormat("%02X", addr), (int)rect.x + 3, (int)rect.y + 3, 10, COL_ADDR);
}

static void DrawBitty(const World *w, float t)
{
    float s = w->tileSize;
    float x = w->originX + w->drawX * s;
    float y = w->originY + w->drawY * s;

    if (w->shake > 0) x += sinf(t * 80) * 3 * (w->shake / 0.3f);
    float bob = (w->mood == MOOD_HAPPY) ? -fabsf(sinf(t * 8)) * 6 : sinf(t * 3) * 2;

    float pad = s * 0.15f;
    Rectangle body = { x + pad, y + pad + bob, s - pad * 2, s - pad * 2 };

    // shadow
    DrawEllipse((int)(x + s / 2), (int)(y + s - pad + 2), (int)(s * 0.3f), 4, (Color){ 0, 0, 0, 90 });

    DrawRectangleRounded(body, 0.3f, 6, (Color){ 210, 215, 225, 255 });
    DrawRectangleRoundedLinesEx(body, 0.3f, 6, 2, DARKGRAY);

    // antenna
    float ax = body.x + body.width / 2;
    DrawLineEx((Vector2){ ax, body.y }, (Vector2){ ax, body.y - s * 0.12f }, 2, DARKGRAY);

    Color eyeCol = { 80, 180, 255, 255 };
    if (w->mood == MOOD_RUNNING) eyeCol = (Color){ 90, 230, 120, 255 };
    if (w->mood == MOOD_ERROR)   eyeCol = (Color){ 255, 70, 70, 255 };
    if (w->mood == MOOD_HAPPY)   eyeCol = (Color){ 255, 220, 90, 255 };
    DrawCircleV((Vector2){ ax, body.y - s * 0.12f }, 3, eyeCol);

    // the eye, which blinks every few seconds
    float blink = fmodf(t, 4.0f) < 0.12f ? 1.0f : s * 0.11f;
    Vector2 eye = { ax, body.y + body.height / 2 };
    DrawCircleV(eye, s * 0.17f, (Color){ 30, 40, 60, 255 });
    DrawEllipse((int)eye.x, (int)eye.y, s * 0.11f, blink, eyeCol);
}

void World_Draw(const World *w)
{
    float t = (float)GetTime();

    for (int r = 0; r < w->rows; r++)
        for (int c = 0; c < w->cols; c++)
            DrawTile(w, c, r, t);

    // write flash
    if (w->flash > 0)
    {
        float s = w->tileSize;
        DrawRectangle((int)(w->originX + w->bittyX * s), (int)(w->originY + w->bittyY * s), (int)s, (int)s,
                      (Color){ 150, 200, 255, (unsigned char)(w->flash / 0.4f * 160) });
    }

    // Bitty disappears into the void on a segfault
    if (w->state != WORLD_SEGFAULT || w->glitch > 0.9f) DrawBitty(w, t);

    if (w->glitch > 0)
    {
        float s = w->tileSize;
        float gx = w->originX + w->drawX * s, gy = w->originY + w->drawY * s;
        for (int i = 0; i < 8; i++)
        {
            int off = (int)(sinf(t * 50 + i * 7) * s * 0.4f);
            DrawRectangle((int)gx + off, (int)(gy + i * s / 8), (int)(s * 0.6f), 3,
                          (i % 2) ? (Color){ 255, 60, 90, 200 } : (Color){ 60, 220, 255, 200 });
        }
    }
}
