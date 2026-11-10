// editor.c - the Code panel. See editor.h.

#include "editor.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PADDING      8
#define TITLE_HEIGHT 30
#define MIN_FONT     12
#define MAX_FONT     32

static bool changedFlag = false;

enum { EDIT_NONE = 0, EDIT_TYPE, EDIT_DELETE, EDIT_OTHER };

// ===========================================================================
// Text helpers
// ===========================================================================
static int LineStart(const Editor *e, int index)
{
    while (index > 0 && e->text[index - 1] != '\n') index--;
    return index;
}

static int LineEnd(const Editor *e, int index)
{
    while (index < e->len && e->text[index] != '\n') index++;
    return index;
}

static int LineOf(const Editor *e, int index)
{
    int line = 0;
    for (int i = 0; i < index && i < e->len; i++)
        if (e->text[i] == '\n') line++;
    return line;
}

static int LineCount(const Editor *e)
{
    return LineOf(e, e->len) + 1;
}

static int StartOfLine(const Editor *e, int line)
{
    int i = 0;
    while (line > 0 && i < e->len)
    {
        if (e->text[i] == '\n') line--;
        i++;
    }
    return i;
}

static bool BlankAt(const Editor *e, int i, int *start, int *end);

static float GutterWidth(const Editor *e) { return e->fontSize * 2.3f; }
static float TextLeft(const Editor *e)    { return e->bounds.x + GutterWidth(e) + PADDING; }
static float TextTop(const Editor *e)     { return e->bounds.y + TITLE_HEIGHT; }

static int VisibleLines(const Editor *e)
{
    return (int)((e->bounds.height - TITLE_HEIGHT - PADDING) / e->lineHeight);
}

static void KeepCursorVisible(Editor *e)
{
    int line = LineOf(e, e->cursor);
    int visible = VisibleLines(e);
    if (line < e->scrollLine) e->scrollLine = line;
    if (line >= e->scrollLine + visible) e->scrollLine = line - visible + 1;
    if (e->scrollLine < 0) e->scrollLine = 0;
}

// Width of text[from..to) in pixels
static float Width(const Editor *e, int from, int to)
{
    char buf[512];
    int n = to - from;
    if (n <= 0) return 0;
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    memcpy(buf, e->text + from, (size_t)n);
    buf[n] = '\0';
    return MeasureTextEx(e->font, buf, e->fontSize, e->spacing).x + e->spacing;
}

// ===========================================================================
// Selection
// ===========================================================================
static bool HasSelection(const Editor *e) { return e->anchor >= 0 && e->anchor != e->cursor; }
static int  SelStart(const Editor *e)     { return e->anchor < e->cursor ? e->anchor : e->cursor; }
static int  SelEnd(const Editor *e)       { return e->anchor > e->cursor ? e->anchor : e->cursor; }
static void ClearSelection(Editor *e)     { e->anchor = -1; }

// ===========================================================================
// Undo / redo
// ===========================================================================
static void Snapshot(const Editor *e, EditorSnapshot *s)
{
    memcpy(s->text, e->text, (size_t)e->len + 1);
    s->len = e->len;
    s->cursor = e->cursor;
}

static void Restore(Editor *e, const EditorSnapshot *s)
{
    memcpy(e->text, s->text, (size_t)s->len + 1);
    e->len = s->len;
    e->cursor = s->cursor;
    ClearSelection(e);
    changedFlag = true;
}

static void PushSnapshot(EditorSnapshot *stack, int *count, const Editor *e)
{
    if (*count == EDITOR_UNDO_MAX)
    {
        memmove(stack, stack + 1, sizeof(EditorSnapshot) * (EDITOR_UNDO_MAX - 1));
        (*count)--;
    }
    Snapshot(e, &stack[(*count)++]);
}

// Call before every change. Typing (or deleting) in a row counts as one step.
static void BeginEdit(Editor *e, int kind)
{
    double now = GetTime();
    bool merge = (kind == EDIT_TYPE || kind == EDIT_DELETE) && kind == e->lastEditKind &&
                 now - e->lastEditTime < 1.0;
    if (!merge) PushSnapshot(e->undo, &e->undoCount, e);
    e->redoCount = 0;
    e->lastEditKind = kind;
    e->lastEditTime = now;
}

