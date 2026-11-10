// world.c - the memory grid Bitty lives in, drawn in 3D, and Bitty itself.
// Implements the World part of game_api.h.
//
// The scene is drawn into a texture the size of the Memory panel, then that
// texture is drawn on screen, so the 3D never spills into the other panels.
//
// Coordinates: tile (col, row) sits at x = col, z = row (shifted so the grid
// is centred on 0,0). The floor's top is y = 0. One tile = 1 unit.
// The camera looks from the south (+z) and above, so Bitty's face (+z) faces us.

#include "game_api.h"
#include "world_internal.h"
#include "raylib.h"
#include "rlgl.h"
#include "customisation.h"

#include <math.h>
#include <string.h>

#define MOVE_SECONDS  0.40f
#define WRITE_SECONDS 0.35f
#define SCAN_SECONDS  0.42f
#define READ_SECONDS  0.36f
#define TEETER_SECONDS 0.35f   // Bitty wobbles on the edge before falling
#define SPAWN_SECONDS 0.75f
#define LAND_SECONDS  0.32f

// ===========================================================================
// Particles: dust, sparks and confetti. Purely decoration.
// ===========================================================================
#define MAX_PARTICLES 200

typedef struct {
    Vector3 pos, vel;
    float   life, maxLife;     // seconds left / seconds it started with
    float   size;
    float   gravity;           // pulls it down (0 = floats)
    Color   col;
} Particle;

static Particle particles[MAX_PARTICLES];

static void Particles_Clear(void)
{
    memset(particles, 0, sizeof particles);
}

static float Rand01(void) { return GetRandomValue(0, 1000) / 1000.0f; }
static float RandRange(float lo, float hi) { return lo + (hi - lo) * Rand01(); }

static void Particle_Spawn(Vector3 pos, Vector3 vel, float life, float size, float gravity, Color col)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (particles[i].life > 0) continue;
        particles[i] = (Particle){ pos, vel, life, life, size, gravity, col };
        return;
    }
}

static void Particles_Update(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &particles[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->vel.y -= p->gravity * dt;
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        p->pos.z += p->vel.z * dt;
        float slow = 1 - 2.2f * dt;            // air resistance
        if (slow < 0) slow = 0;
        p->vel.x *= slow;
        p->vel.z *= slow;
    }
}

// World position of a tile (defined further down with the drawing code)

// A ring of dust puffing outwards along the floor
static void Burst_Dust(const World *w, float col, float row, int count, float power)
{
    Vector3 at = TilePos(w, col, row, 0.04f);
    for (int i = 0; i < count; i++)
    {
        float a = (i + Rand01() * 0.6f) * 6.2832f / count;
        float v = power * RandRange(0.7f, 1.2f);
        Particle_Spawn((Vector3){ at.x + cosf(a) * 0.18f, at.y, at.z + sinf(a) * 0.14f },
                       (Vector3){ cosf(a) * v, RandRange(0.15f, 0.45f), sinf(a) * v * 0.8f },
                       RandRange(0.3f, 0.5f), RandRange(0.22f, 0.34f), 0.2f, (Color){ 150, 165, 215, 110 });
    }
}

// Sparks flying off a point (bonks, gates, glitches)
void Burst_Sparks(Vector3 at, int count, float power, Color col)
{
    for (int i = 0; i < count; i++)
    {
        float a = Rand01() * 6.2832f, up = RandRange(0.4f, 1.0f);
        Particle_Spawn(at, (Vector3){ cosf(a) * power * RandRange(0.4f, 1), up * power * 1.4f, sinf(a) * power * RandRange(0.4f, 1) },
                       RandRange(0.35f, 0.7f), RandRange(0.1f, 0.2f), 3.5f, col);
    }
}

// Confetti for clearing a level
static void Burst_Confetti(Vector3 at)
{
    static const Color COLS[] = {
        { 255, 215, 120, 255 }, { 120, 255, 170, 255 }, { 120, 220, 255, 255 }, { 255, 140, 200, 255 }, { 200, 150, 255, 255 },
    };
    for (int i = 0; i < 46; i++)
    {
        float a = Rand01() * 6.2832f, v = RandRange(0.6f, 2.0f);
        Particle_Spawn((Vector3){ at.x, at.y + 0.3f, at.z },
                       (Vector3){ cosf(a) * v, RandRange(2.2f, 4.4f), sinf(a) * v },
                       RandRange(0.9f, 1.6f), RandRange(0.12f, 0.24f), 4.2f, COLS[i % 5]);
    }
}

// ===========================================================================
// Setup
// ===========================================================================
void World_Init(World *w, int cols, int rows)
{
    memset(w, 0, sizeof *w);
    w->cols  = cols > WORLD_MAX_COLS ? WORLD_MAX_COLS : cols;
    w->rows  = rows > WORLD_MAX_ROWS ? WORLD_MAX_ROWS : rows;
    w->speed = 1.0f;
    w->state = WORLD_RUNNING;
    w->mood  = MOOD_IDLE;
    w->doorCode = -1;
}

// Map letters:  # wall   . empty   E exit   x void   B Bitty's start   G gate   0-9 data tile with that value
void World_LoadMap(World *w, const char *const *rows, int rowCount)
{
    int cols = 0;
    for (int r = 0; r < rowCount; r++)
    {
        int len = (int)strlen(rows[r]);
        if (len > cols) cols = len;
    }
    float speed = w->speed > 0 ? w->speed : 1.0f;
    int theme = w->theme;
    float closeUp = w->closeUp;
    World_Init(w, cols, rowCount);
    w->speed = speed;
    w->theme = theme;
    w->closeUp = closeUp;

    for (int r = 0; r < w->rows; r++)
    {
        int len = (int)strlen(rows[r]);
        for (int c = 0; c < w->cols; c++)
        {
            char ch = c < len ? rows[r][c] : 'x';
            Tile *t = &w->tiles[r][c];
            t->value = 0;

            if (ch == '#')                   t->type = TILE_WALL;
            else if (ch == 'E')              t->type = TILE_EXIT;
            else if (ch == 'G')              t->type = TILE_GATE;
            else if (ch == 'x' || ch == ' ') t->type = TILE_VOID;
            else if (ch >= '0' && ch <= '9') { t->type = TILE_DATA; t->value = ch - '0'; }
            else                             t->type = TILE_EMPTY;

            if (ch == 'B')
            {
                w->bittyX = c;
                w->bittyY = r;
            }
            if (ch == 'b')                   // where Bitty arrives for his walk-in (see World_PlayIntro)
            {
                w->hasIntroStart = true;
                w->introX = c;
                w->introY = r;
            }
        }
    }
    w->drawX = w->fromX = (float)w->bittyX;
    w->drawY = w->fromY = (float)w->bittyY;
    Particles_Clear();
}

void World_PlayIntro(World *w)
{
    if (w->spawn <= 0 && w->hasIntroStart && w->current.type == ACT_NONE)
    {
        w->bittyX = w->introX;
        w->bittyY = w->introY;
        w->drawX = w->fromX = (float)w->bittyX;
        w->drawY = w->fromY = (float)w->bittyY;
    }
    w->spawn = SPAWN_SECONDS;
}

WorldEvent World_TakeEvent(World *w)
{
    WorldEvent e = w->event;
    w->event = EVENT_NONE;
    return e;
}

// ===========================================================================
// Queries
// ===========================================================================
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

// ===========================================================================
// Actions
// ===========================================================================
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

        // Turn a little towards where we're going (but keep the face towards the camera)
        if (a.dir == EAST)  w->facing = 35;
        if (a.dir == WEST)  w->facing = -35;
        if (a.dir == SOUTH) w->facing = 0;
        if (a.dir == NORTH) w->facing = 0;

        bool blocked = InGrid(w, nx, ny) &&
                       (w->tiles[ny][nx].type == TILE_WALL || w->tiles[ny][nx].type == TILE_GATE);
        if (blocked)
        {
            w->lastResult = 0;          // bumped: stays put
            w->impactDone = false;      // the wobble and sparks start when Bitty actually hits (World_Update)
        }
        else
        {
            w->lastResult = 1;
            if (speed < 6) Burst_Dust(w, (float)w->bittyX, (float)w->bittyY, 4, 0.5f);   // push-off
            w->bittyX = nx;             // may be outside the grid = void
            w->bittyY = ny;
        }
        w->land = 0;
        w->idleTime = 0;
        w->actionLength = MOVE_SECONDS / speed;
    }
    else if (a.type == ACT_WRITE)
    {
        Tile *t = &w->tiles[w->bittyY][w->bittyX];
        if (t->type == TILE_EMPTY) t->type = TILE_DATA;
        if (t->type == TILE_DATA)  t->value = a.value;
        w->lastResult = 1;
        w->flash = 0.5f;
        w->idleTime = 0;
        w->tilePress[w->bittyY][w->bittyX] = 1;                 // Bitty stamps the value into the tile
        if (speed < 6)
        {
            Vector3 at = TilePos(w, (float)w->bittyX, (float)w->bittyY, 0.15f);
            Burst_Sparks(at, 8, 0.9f, (Color){ 150, 210, 255, 255 });
        }
        w->actionLength = WRITE_SECONDS / speed;

        // Door codes: writing next to a gate tries the number on it
        if (w->doorCode >= 0)
        {
            static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };
            for (int i = 0; i < 4; i++)
            {
                int gx = w->bittyX + DX[i], gy = w->bittyY + DY[i];
                if (!InGrid(w, gx, gy) || w->tiles[gy][gx].type != TILE_GATE) continue;
                if (a.value == w->doorCode)
                {
                    World_OpenGate(w, gx, gy);
                    w->event = EVENT_DOOR_OPEN;
                }
                else
                {
                    w->shake = 0.3f;
                    w->event = EVENT_WRONG_CODE;
                    Burst_Sparks(TilePos(w, (float)gx, (float)gy, 0.4f), 10, 1.0f, (Color){ 255, 90, 70, 255 });
                }
            }
        }
    }

    else if (a.type == ACT_SCAN || a.type == ACT_READ)
    {
        // scan() and read() answer straight away, but Bitty takes a moment to look,
        // so you can see what he checked and what he found
        if (a.type == ACT_SCAN)
        {
            int dx, dy;
            Step(a.dir, &dx, &dy);
            w->lastResult = World_Scan(w, a.dir);
            w->scanX = w->bittyX + dx;
            w->scanY = w->bittyY + dy;
            if (a.dir == EAST) w->facing = 35;
            if (a.dir == WEST) w->facing = -35;
        }
        else
        {
            w->lastResult = World_Read(w);
            w->scanX = w->bittyX;
            w->scanY = w->bittyY;
        }
        w->scanResult = w->lastResult;
        w->scanWasRead = (a.type == ACT_READ);
        w->scanShow = 1.1f / (speed > 2 ? 2 : speed);
        w->idleTime = 0;
        w->actionLength = (a.type == ACT_SCAN ? SCAN_SECONDS : READ_SECONDS) / speed;
    }

    w->actionTimer = w->actionLength;
    return true;
}

// ===========================================================================
// Gates
// ===========================================================================
void World_OpenGate(World *w, int x, int y)
{
    if (!InGrid(w, x, y) || w->tiles[y][x].type != TILE_GATE) return;
    w->tiles[y][x].type = TILE_EMPTY;
    w->gateFlash  = 0.8f;
    w->gateFlashX = x;
    w->gateFlashY = y;
    Burst_Sparks(TilePos(w, (float)x, (float)y, 0.4f), 16, 1.3f, (Color){ 150, 255, 190, 255 });
}

bool World_NextGate(const World *w, int *x, int *y)
{
    for (int r = 0; r < w->rows; r++)
        for (int c = 0; c < w->cols; c++)
            if (w->tiles[r][c].type == TILE_GATE) { *x = c; *y = r; return true; }
    return false;
}

int World_CountGates(const World *w)
{
    int n = 0;
    for (int r = 0; r < w->rows; r++)
        for (int c = 0; c < w->cols; c++)
            if (w->tiles[r][c].type == TILE_GATE) n++;
    return n;
}

bool World_ActionDone(const World *w)
{
    return w->current.type == ACT_NONE;
}

float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
float Smooth(float v)  { v = Clamp01(v); return v * v * (3 - 2 * v); }

// How far through the current action we are, 0..1
static float ActionProgress(const World *w)
{
    if (w->current.type == ACT_NONE || w->actionLength <= 0) return 0;
    return Clamp01(1 - w->actionTimer / w->actionLength);
}

// A hop has three beats: crouch (stay put), fly, and settle.
// This turns action progress into how far along the jump Bitty is.
#define HOP_CROUCH 0.14f
static float HopTravel(float p)
{
    return Smooth((p - HOP_CROUCH) / (1 - HOP_CROUCH - 0.04f));
}

