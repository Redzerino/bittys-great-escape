// console.c - the Console panel. Keeps the most recent lines, newest at the bottom.

#include "console.h"
#include "game_api.h"

#include <string.h>

#define CONSOLE_LINES     200
#define CONSOLE_LINE_MAX  160

typedef struct {
    char        text[CONSOLE_LINE_MAX];
    ConsoleKind kind;
} ConsoleLine;

static ConsoleLine lines[CONSOLE_LINES];
static int lineCount = 0;
static bool lastLineOpen = false;   // printf("A"); printf("B\n"); should give one line "AB"
static float scrollUp = 0;          // pixels scrolled back from the newest line (mouse wheel)

static ConsoleLine *NewLine(ConsoleKind kind)
{
    if (lineCount == CONSOLE_LINES)
    {
        memmove(lines, lines + 1, sizeof(ConsoleLine) * (CONSOLE_LINES - 1));
        lineCount--;
    }
    ConsoleLine *l = &lines[lineCount++];
    l->text[0] = '\0';
    l->kind = kind;
    return l;
}

void Console_Clear(void)
{
    lineCount = 0;
    lastLineOpen = false;
    scrollUp = 0;
}

void Console_PrintKind(ConsoleKind kind, const char *text)
{
    scrollUp = 0;   // new output: jump back to the newest line
    ConsoleLine *cur = NULL;
    if (lastLineOpen && lineCount > 0 && lines[lineCount - 1].kind == kind) cur = &lines[lineCount - 1];

    for (const char *p = text; *p; p++)
    {
        if (!cur) cur = NewLine(kind);
        if (*p == '\n')
        {
            cur = NULL;
            continue;
        }
        size_t len = strlen(cur->text);
        if (len < CONSOLE_LINE_MAX - 1)
        {
            cur->text[len] = *p;
            cur->text[len + 1] = '\0';
        }
        else
        {
            cur = NewLine(kind);   // wrap very long lines
            cur->text[0] = *p;
            cur->text[1] = '\0';
        }
    }
    lastLineOpen = (cur != NULL);
}

void Console_Print(const char *text)
{
    Console_PrintKind(CONSOLE_NORMAL, text);
}

// Split a long line into pieces that fit the panel width
static int WrapLine(const char *text, Font font, float size, float maxWidth, int *breaks, int maxBreaks)
{
    int count = 0;
    int len = (int)strlen(text);
    int start = 0;
    char buf[CONSOLE_LINE_MAX];

    while (start < len && count < maxBreaks)
    {
        int end = len;
        for (;;)
        {
            int n = end - start;
            memcpy(buf, text + start, (size_t)n);
            buf[n] = '\0';
            if (MeasureTextEx(font, buf, size, 0).x <= maxWidth || n <= 1) break;
            // step back to a space if possible
            int sp = end - 1;
            while (sp > start && text[sp] != ' ') sp--;
            end = (sp > start) ? sp : end - 1;
        }
        breaks[count++] = end;
        start = end;
        while (start < len && text[start] == ' ') start++;
    }
    if (count == 0) breaks[count++] = 0;
    return count;
}

void Console_Draw(Rectangle b, Font font)
{
    DrawRectangleRec(b, (Color){ 20, 24, 36, 255 });
    DrawRectangleLinesEx(b, 2, (Color){ 60, 200, 255, 255 });
    DrawText("CONSOLE", (int)b.x + 10, (int)b.y + 8, 18, (Color){ 60, 200, 255, 255 });

    const float size = 16, lineH = 19;
    float maxWidth = b.width - 36;
    float top = b.y + 32, bottom = b.y + b.height - 8;

    // Mouse wheel over the console scrolls back through older lines
    if (CheckCollisionPointRec(GetMousePosition(), b) && !IsKeyDown(KEY_LEFT_CONTROL))
        scrollUp += GetMouseWheelMove() * lineH * 3;
    if (scrollUp < 0) scrollUp = 0;

    BeginScissorMode((int)b.x + 2, (int)top, (int)b.width - 4, (int)(bottom - top));

    // Draw from the newest line upwards
    float y = bottom + scrollUp;
    for (int i = lineCount - 1; i >= 0; i--)
    {
        const ConsoleLine *l = &lines[i];
        Color col = (Color){ 230, 230, 235, 255 };
        const char *prefix = "> ";
        switch (l->kind)
        {
            case CONSOLE_STORY:   col = (Color){ 150, 200, 255, 255 }; prefix = "  "; break;
            case CONSOLE_WARNING: col = (Color){ 255, 210, 90, 255 };  prefix = "! "; break;
            case CONSOLE_ERROR:   col = (Color){ 255, 110, 110, 255 }; prefix = "X "; break;
            case CONSOLE_SUCCESS: col = (Color){ 110, 240, 140, 255 }; prefix = "* "; break;
            default: break;
        }

        int breaks[8];
        int pieces = WrapLine(l->text, font, size, maxWidth, breaks, 8);
        y -= pieces * lineH;
        if (y > bottom) continue;   // below the panel (scrolled up past it)
        if (y + pieces * lineH < top) break;

        int start = 0;
        for (int k = 0; k < pieces; k++)
        {
            char buf[CONSOLE_LINE_MAX];
            int n = breaks[k] - start;
            if (n < 0) n = 0;
            memcpy(buf, l->text + start, (size_t)n);
            buf[n] = '\0';
            float ly = y + k * lineH;
            if (k == 0) DrawTextEx(font, prefix, (Vector2){ b.x + 10, ly }, size, 0, col);
            DrawTextEx(font, buf, (Vector2){ b.x + 26, ly }, size, 0, col);
            start = breaks[k];
            while (l->text[start] == ' ') start++;
        }
    }
    // Can't scroll further than the oldest line
    if (y > top && scrollUp > 0) { scrollUp -= (y - top); if (scrollUp < 0) scrollUp = 0; }
    EndScissorMode();

    if (scrollUp > 0)
    {
        const char *more = "newer lines below - scroll down";
        float w = MeasureTextEx(font, more, 14, 0).x;
        DrawRectangleRounded((Rectangle){ b.x + b.width - w - 30, b.y + 7, w + 20, 20 }, 0.5f, 6, (Color){ 40, 60, 90, 255 });
        DrawTextEx(font, more, (Vector2){ b.x + b.width - w - 20, b.y + 9 }, 14, 0, (Color){ 160, 200, 255, 255 });
    }
}
