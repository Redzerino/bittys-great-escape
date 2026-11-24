// sfx.c - code-generated sound effects (see sfx.h)
#include "sfx.h"
#include "raylib.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RATE   22050
#define COPIES 3            // a few copies of each, so quick repeats overlap instead of cutting off

static Sound sounds[SFX_COUNT][COPIES];
static int   nextCopy[SFX_COUNT];
static float volumes[SFX_COUNT];
static bool  ready, muted, ownDevice;

// ---- tiny synth ------------------------------------------------------------
static unsigned noiseState = 22222;
static float Noise(void)
{
    noiseState = noiseState * 1103515245u + 12345u;
    return ((noiseState >> 9) & 0xFFFF) / 32768.0f - 1.0f;
}
static float Square(float phase) { return fmodf(phase, 1.0f) < 0.5f ? 1.0f : -1.0f; }
static float Tri(float phase)    { float p = fmodf(phase, 1.0f); return 4.0f * fabsf(p - 0.5f) - 1.0f; }

// attack/decay envelope: quick fade in, exponential fade out
static float Env(float t, float attack, float decay) { return (t < attack ? t / attack : 1.0f) * expf(-(t - attack) * decay); }

typedef float (*Voice)(float t, float len);

// Makes COPIES separate sounds from one generated wave (plain LoadSoundFromWave,
// so it works on any raylib 4.x / 5.x)
static void Build(Voice v, float seconds, Sound out[COPIES])
{
    int frames = (int)(RATE * seconds);
    short *s = malloc(sizeof(short) * (size_t)frames);
    if (!s) return;
    for (int i = 0; i < frames; i++)
    {
        float t = (float)i / RATE;
        float x = v(t, seconds);
        float fade = (seconds - t) < 0.01f ? (seconds - t) / 0.01f : 1.0f;     // no click at the end
        x *= fade;
        if (x > 1) x = 1;
        if (x < -1) x = -1;
        s[i] = (short)(x * 26000);
    }
    Wave w = { (unsigned int)frames, RATE, 16, 1, s };
    for (int c = 0; c < COPIES; c++) out[c] = LoadSoundFromWave(w);
    free(s);
}

