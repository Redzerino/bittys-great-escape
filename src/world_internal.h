// world_internal.h - drawing helpers shared by world.c and room.c.
// Not part of the game API: only the world's own files include this.

#ifndef WORLD_INTERNAL_H
#define WORLD_INTERNAL_H

#include "game_api.h"
#include "raylib.h"

#define PI_F       3.14159265f
#define MAX_LIGHTS 8
#define FALL_SECONDS 1.6f   // falling into the void

// A point light for the lighting shader
typedef struct {
    Vector3 pos;
    Color   color;
    float   strength;
    float   radius;
} Light;

// The colours of the area of memory a level is in (see THEMES in world.c)
typedef struct {
    Color fog;        // what the distance fades into
    Color sky;        // the glow behind everything
    Color a, b;       // the two accent colours (lights, streams, windows)
    float ambient[3]; // colour of the shadows
} Theme;

float        Clamp01(float v);
float        Smooth(float v);                       // 0..1 eased in and out
Color        Mix(Color a, Color b, float t);
Color        Alpha(Color c, float a);               // same colour, alpha 0..255
const Theme *ThemeOf(const World *w);
Color        MoodColor(const World *w);             // Bitty's eye colour
Vector3      TilePos(const World *w, float col, float row, float y);

void DrawBlob(Vector3 pos, Vector3 scale, Color col);            // squashed sphere
void DrawRoundedBox(Vector3 c, Vector3 size, float r, Color col);
void DrawStone(Vector3 pos, Vector3 size, Color col);            // block with a soft top edge
void Glow(Camera3D cam, Vector3 pos, float size, Color col);     // soft dot of light (additive pass)
void DrawLabel(Vector3 at, Camera3D cam, int w, int h, const char *text, int size, Color col);
void DrawBitty(const World *w, float t);                         // at TilePos(w->drawX, w->drawY)
void DrawParticles(Camera3D cam);                                 // dust, sparks, confetti (additive pass)
void Burst_Sparks(Vector3 at, int count, float power, Color col);

// The background (fog colour and the soft glow behind everything)
void World_DrawBackground(const World *w, int vw, int vh);
// Everything drawn between these is lit by 'lights' (does nothing if shaders aren't supported)
void World_BeginLit(const World *w, const Light *lights, int count, Camera3D cam, float t, bool exitPulse);
void World_EndLit(void);
// Where Bitty's head is on screen, for his speech bubbles
void World_SetBittyScreen(const World *w, Vector3 head, Camera3D cam, int vw, int vh);

#endif
