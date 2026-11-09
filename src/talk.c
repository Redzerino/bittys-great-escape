// talk.c - Bitty's speech: the talk box, the little bubbles, and his voice. See talk.h.

#include "talk.h"
#include "customisation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TALK_MAX_STEPS   40
#define TALK_LINE_MAX    240
#define LETTERS_PER_SEC  38.0f
#define BARK_SECONDS     2.8f
#define VOICES           4

typedef struct {
    bool      walk;                 // true = Bitty walks, false = Bitty says 'text'
    Direction dir;
    int       count;
    char      text[TALK_LINE_MAX];
} TalkStep;

static TalkStep steps[TALK_MAX_STEPS];
static int      stepCount = 0;
static int      stepIndex = 0;
static bool     active = false;
static float    shown = 0;          // how many letters of the current line are visible
static int      walkLeft = 0;       // steps still to walk in the current @walk
static bool     walkStarted = false;
static float    arrowTime = 0;

static char     bark[120] = "";
static float    barkTime = 0;       // seconds left
static float    barkShown = 0;

// ---- voice ----
static Sound    voice[VOICES];
static bool     voiceReady = false;
static bool     audioOwned = false;
static bool     muted = false;
static int      nextVoice = 0;
static int      lastBeepLetter = -1;

// ===========================================================================
// Voice: one short blip per letter, pitched by the letter, like a chatty robot
// ===========================================================================
void Talk_Init(void)
{
    if (!IsAudioDeviceReady())
    {
        InitAudioDevice();
        audioOwned = IsAudioDeviceReady();
    }
    if (!IsAudioDeviceReady()) return;      // no sound card: Bitty talks silently

    const int rate = 22050;
    const int frames = (int)(rate * 0.075f);
    short *samples = malloc(sizeof(short) * (size_t)frames);
    if (!samples) return;

    for (int i = 0; i < frames; i++)
    {
        float t = (float)i / rate;
        float freq = 430.0f + 40.0f * sinf(t * 90.0f);             // a tiny warble
        float phase = fmodf(t * freq, 1.0f);
        float triangle = 4.0f * fabsf(phase - 0.5f) - 1.0f;
        float square = phase < 0.5f ? 1.0f : -1.0f;
        float attack = t < 0.004f ? t / 0.004f : 1.0f;
        float decay = expf(-t * 38.0f);
        float v = (triangle * 0.75f + square * 0.25f) * attack * decay;
        samples[i] = (short)(v * 11000);
    }
    Wave wave = { (unsigned int)frames, (unsigned int)rate, 16, 1, samples };
    for (int i = 0; i < VOICES; i++) voice[i] = LoadSoundFromWave(wave);
    free(samples);
    voiceReady = voice[0].frameCount > 0;
}

void Talk_Shutdown(void)
{
    if (voiceReady)
        for (int i = 0; i < VOICES; i++) UnloadSound(voice[i]);
    voiceReady = false;
    if (audioOwned) CloseAudioDevice();
    audioOwned = false;
}

static void Beep(char letter)
{
    if (!voiceReady || muted) return;
    int c = (letter >= 'A' && letter <= 'Z') ? letter - 'A' : (letter >= 'a' && letter <= 'z') ? letter - 'a' : letter % 26;
    bool vowel = (letter == 'a' || letter == 'e' || letter == 'i' || letter == 'o' || letter == 'u');
    float pitch = 0.95f + ((c * 7) % 9) * 0.055f + (vowel ? 0.12f : 0.0f);
    Sound s = voice[nextVoice];
    nextVoice = (nextVoice + 1) % VOICES;
    SetSoundPitch(s, pitch);
    SetSoundVolume(s, 0.5f);
    PlaySound(s);
}

// Number of letters the player sees (backticks only switch the colour)
static int VisibleLength(const char *text)
{
    int n = 0;
    for (const char *p = text; *p; p++)
        if (*p != '`') n++;
    return n;
}

static char VisibleLetter(const char *text, int index)
{
    int n = 0;
    for (const char *p = text; *p; p++)
    {
        if (*p == '`') continue;
        if (n == index) return *p;
        n++;
    }
    return ' ';
}

// Play blips as new letters appear (every second letter, skipping spaces)
static void VoiceFor(const char *text, float visible)
{
    int upTo = (int)visible;
    if (upTo <= lastBeepLetter) return;
    lastBeepLetter = upTo;
    char letter = VisibleLetter(text, upTo - 1 < 0 ? 0 : upTo - 1);
    if (upTo % 2 == 0 && letter != ' ' && letter != '.' && letter != ',') Beep(letter);
}

// ===========================================================================
// Scripts
// ===========================================================================
static bool ParseDirection(const char *word, Direction *out)
{
    if (strcmp(word, "NORTH") == 0) { *out = NORTH; return true; }
    if (strcmp(word, "EAST") == 0)  { *out = EAST;  return true; }
    if (strcmp(word, "SOUTH") == 0) { *out = SOUTH; return true; }
    if (strcmp(word, "WEST") == 0)  { *out = WEST;  return true; }
    return false;
}

