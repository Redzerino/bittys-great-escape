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
#include "raylib.h"
#include "rlgl.h"

#include <math.h>
#include <string.h>

#define MOVE_SECONDS  0.40f
#define WRITE_SECONDS 0.35f
#define FALL_SECONDS  1.6f
#define PI_F          3.14159265f

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

            if (ch == '#')                   t->type = TILE_WALL;
            else if (ch == 'E')              t->type = TILE_EXIT;
            else if (ch == 'x' || ch == ' ') t->type = TILE_VOID;
            else if (ch >= '0' && ch <= '9') { t->type = TILE_DATA; t->value = ch - '0'; }
            else                             t->type = TILE_EMPTY;

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

        bool blocked = InGrid(w, nx, ny) && w->tiles[ny][nx].type == TILE_WALL;
        if (blocked)
        {
            w->lastResult = 0;          // bumped: stays put
            w->shake = 0.35f;
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
        w->flash = 0.5f;
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
            // Bump: lean towards the wall and bounce back
            int dx, dy;
            Step(w->current.dir, &dx, &dy);
            float lean = sinf(p * PI_F) * 0.25f;
            w->drawX = w->fromX + dx * lean;
            w->drawY = w->fromY + dy * lean;
        }
    }

    if (w->actionTimer <= 0)
    {
        w->drawX = (float)w->bittyX;
        w->drawY = (float)w->bittyY;
        w->current.type = ACT_NONE;

        if (!InGrid(w, w->bittyX, w->bittyY) || w->tiles[w->bittyY][w->bittyX].type == TILE_VOID)
        {
            w->state  = WORLD_SEGFAULT;
            w->glitch = FALL_SECONDS;
            w->mood   = MOOD_ERROR;
        }
        else if (w->tiles[w->bittyY][w->bittyX].type == TILE_EXIT)
        {
            w->state  = WORLD_WON;
            w->mood   = MOOD_HAPPY;
            w->facing = 0;              // turn to the camera to celebrate
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

#define MAX_LIGHTS 8
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
static Vector3 TilePos(const World *w, float col, float row, float y)
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
    unsigned h = (unsigned)(a * 73856093) ^ (unsigned)(b * 19349663);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

static Color Mix(Color a, Color b, float t)
{
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}

// A squashed sphere
static void DrawBlob(Vector3 pos, Vector3 scale, Color col)
{
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlScalef(scale.x, scale.y, scale.z);
    DrawSphereEx((Vector3){ 0, 0, 0 }, 1.0f, 14, 16, col);
    rlPopMatrix();
}

// A box with rounded edges and corners (Bitty's chunky stone body)
static void DrawRoundedBox(Vector3 c, Vector3 size, float r, Color col)
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

static Color MoodColor(const World *w)
{
    switch (w->mood)
    {
        case MOOD_RUNNING: return (Color){ 120, 250, 170, 255 };
        case MOOD_ERROR:   return (Color){ 255, 90, 110, 255 };
        case MOOD_HAPPY:   return (Color){ 255, 215, 120, 255 };
        default:           return (Color){ 130, 210, 255, 255 };
    }
}

// Where Bitty is right now (including the hop and the fall)
static float BittyHop(const World *w, float t)
{
    if (w->mood == MOOD_HAPPY) return fabsf(sinf(t * 7)) * 0.3f;
    if (w->current.type == ACT_MOVE && w->lastResult && w->actionLength > 0)
    {
        float p = 1 - w->actionTimer / w->actionLength;
        if (p < 0) p = 0;
        if (p > 1) p = 1;
        return sinf(p * PI_F) * 0.35f;
    }
    return 0;
}

static float BittyFall(const World *w)
{
    if (w->state != WORLD_SEGFAULT) return 0;
    float f = 1 - w->glitch / FALL_SECONDS;
    if (f < 0) f = 0;
    return f * f * 3.0f;
}

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------
typedef struct {
    Vector3 pos;
    Color   color;
    float   strength;
    float   radius;
} Light;

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
            if (w->tiles[r][c].type == TILE_EXIT)
                lights[n++] = (Light){ TilePos(w, (float)c, (float)r, 0.6f), (Color){ 110, 255, 170, 255 },
                                       1.2f + sinf(t * 3) * 0.2f, 2.6f };

    // Crystals
    for (int r = 0; r < w->rows && n < MAX_LIGHTS; r++)
        for (int c = 0; c < w->cols && n < MAX_LIGHTS; c++)
            if (HasCrystal(w, c, r))
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

static void ApplyLights(const World *w, const Light *lights, int count, Camera3D cam, float t)
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
    float ambient[3] = { 0.16f, 0.17f, 0.27f };   // deep blue shadows
    float view[3]    = { cam.position.x, cam.position.y, cam.position.z };

    SetShaderValue(lightShader, locViewPos, view, SHADER_UNIFORM_VEC3);
    SetShaderValue(lightShader, locAmbient, ambient, SHADER_UNIFORM_VEC3);
    SetShaderValue(lightShader, locLightCount, &count, SHADER_UNIFORM_INT);
    SetShaderValueV(lightShader, locLightPos, pos, SHADER_UNIFORM_VEC3, count);
    SetShaderValueV(lightShader, locLightColor, col, SHADER_UNIFORM_VEC3, count);
    SetShaderValueV(lightShader, locLightRadius, rad, SHADER_UNIFORM_FLOAT, count);

    float camDist = Vector3Length3(cam.position, cam.target);
    float fogColor[3] = { 7 / 255.0f, 8 / 255.0f, 18 / 255.0f };
    float fogRange[2] = { camDist * 0.95f, camDist * 2.4f };
    SetShaderValue(lightShader, locFogColor, fogColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(lightShader, locFogRange, fogRange, SHADER_UNIFORM_VEC2);

    Vector3 exitPos = { 0, 0, 0 };
    float strength = 0, radius = 0;
    if (ExitPosition(w, &exitPos))
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
static void DrawStone(Vector3 pos, Vector3 size, Color col)
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
            Vector3 p = TilePos(w, (float)c, (float)r, 0);
            unsigned h = Hash(c, r);
            float jitter = (h % 7) * 0.008f;   // tiny height differences feel hand-made

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
                    DrawStone((Vector3){ p.x, -0.12f, p.z }, (Vector3){ 0.96f, 0.2f, 0.96f }, COL_EXIT);
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
}

// A soft glowing dot that always faces the camera
static void Glow(Camera3D cam, Vector3 pos, float size, Color col)
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
    Color trace = { 70, 140, 220, 80 };
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
        Color c = (i % 3) ? (Color){ 90, 200, 255, 200 } : (Color){ 170, 120, 255, 200 };
        Glow(cam, pos, 1.6f, c);
    }

    // Glowing tips on the pillars
    for (int i = 0; i < 14; i++)
    {
        float a = PI_F + (i / 13.0f) * PI_F + (Hash(i, 3) % 100) / 400.0f;
        float dist = R + 2.2f + (Hash(i, 7) % 100) / 30.0f;
        float x = cosf(a) * dist * 1.35f, z = sinf(a) * dist * 0.9f;
        float top = -1.4f + (Hash(i, 11) % 100) / 45.0f;
        float blink = 0.6f + 0.4f * sinf(t * (0.8f + (i % 3) * 0.4f) + i);
        Color c = (i % 2) ? (Color){ 110, 210, 255, 255 } : (Color){ 180, 130, 255, 255 };
        c.a = (unsigned char)(200 * blink);
        Glow(cam, (Vector3){ x, top + 0.15f, z }, 1.3f, c);
    }
}