void World_Update(World *w, float dt)
{
    if (w->shake  > 0) w->shake  -= dt;
    if (w->flash  > 0) w->flash  -= dt;
    if (w->glitch > 0) w->glitch -= dt;
    if (w->gateFlash > 0) w->gateFlash -= dt;
    if (w->scanShow > 0) w->scanShow -= dt;
    if (w->land > 0) w->land -= dt;
    if (w->state == WORLD_WON) w->winTime += dt;
    w->idleTime += dt;
    Particles_Update(dt);

    // the camera eases in when Bitty talks, and back out afterwards
    w->closeUp += ((w->talking ? 1.0f : 0.0f) - w->closeUp) * (1 - expf(-dt * 3.5f));

    // Bitty turns smoothly towards where he's heading
    w->facingShown += (w->facing - w->facingShown) * (1 - expf(-dt * 14));

    // tiles spring back up, and the footprints fade to a faint trail
    for (int r = 0; r < w->rows; r++)
        for (int c = 0; c < w->cols; c++)
        {
            if (w->tilePress[r][c] > 0) w->tilePress[r][c] -= dt * 3.2f;
            if (w->tilePress[r][c] < 0) w->tilePress[r][c] = 0;
            if (w->tileTrail[r][c] > 0.3f) w->tileTrail[r][c] -= dt * 0.5f;
        }

    // dropping into the level
    if (w->spawn > 0)
    {
        w->spawn -= dt;
        if (w->spawn <= 0)
        {
            w->spawn = 0;
            w->land = w->landLength = LAND_SECONDS;
            if (InGrid(w, w->bittyX, w->bittyY)) w->tilePress[w->bittyY][w->bittyX] = 1;
            Burst_Dust(w, w->drawX, w->drawY, 10, 1.3f);
        }
    }

    Room_Update(w, dt);

    if (w->current.type == ACT_NONE) return;

    w->actionTimer -= dt;
    float p = 1.0f - (w->actionTimer / w->actionLength);
    if (p > 1) p = 1;

    if (w->current.type == ACT_MOVE)
    {
        if (w->lastResult)
        {
            float e = HopTravel(p);
            w->drawX = w->fromX + (w->bittyX - w->fromX) * e;
            w->drawY = w->fromY + (w->bittyY - w->fromY) * e;
        }
        else
        {
            // Bump: run at the wall, hit it, and bounce back with a wobble
            int dx, dy;
            Step(w->current.dir, &dx, &dy);
            float reach = 0.3f, lean;
            if (p < 0.4f) lean = reach * (p / 0.4f) * (p / 0.4f);
            else
            {
                float q = (p - 0.4f) / 0.6f;
                lean = reach * (1 - q) * (1 - q) * cosf(q * PI_F * 1.5f);
            }
            w->drawX = w->fromX + dx * lean;
            w->drawY = w->fromY + dy * lean;

            if (p >= 0.4f && !w->impactDone)
            {
                w->impactDone = true;
                w->shake = 0.35f;
                w->event = EVENT_BONK;
                Vector3 hit = TilePos(w, w->fromX + dx * 0.5f, w->fromY + dy * 0.5f, 0.45f);
                Burst_Sparks(hit, 9, 1.0f, (Color){ 255, 225, 140, 255 });
            }
        }
    }

    if (w->actionTimer <= 0)
    {
        bool moved = (w->current.type == ACT_MOVE && w->lastResult);
        float speed = w->speed > 0 ? w->speed : 1.0f;
        w->drawX = (float)w->bittyX;
        w->drawY = (float)w->bittyY;
        w->current.type = ACT_NONE;
        w->idleTime = 0;

        if (!InGrid(w, w->bittyX, w->bittyY) || w->tiles[w->bittyY][w->bittyX].type == TILE_VOID)
        {
            w->state  = WORLD_SEGFAULT;
            w->glitch = FALL_SECONDS;
            w->mood   = MOOD_ERROR;
            w->event  = EVENT_FALL;
            Burst_Sparks(TilePos(w, w->drawX, w->drawY, 0.4f), 18, 1.2f, (Color){ 255, 90, 150, 255 });
        }
        else
        {
            if (moved)
            {
                // landing: squash, a puff of dust, the tile dips and remembers the footstep
                w->land = w->landLength = LAND_SECONDS / (speed > 3 ? 3 : speed);
                w->tilePress[w->bittyY][w->bittyX] = 1;
                w->tileTrail[w->bittyY][w->bittyX] = 1;
                if (InGrid(w, (int)w->fromX, (int)w->fromY) && w->tileTrail[(int)w->fromY][(int)w->fromX] < 0.3f)
                    w->tileTrail[(int)w->fromY][(int)w->fromX] = 0.3f;      // the start tile joins the trail
                if (speed < 6) Burst_Dust(w, w->drawX, w->drawY, 7, 0.95f);
            }
            if (w->tiles[w->bittyY][w->bittyX].type == TILE_EXIT)
            {
                w->state   = WORLD_WON;
                w->mood    = MOOD_HAPPY;
                w->facing  = 0;             // turn to the camera to celebrate
                w->winTime = 0;
                w->event   = EVENT_WIN;
                Burst_Confetti(TilePos(w, w->drawX, w->drawY, 0.2f));
            }
        }
    }
}


// ===========================================================================
// LOOK & FEEL
//
// Inspired by games like Pode: the memory is dark, and Bitty is a little
// light walking through it. A small lighting shader makes tiles near Bitty
// glow warm while everything else falls into deep blue shadow. The exit
// portal and a few memory crystals give off their own light.
// ===========================================================================

// Indexed by the chip the level sits on in the Escape Circuit map (0 = bootcamp),
// so the colours match the map: the accent colour is the chip's colour.
static const Theme THEMES[] = {
    /* 0 Bootcamp         */ { {  6,  9, 18, 255 }, { 20, 32, 60, 255 }, { 150, 215, 255, 255 }, { 230, 240, 255, 255 }, { 0.16f, 0.18f, 0.27f } },
    /* 1 Boot Sector      */ { {  5, 12, 12, 255 }, { 14, 44, 40, 255 }, { 100, 245, 175, 255 }, { 110, 200, 255, 255 }, { 0.13f, 0.20f, 0.22f } },
    /* 2 Register Bank    */ { { 16, 11,  6, 255 }, { 54, 36, 18, 255 }, { 255, 198,  95, 255 }, { 255, 150,  90, 255 }, { 0.24f, 0.19f, 0.16f } },
    /* 3 CPU Control Unit */ { { 17,  7,  9, 255 }, { 58, 22, 28, 255 }, { 255, 135, 125, 255 }, { 255, 190, 140, 255 }, { 0.25f, 0.15f, 0.18f } },
    /* 4 Processing Core  */ { {  5, 10, 20, 255 }, { 16, 36, 68, 255 }, { 110, 195, 255, 255 }, { 120, 255, 230, 255 }, { 0.13f, 0.18f, 0.28f } },
    /* 5 System Library   */ { { 11,  7, 20, 255 }, { 38, 24, 66, 255 }, { 185, 150, 255, 255 }, { 255, 150, 220, 255 }, { 0.19f, 0.16f, 0.28f } },
    /* 6 RAM Banks        */ { {  4, 13, 15, 255 }, { 12, 46, 52, 255 }, { 100, 225, 220, 255 }, { 120, 170, 255, 255 }, { 0.12f, 0.21f, 0.24f } },
    /* 7 Data Bus         */ { { 16,  6, 13, 255 }, { 58, 20, 46, 255 }, { 245, 145, 210, 255 }, { 190, 150, 255, 255 }, { 0.24f, 0.15f, 0.22f } },
    /* 8 Address Network  */ { {  9, 14,  5, 255 }, { 32, 48, 16, 255 }, { 180, 235, 115, 255 }, { 255, 225, 120, 255 }, { 0.18f, 0.22f, 0.16f } },
    /* 9 USB Gateway      */ { {  8,  9, 14, 255 }, { 34, 38, 54, 255 }, { 235, 242, 255, 255 }, { 120, 220, 255, 255 }, { 0.20f, 0.21f, 0.27f } },
};

const Theme *ThemeOf(const World *w)
{
    int i = w->theme;
    if (i < 0 || i > 9) i = 1;
    return &THEMES[i];
}

Color Alpha(Color c, float a)
{
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    c.a = (unsigned char)a;
    return c;
}

#define MOTE_COUNT 26

static RenderTexture2D target;
static int       targetW = 0, targetH = 0;
static Shader    lightShader;
static bool      shaderReady = false, shaderOk = false;
static int       locViewPos, locAmbient, locLightCount, locLightPos, locLightColor, locLightRadius;
static int       locFogColor, locFogRange, locPulsePos, locPulseRadius, locPulseStrength;
static Vector3   camFollow = { 0, 0, 0 };   // where the camera is looking (eases towards Bitty)
static bool      camFollowSet = false;
static Texture2D glowTex;
static bool      glowReady = false;

// ---------------------------------------------------------------------------
// The lighting shader (GLSL 3.3, which raylib uses on Windows desktop)
// ---------------------------------------------------------------------------
static const char *LIGHT_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "out vec3 fragPos;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragPos = vertexPosition;\n"           // raylib's shape functions give world positions
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *LIGHT_FS =
    "#version 330\n"
    "in vec3 fragPos;\n"
    "in vec4 fragColor;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 viewPos;\n"
    "uniform vec3 ambient;\n"
    "uniform int lightCount;\n"
    "uniform vec3 lightPos[8];\n"
    "uniform vec3 lightColor[8];\n"
    "uniform float lightRadius[8];\n"
    "uniform vec3 fogColor;\n"
    "uniform vec2 fogRange;\n"
    "uniform vec3 pulsePos;\n"
    "uniform float pulseRadius;\n"
    "uniform float pulseStrength;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 base = fragColor.rgb * colDiffuse.rgb;\n"
    "    vec3 n = normalize(cross(dFdx(fragPos), dFdy(fragPos)));\n"   // flat normal per face
    "    if (dot(n, viewPos - fragPos) < 0.0) n = -n;\n"
    "    vec3 light = ambient + vec3(0.05, 0.06, 0.11) * max(n.y, 0.0);\n"
    "    for (int i = 0; i < 8; i++) {\n"
    "        if (i >= lightCount) break;\n"
    "        vec3 L = lightPos[i] - fragPos;\n"
    "        float d = length(L);\n"
    "        float att = clamp(1.0 - d / lightRadius[i], 0.0, 1.0);\n"
    "        att = att * att;\n"
    "        float diff = max(dot(n, L / max(d, 0.001)), 0.0) * 0.7 + 0.3;\n"
    "        light += lightColor[i] * att * diff;\n"
    "    }\n"
    "    if (fragPos.y > -0.3) {\n"                                     // beacon ripple across the floor
    "        float ring = length(fragPos.xz - pulsePos.xz) - pulseRadius;\n"
    "        light += vec3(0.35, 0.9, 0.65) * exp(-ring * ring * 6.0) * pulseStrength;\n"
    "    }\n"
    "    float pitFade = clamp(1.0 + fragPos.y * 0.45, 0.15, 1.0);\n"   // things lower down are darker
    "    vec3 col = base * light * pitFade;\n"
    "    float fog = smoothstep(fogRange.x, fogRange.y, length(viewPos - fragPos));\n"
    "    finalColor = vec4(mix(col, fogColor, fog), fragColor.a * colDiffuse.a);\n"
    "}\n";