void Talk_Start(const char *script)
{
    stepCount = 0;
    stepIndex = 0;
    shown = 0;
    lastBeepLetter = -1;
    walkStarted = false;
    active = false;
    if (!script) return;

    const char *p = script;
    while (*p && stepCount < TALK_MAX_STEPS)
    {
        // one line
        char line[TALK_LINE_MAX];
        int n = 0;
        while (*p && *p != '\n')
        {
            if (n < TALK_LINE_MAX - 1 && *p != '\r') line[n++] = *p;
            p++;
        }
        if (*p == '\n') p++;
        while (n > 0 && line[n - 1] == ' ') n--;
        line[n] = '\0';
        const char *start = line;
        while (*start == ' ') start++;
        if (*start == '\0') continue;

        TalkStep *step = &steps[stepCount];
        memset(step, 0, sizeof *step);
        if (strncmp(start, "@walk", 5) == 0)
        {
            char word[16] = "";
            int count = 1;
            if (sscanf(start + 5, "%15s %d", word, &count) >= 1 && ParseDirection(word, &step->dir))
            {
                step->walk = true;
                step->count = count < 1 ? 1 : count > 16 ? 16 : count;
                stepCount++;
            }
            continue;       // a line starting with @ is never spoken
        }
        if (*start == '@') continue;
        snprintf(step->text, sizeof step->text, "%s", start);
        stepCount++;
    }
    active = stepCount > 0;
    barkTime = 0;
}

void Talk_Skip(void)
{
    active = false;
}

bool Talk_Active(void)
{
    return active;
}

void Talk_Bark(const char *text)
{
    if (active) return;             // don't talk over the lesson
    snprintf(bark, sizeof bark, "%s", text);
    barkTime = BARK_SECONDS;
    barkShown = 0;
    lastBeepLetter = -1;
}

static void NextStep(void)
{
    stepIndex++;
    shown = 0;
    lastBeepLetter = -1;
    walkStarted = false;
    if (stepIndex >= stepCount) active = false;
}

bool Talk_Update(World *w, float dt, Rectangle panel)
{
    arrowTime += dt;

    // the little bubble
    if (barkTime > 0)
    {
        barkTime -= dt;
        barkShown += dt * LETTERS_PER_SEC * 1.4f;
        if (barkShown < VisibleLength(bark)) VoiceFor(bark, barkShown);
    }

    if (!active)
    {
        w->talking = false;
        w->speaking = barkTime > 0 && barkShown < VisibleLength(bark);
        return false;
    }

    if (IsKeyPressed(KEY_ESCAPE)) active = false;

    if (active && w->spawn <= 0)        // wait until Bitty has landed
    {
        TalkStep *step = &steps[stepIndex];
        if (step->walk)
        {
            w->talking = false;
            w->speaking = false;
            if (!walkStarted) { walkLeft = step->count; walkStarted = true; }
            if (World_ActionDone(w))
            {
                if (walkLeft > 0 && w->state == WORLD_RUNNING)
                {
                    Action a = { ACT_MOVE, step->dir, 0, 0 };
                    World_StartAction(w, a);
                    walkLeft--;
                }
                else NextStep();
            }
        }
        else
        {
            int length = VisibleLength(step->text);
            w->talking = true;
            w->speaking = shown < length;
            if (shown < length)
            {
                shown += dt * LETTERS_PER_SEC;
                VoiceFor(step->text, shown);
            }

            bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), panel);
            // the sound on/off label has its own click (see Talk_Draw)
            Rectangle soundLabel = { panel.x + panel.width - 150, panel.y + panel.height - 40, 134, 26 };
            if (click && CheckCollisionPointRec(GetMousePosition(), soundLabel))
            {
                muted = !muted;
                click = false;
            }
            if (click || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))
            {
                if (shown < length) shown = (float)length;      // first press: finish the line
                else NextStep();                                // second press: next line
            }
        }
    }

    if (!active)
    {
        w->talking = false;
        w->speaking = false;
        return true;        // finished this frame
    }
    return false;
}

// ===========================================================================
// Drawing
// ===========================================================================

// Draws 'text' wrapped to maxWidth, showing only the first 'visible' letters.
// Words inside `backticks` are gold. Returns the height used.
static float DrawSpeech(Font font, const char *text, float x, float y, float maxWidth, float size, int visible, Color ink, bool draw)
{
    const Color GOLD_INK = { 255, 214, 110, 255 };
    float lineHeight = size * 1.35f;
    float cx = x, cy = y;
    bool code = false;
    int index = 0;      // visible letter number

    const char *p = text;
    while (*p)
    {
        // measure the next word (up to a space)
        float wordWidth = 0;
        const char *q = p;
        while (*q && *q != ' ')
        {
            if (*q != '`')
            {
                char one[2] = { *q, '\0' };
                wordWidth += MeasureTextEx(font, one, size, 0).x;
            }
            q++;
        }
        if (cx > x && cx + wordWidth > x + maxWidth) { cx = x; cy += lineHeight; }

        // draw it letter by letter
        while (p < q)
        {
            if (*p == '`') { code = !code; p++; continue; }
            char one[2] = { *p, '\0' };
            if (draw && index < visible) DrawTextEx(font, one, (Vector2){ cx, cy }, size, 0, code ? GOLD_INK : ink);
            cx += MeasureTextEx(font, one, size, 0).x;
            index++;
            p++;
        }
        if (*p == ' ')
        {
            cx += MeasureTextEx(font, " ", size, 0).x;
            index++;
            p++;
        }
    }
    return cy + lineHeight - y;
}