static void Undo(Editor *e)
{
    if (e->undoCount == 0) return;
    PushSnapshot(e->redo, &e->redoCount, e);
    Restore(e, &e->undo[--e->undoCount]);
    e->lastEditKind = EDIT_NONE;
}

static void Redo(Editor *e)
{
    if (e->redoCount == 0) return;
    PushSnapshot(e->undo, &e->undoCount, e);
    Restore(e, &e->redo[--e->redoCount]);
    e->lastEditKind = EDIT_NONE;
}

// ===========================================================================
// Changing the text
// ===========================================================================
static void Insert(Editor *e, const char *s, int n)
{
    if (e->len + n >= EDITOR_MAX_TEXT) n = EDITOR_MAX_TEXT - 1 - e->len;
    if (n <= 0) return;
    memmove(e->text + e->cursor + n, e->text + e->cursor, (size_t)(e->len - e->cursor));
    memcpy(e->text + e->cursor, s, (size_t)n);
    e->len += n;
    e->cursor += n;
    e->text[e->len] = '\0';
    changedFlag = true;
}

static void DeleteRange(Editor *e, int from, int to)
{
    if (from < 0) from = 0;
    if (to > e->len) to = e->len;
    if (to <= from) return;
    memmove(e->text + from, e->text + to, (size_t)(e->len - to));
    e->len -= (to - from);
    e->text[e->len] = '\0';
    e->cursor = from;
    changedFlag = true;
}

static void DeleteSelection(Editor *e)
{
    if (!HasSelection(e)) return;
    DeleteRange(e, SelStart(e), SelEnd(e));
    ClearSelection(e);
}

static void CopySelection(const Editor *e)
{
    static char buf[EDITOR_MAX_TEXT];
    int n = SelEnd(e) - SelStart(e);
    memcpy(buf, e->text + SelStart(e), (size_t)n);
    buf[n] = '\0';
    SetClipboardText(buf);
}

static void TypeNewline(Editor *e)
{
    BeginEdit(e, EDIT_OTHER);
    DeleteSelection(e);

    // Keep the same indent as the line above, plus 4 after a {
    int start = LineStart(e, e->cursor);
    int indent = 0;
    while (start + indent < e->cursor && e->text[start + indent] == ' ') indent++;

    int k = e->cursor - 1;
    while (k >= start && e->text[k] == ' ') k--;
    if (k >= start && e->text[k] == '{') indent += 4;

    char buf[80];
    if (indent > 64) indent = 64;
    buf[0] = '\n';
    memset(buf + 1, ' ', (size_t)indent);
    Insert(e, buf, indent + 1);
}

static void TypeChar(Editor *e, int ch)
{
    // Typing over a selection starts a new undo step, and the letters
    // typed after it join that same step (so one Ctrl+Z brings it all back)
    if (HasSelection(e)) e->lastEditKind = EDIT_NONE;
    BeginEdit(e, EDIT_TYPE);
    DeleteSelection(e);

    // Typing } on a line that's only spaces: step the indent back by 4
    if (ch == '}')
    {
        int start = LineStart(e, e->cursor);
        bool onlySpaces = true;
        for (int i = start; i < e->cursor; i++) if (e->text[i] != ' ') onlySpaces = false;
        if (onlySpaces && e->cursor - start >= 4) DeleteRange(e, e->cursor - 4, e->cursor);
    }
    char c = (char)ch;
    Insert(e, &c, 1);
}

static void Paste(Editor *e)
{
    const char *clip = GetClipboardText();
    if (!clip || !clip[0]) return;
    BeginEdit(e, EDIT_OTHER);
    DeleteSelection(e);
    for (int i = 0; clip[i]; i++)
    {
        char c = clip[i];
        if (c == '\r') continue;
        if (c == '\t') { Insert(e, "    ", 4); continue; }
        if (c == '\n' || (c >= 32 && c < 127)) Insert(e, &c, 1);
    }
}

