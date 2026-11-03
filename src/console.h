// console.h - the Console panel: printf output and game messages.

#ifndef CONSOLE_H
#define CONSOLE_H

#include "raylib.h"

typedef enum {
    CONSOLE_NORMAL = 0,   // printf output
    CONSOLE_STORY,        // Bitty talking / level text
    CONSOLE_WARNING,      // yellow
    CONSOLE_ERROR,        // red
    CONSOLE_SUCCESS       // green
} ConsoleKind;

void Console_Clear(void);
void Console_PrintKind(ConsoleKind kind, const char *text);   // Console_Print (game_api.h) = CONSOLE_NORMAL
void Console_Draw(Rectangle bounds, Font font);

#endif