static void DrawTalkBox(Rectangle panel, Font font)
{
    const TalkStep *step = &steps[stepIndex];
    if (step->walk) return;                 // nothing to say while walking

    Color glow = Customisation_Glow();
    float size = panel.width > 760 ? 24.0f : 19.0f;
    float textH = DrawSpeech(font, step->text, 0, 0, panel.width - 36 - 44, size, 0, WHITE, false);
    if (textH < size * 1.35f * 2) textH = size * 1.35f * 2;
    float boxH = textH + 66;
    Rectangle box = { panel.x + 18, panel.y + panel.height - boxH - 16, panel.width - 36, boxH };

    // a tail pointing up at Bitty
    float bx, by;
    if (World_BittyOnScreen(&bx, &by))
    {
        if (bx < box.x + 60) bx = box.x + 60;
        if (bx > box.x + box.width - 60) bx = box.x + box.width - 60;
        if (by < box.y - 14)
            DrawTriangle((Vector2){ bx, box.y - 16 }, (Vector2){ bx - 12, box.y + 2 }, (Vector2){ bx + 12, box.y + 2 },
                         (Color){ 12, 16, 30, 240 });
    }

    DrawRectangleRounded(box, 0.18f, 10, (Color){ 12, 16, 30, 240 });
    DrawRectangleRoundedLinesEx(box, 0.18f, 10, 2, glow);

    // name tag
    Rectangle tag = { box.x + 18, box.y - 15, 92, 28 };
    DrawRectangleRounded(tag, 0.5f, 8, glow);
    DrawText("BITTY", (int)tag.x + 14, (int)tag.y + 5, 20, (Color){ 12, 16, 30, 255 });

    int length = VisibleLength(step->text);
    DrawSpeech(font, step->text, box.x + 22, box.y + 24, box.width - 44, size, (int)shown, (Color){ 235, 240, 252, 255 }, true);

    // bottom row: how to continue, sound switch, and how far through we are
    Color faint = { 130, 150, 190, 255 };
    float rowY = box.y + box.height - 26;
    DrawTextEx(font, "Space or click: next     Esc: skip", (Vector2){ box.x + 22, rowY }, 14, 0, faint);
    int speech = 0, speechTotal = 0;
    for (int i = 0; i < stepCount; i++)
        if (!steps[i].walk) { speechTotal++; if (i <= stepIndex) speech++; }
    const char *count = TextFormat("%s     %d / %d", muted ? "sound: off" : "sound: on", speech, speechTotal);
    DrawTextEx(font, count, (Vector2){ box.x + box.width - 22 - MeasureTextEx(font, count, 14, 0).x, rowY }, 14, 0, faint);

    // bouncing arrow when the line is finished
    if ((int)shown >= length)
    {
        float bob = sinf(arrowTime * 6) * 3;
        float ax = box.x + box.width - 34, ay = box.y + box.height - 50 + bob;
        DrawTriangle((Vector2){ ax - 8, ay }, (Vector2){ ax, ay + 10 }, (Vector2){ ax + 8, ay }, glow);
    }
}

static void DrawBark(Rectangle panel, Font font)
{
    float bx, by;
    if (barkTime <= 0 || !World_BittyOnScreen(&bx, &by)) return;

    float size = 17;
    float fade = barkTime < 0.4f ? barkTime / 0.4f : 1.0f;
    float maxWidth = 250;
    float textW = MeasureTextEx(font, bark, size, 0).x;
    if (textW > maxWidth) textW = maxWidth;
    float textH = DrawSpeech(font, bark, 0, 0, maxWidth, size, 0, WHITE, false);

    Rectangle bubble = { bx - textW / 2 - 12, by - textH - 34, textW + 24, textH + 14 };
    if (bubble.x < panel.x + 8) bubble.x = panel.x + 8;
    if (bubble.x + bubble.width > panel.x + panel.width - 8) bubble.x = panel.x + panel.width - 8 - bubble.width;
    if (bubble.y < panel.y + 34) bubble.y = panel.y + 34;

    Color fill = { 235, 240, 252, (unsigned char)(235 * fade) };
    DrawRectangleRounded(bubble, 0.45f, 8, fill);
    DrawTriangle((Vector2){ bx - 8, bubble.y + bubble.height - 1 }, (Vector2){ bx, bubble.y + bubble.height + 10 },
                 (Vector2){ bx + 8, bubble.y + bubble.height - 1 }, fill);
    DrawSpeech(font, bark, bubble.x + 12, bubble.y + 7, maxWidth, size, (int)barkShown,
               (Color){ 20, 26, 46, (unsigned char)(255 * fade) }, true);
}

void Talk_Draw(Rectangle panel, Font font)
{
    if (active) DrawTalkBox(panel, font);
    else DrawBark(panel, font);
}