static void Backspace(Editor *e)
{
    if (HasSelection(e)) { BeginEdit(e, EDIT_OTHER); DeleteSelection(e); return; }
    if (e->cursor == 0) return;
    BeginEdit(e, EDIT_DELETE);

    // Backspace over a 4-space indent in one go
    int start = LineStart(e, e->cursor);
    bool spaces = e->cursor - start >= 4;
    for (int i = e->cursor - 4; spaces && i < e->cursor; i++) if (e->text[i] != ' ') spaces = false;
    bool onlyIndent = true;
    for (int i = start; i < e->cursor; i++) if (e->text[i] != ' ') onlyIndent = false;
    if (spaces && onlyIndent) DeleteRange(e, e->cursor - 4, e->cursor);
    else                      DeleteRange(e, e->cursor - 1, e->cursor);
}

static void DeleteForward(Editor *e)
{
    if (HasSelection(e)) { BeginEdit(e, EDIT_OTHER); DeleteSelection(e); return; }
    if (e->cursor >= e->len) return;
    BeginEdit(e, EDIT_DELETE);
    DeleteRange(e, e->cursor, e->cursor + 1);
}

static void SetFontSize(Editor *e, float size)
{
    if (size < MIN_FONT) size = MIN_FONT;
    if (size > MAX_FONT) size = MAX_FONT;
    e->fontSize = size;
    e->lineHeight = size + 4;
    KeepCursorVisible(e);
}

// ===========================================================================
// Public
// ===========================================================================
void Editor_Init(Editor *e, Rectangle bounds, Font font, float fontSize)
{
    memset(e, 0, sizeof *e);
    e->bounds  = bounds;
    e->font    = font;
    e->spacing = 0;
    e->focused = true;
    e->anchor  = -1;
    SetFontSize(e, fontSize);
}

void Editor_SetBounds(Editor *e, Rectangle bounds)
{
    e->bounds = bounds;
}

void Editor_SetText(Editor *e, const char *text)
{
    snprintf(e->text, EDITOR_MAX_TEXT, "%s", text);
    e->len = (int)strlen(e->text);
    e->cursor = e->len;
    e->scrollLine = 0;
    e->runLine = e->errorLine = 0;
    e->undoCount = e->redoCount = 0;
    e->lastEditKind = EDIT_NONE;
    ClearSelection(e);
    KeepCursorVisible(e);
}

void Editor_ReplaceAll(Editor *e, const char *text)
{
    BeginEdit(e, EDIT_OTHER);
    snprintf(e->text, EDITOR_MAX_TEXT, "%s", text);
    e->len = (int)strlen(e->text);
    e->cursor = e->len;
    ClearSelection(e);
    changedFlag = true;
    KeepCursorVisible(e);
}

const char *Editor_GetText(const Editor *e) { return e->text; }

bool Editor_TakeChanged(Editor *e)
{
    (void)e;
    bool c = changedFlag;
    changedFlag = false;
    return c;
}

