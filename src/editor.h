// editor.h - the Code panel: a small text editor with syntax colours.
//
// Highlights:  runLine   (yellow) = the line the VM is running now
//              errorLine (red)    = the line with a compile or runtime error

#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include "raylib.h"

#define EDITOR_MAX_TEXT 8192

typedef struct {
    char      text[EDITOR_MAX_TEXT];
    int       len;
    int       cursor;       // index into text
    int       scrollLine;   // first visible line
    float     blink;
    bool      focused;
    bool      readOnly;     // true while the program runs

    int       runLine;      // 0 = none
    int       errorLine;    // 0 = none

    Rectangle bounds;
    Font      font;
    float     fontSize;
    float     spacing;
    float     lineHeight;
} Editor;

void        Editor_Init(Editor *e, Rectangle bounds, Font font, float fontSize);
void        Editor_SetText(Editor *e, const char *text);
const char *Editor_GetText(const Editor *e);
void        Editor_Update(Editor *e);          // keyboard + mouse
void        Editor_Draw(const Editor *e);

// Returns true (once) if the text was changed since the last call
bool        Editor_TakeChanged(Editor *e);

#endif