// ---------------------------------------------------------------------------
// Bitty (self-lit: Bitty IS the light)
// ---------------------------------------------------------------------------
static void DrawBitty(const World *w, float t)
{
    float squashY = 1, lean = 0;
    float hop = BittyHop(w, t);

    if (w->current.type == ACT_MOVE && w->actionLength > 0)
    {
        float p = 1 - w->actionTimer / w->actionLength;
        if (p < 0) p = 0;
        if (p > 1) p = 1;
        if (w->lastResult)
            squashY = 1 + sinf(p * PI_F) * 0.18f
                        - (p < 0.15f ? (0.15f - p) * 1.2f : 0)          // crouch before the jump
                        - (p > 0.85f ? (p - 0.85f) * 1.2f : 0);         // squish on landing
        else
            squashY = 1 - sinf(p * PI_F) * 0.15f;                       // bonk!
        lean = (w->current.dir == EAST ? -1.0f : w->current.dir == WEST ? 1.0f : 0.0f) * sinf(p * PI_F) * 12;
    }
    else
    {
        squashY = 1 + sinf(t * 2.5f) * 0.03f;                           // breathing
    }

    float fall = BittyFall(w);
    float spin = (w->state == WORLD_SEGFAULT) ? (fall / 3.0f) * 540 : 0;
    if (fall > 2.6f) return;                                            // gone

    float shakeX = (w->shake > 0) ? sinf(t * 70) * 0.04f * (w->shake / 0.35f) : 0;
    Vector3 base = TilePos(w, w->drawX, w->drawY, 0);

    // soft shadow
    if (fall == 0)
        DrawCylinderEx((Vector3){ base.x, 0.0f, base.z }, (Vector3){ base.x, 0.005f, base.z },
                       0.3f - hop * 0.2f, 0.3f - hop * 0.2f, 18, (Color){ 0, 0, 10, 110 });

    rlPushMatrix();
    rlTranslatef(base.x + shakeX, hop - fall, base.z);
    rlRotatef(w->facing * 0.6f + spin, 0, 1, 0);
    rlRotatef(-24, 1, 0, 0);            // lean back a little so the camera sees Bitty's face
    rlRotatef(lean, 0, 0, 1);
    rlScalef(1.5f / sqrtf(squashY), 1.5f * squashY, 1.5f / sqrtf(squashY));

    // Slate-grey stone, like Bulder from Pode: matte, darker underneath, no shine
    const Color BODY   = { 104, 112, 128, 255 };
    const Color BODY_HI= { 122, 130, 146, 255 };
    const Color BODY_D = { 76, 82, 98, 255 };
    const Color DARK   = { 50, 54, 68, 255 };
    const Color SCREEN = { 16, 20, 34, 255 };
    Color eye = MoodColor(w);

    // feet
    DrawBlob((Vector3){ -0.11f, 0.05f, 0.04f }, (Vector3){ 0.085f, 0.055f, 0.1f }, DARK);
    DrawBlob((Vector3){  0.11f, 0.05f, 0.04f }, (Vector3){ 0.085f, 0.055f, 0.1f }, DARK);

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
                   (Color){ eye.r, eye.g, eye.b, 255 });

    // stubby arms (wave when happy)
    float wave = (w->mood == MOOD_HAPPY) ? sinf(t * 12) * 0.07f : sinf(t * 2.5f) * 0.01f;
    DrawBlob((Vector3){ -0.34f, 0.22f + wave, 0.05f }, (Vector3){ 0.055f, 0.075f, 0.055f }, BODY_D);
    DrawBlob((Vector3){  0.34f, 0.22f - wave, 0.05f }, (Vector3){ 0.055f, 0.075f, 0.055f }, BODY_D);

    // headphone ear-pieces with glowing rings
    for (int side = -1; side <= 1; side += 2)
    {
        DrawCylinderEx((Vector3){ side * 0.30f, 0.37f, 0 }, (Vector3){ side * 0.37f, 0.37f, 0 },
                       0.09f, 0.08f, 16, DARK);
        DrawCylinderEx((Vector3){ side * 0.37f, 0.37f, 0 }, (Vector3){ side * 0.385f, 0.37f, 0 },
                       0.055f, 0.045f, 16, eye);
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
        else
        {
            DrawBar((Vector3){ -0.012f, 0.29f, ez }, 0.028f, 0.012f, -22, eye);
            DrawBar((Vector3){  0.012f, 0.29f, ez }, 0.028f, 0.012f,  22, eye);
        }
    }


    // antenna with a glowing bulb
    float wob = sinf(t * 4) * 0.03f + (w->current.type == ACT_MOVE ? sinf(t * 25) * 0.03f : 0);
    DrawCylinderEx((Vector3){ 0.08f, 0.50f, -0.05f }, (Vector3){ 0.08f + wob, 0.72f, -0.05f }, 0.014f, 0.011f, 6, DARK);
    DrawSphereEx((Vector3){ 0.08f + wob, 0.74f, -0.05f }, 0.05f, 8, 10, eye);

    rlPopMatrix();
}