static bool KeyRepeat(int key)
{
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

static int IndexFromMouse(const Editor *e, Vector2 m)
{
    int line = (int)floorf((m.y - TextTop(e)) / e->lineHeight) + e->scrollLine;
    if (line < 0) return 0;
    if (line >= LineCount(e)) return e->len;

    int start = StartOfLine(e, line);
    int end   = LineEnd(e, start);
    float x = m.x - TextLeft(e);
    for (int i = start; i < end; i++)
    {
        float mid = (Width(e, start, i) + Width(e, start, i + 1)) / 2;
        if (x < mid) return i;
    }
    return end;
}

// Move the cursor; with Shift held the selection grows, without it the selection goes
static void MoveTo(Editor *e, int index, bool shift)
{
    if (shift) { if (e->anchor < 0) e->anchor = e->cursor; }
    else       ClearSelection(e);
    e->cursor = index;
}

void Editor_Update(Editor *e)
{
    e->blink += GetFrameTime();
    Vector2 m = GetMousePosition();
    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    // ---- Mouse: focus, click to place the cursor, drag to select ----
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        bool inside = CheckCollisionPointRec(m, e->bounds);
        if (inside || m.y >= e->bounds.y) e->focused = inside;   // toolbar clicks keep focus
        if (e->focused && m.y > TextTop(e))
        {
            int at = IndexFromMouse(e, m);
            if (shift) { if (e->anchor < 0) e->anchor = e->cursor; }
            else       e->anchor = at;
            e->cursor = at;
            e->dragging = true;
            e->blink = 0;
            int s, t;
            if (!shift && !e->readOnly && BlankAt(e, at, &s, &t))
            {
                // clicked a gap: select all of it, so typing replaces it
                e->anchor = s;
                e->cursor = t;
                e->dragging = false;
            }
        }
    }
    if (e->dragging)
    {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            e->cursor = IndexFromMouse(e, m);
            if (m.y < TextTop(e) && e->scrollLine > 0) e->scrollLine--;                         // drag up: scroll
            if (m.y > e->bounds.y + e->bounds.height - 4) e->scrollLine++;                      // drag down: scroll
        }
        else
        {
            e->dragging = false;
            if (e->anchor == e->cursor) ClearSelection(e);
        }
    }

    // ---- Mouse wheel: scroll, or Ctrl + wheel to zoom ----
    if (CheckCollisionPointRec(m, e->bounds))
    {
        float wheel = GetMouseWheelMove();
        if (ctrl && wheel != 0) SetFontSize(e, e->fontSize + (wheel > 0 ? 1 : -1));
        else
        {
            e->scrollLine -= (int)(wheel * 3);
            int maxScroll = LineCount(e) - VisibleLines(e);
            if (e->scrollLine > maxScroll) e->scrollLine = maxScroll;
            if (e->scrollLine < 0) e->scrollLine = 0;
        }
    }

    if (!e->focused) return;

    int before = e->cursor;

    // ---- Text size ----
    if (ctrl && (KeyRepeat(KEY_EQUAL) || KeyRepeat(KEY_KP_ADD)))      SetFontSize(e, e->fontSize + 1);
    if (ctrl && (KeyRepeat(KEY_MINUS) || KeyRepeat(KEY_KP_SUBTRACT))) SetFontSize(e, e->fontSize - 1);
    if (ctrl && IsKeyPressed(KEY_ZERO))                               SetFontSize(e, 16);

    // ---- Movement (Shift = select) ----
    if (KeyRepeat(KEY_LEFT))
    {
        if (HasSelection(e) && !shift) MoveTo(e, SelStart(e), false);
        else if (e->cursor > 0)        MoveTo(e, e->cursor - 1, shift);
    }
    if (KeyRepeat(KEY_RIGHT))
    {
        if (HasSelection(e) && !shift) MoveTo(e, SelEnd(e), false);
        else if (e->cursor < e->len)   MoveTo(e, e->cursor + 1, shift);
    }
    if (KeyRepeat(KEY_HOME)) MoveTo(e, ctrl ? 0 : LineStart(e, e->cursor), shift);
    if (KeyRepeat(KEY_END))  MoveTo(e, ctrl ? e->len : LineEnd(e, e->cursor), shift);
    if (KeyRepeat(KEY_UP) || KeyRepeat(KEY_DOWN))
    {
        int start = LineStart(e, e->cursor);
        int col = e->cursor - start;
        int line = LineOf(e, e->cursor) + (IsKeyDown(KEY_UP) ? -1 : 1);
        if (line >= 0 && line < LineCount(e))
        {
            int s = StartOfLine(e, line);
            int end = LineEnd(e, s);
            MoveTo(e, (s + col < end) ? s + col : end, shift);
        }
    }
    if (ctrl && IsKeyPressed(KEY_A)) { e->anchor = 0; e->cursor = e->len; }

    // ---- Copy works even while the program runs ----
    if (ctrl && IsKeyPressed(KEY_C))
    {
        if (HasSelection(e)) CopySelection(e);
        else                 SetClipboardText(e->text);   // nothing selected: copy all the code
    }

    // ---- Editing (not while the program is running) ----
    if (!e->readOnly)
    {
        if (ctrl && IsKeyPressed(KEY_X) && HasSelection(e)) { CopySelection(e); BeginEdit(e, EDIT_OTHER); DeleteSelection(e); }
        if (ctrl && IsKeyPressed(KEY_V)) Paste(e);
        if (ctrl && KeyRepeat(KEY_Z))   { if (shift) Redo(e); else Undo(e); }
        if (ctrl && KeyRepeat(KEY_Y))   Redo(e);

        int ch;
        while ((ch = GetCharPressed()) > 0)
        {
            if (ctrl) continue;
            if (ch >= 32 && ch < 127) TypeChar(e, ch);
        }

        if (KeyRepeat(KEY_ENTER) || KeyRepeat(KEY_KP_ENTER)) TypeNewline(e);
        if (KeyRepeat(KEY_TAB))
        {
            // Tab jumps to the next gap while there are any; otherwise it indents
            if (!Editor_SelectNextBlank(e)) { BeginEdit(e, EDIT_OTHER); DeleteSelection(e); Insert(e, "    ", 4); }
        }
        if (KeyRepeat(KEY_BACKSPACE)) Backspace(e);
        if (KeyRepeat(KEY_DELETE))    DeleteForward(e);
    }
    else
    {
        while (GetCharPressed() > 0) {}   // ignore typing while running
    }

    if (e->cursor != before || changedFlag)
    {
        e->blink = 0;
        if (!e->dragging) KeepCursorVisible(e);
    }
}

