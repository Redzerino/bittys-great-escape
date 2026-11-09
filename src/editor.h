// editor.h - the Code panel: a small text editor with syntax colours.
//
// Keys:  Ctrl+Z / Ctrl+Y   undo / redo         Ctrl+A        select all
//        Ctrl+C / X / V    copy / cut / paste  Shift+arrows  select
//        Ctrl + / -        bigger / smaller text (also Ctrl + mouse wheel)
//
// Highlights:  runLine   (yellow) = the line the VM is running now
//              errorLine (red)    = the line with a compile or runtime error

#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include "raylib.h"

#define EDITOR_MAX_TEXT  8192
#define EDITOR_UNDO_MAX  48

typedef struct {
    char text[EDITOR_MAX_TEXT];
    int  len;
    int  cursor;
} EditorSnapshot;

typedef struct {
    char      text[EDITOR_MAX_TEXT];
    int       len;
    int       cursor;       // index into text
    int       anchor;       // other end of the selection (-1 = nothing selected)
    bool      dragging;     // selecting with the mouse
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

    // undo / redo
    EditorSnapshot undo[EDITOR_UNDO_MAX];
    int       undoCount;
    EditorSnapshot redo[EDITOR_UNDO_MAX];
    int       redoCount;
    int       lastEditKind;
    double    lastEditTime;
} Editor;

void        Editor_Init(Editor *e, Rectangle bounds, Font font, float fontSize);
void        Editor_SetBounds(Editor *e, Rectangle bounds);
void        Editor_SetText(Editor *e, const char *text);       // new level: clears undo history
void        Editor_ReplaceAll(Editor *e, const char *text);    // e.g. back to the starter code: can be undone
const char *Editor_GetText(const Editor *e);
void        Editor_Update(Editor *e);          // keyboard + mouse
void        Editor_Draw(const Editor *e);

// Returns true (once) if the text was changed since the last call
bool        Editor_TakeChanged(Editor *e);

#endif