// ---------------------------------------------------------------------------
// Glows (additive, after everything solid, no depth writes)
// ---------------------------------------------------------------------------

static void DrawGlows(const World *w, Camera3D cam, float t)
{
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    DrawEnvironmentGlow(w, cam, t);

    // Bitty's halo
    float fall = BittyFall(w);
    if (fall < 2.6f)
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
            Vector3 p = TilePos(w, (float)c, (float)r, 0);
            TileType type = w->tiles[r][c].type;

            if (type == TILE_EXIT)
            {
                float pulse = 0.5f + 0.5f * sinf(t * 3);
                DrawCylinderEx((Vector3){ p.x, 0.0f, p.z }, (Vector3){ p.x, 1.8f, p.z }, 0.32f, 0.08f, 18,
                               (Color){ 60, 200, 120, (unsigned char)(50 + pulse * 40) });
                Glow(cam, (Vector3){ p.x, 0.3f, p.z }, 1.8f, (Color){ 90, 255, 160, (unsigned char)(70 + pulse * 40) });
                // sparkles rising from the portal
                for (int i = 0; i < 5; i++)
                {
                    float k = fmodf(t * 0.5f + i * 0.2f, 1.0f);
                    float a = i * 1.3f + t;
                    Glow(cam, (Vector3){ p.x + sinf(a) * 0.25f, k * 1.6f, p.z + cosf(a) * 0.25f }, 0.18f,
                         (Color){ 160, 255, 200, (unsigned char)((1 - k) * 200) });
                }
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

    // write() flash
    if (w->flash > 0)
    {
        Vector3 p = TilePos(w, (float)w->bittyX, (float)w->bittyY, 0.1f);
        Glow(cam, p, 1.4f, (Color){ 150, 210, 255, (unsigned char)(w->flash / 0.5f * 220) });
    }

    // memory motes drifting through the dark
    float spanX = w->cols / 2.0f + 0.5f, spanZ = w->rows / 2.0f + 0.5f;
    for (int i = 0; i < MOTE_COUNT; i++)
    {
        float k = fmodf(t * (0.04f + (i % 5) * 0.01f) + i * 0.137f, 1.0f);
        float x = sinf(i * 12.9898f) * spanX + sinf(t * 0.3f + i) * 0.3f;
        float z = cosf(i * 78.233f) * spanZ + cosf(t * 0.25f + i * 2) * 0.3f;
        float y = 0.1f + k * 2.2f;
        unsigned char a = (unsigned char)(sinf(k * PI_F) * (120 + 60 * sinf(t * 3 + i)));
        Color col = (i % 3) ? (Color){ 140, 220, 255, a } : (Color){ 255, 220, 170, a };
        Glow(cam, (Vector3){ x, y, z }, 0.12f + (i % 4) * 0.03f, col);
    }

    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

// ---------------------------------------------------------------------------
// Labels: addresses are revealed by Bitty's light
// ---------------------------------------------------------------------------
static void DrawLabel(Vector3 at, Camera3D cam, int w, int h, const char *text, int size, Color col)
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
            else if (tile->type == TILE_EXIT && !underBitty)
                DrawLabel((Vector3){ p.x, 1.1f, p.z }, cam, vw, vh, "EXIT", 14, (Color){ 210, 255, 225, 230 });
        }
    }
}