// ===========================================================================
// Drawing with syntax colours
// ===========================================================================
static const Color COL_TEXT     = { 220, 225, 235, 255 };
static const Color COL_COMMENT  = { 110, 120, 140, 255 };
static const Color COL_KEYWORD  = { 200, 140, 255, 255 };
static const Color COL_TYPE     = { 100, 180, 255, 255 };
static const Color COL_NUMBER   = { 255, 170, 90, 255 };
static const Color COL_STRING   = { 140, 220, 130, 255 };
static const Color COL_BUILTIN  = { 255, 220, 110, 255 };
static const Color COL_CONSTANT = { 90, 220, 220, 255 };

static bool InList(const char *word, const char *const *list)
{
    for (int i = 0; list[i]; i++) if (strcmp(word, list[i]) == 0) return true;
    return false;
}

static Color WordColour(const char *w)
{
    static const char *const TYPES[]     = { "int", "char", "bool", "void", "const", NULL };
    static const char *const KEYWORDS[]  = { "if", "else", "for", "while", "do", "switch", "case", "default",
                                             "break", "continue", "return", "true", "false", "NULL", "nullptr",
                                             "struct", "class", "new", "delete", "using", "namespace", NULL };
    static const char *const BUILTINS[]  = { "move", "read", "write", "scan", "get_x", "get_y", "printf",
                                             "cout", "endl", "std", NULL };
    if (InList(w, TYPES))    return COL_TYPE;
    if (InList(w, KEYWORDS)) return COL_KEYWORD;
    if (InList(w, BUILTINS)) return COL_BUILTIN;
    if (strncmp(w, "TILE_", 5) == 0 || !strcmp(w, "NORTH") || !strcmp(w, "EAST") ||
        !strcmp(w, "SOUTH") || !strcmp(w, "WEST")) return COL_CONSTANT;
    return COL_TEXT;
}

static float DrawPiece(const Editor *e, const char *s, int n, float x, float y, Color col)
{
    char buf[512];
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    memcpy(buf, s, (size_t)n);
    buf[n] = '\0';
    DrawTextEx(e->font, buf, (Vector2){ x, y }, e->fontSize, e->spacing, col);
    return x + MeasureTextEx(e->font, buf, e->fontSize, e->spacing).x + e->spacing;
}