// ---- the sounds ------------------------------------------------------------
static float VClick(float t, float l)  { (void)l; return (Tri(t * 1800) * 0.6f + Noise() * 0.15f) * Env(t, 0.001f, 90); }
static float VHover(float t, float l)  { (void)l; return Tri(t * 2600) * Env(t, 0.001f, 160) * 0.35f; }
static float VRun(float t, float l)
{
    (void)l;
    float f = 300 + 900 * t / 0.28f;                         // rising sweep: "power up"
    return (Square(t * f) * 0.25f + Tri(t * f * 0.5f) * 0.5f) * Env(t, 0.01f, 9);
}
static float VStep(float t, float l)   { (void)l; return (Tri(t * 880) * 0.7f + Tri(t * 1320) * 0.3f) * Env(t, 0.002f, 45) * 0.8f; }
static float VBack(float t, float l)   { (void)l; float f = 700 - 300 * t / 0.12f; return Tri(t * f) * Env(t, 0.002f, 40) * 0.7f; }
static float VPass(float t, float l)
{
    (void)l;                                                  // two quick bright notes
    float f = t < 0.07f ? 1046.5f : 1568.0f;
    float lt = t < 0.07f ? t : t - 0.07f;
    return (Tri(t * f) * 0.7f + Square(t * f) * 0.1f) * Env(lt, 0.003f, 22);
}
static float VFail(float t, float l)
{
    (void)l;                                                  // low wobbly "bwomp"
    float f = 220 - 90 * t / 0.35f + 12 * sinf(t * 60);
    return (Square(t * f) * 0.35f + Tri(t * f) * 0.45f) * Env(t, 0.005f, 7);
}
static float VClear(float t, float l)
{
    (void)l;                                                  // C E G C arpeggio, then a held chord
    static const float notes[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
    float x = 0;
    for (int n = 0; n < 4; n++)
    {
        float start = n * 0.09f;
        if (t < start) continue;
        float lt = t - start;
        float decay = n == 3 ? 3.0f : 9.0f;
        x += (Tri(lt * notes[n]) * 0.55f + Square(lt * notes[n]) * 0.08f) * Env(lt, 0.005f, decay);
    }
    if (t > 0.36f) { float lt = t - 0.36f; x += (Tri(lt * 659.25f) + Tri(lt * 783.99f)) * 0.22f * Env(lt, 0.02f, 3.5f); }
    return x * 0.6f;
}
static float VCrash(float t, float l)
{
    (void)l;                                                  // crunchy noise burst + falling tone
    float f = 400 - 340 * t / 0.5f;
    float crush = floorf(Noise() * 4) / 4;
    return (crush * 0.55f * Env(t, 0.002f, 8) + Square(t * f) * 0.3f * Env(t, 0.01f, 5));
}
static float VError(float t, float l)  { (void)l; return Square(t * 150) * 0.4f * (fmodf(t, 0.1f) < 0.06f ? 1.0f : 0.0f) * Env(t, 0.003f, 6); }
static float VMove(float t, float l)   { (void)l; return (Noise() * 0.25f + Tri(t * 140) * 0.6f) * Env(t, 0.002f, 55); }
static float VWhoosh(float t, float l)
{
    // filtered noise that swells and fades
    static float low = 0;
    float n = Noise();
    low += (n - low) * (0.05f + 0.25f * sinf(PI * t / l));
    return low * 1.0f * sinf(PI * t / l);
}
static float VStreak(float t, float l)
{
    (void)l;                                                  // three rising notes
    static const float notes[3] = { 783.99f, 987.77f, 1318.5f };
    int n = (int)(t / 0.07f); if (n > 2) n = 2;
    float lt = t - n * 0.07f;
    return Tri(t * notes[n]) * 0.7f * Env(lt, 0.003f, n == 2 ? 8 : 25);
}
static float VLogin(float t, float l)
{
    (void)l;                                                  // soft major chord bloom
    return (Tri(t * 392.0f) + Tri(t * 493.88f) + Tri(t * 587.33f)) * 0.25f * Env(t, 0.04f, 3.0f);
}
static float VType(float t, float l)   { (void)l; return (Noise() * 0.3f + Tri(t * 2200) * 0.3f) * Env(t, 0.0005f, 220); }
static float VSelect(float t, float l)
{
    (void)l;                                                  // big "boom" with a shimmer: the cinematic pick
    float boom = sinf(2 * PI * (60 + 40 * expf(-t * 12)) * t) * Env(t, 0.003f, 4.5f);
    float shimmer = (Tri(t * 1046.5f) + Tri(t * 1568.0f)) * 0.18f * Env(t, 0.05f, 3.5f);
    return boom * 0.8f + shimmer;
}

// ---- API -------------------------------------------------------------------
void Sfx_Init(void)
{
    if (!IsAudioDeviceReady()) { InitAudioDevice(); ownDevice = IsAudioDeviceReady(); }
    if (!IsAudioDeviceReady()) return;            // no sound card: play silently

    struct { SfxId id; Voice v; float len, vol; } list[] = {
        { SFX_CLICK, VClick, 0.06f, 0.45f }, { SFX_HOVER, VHover, 0.04f, 0.25f },
        { SFX_RUN, VRun, 0.32f, 0.55f },     { SFX_STEP, VStep, 0.10f, 0.5f },
        { SFX_BACK, VBack, 0.12f, 0.5f },    { SFX_PASS, VPass, 0.22f, 0.5f },
        { SFX_FAIL, VFail, 0.40f, 0.55f },   { SFX_CLEAR, VClear, 1.1f, 0.65f },
        { SFX_CRASH, VCrash, 0.55f, 0.55f }, { SFX_ERROR, VError, 0.32f, 0.45f },
        { SFX_MOVE, VMove, 0.07f, 0.35f },   { SFX_WHOOSH, VWhoosh, 0.35f, 0.4f },
        { SFX_STREAK, VStreak, 0.32f, 0.55f },{ SFX_LOGIN, VLogin, 0.9f, 0.55f },
        { SFX_TYPE, VType, 0.03f, 0.3f },    { SFX_SELECT, VSelect, 1.2f, 0.75f },
    };
    for (size_t i = 0; i < sizeof list / sizeof list[0]; i++)
    {
        Build(list[i].v, list[i].len, sounds[list[i].id]);
        volumes[list[i].id] = list[i].vol;
    }
    ready = true;
}

void Sfx_Shutdown(void)
{
    if (!ready) return;
    for (int i = 0; i < SFX_COUNT; i++)
    {
        for (int c = 0; c < COPIES; c++) if (sounds[i][c].frameCount) UnloadSound(sounds[i][c]);
    }
    ready = false;
    if (ownDevice) CloseAudioDevice();
}

void Sfx_PlayPitched(SfxId id, float pitch)
{
    if (!ready || muted || id < 0 || id >= SFX_COUNT || sounds[id][0].frameCount == 0) return;
    Sound s = sounds[id][nextCopy[id]];
    nextCopy[id] = (nextCopy[id] + 1) % COPIES;
    SetSoundPitch(s, pitch);
    SetSoundVolume(s, volumes[id]);
    PlaySound(s);
}

void Sfx_Play(SfxId id) { Sfx_PlayPitched(id, 1.0f); }

void Sfx_Update(void)
{
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_M)) muted = !muted;
}

bool Sfx_Muted(void) { return muted; }