static void InitGraphics(void)
{
    if (!shaderReady)
    {
        shaderReady = true;
        lightShader = LoadShaderFromMemory(LIGHT_VS, LIGHT_FS);
        shaderOk = lightShader.id != 0 && lightShader.id != rlGetShaderIdDefault();
        if (shaderOk)
        {
            locViewPos     = GetShaderLocation(lightShader, "viewPos");
            locAmbient     = GetShaderLocation(lightShader, "ambient");
            locLightCount  = GetShaderLocation(lightShader, "lightCount");
            locLightPos    = GetShaderLocation(lightShader, "lightPos");
            locLightColor  = GetShaderLocation(lightShader, "lightColor");
            locLightRadius = GetShaderLocation(lightShader, "lightRadius");
            locFogColor      = GetShaderLocation(lightShader, "fogColor");
            locFogRange      = GetShaderLocation(lightShader, "fogRange");
            locPulsePos      = GetShaderLocation(lightShader, "pulsePos");
            locPulseRadius   = GetShaderLocation(lightShader, "pulseRadius");
            locPulseStrength = GetShaderLocation(lightShader, "pulseStrength");
        }
    }
    if (!glowReady)
    {
        glowReady = true;
        Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
        glowTex = LoadTextureFromImage(img);
        SetTextureFilter(glowTex, TEXTURE_FILTER_BILINEAR);
        UnloadImage(img);
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
Vector3 TilePos(const World *w, float col, float row, float y)
{
    return (Vector3){ col - (w->cols - 1) / 2.0f, y, row - (w->rows - 1) / 2.0f };
}

static float Vector3Length3(Vector3 a, Vector3 b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static unsigned Hash(int a, int b)
{
    unsigned h = ((unsigned)a * 73856093u) ^ ((unsigned)b * 19349663u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

Color Mix(Color a, Color b, float t)
{
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}

// A squashed sphere
void DrawBlob(Vector3 pos, Vector3 scale, Color col)
{
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlScalef(scale.x, scale.y, scale.z);
    DrawSphereEx((Vector3){ 0, 0, 0 }, 1.0f, 14, 16, col);
    rlPopMatrix();
}

// A box with rounded edges and corners (Bitty's chunky stone body)
void DrawRoundedBox(Vector3 c, Vector3 size, float r, Color col)
{
    float hx = size.x / 2 - r, hy = size.y / 2 - r, hz = size.z / 2 - r;

    DrawCubeV(c, (Vector3){ size.x, size.y - 2 * r, size.z - 2 * r }, col);
    DrawCubeV(c, (Vector3){ size.x - 2 * r, size.y, size.z - 2 * r }, col);
    DrawCubeV(c, (Vector3){ size.x - 2 * r, size.y - 2 * r, size.z }, col);

    for (int i = 0; i < 8; i++)
    {
        float x = (i & 1) ? hx : -hx, y = (i & 2) ? hy : -hy, z = (i & 4) ? hz : -hz;
        DrawSphereEx((Vector3){ c.x + x, c.y + y, c.z + z }, r, 6, 8, col);
    }
    for (int i = 0; i < 4; i++)
    {
        float a = (i & 1) ? 1.0f : -1.0f, b = (i & 2) ? 1.0f : -1.0f;
        DrawCylinderEx((Vector3){ c.x - hx, c.y + a * hy, c.z + b * hz }, (Vector3){ c.x + hx, c.y + a * hy, c.z + b * hz }, r, r, 10, col);
        DrawCylinderEx((Vector3){ c.x + a * hx, c.y - hy, c.z + b * hz }, (Vector3){ c.x + a * hx, c.y + hy, c.z + b * hz }, r, r, 10, col);
        DrawCylinderEx((Vector3){ c.x + a * hx, c.y + b * hy, c.z - hz }, (Vector3){ c.x + a * hx, c.y + b * hy, c.z + hz }, r, r, 10, col);
    }
}

// A thin bar in Bitty's face plane, rotated around z (for ^ ^, x x and the smile)
static void DrawBar(Vector3 pos, float length, float thick, float angle, Color col)
{
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(angle, 0, 0, 1);
    DrawCube((Vector3){ 0, 0, 0 }, length, thick, 0.02f, col);
    rlPopMatrix();
}

// Fog (levels like 4.2, where you mustn't know how long the corridor is):
// how much of this tile Bitty can see, 0..1
static float TileSeen(const World *w, int c, int r)
{
    if (w->fogRadius <= 0) return 1;
    float dx = c - w->drawX, dz = r - w->drawY;
    float d = sqrtf(dx * dx + dz * dz);
    return Clamp01(1 - (d - w->fogRadius) / 0.8f);
}

static bool IsWalkable(const World *w, int c, int r)
{
    if (c < 0 || r < 0 || c >= w->cols || r >= w->rows) return false;
    TileType t = w->tiles[r][c].type;
    return t == TILE_EMPTY || t == TILE_DATA || t == TILE_EXIT;
}

// Walls next to the path sometimes grow a glowing memory crystal
static bool HasCrystal(const World *w, int c, int r)
{
    if (w->tiles[r][c].type != TILE_WALL) return false;
    bool nearPath = IsWalkable(w, c + 1, r) || IsWalkable(w, c - 1, r) ||
                    IsWalkable(w, c, r + 1) || IsWalkable(w, c, r - 1);
    return nearPath && Hash(c, r) % 6 == 0;
}

static Color CrystalColor(int c, int r)
{
    return (Hash(r, c) % 2) ? (Color){ 120, 230, 255, 255 } : (Color){ 190, 140, 255, 255 };
}

Color MoodColor(const World *w)
{
    switch (w->mood)
    {
        case MOOD_RUNNING: return (Color){ 120, 250, 170, 255 };
        case MOOD_ERROR:   return (Color){ 255, 90, 110, 255 };
        case MOOD_HAPPY:   return (Color){ 255, 215, 120, 255 };
        default:           return Customisation_Glow();
    }
}

// Where Bitty is right now (including the hop and the fall)
static float BittyHop(const World *w, float t)
{
    (void)t;
    if (w->spawn > 0)                                       // dropping in from above
    {
        float k = w->spawn / SPAWN_SECONDS;
        return k * k * 3.2f;
    }
    if (w->state == WORLD_WON)
    {
        if (w->winTime < 0.6f) return sinf(w->winTime / 0.6f * PI_F) * 0.8f;     // one big spinning jump
        return fabsf(sinf((w->winTime - 0.6f) * 6.5f)) * 0.26f;                  // then happy little hops
    }
    if (w->current.type == ACT_MOVE && w->lastResult)
        return sinf(HopTravel(ActionProgress(w)) * PI_F) * 0.4f;
    if (w->mood == MOOD_IDLE && w->idleTime > 2.0f)         // idle: a tiny bounce every few seconds
    {
        float phase = fmodf(w->idleTime, 6.0f);
        if (phase > 5.2f && phase < 5.6f) return sinf((phase - 5.2f) / 0.4f * PI_F) * 0.1f;
    }
    return 0;
}

// Segfault: Bitty teeters on the edge for a moment, then drops
static float BittyFall(const World *w)
{
    if (w->state != WORLD_SEGFAULT) return 0;
    float since = FALL_SECONDS - w->glitch;
    float f = (since - TEETER_SECONDS) / (FALL_SECONDS - TEETER_SECONDS);
    if (f < 0) f = 0;
    return f * f * 3.4f;
}

// How far the tile under a point has dipped (so Bitty and the tile move together)
static float TileDip(const World *w, int c, int r)
{
    if (c < 0 || r < 0 || c >= w->cols || r >= w->rows) return 0;
    float k = w->tilePress[r][c];
    return k * k * 0.07f;
}

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

static int GatherLights(const World *w, float t, Light *lights)
{
    int n = 0;

    // Bitty: a warm light, tinted by mood. It dims as Bitty falls into the void.
    Vector3 b = TilePos(w, w->drawX, w->drawY, 0.85f + BittyHop(w, t) - BittyFall(w));
    float fallDim = 1.0f - BittyFall(w) / 2.0f;
    if (fallDim < 0) fallDim = 0;
    Color cool = { 200, 225, 255, 255 };
    lights[n++] = (Light){ b, Mix(cool, MoodColor(w), 0.45f), (1.9f + sinf(t * 2.2f) * 0.08f) * fallDim, 4.6f };

    // Exit portals
    for (int r = 0; r < w->rows && n < MAX_LIGHTS; r++)
        for (int c = 0; c < w->cols && n < MAX_LIGHTS; c++)
            if (w->tiles[r][c].type == TILE_EXIT && TileSeen(w, c, r) > 0.3f)
                lights[n++] = (Light){ TilePos(w, (float)c, (float)r, 0.6f), (Color){ 110, 255, 170, 255 },
                                       1.2f + sinf(t * 3) * 0.2f, 2.6f };

    // Gates
    for (int r = 0; r < w->rows && n < MAX_LIGHTS; r++)
        for (int c = 0; c < w->cols && n < MAX_LIGHTS; c++)
            if (w->tiles[r][c].type == TILE_GATE && TileSeen(w, c, r) > 0.3f)
                lights[n++] = (Light){ TilePos(w, (float)c, (float)r, 0.5f), (Color){ 255, 140, 60, 255 },
                                       0.8f + sinf(t * 9 + c) * 0.08f, 1.6f };

    // Crystals
    for (int r = 0; r < w->rows && n < MAX_LIGHTS; r++)
        for (int c = 0; c < w->cols && n < MAX_LIGHTS; c++)
            if (HasCrystal(w, c, r) && TileSeen(w, c, r) > 0.3f)
                lights[n++] = (Light){ TilePos(w, (float)c, (float)r, 0.8f), CrystalColor(c, r),
                                       0.7f + sinf(t * 1.5f + c) * 0.1f, 2.0f };
    return n;
}

#define PULSE_PERIOD 5.0f

// The beacon: a ring of light that ripples out from the exit every few seconds
static bool ExitPosition(const World *w, Vector3 *out)
{
    for (int r = 0; r < w->rows; r++)
        for (int c = 0; c < w->cols; c++)
            if (w->tiles[r][c].type == TILE_EXIT) { *out = TilePos(w, (float)c, (float)r, 0); return true; }
    return false;
}

static void ApplyLights(const World *w, const Light *lights, int count, Camera3D cam, float t, bool exitPulse)
{
    float pos[MAX_LIGHTS * 3], col[MAX_LIGHTS * 3], rad[MAX_LIGHTS];
    for (int i = 0; i < count; i++)
    {
        pos[i * 3 + 0] = lights[i].pos.x;
        pos[i * 3 + 1] = lights[i].pos.y;
        pos[i * 3 + 2] = lights[i].pos.z;
        col[i * 3 + 0] = lights[i].color.r / 255.0f * lights[i].strength;
        col[i * 3 + 1] = lights[i].color.g / 255.0f * lights[i].strength;
        col[i * 3 + 2] = lights[i].color.b / 255.0f * lights[i].strength;
        rad[i] = lights[i].radius;
    }
    const Theme *th = ThemeOf(w);
    float ambient[3] = { th->ambient[0], th->ambient[1], th->ambient[2] };   // the colour of the shadows
    float view[3]    = { cam.position.x, cam.position.y, cam.position.z };

    SetShaderValue(lightShader, locViewPos, view, SHADER_UNIFORM_VEC3);
    SetShaderValue(lightShader, locAmbient, ambient, SHADER_UNIFORM_VEC3);
    SetShaderValue(lightShader, locLightCount, &count, SHADER_UNIFORM_INT);
    SetShaderValueV(lightShader, locLightPos, pos, SHADER_UNIFORM_VEC3, count);
    SetShaderValueV(lightShader, locLightColor, col, SHADER_UNIFORM_VEC3, count);
    SetShaderValueV(lightShader, locLightRadius, rad, SHADER_UNIFORM_FLOAT, count);

    float camDist = Vector3Length3(cam.position, cam.target);
    float fogColor[3] = { th->fog.r / 255.0f, th->fog.g / 255.0f, th->fog.b / 255.0f };
    float fogRange[2] = { camDist * 0.95f, camDist * 2.4f };
    SetShaderValue(lightShader, locFogColor, fogColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(lightShader, locFogRange, fogRange, SHADER_UNIFORM_VEC2);

    Vector3 exitPos = { 0, 0, 0 };
    float strength = 0, radius = 0;
    if (exitPulse && ExitPosition(w, &exitPos))
    {
        float k = fmodf(t, PULSE_PERIOD) / PULSE_PERIOD;     // 0 -> 1
        radius = k * 14.0f;
        strength = (1 - k) * (1 - k) * 0.9f;
    }
    float pulsePos[3] = { exitPos.x, exitPos.y, exitPos.z };
    SetShaderValue(lightShader, locPulsePos, pulsePos, SHADER_UNIFORM_VEC3);
    SetShaderValue(lightShader, locPulseRadius, &radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lightShader, locPulseStrength, &strength, SHADER_UNIFORM_FLOAT);
}

// ---------------------------------------------------------------------------
// Tiles (drawn with the lighting shader)
// ---------------------------------------------------------------------------
static const Color COL_FLOOR  = { 120, 160, 210, 255 };
static const Color COL_FLOOR2 = { 110, 148, 200, 255 };
static const Color COL_WALL   = { 66, 58, 104, 255 };
static const Color COL_DATA   = { 80, 140, 230, 255 };
static const Color COL_EXIT   = { 90, 230, 150, 255 };

// A soft-edged block: a body plus a slightly smaller cap, so the top edge looks rounded
void DrawStone(Vector3 pos, Vector3 size, Color col)
{
    DrawCubeV(pos, size, col);
    Vector3 cap = { pos.x, pos.y + size.y / 2 + 0.025f, pos.z };
    DrawCubeV(cap, (Vector3){ size.x - 0.08f, 0.05f, size.z - 0.08f }, col);
}

static void DrawTiles(const World *w)
{
    for (int r = 0; r < w->rows; r++)
    {
        for (int c = 0; c < w->cols; c++)
        {
            const Tile *tile = &w->tiles[r][c];
            if (TileSeen(w, c, r) < 0.3f) continue;         // hidden in the fog
            Vector3 p = TilePos(w, (float)c, (float)r, 0);
            unsigned h = Hash(c, r);
            float jitter = (h % 7) * 0.008f;   // tiny height differences feel hand-made
            jitter -= TileDip(w, c, r);        // the tile sinks a little when Bitty lands on it

            // Every solid tile sits on a column, so the board is a floating island
            // (void tiles are holes straight down into the abyss)
            if (tile->type != TILE_VOID)
            {
                float depth = 1.2f + (h % 9) * 0.25f;
                DrawCubeV((Vector3){ p.x, -0.22f - depth / 2, p.z }, (Vector3){ 0.94f, depth, 0.94f },
                          (Color){ 70, 66, 112, 255 });
            }

            switch (tile->type)
            {
                case TILE_WALL:
                {
                    float height = 0.62f + (h % 5) * 0.05f;
                    DrawStone((Vector3){ p.x, height / 2 - 0.1f, p.z }, (Vector3){ 0.96f, height, 0.96f }, COL_WALL);
                    if (HasCrystal(w, c, r))
                    {
                        Color cc = CrystalColor(c, r);
                        float top = height - 0.05f;
                        DrawCylinderEx((Vector3){ p.x - 0.1f, top, p.z }, (Vector3){ p.x - 0.06f, top + 0.42f, p.z + 0.02f },
                                       0.07f, 0.0f, 5, cc);
                        DrawCylinderEx((Vector3){ p.x + 0.1f, top, p.z + 0.08f }, (Vector3){ p.x + 0.16f, top + 0.26f, p.z + 0.1f },
                                       0.05f, 0.0f, 5, cc);
                    }
                    break;
                }

                case TILE_VOID:
                    // a pit: just a far-away dark floor (the shader fades it to black)
                    DrawCubeV((Vector3){ p.x, -1.3f, p.z }, (Vector3){ 0.98f, 0.1f, 0.98f }, (Color){ 60, 40, 90, 255 });
                    break;

                case TILE_EXIT:
                    DrawStone((Vector3){ p.x, -0.12f - TileDip(w, c, r), p.z }, (Vector3){ 0.96f, 0.2f, 0.96f }, COL_EXIT);
                    break;

                case TILE_GATE:
                    // floor plus two posts; the energy barrier itself is drawn in the glow pass
                    DrawStone((Vector3){ p.x, -0.12f, p.z }, (Vector3){ 0.96f, 0.2f, 0.96f }, COL_FLOOR2);
                    DrawCubeV((Vector3){ p.x, 0.35f, p.z - 0.42f }, (Vector3){ 0.16f, 0.75f, 0.12f }, (Color){ 80, 76, 120, 255 });
                    DrawCubeV((Vector3){ p.x, 0.35f, p.z + 0.42f }, (Vector3){ 0.16f, 0.75f, 0.12f }, (Color){ 80, 76, 120, 255 });
                    break;

                case TILE_DATA:
                    DrawStone((Vector3){ p.x, -0.12f + jitter, p.z }, (Vector3){ 0.96f, 0.2f, 0.96f }, COL_DATA);
                    break;

                default:
                    DrawStone((Vector3){ p.x, -0.12f + jitter, p.z }, (Vector3){ 0.96f, 0.2f, 0.96f },
                              ((r + c) % 2) ? COL_FLOOR : COL_FLOOR2);
                    break;
            }
        }
    }

    // A gate that just opened: its two posts slide down into the floor
    if (w->gateFlash > 0)
    {
        float k = Smooth(w->gateFlash / 0.8f);          // 1 -> 0
        Vector3 p = TilePos(w, (float)w->gateFlashX, (float)w->gateFlashY, 0);
        float y = 0.35f - (1 - k) * 0.8f;
        DrawCubeV((Vector3){ p.x, y, p.z - 0.42f }, (Vector3){ 0.16f, 0.75f, 0.12f }, (Color){ 80, 76, 120, 255 });
        DrawCubeV((Vector3){ p.x, y, p.z + 0.42f }, (Vector3){ 0.16f, 0.75f, 0.12f }, (Color){ 80, 76, 120, 255 });
    }
}

// ---------------------------------------------------------------------------
// Signs: a board on top of a wall tile (e.g. "EXIT 11 TILES >>")
// ---------------------------------------------------------------------------
static int SignLines(const char *text, char lines[3][64])
{
    int n = 0, k = 0;
    for (const char *c = text; ; c++)
    {
        if (*c == '|' || *c == '\0')
        {
            lines[n][k] = '\0';
            n++;
            k = 0;
            if (*c == '\0' || n == 3) break;
        }
        else if (k < 63) lines[n][k++] = *c;
    }
    return n;
}


static void DrawSigns(const World *w)
{
    for (int i = 0; i < w->signCount; i++)
    {
        const WorldSign *sg = &w->signs[i];
        if (TileSeen(w, sg->col, sg->row) < 0.3f) continue;
        Vector3 p = TilePos(w, (float)sg->col, (float)sg->row, 0);
        Color post = { 70, 64, 104, 255 };
        DrawCubeV((Vector3){ p.x, 0.9f, p.z }, (Vector3){ 0.08f, 0.7f, 0.08f }, post);
        DrawCubeV((Vector3){ p.x, 1.28f, p.z }, (Vector3){ 0.5f, 0.12f, 0.08f }, ThemeOf(w)->a);
    }
}

// The writing on a sign: a panel on screen, pinned to the top of its post
static void DrawSignLabels(const World *w, Camera3D cam, int vw, int vh)
{
    for (int i = 0; i < w->signCount; i++)
    {
        const WorldSign *sg = &w->signs[i];
        if (TileSeen(w, sg->col, sg->row) < 0.5f) continue;
        char lines[3][64];
        int n = SignLines(sg->text, lines);
        Vector2 top = GetWorldToScreenEx(TilePos(w, (float)sg->col, (float)sg->row, 1.34f), cam, vw, vh);
        int size = 16, wide = 0;
        for (int k = 0; k < n; k++) if (MeasureText(lines[k], size) > wide) wide = MeasureText(lines[k], size);
        float bw = wide + 20.0f, bh = n * (size + 4) + 12.0f;
        Rectangle box = { top.x - bw / 2, top.y - bh, bw, bh };
        if (box.y < 4) box.y = 4;
        DrawRectangleRounded(box, 0.18f, 6, (Color){ 14, 18, 36, 235 });
        DrawRectangleRoundedLinesEx(box, 0.18f, 6, 2, ThemeOf(w)->a);
        for (int k = 0; k < n; k++)
        {
            int tw = MeasureText(lines[k], size);
            DrawText(lines[k], (int)(top.x - tw / 2.0f), (int)(box.y + 7 + k * (size + 4)), size,
                     k == 0 ? (Color){ 255, 236, 170, 255 } : WHITE);
        }
    }
}

// Fog: everything further than a couple of tiles from Bitty fades to black,
// like a torch in the dark
static void DrawFog(const World *w, Camera3D cam, int vw, int vh)
{
    if (w->fogRadius <= 0) return;
    Vector3 b = TilePos(w, w->drawX, w->drawY, 0.3f);
    Vector3 edge = TilePos(w, w->drawX + w->fogRadius + 1.0f, w->drawY, 0.3f);
    Vector2 c = GetWorldToScreenEx(b, cam, vw, vh), e = GetWorldToScreenEx(edge, cam, vw, vh);
    float R = fabsf(e.x - c.x);
    Color dark = ThemeOf(w)->fog;
    dark.a = 250;
    Color clear = dark;
    clear.a = 0;
    (void)clear;
    // a soft edge: rings getting darker towards the outside
    for (int k = 0; k < 10; k++)
    {
        float r0 = R * (0.55f + k * 0.045f), r1 = R * (0.55f + (k + 1) * 0.045f);
        Color ring = dark;
        ring.a = (unsigned char)(250 * (k + 1) / 11.0f);
        DrawRing(c, r0, r1, 0, 360, 64, ring);
    }
    DrawRing(c, R - 0.5f, (float)(vw + vh) * 2, 0, 360, 64, dark);
    const char *note = "It's dark: Bitty can only see the tiles next to him";
    int nw = MeasureText(note, 14);
    DrawRectangleRounded((Rectangle){ (vw - nw) / 2.0f - 10, vh - 64.0f, nw + 20.0f, 24 }, 0.4f, 6, (Color){ 20, 26, 48, 230 });
    DrawText(note, (vw - nw) / 2, vh - 59, 14, (Color){ 190, 205, 240, 255 });
}

// A soft glowing dot that always faces the camera
void Glow(Camera3D cam, Vector3 pos, float size, Color col)
{
    DrawBillboard(cam, glowTex, pos, size, col);
}

// ---------------------------------------------------------------------------
// The world around the board: an abyss, pillars and drifting chips
// ---------------------------------------------------------------------------
static float BoardRadius(const World *w)
{
    float a = w->cols / 2.0f, b = w->rows / 2.0f;
    return sqrtf(a * a + b * b);
}

#define TOWER_COUNT 34
#define CABLE_COUNT 7

// The camera looks down on the island, so the scenery lives in the abyss below
// and around it: a city of memory blocks seen from above.
// Where block i stands, and how big it is.
static void TowerShape(const World *w, int i, Vector3 *base, float *wide, float *tall)
{
    float R = BoardRadius(w);
    float a = (i / (float)TOWER_COUNT) * 2 * PI_F + (Hash(i, 31) % 100) / 300.0f;
    float dist = R + 2.6f + (Hash(i, 37) % 100) / 16.0f;                    // a wide ring round the island
    *wide = 1.3f + (Hash(i, 33) % 100) / 60.0f;
    float top = -6.2f + (Hash(i, 35) % 100) / 36.0f;                        // roofs sit well below the board
    *tall = top + 10.0f;
    float depth = sinf(a) > 0 ? 0.5f : 0.95f;                               // nearer in front, so they stay in view
    *base = (Vector3){ cosf(a) * dist * 1.45f, -10.0f, sinf(a) * dist * depth - 0.8f };
}

// A point along cable i (k = 0 at the island, 1 deep in the abyss). They sway a little.
static Vector3 CablePoint(const World *w, int i, float k, float t)
{
    float x = ((Hash(i, 41) % 100) / 100.0f - 0.5f) * w->cols * 0.9f;
    float z = ((Hash(i, 43) % 100) / 100.0f - 0.5f) * w->rows * 0.9f + w->rows * 0.25f;
    float sway = sinf(t * 0.6f + i * 1.7f) * 0.35f * k;
    float bow = sinf(k * PI_F) * 0.9f * ((i % 2) ? 1 : -1);
    return (Vector3){ x + sway + bow, -1.2f - k * 7.5f, z + k * 1.5f };
}

// Solid parts (lit by the shader)
static void DrawEnvironment(const World *w, float t)
{
    float R = BoardRadius(w);

    // Tall pillars rising out of the abyss, behind and beside the board
    for (int i = 0; i < 14; i++)
    {
        float a = PI_F + (i / 13.0f) * PI_F + (Hash(i, 3) % 100) / 400.0f;   // back half of a circle
        float dist = R + 2.2f + (Hash(i, 7) % 100) / 30.0f;
        float x = cosf(a) * dist * 1.35f, z = sinf(a) * dist * 0.9f;
        float top = -1.4f + (Hash(i, 11) % 100) / 45.0f;
        float rad = 0.22f + (Hash(i, 5) % 100) / 400.0f;
        DrawCylinderEx((Vector3){ x, -9.0f, z }, (Vector3){ x, top, z }, rad * 1.3f, rad, 10, (Color){ 64, 62, 104, 255 });
        DrawCylinderEx((Vector3){ x, top, z }, (Vector3){ x, top + 0.08f, z }, rad, rad * 0.8f, 10, (Color){ 90, 90, 140, 255 });
    }

    // Memory towers: huge blocks standing far off in the dark (their windows are drawn in the glow pass)
    for (int i = 0; i < TOWER_COUNT; i++)
    {
        Vector3 base; float wide, tall;
        TowerShape(w, i, &base, &wide, &tall);
        DrawCubeV((Vector3){ base.x, base.y + tall / 2, base.z }, (Vector3){ wide, tall, wide }, (Color){ 84, 84, 140, 255 });
        DrawCubeV((Vector3){ base.x, base.y + tall + 0.1f, base.z }, (Vector3){ wide * 0.55f, 0.2f, wide * 0.55f }, (Color){ 110, 110, 172, 255 });
    }

    // Cables hanging from under the island down into the abyss
    for (int i = 0; i < CABLE_COUNT; i++)
    {
        Vector3 prev = CablePoint(w, i, 0, t);
        for (int k = 1; k <= 6; k++)
        {
            Vector3 next = CablePoint(w, i, k / 6.0f, t);
            DrawCylinderEx(prev, next, 0.045f, 0.045f, 5, (Color){ 58, 56, 96, 255 });
            prev = next;
        }
    }

    // Chips drifting slowly in the distance
    for (int i = 0; i < 9; i++)
    {
        float a = PI_F * 1.1f + (i / 8.0f) * PI_F * 0.8f;
        float dist = R + 4.5f + (Hash(i, 21) % 100) / 25.0f;
        float x = cosf(a) * dist * 1.3f, z = sinf(a) * dist;
        float y = -1.0f + (Hash(i, 23) % 100) / 30.0f + sinf(t * 0.4f + i) * 0.25f;
        rlPushMatrix();
        rlTranslatef(x, y, z);
        rlRotatef(t * (6 + i % 4 * 3) + i * 40, 0.3f, 1, 0.2f);
        DrawCubeV((Vector3){ 0, 0, 0 }, (Vector3){ 0.7f, 0.12f, 0.5f }, (Color){ 60, 64, 96, 255 });
        rlPopMatrix();
    }
}

// Glowing parts (drawn in the additive pass)
static void DrawEnvironmentGlow(const World *w, Camera3D cam, float t)
{
    float R = BoardRadius(w);
    float span = R + 9;

    // Circuit traces far below, with pulses of data running along them
    const float deep = -6.0f;
    const Theme *th = ThemeOf(w);
    Color trace = Alpha(th->a, 70);
    for (int i = -6; i <= 6; i++)
    {
        float k = i * span / 6.0f;
        if (Hash(i, 1) % 3 != 0) DrawLine3D((Vector3){ -span, deep, k }, (Vector3){ span, deep, k }, trace);
        if (Hash(i, 2) % 3 != 0) DrawLine3D((Vector3){ k, deep, -span }, (Vector3){ k, deep, span }, trace);
    }
    for (int i = 0; i < 16; i++)
    {
        float lane = ((int)(Hash(i, 9) % 13) - 6) * span / 6.0f;
        float k = fmodf(t * (0.05f + (i % 4) * 0.02f) + i * 0.31f, 1.0f) * 2 - 1;
        Vector3 pos = (i % 2) ? (Vector3){ k * span, deep, lane } : (Vector3){ lane, deep, k * span };
        Color c = Alpha((i % 3) ? th->a : th->b, 200);
        Glow(cam, pos, 1.6f, c);
    }

    // Haze deep in the abyss, so the city below reads as dark shapes against it
    for (int i = 0; i < 9; i++)
    {
        float a = i * 0.698f + t * 0.02f;
        float d = R + 5.5f;
        Glow(cam, (Vector3){ cosf(a) * d * 1.4f, -8.5f, sinf(a) * d }, 15.0f, Alpha((i % 2) ? th->a : th->b, 30));
    }

    // A soft light under the island, as if it's being held up by its own power
    Glow(cam, (Vector3){ 0, -2.6f, 0.5f }, R * 3.2f, Alpha(th->a, 30));

    // The memory city: lit windows on the side facing us and a light on each roof,
    // switching on and off as data is used
    for (int i = 0; i < TOWER_COUNT; i++)
    {
        Vector3 base; float wide, tall;
        TowerShape(w, i, &base, &wide, &tall);
        float top = base.y + tall;
        for (int f = 0; f < 4; f++)
            for (int k = 0; k < 2; k++)
            {
                unsigned h = Hash(i * 7 + k, f);
                float on = sinf(t * (0.25f + (h % 5) * 0.12f) + (h % 628) / 100.0f);
                if (h % 4 == 0 || on < 0.0f) continue;                 // some stay dark
                Vector3 p = { base.x + (k - 0.5f) * wide * 0.5f, top - 0.45f - f * 0.6f, base.z + wide * 0.52f };
                Glow(cam, p, 0.34f, Alpha((h % 3) ? th->a : th->b, 70 + 150 * on));
            }
        float blink = 0.5f + 0.5f * sinf(t * 1.1f + i * 2.1f);
        Color neon = (i % 3) ? th->a : th->b;
        Glow(cam, (Vector3){ base.x, top + 0.3f, base.z }, 0.7f, Alpha(neon, 60 + 150 * blink));

        // neon edges round the roof and a faintly lit rooftop, like a city at night seen from above
        float hw = wide / 2;
        Vector3 c0 = { base.x - hw, top + 0.02f, base.z - hw }, c1 = { base.x + hw, top + 0.02f, base.z - hw };
        Vector3 c2 = { base.x + hw, top + 0.02f, base.z + hw }, c3 = { base.x - hw, top + 0.02f, base.z + hw };
        Color edge = Alpha(neon, 110 + 70 * blink);
        DrawLine3D(c0, c1, edge); DrawLine3D(c1, c2, edge); DrawLine3D(c2, c3, edge); DrawLine3D(c3, c0, edge);
        DrawLine3D(c2, (Vector3){ c2.x, top - 2.2f, c2.z }, Alpha(neon, 60));
        DrawLine3D(c3, (Vector3){ c3.x, top - 2.2f, c3.z }, Alpha(neon, 60));
        DrawCubeV((Vector3){ base.x, top + 0.01f, base.z }, (Vector3){ wide, 0.02f, wide }, Alpha(neon, 22 + 16 * blink));
    }

    // The core: two rings of light orbiting the island, turning opposite ways
    {
        float rx = w->cols / 2.0f + 1.5f, rz = w->rows / 2.0f + 1.5f;
        for (int i = 0; i < 80; i++)
        {
            float a = i * 6.2832f / 80 + t * 0.1f;
            float bright = 0.5f + 0.5f * sinf(a * 2 - t * 1.4f);            // a brighter band chases round the ring
            Glow(cam, (Vector3){ cosf(a) * rx, -1.5f, sinf(a) * rz }, 0.42f, Alpha(th->a, 40 + 200 * bright));
        }
        for (int i = 0; i < 60; i++)
        {
            float a = i * 6.2832f / 60 - t * 0.17f;
            float bright = 0.5f + 0.5f * sinf(a * 3 + t);
            Glow(cam, (Vector3){ cosf(a) * (rx + 1.6f), -2.6f, sinf(a) * (rz + 1.4f) }, 0.34f, Alpha(th->b, 30 + 130 * bright));
        }
    }

    // Data streams: packets of light racing through the streets of the city, leaving a tail
    for (int i = 0; i < 12; i++)
    {
        float speed = 0.06f + (Hash(i, 51) % 100) / 1200.0f;
        float k = fmodf(t * speed + (Hash(i, 53) % 100) / 100.0f, 1.0f);
        float dir = (i % 2) ? 1.0f : -1.0f;
        float y = -3.2f + (Hash(i, 55) % 100) / 110.0f;
        float lane = ((Hash(i, 57) % 100) / 100.0f - 0.5f) * 2 * (R + 7);
        float span2 = R + 12;
        Color col = (i % 3) ? th->a : th->b;
        for (int tail = 0; tail < 8; tail++)
        {
            float along = dir * ((k - tail * 0.01f) * 2 - 1) * span2;
            Vector3 p = (i % 4 < 2) ? (Vector3){ along, y, lane } : (Vector3){ lane * 1.3f, y, along };
            Glow(cam, p, 0.7f - tail * 0.07f, Alpha(col, 230 - tail * 28));
        }
    }

    // Pulses of light running down the cables
    for (int i = 0; i < CABLE_COUNT; i++)
    {
        float k = fmodf(t * 0.35f + i * 0.37f, 1.0f);
        Glow(cam, CablePoint(w, i, k, t), 0.5f, Alpha((i % 2) ? th->a : th->b, 220 * (1 - k)));
    }

    // Glowing tips on the pillars
    for (int i = 0; i < 14; i++)
    {
        float a = PI_F + (i / 13.0f) * PI_F + (Hash(i, 3) % 100) / 400.0f;
        float dist = R + 2.2f + (Hash(i, 7) % 100) / 30.0f;
        float x = cosf(a) * dist * 1.35f, z = sinf(a) * dist * 0.9f;
        float top = -1.4f + (Hash(i, 11) % 100) / 45.0f;
        float blink = 0.6f + 0.4f * sinf(t * (0.8f + (i % 3) * 0.4f) + i);
        Color c = (i % 2) ? th->a : th->b;
        c.a = (unsigned char)(200 * blink);
        Glow(cam, (Vector3){ x, top + 0.15f, z }, 1.3f, c);
    }
}

// ---------------------------------------------------------------------------
// Bitty (self-lit: Bitty IS the light)
// ---------------------------------------------------------------------------
void DrawBitty(const World *w, float t)
{
    float squashY = 1;          // >1 = stretched tall, <1 = squashed flat
    float roll = 0;             // lean left / right (degrees)
    float pitch = 0;            // lean forwards / back
    float yaw = 0;              // extra turn on top of w->facingShown
    float armLift = 0;          // arms go up in the air
    float footSwing = 0;        // feet swing while hopping
    float hop = BittyHop(w, t);
    float p = ActionProgress(w);

    if (w->spawn > 0)
    {
        squashY = 1.2f;                                                 // stretched while dropping in
        armLift = 0.1f;
    }
    else if (w->current.type == ACT_MOVE)
    {
        float dirX = (w->current.dir == EAST) ? 1.0f : (w->current.dir == WEST) ? -1.0f : 0.0f;
        float dirZ = (w->current.dir == SOUTH) ? 1.0f : (w->current.dir == NORTH) ? -1.0f : 0.0f;
        if (w->lastResult)
        {
            float q = HopTravel(p);
            if (p < HOP_CROUCH) squashY = 1 - 0.22f * sinf(p / HOP_CROUCH * PI_F / 2);     // crouch...
            else                squashY = 0.78f + 0.4f * Smooth(q * 5) - 0.18f * Smooth((q - 0.7f) / 0.3f);   // ...spring up, then reach for the floor
            float air = sinf(q * PI_F);
            roll  = -dirX * air * 14;
            pitch =  dirZ * air * 12;
            armLift = air * 0.08f;
            footSwing = sinf(q * PI_F * 2) * 0.06f;
        }
        else
        {
            // bonk: stretch into the run, then flatten against the wall and wobble
            if (p < 0.4f) { squashY = 1 + 0.06f * (p / 0.4f); roll = -dirX * 10 * (p / 0.4f); pitch = dirZ * 10 * (p / 0.4f); }
            else
            {
                float q = (p - 0.4f) / 0.6f;
                squashY = 1 - 0.2f * (1 - q) * cosf(q * PI_F * 2.5f);
                roll  = dirX * 16 * (1 - q) * sinf(q * PI_F * 3);
                pitch = -dirZ * 12 * (1 - q) * sinf(q * PI_F * 3);
                armLift = 0.07f * (1 - q);
            }
        }
    }
    else if (w->current.type == ACT_WRITE)
    {
        squashY = 1 - 0.2f * sinf(p * PI_F);                            // stamps the value down
        armLift = -0.05f * sinf(p * PI_F);
    }
    else if (w->current.type == ACT_SCAN)
    {
        // leans towards the tile and sweeps his head across it
        float lean = sinf(p * PI_F);
        float dirX = (w->current.dir == EAST) ? 1.0f : (w->current.dir == WEST) ? -1.0f : 0.0f;
        float dirZ = (w->current.dir == SOUTH) ? 1.0f : (w->current.dir == NORTH) ? -1.0f : 0.0f;
        roll  = -dirX * lean * 9;
        pitch =  dirZ * lean * 10;
        yaw   = sinf(p * PI_F * 2) * 10 * lean;
    }
    else if (w->current.type == ACT_READ)
    {
        pitch = 16 * sinf(p * PI_F);                                    // looks down at the tile
        squashY = 1 - 0.06f * sinf(p * PI_F);
    }
    else if (w->land > 0 && w->landLength > 0)
    {
        float u = 1 - w->land / w->landLength;                          // landed: squash, then spring back
        squashY = 1 - 0.24f * (1 - u) * cosf(u * PI_F * 2.4f);
    }
    else
    {
        squashY = 1 + sinf(t * 2.5f) * 0.03f;                           // breathing
        if (w->mood == MOOD_IDLE && w->idleTime > 2.0f)                 // idle: look around now and then
        {
            float phase = fmodf(w->idleTime, 6.0f);
            if (phase > 2.6f && phase < 4.2f) yaw = sinf((phase - 2.6f) / 1.6f * PI_F * 2) * 22;
        }
    }

    if (w->state == WORLD_WON)
    {
        if (w->winTime < 0.6f) yaw = Smooth(w->winTime / 0.6f) * 360;   // victory spin
        else roll = sinf((w->winTime - 0.6f) * 6.5f) * 6;               // sway while hopping
        armLift = 0.1f;
    }

    float fall = BittyFall(w);
    float spin = 0;
    if (w->state == WORLD_SEGFAULT)
    {
        float since = FALL_SECONDS - w->glitch;
        if (since < TEETER_SECONDS)                                     // teeter on the edge, arms flailing
        {
            roll = sinf(since * 38) * 16;
            armLift = 0.1f;
        }
        else spin = (fall / 3.4f) * 620;
    }
    if (fall > 2.9f) return;                                            // gone

    // a memory error (segfault / out of bounds in the code): Bitty glitches
    float glitchX = 0;
    if (w->glitch > 0 && w->state != WORLD_SEGFAULT)
        glitchX = ((int)(t * 30) % 2 ? 0.05f : -0.05f) * Clamp01(w->glitch);

    float shakeX = (w->shake > 0) ? sinf(t * 70) * 0.04f * (w->shake / 0.35f) : 0;
    Vector3 base = TilePos(w, w->drawX, w->drawY, 0);
    float dip = (w->current.type == ACT_MOVE) ? 0 : TileDip(w, w->bittyX, w->bittyY);

    // soft shadow (smaller the higher he is)
    if (fall == 0)
    {
        float sh = (0.32f - hop * 0.09f) * (w->bittyScale > 0 ? w->bittyScale : 1.0f);
        if (sh < 0.06f) sh = 0.06f;
        DrawCylinderEx((Vector3){ base.x, 0.0f - dip, base.z }, (Vector3){ base.x, 0.005f - dip, base.z },
                       sh, sh, 18, (Color){ 0, 0, 10, 110 });
    }

    rlPushMatrix();
    rlTranslatef(base.x + shakeX + glitchX, hop - fall - dip, base.z);
    rlRotatef(w->facingShown * 0.6f + spin + yaw, 0, 1, 0);
    rlRotatef(-24 + pitch, 1, 0, 0);    // lean back a little so the camera sees Bitty's face
    rlRotatef(roll, 0, 0, 1);
    float size = 1.5f * (w->bittyScale > 0 ? w->bittyScale : 1.0f);
    rlScalef(size / sqrtf(squashY), size * squashY, size / sqrtf(squashY));
    if (w->teleport > 0)
    {
        // beaming: he stretches into a thin line of light
        float k = Smooth(w->teleport);
        rlScalef(1 - 0.9f * k, 1 + 1.8f * k, 1 - 0.9f * k);
    }

    // Slate-grey stone, like Bulder from Pode: matte, darker underneath, no shine
    const Color BODY = Customisation_Body();
    const Color BODY_HI = Customisation_Highlight();
    const Color BODY_D = Customisation_Shadow();
    const Color DARK   = { 50, 54, 68, 255 };
    const Color SCREEN = { 16, 20, 34, 255 };
    Color eye = MoodColor(w);
    Color trim = Customisation_Glow();

    // feet (they swing while he hops)
    DrawBlob((Vector3){ -0.11f, 0.05f - fabsf(footSwing) * 0.4f, 0.04f + footSwing }, (Vector3){ 0.085f, 0.055f, 0.1f }, DARK);
    DrawBlob((Vector3){  0.11f, 0.05f - fabsf(footSwing) * 0.4f, 0.04f - footSwing }, (Vector3){ 0.085f, 0.055f, 0.1f }, DARK);

    // chunky block of stone with soft edges, like Bulder: wide, flat-ish top
    DrawRoundedBox((Vector3){ 0, 0.30f, 0 }, (Vector3){ 0.62f, 0.40f, 0.50f }, 0.11f, BODY);
    DrawRoundedBox((Vector3){ 0, 0.155f, 0 }, (Vector3){ 0.60f, 0.11f, 0.48f }, 0.05f, BODY_D);   // darker base
    DrawRoundedBox((Vector3){ -0.02f, 0.495f, -0.03f }, (Vector3){ 0.44f, 0.03f, 0.32f }, 0.012f, BODY_HI); // lighter top

    // a few stony bumps on top and at the back
    DrawBlob((Vector3){ 0.18f, 0.5f, -0.12f }, (Vector3){ 0.07f, 0.035f, 0.06f }, BODY_HI);
    DrawBlob((Vector3){ -0.2f, 0.49f, -0.15f }, (Vector3){ 0.06f, 0.03f, 0.05f }, BODY_D);
    DrawBlob((Vector3){ 0.03f, 0.48f, -0.22f }, (Vector3){ 0.05f, 0.03f, 0.04f }, BODY_D);

    // a glowing seam around his middle
    DrawRoundedBox((Vector3){ 0, 0.215f, 0 }, (Vector3){ 0.635f, 0.022f, 0.515f }, 0.011f,
                   (Color){ trim.r, trim.g, trim.b, 255 });

    // stubby arms (wave when happy)
    float wave = (w->mood == MOOD_HAPPY) ? sinf(t * 12) * 0.07f : sinf(t * 2.5f) * 0.01f;
    DrawBlob((Vector3){ -0.34f, 0.22f + wave + armLift, 0.05f }, (Vector3){ 0.055f, 0.075f, 0.055f }, BODY_D);
    DrawBlob((Vector3){  0.34f, 0.22f - wave + armLift, 0.05f }, (Vector3){ 0.055f, 0.075f, 0.055f }, BODY_D);

    // Optional classic headphone ear-pieces.
    if (Customisation_Headphones())
    for (int side = -1; side <= 1; side += 2)
    {
        DrawCylinderEx((Vector3){ side * 0.30f, 0.37f, 0 }, (Vector3){ side * 0.37f, 0.37f, 0 },
                       0.09f, 0.08f, 16, DARK);
        DrawCylinderEx((Vector3){ side * 0.37f, 0.37f, 0 }, (Vector3){ side * 0.385f, 0.37f, 0 },
                       0.055f, 0.045f, 16, trim);
    }

    // flat screen face with rounded corners
    DrawRoundedBox((Vector3){ 0, 0.34f, 0.21f }, (Vector3){ 0.40f, 0.24f, 0.12f }, 0.055f, SCREEN);

    float ez = 0.282f;
    for (int side = -1; side <= 1; side += 2)
    {
        Vector3 e = { side * 0.085f, 0.36f, ez };

        if (w->mood == MOOD_HAPPY)
        {
            DrawBar((Vector3){ e.x - 0.022f, e.y, e.z }, 0.065f, 0.024f,  45, eye);   // ^
            DrawBar((Vector3){ e.x + 0.022f, e.y, e.z }, 0.065f, 0.024f, -45, eye);
        }
        else if (w->mood == MOOD_ERROR)
        {
            DrawBar(e, 0.085f, 0.024f,  45, eye);                                      // x
            DrawBar(e, 0.085f, 0.024f, -45, eye);
        }
        else if (Customisation_Face() == 1)
        {
            DrawBar((Vector3){e.x - 0.022f,e.y,e.z},0.065f,0.024f,45,eye);
            DrawBar((Vector3){e.x + 0.022f,e.y,e.z},0.065f,0.024f,-45,eye);
        }
        else if (Customisation_Face() == 2)
        {
            DrawBar(e,0.085f,0.025f,side * 12,eye);
        }
        else if (Customisation_Face() == 3)
        {
            DrawBar(e,0.075f,0.014f,0,eye);
        }
        else
        {
            float blink = (fmodf(t + 0.3f, 3.7f) < 0.13f) ? 0.12f : 1.0f;
            float look = (w->current.type == ACT_MOVE)
                       ? ((w->current.dir == EAST) ? 0.016f : (w->current.dir == WEST) ? -0.016f : 0) : 0;
            DrawBlob((Vector3){ e.x + look, e.y, e.z }, (Vector3){ 0.05f, 0.064f * blink, 0.02f }, eye);
            if (blink > 0.5f)
            {
                DrawBlob((Vector3){ e.x + look + 0.017f, e.y + 0.024f, e.z + 0.012f },
                         (Vector3){ 0.015f, 0.015f, 0.008f }, WHITE);                  // sparkle
                DrawBlob((Vector3){ e.x + look - 0.014f, e.y - 0.022f, e.z + 0.012f },
                         (Vector3){ 0.007f, 0.007f, 0.006f }, WHITE);
            }
        }
    }

    // little smile (a tiny "o" when bonking)
    if (w->mood != MOOD_ERROR)
    {
        if (w->shake > 0)
            DrawBlob((Vector3){ 0, 0.29f, ez }, (Vector3){ 0.016f, 0.018f, 0.01f }, eye);
        else if (w->speaking)
        {
            // talking: his mouth opens and closes like a little speaker
            float open = 0.35f + 0.65f * fabsf(sinf(t * 16)) * (0.6f + 0.4f * sinf(t * 5.3f));
            DrawBlob((Vector3){ 0, 0.285f, ez }, (Vector3){ 0.03f, 0.006f + 0.02f * open, 0.01f }, eye);
        }
        else
        {
            DrawBar((Vector3){ -0.012f, 0.29f, ez }, 0.028f, 0.012f, -22, eye);
            DrawBar((Vector3){  0.012f, 0.29f, ez }, 0.028f, 0.012f,  22, eye);
        }
    }


    Customisation_DrawAccessories(t);

    rlPopMatrix();
}

// ---------------------------------------------------------------------------
// Glows (additive, after everything solid, no depth writes)
// ---------------------------------------------------------------------------

static Vector2 bittyScreen;
static bool    bittyScreenKnown = false;

bool World_BittyOnScreen(float *x, float *y)
{
    if (!bittyScreenKnown) return false;
    *x = bittyScreen.x;
    *y = bittyScreen.y;
    return true;
}

void World_DrawBittyPreview(const World *w, float time)
{
    DrawBitty(w, time);
}

// dust, sparks and confetti
void DrawParticles(Camera3D cam)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        const Particle *q = &particles[i];
        if (q->life <= 0) continue;
        float k = q->life / q->maxLife;
        Glow(cam, q->pos, q->size * (0.6f + 0.4f * k), (Color){ q->col.r, q->col.g, q->col.b, (unsigned char)(q->col.a * k) });
    }
}

static void DrawGlows(const World *w, Camera3D cam, float t)
{
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    DrawEnvironmentGlow(w, cam, t);

    // Bitty's halo
    float fall = BittyFall(w);
    if (fall < 2.9f)
    {
        Vector3 b = TilePos(w, w->drawX, w->drawY, 0.5f + BittyHop(w, t) - fall);
        Color m = MoodColor(w);
        float pulse = 1 + sinf(t * 2.2f) * 0.05f;
        // a soft glow around him (kept faint so it doesn't wash his stone body out)
        Glow(cam, (Vector3){ b.x, b.y - 0.25f, b.z }, 2.4f * pulse, (Color){ m.r, m.g, m.b, 45 });
        // antenna bulb
        Glow(cam, (Vector3){ b.x, b.y + 0.78f, b.z - 0.25f }, 0.5f, (Color){ m.r, m.g, m.b, 160 });
    }

    for (int r = 0; r < w->rows; r++)
    {
        for (int c = 0; c < w->cols; c++)
        {
            if (TileSeen(w, c, r) < 0.3f) continue;
            Vector3 p = TilePos(w, (float)c, (float)r, 0);
            TileType type = w->tiles[r][c].type;

            if (type == TILE_EXIT)
            {
                float pulse = 0.5f + 0.5f * sinf(t * 3);
                DrawCylinderEx((Vector3){ p.x, 0.0f, p.z }, (Vector3){ p.x, 1.8f, p.z }, 0.32f, 0.08f, 18,
                               (Color){ 60, 200, 120, (unsigned char)(50 + pulse * 40) });
                Glow(cam, (Vector3){ p.x, 0.3f, p.z }, 1.8f, (Color){ 90, 255, 160, (unsigned char)(70 + pulse * 40) });
                // two rings of light circling the portal, turning opposite ways
                for (int i = 0; i < 10; i++)
                {
                    float a = i * 0.6283f + t * 1.6f, b2 = i * 0.6283f - t * 2.3f;
                    float lift = 0.5f + 0.5f * sinf(t * 1.3f);
                    Glow(cam, (Vector3){ p.x + cosf(a) * 0.42f, 0.12f, p.z + sinf(a) * 0.42f }, 0.16f, (Color){ 130, 255, 185, 170 });
                    Glow(cam, (Vector3){ p.x + cosf(b2) * 0.3f, 0.35f + lift * 0.5f, p.z + sinf(b2) * 0.3f }, 0.12f, (Color){ 190, 255, 220, 140 });
                }
                // sparkles rising from the portal
                for (int i = 0; i < 5; i++)
                {
                    float k = fmodf(t * 0.5f + i * 0.2f, 1.0f);
                    float a = i * 1.3f + t;
                    Glow(cam, (Vector3){ p.x + sinf(a) * 0.25f, k * 1.6f, p.z + cosf(a) * 0.25f }, 0.18f,
                         (Color){ 160, 255, 200, (unsigned char)((1 - k) * 200) });
                }
            }
            else if (type == TILE_GATE)
            {
                // a crackling energy barrier across the path
                float flicker = 0.75f + 0.25f * sinf(t * 23 + c * 3) * sinf(t * 7 + r);
                unsigned char a = (unsigned char)(110 * flicker);
                DrawCubeV((Vector3){ p.x, 0.35f, p.z }, (Vector3){ 0.05f, 0.62f, 0.76f }, (Color){ 255, 110, 50, a });
                for (int i = 0; i < 4; i++)
                {
                    float y = 0.1f + i * 0.17f + sinf(t * 3 + i) * 0.02f;
                    DrawCubeV((Vector3){ p.x, y, p.z }, (Vector3){ 0.07f, 0.02f, 0.8f }, (Color){ 255, 200, 120, (unsigned char)(160 * flicker) });
                }
                Glow(cam, (Vector3){ p.x, 0.35f, p.z }, 1.3f, (Color){ 255, 120, 50, (unsigned char)(80 * flicker) });
            }
            else if (HasCrystal(w, c, r))
            {
                Color cc = CrystalColor(c, r);
                Glow(cam, (Vector3){ p.x, 0.75f, p.z }, 1.2f, (Color){ cc.r, cc.g, cc.b, 70 });
            }
            else if (type == TILE_VOID)
            {
                // a sickly purple glow deep in the pit, so it reads as "danger, nothing here"
                float throb = 0.7f + 0.3f * sinf(t * 1.7f + c * 0.9f + r);
                Glow(cam, (Vector3){ p.x, -0.6f, p.z }, 1.3f, (Color){ 150, 60, 220, (unsigned char)(90 * throb) });
                Glow(cam, (Vector3){ p.x, 0.12f, p.z }, 1.05f, (Color){ 140, 50, 210, (unsigned char)(85 * throb) });   // mist

                // faint purple sparks drifting up out of the pit
                for (int i = 0; i < 2; i++)
                {
                    float k = fmodf(t * 0.35f + (Hash(c, r + i) % 100) / 100.0f, 1.0f);
                    float a = (float)(Hash(r, c + i) % 628) / 100.0f;
                    Glow(cam, (Vector3){ p.x + sinf(a) * 0.3f, -1.1f + k * 1.2f, p.z + cosf(a) * 0.3f }, 0.2f,
                         (Color){ 200, 110, 255, (unsigned char)(sinf(k * PI_F) * 230) });
                }
            }
            else if (type == TILE_DATA)
            {
                Glow(cam, (Vector3){ p.x, 0.1f, p.z }, 0.9f, (Color){ 90, 150, 255, 45 });
            }
        }
    }

    // a gate bursting open
    if (w->gateFlash > 0)
    {
        float k = w->gateFlash / 0.8f;                  // 1 -> 0
        Vector3 p = TilePos(w, (float)w->gateFlashX, (float)w->gateFlashY, 0.35f);
        // the energy bars turn green, pull apart and fade
        for (int i = 0; i < 4; i++)
        {
            float y = 0.1f + i * 0.17f;
            float gap = (1 - k) * 0.45f;
            unsigned char a = (unsigned char)(200 * k);
            DrawCubeV((Vector3){ p.x, y, p.z - 0.2f - gap }, (Vector3){ 0.07f, 0.02f, 0.4f * k }, (Color){ 140, 255, 180, a });
            DrawCubeV((Vector3){ p.x, y, p.z + 0.2f + gap }, (Vector3){ 0.07f, 0.02f, 0.4f * k }, (Color){ 140, 255, 180, a });
        }
        Glow(cam, p, 0.6f + (1 - k) * 2.5f, (Color){ 120, 255, 170, (unsigned char)(220 * k) });
        for (int i = 0; i < 10; i++)
        {
            float a = i * 0.628f;
            float d = (1 - k) * 0.9f;
            Glow(cam, (Vector3){ p.x + sinf(a) * d * 0.4f, p.y + cosf(a) * d * 0.5f, p.z + cosf(a) * d },
                 0.18f, (Color){ 180, 255, 210, (unsigned char)(255 * k) });
        }
    }

    // write() flash
    if (w->flash > 0)
    {
        Vector3 p = TilePos(w, (float)w->bittyX, (float)w->bittyY, 0.1f);
        Glow(cam, p, 1.4f, (Color){ 150, 210, 255, (unsigned char)(w->flash / 0.5f * 220) });
    }

    // footprints: the tiles Bitty has stepped on keep a soft glow, so you can see the path the code took
    Color trail = MoodColor(w);
    if (w->mood == MOOD_ERROR) trail = Customisation_Glow();
    for (int r = 0; r < w->rows; r++)
        for (int c = 0; c < w->cols; c++)
        {
            float k = w->tileTrail[r][c];
            if (k <= 0 || w->tiles[r][c].type == TILE_EXIT) continue;
            Vector3 p = TilePos(w, (float)c, (float)r, 0.03f);
            Glow(cam, p, 0.55f + k * 0.5f, (Color){ trail.r, trail.g, trail.b, (unsigned char)(30 + k * 110) });
        }

    // the beam Bitty arrives in
    if (w->spawn > 0)
    {
        float k = w->spawn / SPAWN_SECONDS;
        Vector3 p = TilePos(w, w->drawX, w->drawY, 0);
        Color m = Customisation_Glow();
        DrawCylinderEx((Vector3){ p.x, 0, p.z }, (Vector3){ p.x, 4.5f, p.z }, 0.3f * k + 0.05f, 0.3f * k + 0.05f, 14,
                       (Color){ m.r, m.g, m.b, (unsigned char)(90 * k) });
        Glow(cam, (Vector3){ p.x, 0.1f, p.z }, 1.6f, (Color){ m.r, m.g, m.b, (unsigned char)(120 * k) });
    }

    // the beam Bitty is teleported in (to and from the memory room)
    if (w->teleport > 0)
    {
        float k = Smooth(w->teleport);
        Vector3 p = TilePos(w, w->drawX, w->drawY, 0);
        Color m = Customisation_Glow();
        DrawCylinderEx((Vector3){ p.x, 0, p.z }, (Vector3){ p.x, 6.0f, p.z }, 0.12f + 0.3f * k, 0.12f + 0.3f * k, 14,
                       (Color){ m.r, m.g, m.b, (unsigned char)(150 * k) });
        Glow(cam, (Vector3){ p.x, 0.5f, p.z }, 2.6f * k, (Color){ 255, 255, 255, (unsigned char)(160 * k) });
    }

    // scan() / read(): a beam from Bitty's eye sweeping the tile he is checking
    if (w->current.type == ACT_SCAN || w->current.type == ACT_READ || w->scanShow > 0)
    {
        bool active = (w->current.type == ACT_SCAN || w->current.type == ACT_READ);
        float p = active ? 1 - w->actionTimer / w->actionLength : 1;
        float fade = active ? 1.0f : Clamp01(w->scanShow / 0.4f);
        Vector3 eye = TilePos(w, w->drawX, w->drawY, 0.55f);
        Vector3 to = TilePos(w, (float)w->scanX, (float)w->scanY, 0.06f);
        Color c = { 120, 235, 255, 255 };
        if (active)
            for (int i = 0; i <= 10; i++)
            {
                float u = i / 10.0f;
                Vector3 q = { eye.x + (to.x - eye.x) * u, eye.y + (to.y - eye.y) * u, eye.z + (to.z - eye.z) * u };
                Glow(cam, q, 0.14f + u * 0.25f, Alpha(c, 150 * sinf(Clamp01(p * 1.2f) * PI_F) + 20));
            }
        // a bar of light sweeping across the tile, then the whole tile glows
        float sweep = (p * 2 - 0.5f);
        if (active && sweep > -0.5f && sweep < 0.5f)
            DrawCubeV((Vector3){ to.x + sweep * 0.9f, 0.05f, to.z }, (Vector3){ 0.05f, 0.04f, 0.9f }, Alpha(c, 220));
        DrawCubeWiresV((Vector3){ to.x, 0.04f, to.z }, (Vector3){ 0.96f, 0.06f, 0.96f }, Alpha(c, 200 * fade));
        Glow(cam, to, 1.3f, Alpha(c, 90 * fade));
    }

    // signs on the walls glow softly
    for (int i = 0; i < w->signCount; i++)
    {
        Vector3 p = TilePos(w, (float)w->signs[i].col, (float)w->signs[i].row, 1.2f);
        Glow(cam, (Vector3){ p.x, p.y, p.z + 0.1f }, 2.2f, Alpha(ThemeOf(w)->a, 45));
    }

    DrawParticles(cam);

    // memory motes drifting through the dark
    float spanX = w->cols / 2.0f + 0.5f, spanZ = w->rows / 2.0f + 0.5f;
    for (int i = 0; i < MOTE_COUNT; i++)
    {
        float k = fmodf(t * (0.04f + (i % 5) * 0.01f) + i * 0.137f, 1.0f);
        float x = sinf(i * 12.9898f) * spanX + sinf(t * 0.3f + i) * 0.3f;
        float z = cosf(i * 78.233f) * spanZ + cosf(t * 0.25f + i * 2) * 0.3f;
        float y = 0.1f + k * 2.2f;
        unsigned char a = (unsigned char)(sinf(k * PI_F) * (120 + 60 * sinf(t * 3 + i)));
        Color col = Alpha((i % 3) ? ThemeOf(w)->a : (Color){ 255, 220, 170, 255 }, a);
        Glow(cam, (Vector3){ x, y, z }, 0.12f + (i % 4) * 0.03f, col);
    }

    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

// ---------------------------------------------------------------------------
// Labels: addresses are revealed by Bitty's light
// ---------------------------------------------------------------------------
void DrawLabel(Vector3 at, Camera3D cam, int w, int h, const char *text, int size, Color col)
{
    Vector2 s = GetWorldToScreenEx(at, cam, w, h);
    int tw = MeasureText(text, size);
    DrawText(text, (int)(s.x - tw / 2.0f), (int)(s.y - size / 2.0f), size, col);
}

static void DrawLabels(const World *w, Camera3D cam, int vw, int vh, float t)
{
    for (int r = 0; r < w->rows; r++)
    {
        for (int c = 0; c < w->cols; c++)
        {
            const Tile *tile = &w->tiles[r][c];
            if (TileSeen(w, c, r) < 0.5f) continue;
            Vector3 p = TilePos(w, (float)c, (float)r, 0.01f);
            bool underBitty = (c == w->bittyX && r == w->bittyY) ||
                              (fabsf(c - w->drawX) < 0.9f && fabsf(r - w->drawY) < 0.9f);

            if (tile->type == TILE_EMPTY && !underBitty)
            {
                float dx = c - w->drawX, dz = r - w->drawY;
                float near = 1.0f - sqrtf(dx * dx + dz * dz) / 3.2f;
                if (near > 0)
                    DrawLabel((Vector3){ p.x - 0.28f, p.y, p.z - 0.3f }, cam, vw, vh,
                              TextFormat("%02X", r * w->cols + c), 10, (Color){ 210, 225, 255, (unsigned char)(near * 200) });
            }
            else if (tile->type == TILE_DATA)
                DrawLabel((Vector3){ p.x, 0.3f + sinf(t * 2 + c) * 0.04f, p.z }, cam, vw, vh,
                          TextFormat("%d", tile->value), 22, (Color){ 190, 225, 255, 255 });
            else if (tile->type == TILE_GATE && w->doorCode >= 0)
                DrawLabel((Vector3){ p.x, 1.15f, p.z }, cam, vw, vh, "CODE ?", 14, (Color){ 255, 190, 120, 235 });
            else if (tile->type == TILE_EXIT && !underBitty)
                DrawLabel((Vector3){ p.x, 1.1f, p.z }, cam, vw, vh, "EXIT", 14, (Color){ 210, 255, 225, 230 });
        }
    }

    DrawSignLabels(w, cam, vw, vh);

    // what scan() / read() found
    if (w->scanShow > 0 || w->current.type == ACT_SCAN || w->current.type == ACT_READ)
    {
        static const char *NAMES[] = { "EMPTY", "WALL", "DATA", "GATE", "EXIT", "VOID!" };
        const char *text;
        if (w->scanWasRead) text = TextFormat("read() = %d", w->scanResult);
        else text = (w->scanResult >= 0 && w->scanResult <= 5) ? TextFormat("TILE_%s", NAMES[w->scanResult]) : "?";
        if (!w->scanWasRead && w->scanResult == TILE_VOID) text = "TILE_VOID!";
        float a = Clamp01(w->scanShow / 0.3f);
        if (w->current.type == ACT_SCAN || w->current.type == ACT_READ) a = 1;
        // read(): under Bitty's feet (towards the camera); scan(): above the tile he checked
        Vector3 at = w->scanWasRead ? TilePos(w, (float)w->scanX, w->scanY + 0.62f, 0.05f)
                                    : TilePos(w, (float)w->scanX, (float)w->scanY, 1.0f);
        Color col = (!w->scanWasRead && w->scanResult == TILE_VOID) ? (Color){ 255, 120, 150, 255 } :
                    (!w->scanWasRead && w->scanResult == TILE_WALL) ? (Color){ 255, 200, 120, 255 } : (Color){ 150, 240, 255, 255 };
        Vector2 sp = GetWorldToScreenEx(at, cam, vw, vh);
        int tw = MeasureText(text, 16);
        DrawRectangleRounded((Rectangle){ sp.x - tw / 2.0f - 6, sp.y - 11, tw + 12.0f, 22 }, 0.4f, 6, (Color){ 8, 12, 26, (unsigned char)(210 * a) });
        DrawLabel(at, cam, vw, vh, text, 16, Alpha(col, 255 * a));
    }

    // Loose bits of data drifting up out of the abyss around the island: 0s and 1s
    const Theme *th = ThemeOf(w);
    float R = BoardRadius(w);
    for (int i = 0; i < 18; i++)
    {
        float k = fmodf(t * (0.03f + (i % 4) * 0.008f) + (Hash(i, 61) % 100) / 100.0f, 1.0f);
        float a = (Hash(i, 63) % 628) / 100.0f;
        float d = R + 1.2f + (Hash(i, 65) % 100) / 40.0f;
        Vector3 p = { cosf(a) * d * 1.25f, -3.5f + k * 7.0f, sinf(a) * d * 0.8f - 1.0f };
        float fade = sinf(k * PI_F);
        DrawLabel(p, cam, vw, vh, ((Hash(i, 67) + (int)(t * 0.7f + i)) % 2) ? "1" : "0", 12,
                  Alpha((i % 2) ? th->a : th->b, 120 * fade));
    }
}

// ---------------------------------------------------------------------------
// World_Draw
// ---------------------------------------------------------------------------
static Camera3D MakeCamera(const World *w, float aspect)
{
    // Wide levels (like a corridor of gates) don't zoom right out: the camera
    // shows about 11 columns and follows Bitty along instead.
    bool wide = w->cols + 1 > 11;
    float needW = wide ? 11.0f : w->cols + 1.0f;
    float needD = w->rows + 1.0f;
    float fitW = needW / (2 * tanf(22.5f * PI_F / 180) * aspect);
    float fitD = needD * 1.05f;
    float dist = fitW > fitD ? fitW : fitD;
    dist *= 1.12f;                       // leave room to see the world around the board
    if (dist < 6.0f) dist = 6.0f;

    // During a repair test the camera moves in and looks more from the front,
    // so the floating variables and memory blocks above the corridor are easy to read
    static float focusCur = 0;
    focusCur += (w->focus - focusCur) * (1 - expf(-GetFrameTime() * 3.0f));
    dist *= 1.0f - 0.3f * focusCur;
    dist *= 1.0f - 0.36f * w->closeUp;      // Bitty is talking: come in close
    if (w->fogRadius > 0) dist *= 0.62f;     // fog: stay close to Bitty (the map's size would give it away)

    // Ease the camera a little towards Bitty, with a slow sway, so the world feels alive
    Vector3 b = TilePos(w, w->drawX, w->drawY, 0);
    Vector3 want = { b.x * (wide ? 0.85f : 0.3f), 0, b.z * 0.25f };
    if (w->fogRadius > 0) want = (Vector3){ b.x, 0, b.z };
    if (w->test.active)
    {
        Vector3 a = TilePos(w, w->test.gateX - 0.5f, (float)w->test.gateY, 0);
        want.x = want.x + (a.x - want.x) * focusCur;
        want.z = want.z + (a.z - 0.4f - want.z) * focusCur;
    }
    want.x += (b.x - want.x) * w->closeUp;
    want.z += (b.z + 0.5f - want.z) * w->closeUp;
    if (!camFollowSet) { camFollow = want; camFollowSet = true; }
    float ease = 1 - expf(-GetFrameTime() * 2.5f);
    camFollow.x += (want.x - camFollow.x) * ease;
    camFollow.z += (want.z - camFollow.z) * ease;

    float t = (float)GetTime();
    float swayX = sinf(t * 0.23f) * 0.25f, swayY = sinf(t * 0.31f) * 0.12f;

    // a small jolt when Bitty bonks into something
    if (w->shake > 0) swayX += sinf(t * 55) * 0.05f * (w->shake / 0.35f);

    Camera3D cam = { 0 };
    cam.target     = (Vector3){ camFollow.x, 0.95f * focusCur, camFollow.z + 0.25f };
    cam.position   = (Vector3){ camFollow.x + swayX, dist * (1.0f - 0.25f * focusCur) + swayY,
                                camFollow.z + dist * (0.50f + 0.3f * focusCur) };
    cam.up         = (Vector3){ 0, 1, 0 };
    cam.fovy       = 45;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

// ===========================================================================
// Repair tests: the gate terminal (the test itself is acted out in the memory room, room.c)
// ===========================================================================
// The gate terminal: the question this gate asks, and how the code is doing
static void DrawTestHud(const World *w, int vw)
{
    const TestDisplay *td = &w->test;
    if (!td->active) return;

    Color edge = td->state == TEST_PASSED ? (Color){ 110, 240, 150, 255 } :
                 td->state == TEST_FAILED ? (Color){ 255, 100, 110, 255 } : (Color){ 255, 205, 90, 255 };
    Rectangle box = { 10, 8, (float)vw - 20, 84 };
    DrawRectangleRounded(box, 0.15f, 6, (Color){ 8, 10, 22, 225 });
    DrawRectangleRoundedLinesEx(box, 0.15f, 6, 2, edge);

    DrawText(td->title, 22, 16, 14, edge);
    if (td->fastForward && td->state == TEST_RUNNING)
        DrawText("FAST-FORWARD >>", vw - 160, 16, 14, (Color){ 140, 200, 255, 255 });

    int fs = 22;
    while (fs > 12 && MeasureText(td->call, fs) > vw - 44) fs -= 2;
    DrawText(td->call, 22, 34, fs, WHITE);
    DrawText(td->expect, 22, 62, 14, (Color){ 160, 175, 210, 255 });

    const char *status = td->state == TEST_PASSED ? "PASS" : td->state == TEST_FAILED ? "FAIL" : "running...";
    const char *res = td->state == TEST_RUNNING ? status : TextFormat("%s   %s", td->result, status);
    int rs = 14;
    int rw = MeasureText(res, rs);
    int rx = vw - 22 - rw;
    int ew = MeasureText(td->expect, 14);
    if (rx < 22 + ew + 16) { rx = 22; }               // not enough room: put it on its own line
    int ry = (rx == 22) ? 78 : 62;
    if (rx == 22) { DrawRectangle(12, 76, vw - 24, 16, (Color){ 8, 10, 22, 225 }); }
    DrawText(res, rx, ry, rs, edge);
}

static void DrawVignette(int vw, int vh)
{
    Color dark = { 4, 5, 12, 230 }, clear = { 4, 5, 12, 0 };
    int ex = vw / 5, ey = vh / 5;
    DrawRectangleGradientV(0, 0, vw, ey, dark, clear);
    DrawRectangleGradientV(0, vh - ey, vw, ey, clear, dark);
    DrawRectangleGradientH(0, 0, ex, vh, dark, clear);
    DrawRectangleGradientH(vw - ex, 0, ex, vh, clear, dark);
}

void World_DrawBackground(const World *w, int vw, int vh)
{
    const Theme *th = ThemeOf(w);
    ClearBackground(th->fog);
    // soft glow in the middle of the background (uses the glow texture,
    // which works the same on every raylib version)
    float glowR = vw * 0.75f;
    DrawTexturePro(glowTex, (Rectangle){ 0, 0, (float)glowTex.width, (float)glowTex.height },
                   (Rectangle){ vw / 2.0f - glowR, vh / 2.0f - glowR, glowR * 2, glowR * 2 },
                   (Vector2){ 0, 0 }, 0, th->sky);
}

void World_BeginLit(const World *w, const Light *lights, int count, Camera3D cam, float t, bool exitPulse)
{
    if (!shaderOk) return;
    ApplyLights(w, lights, count, cam, t, exitPulse);
    BeginShaderMode(lightShader);
}

void World_EndLit(void)
{
    if (shaderOk) EndShaderMode();
}

void World_SetBittyScreen(const World *w, Vector3 head, Camera3D cam, int vw, int vh)
{
    Vector2 onScreen = GetWorldToScreenEx(head, cam, vw, vh);
    bittyScreen = (Vector2){ w->viewX + onScreen.x, w->viewY + onScreen.y };
    bittyScreenKnown = true;
}

// Is the memory room on screen (rather than the corridor)? Half way through
// beaming in, the picture switches over.
static bool RoomOnScreen(const World *w)
{
    const RoomState *rm = &w->room;
    return rm->mode == ROOM_INSIDE || (rm->mode == ROOM_BEAM_IN && rm->beam >= 0.5f) ||
           (rm->mode == ROOM_BEAM_OUT && rm->beam < 0.5f);
}

void World_Draw(const World *w)
{
    int vw = (int)w->viewW, vh = (int)w->viewH;
    if (vw <= 0 || vh <= 0) return;

    InitGraphics();
    if (vw != targetW || vh != targetH)
    {
        if (targetW > 0) UnloadRenderTexture(target);
        target = LoadRenderTexture(vw, vh);
        targetW = vw;
        targetH = vh;
    }

    float t = (float)GetTime();
    BeginTextureMode(target);

    if (RoomOnScreen(w))
    {
        Room_Draw(w, vw, vh);
    }
    else
    {
        Camera3D cam = MakeCamera(w, (float)vw / vh);

        // remember where Bitty's head is on screen (his speech bubbles point at it)
        World_SetBittyScreen(w, TilePos(w, w->drawX, w->drawY, 1.05f + BittyHop(w, t) - BittyFall(w)), cam, vw, vh);

        World_DrawBackground(w, vw, vh);
        BeginMode3D(cam);

        Light lights[MAX_LIGHTS];
        int count = GatherLights(w, t, lights);
        World_BeginLit(w, lights, count, cam, t, w->fogRadius <= 0);   // in fog the exit's ripple would give it away
        DrawTiles(w);
        DrawEnvironment(w, t);
        DrawSigns(w);
        World_EndLit();

        DrawBitty(w, t);
        DrawGlows(w, cam, t);
        EndMode3D();

        DrawLabels(w, cam, vw, vh, t);
        DrawFog(w, cam, vw, vh);
    }

    DrawVignette(vw, vh);
    DrawTestHud(w, vw);

    // beaming in or out: a flash of light as the picture switches over
    const RoomState *rm = &w->room;
    if (rm->mode == ROOM_BEAM_IN || rm->mode == ROOM_BEAM_OUT)
    {
        float k = 1 - fabsf(rm->beam - 0.5f) / 0.14f;
        if (k > 0)
        {
            Color m = Customisation_Glow();
            DrawRectangle(0, 0, vw, vh, (Color){ (unsigned char)((m.r + 255) / 2), (unsigned char)((m.g + 255) / 2),
                                                 (unsigned char)((m.b + 255) / 2), (unsigned char)(235 * Clamp01(k)) });
        }
    }
    EndTextureMode();

    // Render textures are stored upside down, hence the negative height
    DrawTextureRec(target.texture, (Rectangle){ 0, 0, (float)vw, (float)-vh }, (Vector2){ w->viewX, w->viewY }, WHITE);
}