static void DrawCodeLine(const Editor *e, int start, int end, float x, float y)
{
    int i = start;
    while (i < end)
    {
        const char *s = e->text;
        int j = i;
        Color col = COL_TEXT;

        if (s[i] == '/' && i + 1 < end && s[i + 1] == '/')                 { j = end; col = COL_COMMENT; }
        else if (s[i] == '"' || s[i] == '\'')
        {
            char q = s[i];
            j = i + 1;
            while (j < end && s[j] != q) { if (s[j] == '\\') j++; j++; }
            if (j < end) j++;
            col = COL_STRING;
        }
        else if (isdigit((unsigned char)s[i]))
        {
            while (j < end && isalnum((unsigned char)s[j])) j++;
            col = COL_NUMBER;
        }
        else if (isalpha((unsigned char)s[i]) || s[i] == '_')
        {
            char word[64];
            int n = 0;
            while (j < end && (isalnum((unsigned char)s[j]) || s[j] == '_'))
            {
                if (n < 63) word[n++] = s[j];
                j++;
            }
            word[n] = '\0';
            col = WordColour(word);
        }
        else if (s[i] == '#')                                               { j = end; col = COL_COMMENT; }
        else
        {
            j = i + 1;
        }
        if (j > end) j = end;

        x = DrawPiece(e, s + i, j - i, x, y, col);
        i = j;
    }
}

// ---------------------------------------------------------------------------
// Blanks: ___ in the code is a gap for the player to fill in
// ---------------------------------------------------------------------------
static bool BlankAt(const Editor *e, int i, int *start, int *end)
{
    if (i < 0 || i > e->len) return false;
    int s = i, t = i;
    while (s > 0 && e->text[s - 1] == '_') s--;
    while (t < e->len && e->text[t] == '_') t++;
    if (t - s < 3) return false;
    *start = s;
    *end = t;
    return true;
}

bool Editor_SelectNextBlank(Editor *e)
{
    for (int pass = 0; pass < 2; pass++)
    {
        int from = pass == 0 ? e->cursor : 0;
        for (int i = from; i + 2 < e->len; i++)
        {
            int s, t;
            if (e->text[i] == '_' && BlankAt(e, i, &s, &t) && s >= from)
            {
                e->anchor = s;
                e->cursor = t;
                e->blink = 0;
                KeepCursorVisible(e);
                return true;
            }
        }
    }
    return false;
}

bool Editor_InGap(const Editor *e)
{
    int s, t;
    return e->anchor >= 0 && e->anchor != e->cursor && BlankAt(e, e->cursor - 1, &s, &t) &&
           ((s == e->anchor && t == e->cursor) || (s == e->cursor && t == e->anchor));
}

void Editor_InsertText(Editor *e, const char *text)
{
    if (e->readOnly) return;
    BeginEdit(e, EDIT_OTHER);
    DeleteSelection(e);
    Insert(e, text, (int)strlen(text));
    e->focused = true;
}

int Editor_LineAt(const Editor *e, Vector2 point)
{
    if (!CheckCollisionPointRec(point, e->bounds) || point.y < TextTop(e)) return 0;
    int line = (int)floorf((point.y - TextTop(e)) / e->lineHeight) + e->scrollLine;
    if (line >= LineCount(e)) return 0;
    return line + 1;
}

bool Editor_CharPos(const Editor *e, int line, int col, Vector2 *pos)
{
    int L = line - 1;
    if (L < e->scrollLine || L >= e->scrollLine + VisibleLines(e)) return false;
    int start = StartOfLine(e, L);
    int end = LineEnd(e, start);
    int at = start + (col > 0 ? col - 1 : 0);
    if (at > end) at = end;
    pos->x = TextLeft(e) + Width(e, start, at);
    pos->y = TextTop(e) + (L - e->scrollLine) * e->lineHeight;
    return true;
}