// ---------------------------------------------------------------------------
// World_Draw
// ---------------------------------------------------------------------------
static Camera3D MakeCamera(const World *w, float aspect)
{
    float needW = w->cols + 1.0f;
    float needD = w->rows + 1.0f;
    float fitW = needW / (2 * tanf(22.5f * PI_F / 180) * aspect);
    float fitD = needD * 1.05f;
    float dist = fitW > fitD ? fitW : fitD;
    dist *= 1.12f;                       // leave room to see the world around the board
    if (dist < 6.0f) dist = 6.0f;

    // Ease the camera a little towards Bitty, with a slow sway, so the world feels alive
    Vector3 b = TilePos(w, w->drawX, w->drawY, 0);
    Vector3 want = { b.x * 0.3f, 0, b.z * 0.25f };
    if (!camFollowSet) { camFollow = want; camFollowSet = true; }
    float ease = 1 - expf(-GetFrameTime() * 2.5f);
    camFollow.x += (want.x - camFollow.x) * ease;
    camFollow.z += (want.z - camFollow.z) * ease;

    float t = (float)GetTime();
    float swayX = sinf(t * 0.23f) * 0.25f, swayY = sinf(t * 0.31f) * 0.12f;

    Camera3D cam = { 0 };
    cam.target     = (Vector3){ camFollow.x, 0, camFollow.z + 0.25f };
    cam.position   = (Vector3){ camFollow.x + swayX, dist * 1.0f + swayY, camFollow.z + dist * 0.50f };
    cam.up         = (Vector3){ 0, 1, 0 };
    cam.fovy       = 45;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
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
    Camera3D cam = MakeCamera(w, (float)vw / vh);

    BeginTextureMode(target);
    ClearBackground((Color){ 7, 8, 18, 255 });
    // soft blue glow in the middle of the background (uses the glow texture,
    // which works the same on every raylib version)
    float glowR = vw * 0.6f;
    DrawTexturePro(glowTex, (Rectangle){ 0, 0, (float)glowTex.width, (float)glowTex.height },
                   (Rectangle){ vw / 2.0f - glowR, vh / 2.0f - glowR, glowR * 2, glowR * 2 },
                   (Vector2){ 0, 0 }, 0, (Color){ 22, 24, 52, 255 });

    BeginMode3D(cam);

    if (shaderOk)
    {
        Light lights[MAX_LIGHTS];
        int count = GatherLights(w, t, lights);
        ApplyLights(w, lights, count, cam, t);
        BeginShaderMode(lightShader);
        DrawTiles(w);
        DrawEnvironment(w, t);
        EndShaderMode();
    }
    else
    {
        DrawTiles(w);   // no shader support: still playable, just flat
        DrawEnvironment(w, t);
    }

    DrawBitty(w, t);
    DrawGlows(w, cam, t);
    EndMode3D();

    DrawLabels(w, cam, vw, vh, t);
    DrawVignette(vw, vh);
    EndTextureMode();

    // Render textures are stored upside down, hence the negative height
    DrawTextureRec(target.texture, (Rectangle){ 0, 0, (float)vw, (float)-vh }, (Vector2){ w->viewX, w->viewY }, WHITE);
}