void Editor_Draw(const Editor *e)
{
    Rectangle b = e->bounds;
    float gutter = GutterWidth(e);
    DrawRectangleRec(b, (Color){ 20, 24, 36, 255 });
    DrawRectangleLinesEx(b, 2, e->focused ? (Color){ 90, 220, 255, 255 } : (Color){ 60, 200, 255, 160 });
    DrawText("CODE", (int)b.x + 10, (int)b.y + 8, 18, (Color){ 60, 200, 255, 255 });
    if (e->readOnly)
        DrawText("running...", (int)(b.x + b.width - 90), (int)b.y + 10, 14, (Color){ 90, 230, 120, 255 });

    // Gutter
    DrawRectangle((int)b.x + 2, (int)TextTop(e) - 4, (int)gutter, (int)(b.height - TITLE_HEIGHT + 2),
                  (Color){ 16, 19, 29, 255 });

    BeginScissorMode((int)b.x + 2, (int)TextTop(e) - 4, (int)b.width - 4, (int)(b.height - TITLE_HEIGHT + 2));

    int lines = LineCount(e);
    int visible = VisibleLines(e);
    int cursorLine = LineOf(e, e->cursor);
    bool sel = HasSelection(e);
    int s0 = sel ? SelStart(e) : 0, s1 = sel ? SelEnd(e) : 0;

    for (int L = e->scrollLine; L < lines && L < e->scrollLine + visible + 1; L++)
    {
        float y = TextTop(e) + (L - e->scrollLine) * e->lineHeight;
        int lineNo = L + 1;
        int start = StartOfLine(e, L);
        int end = LineEnd(e, start);

        // highlight bars
        Rectangle bar = { b.x + 2, y - 2, b.width - 4, e->lineHeight };
        if (lineNo == e->errorLine)
            DrawRectangleRec(bar, (Color){ 140, 30, 40, 160 });
        else if (lineNo == e->runLine)
            DrawRectangleRec(bar, (Color){ 140, 120, 20, 150 });
        else if (e->focused && !e->readOnly && L == cursorLine && !sel)
            DrawRectangleRec(bar, (Color){ 255, 255, 255, 10 });

        // selection on this line
        if (sel && s0 <= end && s1 >= start && !(s0 == s1))
        {
            int a = s0 > start ? s0 : start;
            int z = s1 < end ? s1 : end;
            float x0 = TextLeft(e) + Width(e, start, a);
            float x1 = TextLeft(e) + Width(e, start, z) + ((s1 > end) ? e->fontSize * 0.4f : 0);
            if (x1 > x0) DrawRectangle((int)x0, (int)y - 2, (int)(x1 - x0), (int)e->lineHeight, (Color){ 60, 110, 200, 120 });
        }

        const char *num = TextFormat("%d", lineNo);
        Color numCol = (lineNo == e->runLine) ? (Color){ 255, 220, 90, 255 } :
                       (lineNo == e->errorLine) ? (Color){ 255, 120, 120, 255 } : (Color){ 80, 90, 115, 255 };
        DrawTextEx(e->font, num, (Vector2){ b.x + gutter - 6 - MeasureTextEx(e->font, num, e->fontSize - 2, 0).x, y + 1 },
                   e->fontSize - 2, 0, numCol);

        // gaps to fill in: a pulsing gold box behind each ___
        for (int i = start; i + 2 < end; i++)
        {
            int s, t;
            if (e->text[i] != '_' || !BlankAt(e, i, &s, &t) || s != i) continue;
            float x0 = TextLeft(e) + Width(e, start, s), x1 = TextLeft(e) + Width(e, start, t);
            float pulse = 0.55f + 0.45f * sinf((float)GetTime() * 4);
            Rectangle gap = { x0 - 3, y - 2, x1 - x0 + 4, e->lineHeight };
            DrawRectangleRounded(gap, 0.3f, 4, (Color){ 120, 90, 20, (unsigned char)(150 + 60 * pulse) });
            DrawRectangleRoundedLinesEx(gap, 0.3f, 4, 2, (Color){ 255, 205, 80, (unsigned char)(160 + 95 * pulse) });
            i = t;
        }

        DrawCodeLine(e, start, end, TextLeft(e), y);
    }

    // Cursor
    if (e->focused && !e->readOnly && fmodf(e->blink, 1.0f) < 0.6f && cursorLine >= e->scrollLine)
    {
        int start = LineStart(e, e->cursor);
        float x = TextLeft(e) + Width(e, start, e->cursor);
        float y = TextTop(e) + (cursorLine - e->scrollLine) * e->lineHeight;
        DrawRectangle((int)x, (int)y - 1, 2, (int)e->fontSize + 2, (Color){ 90, 220, 255, 255 });
    }

    EndScissorMode();
}
