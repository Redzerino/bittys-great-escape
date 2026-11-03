// editor.c - the Code panel. See editor.h.

#include "editor.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GUTTER_WIDTH 36
#define PADDING      8
#define TITLE_HEIGHT 30

static bool changedFlag = false;

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------
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

static float TextLeft(const Editor *e) { return e->bounds.x + GUTTER_WIDTH + PADDING; }
static float TextTop(const Editor *e)  { return e->bounds.y + TITLE_HEIGHT; }
static int   VisibleLines(const Editor *e)
{
    return (int)((e->bounds.height - TITLE_HEIGHT - PADDING) / e->lineHeight);
}

static void KeepCursorVisible(Editor *e)
{
    int line = LineOf(e, e->cursor);
    int visible = VisibleLines(e);
    if (line < e->scrollLine) e->scrollLine = line;
    if (line >= e->scrollLine + visible) e->scrollLine = line - visible + 1;
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void Editor_Init(Editor *e, Rectangle bounds, Font font, float fontSize)
{
    memset(e, 0, sizeof *e);
    e->bounds     = bounds;
    e->font       = font;
    e->fontSize   = fontSize;
    e->spacing    = 0;
    e->lineHeight = fontSize + 4;
    e->focused    = true;
}

void Editor_SetText(Editor *e, const char *text)
{
    snprintf(e->text, EDITOR_MAX_TEXT, "%s", text);
    e->len = (int)strlen(e->text);
    e->cursor = e->len;
    e->scrollLine = 0;
    e->runLine = e->errorLine = 0;
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

static void TypeNewline(Editor *e)
{
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
    if (!clip) return;
    for (int i = 0; clip[i]; i++)
    {
        char c = clip[i];
        if (c == '\r') continue;
        if (c == '\t') { Insert(e, "    ", 4); continue; }
        if (c == '\n' || (c >= 32 && c < 127)) Insert(e, &c, 1);
    }
}

static int IndexFromMouse(const Editor *e, Vector2 m)
{
    int line = (int)((m.y - TextTop(e)) / e->lineHeight) + e->scrollLine;
    if (line < 0) line = 0;
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

void Editor_Update(Editor *e)
{
    e->blink += GetFrameTime();

    // Focus: clicking in the editor focuses it; clicking another panel unfocuses it.
    // (Clicks on the toolbar above the panels leave it focused.)
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        Vector2 m = GetMousePosition();
        bool inside = CheckCollisionPointRec(m, e->bounds);
        if (inside || m.y >= e->bounds.y) e->focused = inside;
        if (e->focused && m.y > TextTop(e))
        {
            e->cursor = IndexFromMouse(e, m);
            e->blink = 0;
        }
    }

    // Scroll
    if (CheckCollisionPointRec(GetMousePosition(), e->bounds))
    {
        float wheel = GetMouseWheelMove();
        e->scrollLine -= (int)(wheel * 3);
        int maxScroll = LineCount(e) - VisibleLines(e);
        if (e->scrollLine > maxScroll) e->scrollLine = maxScroll;
        if (e->scrollLine < 0) e->scrollLine = 0;
    }

    if (!e->focused) return;

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    int before = e->cursor;

    // ---- Movement ----
    if (KeyRepeat(KEY_LEFT)  && e->cursor > 0)      e->cursor--;
    if (KeyRepeat(KEY_RIGHT) && e->cursor < e->len) e->cursor++;
    if (KeyRepeat(KEY_HOME)) e->cursor = LineStart(e, e->cursor);
    if (KeyRepeat(KEY_END))  e->cursor = LineEnd(e, e->cursor);
    if (KeyRepeat(KEY_UP) || KeyRepeat(KEY_DOWN))
    {
        int start = LineStart(e, e->cursor);
        int col = e->cursor - start;
        int line = LineOf(e, e->cursor) + (IsKeyDown(KEY_UP) ? -1 : 1);
        if (line >= 0 && line < LineCount(e))
        {
            int s = StartOfLine(e, line);
            int end = LineEnd(e, s);
            e->cursor = (s + col < end) ? s + col : end;
        }
    }

    // ---- Editing (not while the program is running) ----
    if (!e->readOnly)
    {
        if (ctrl && IsKeyPressed(KEY_V)) Paste(e);

        int ch;
        while ((ch = GetCharPressed()) > 0)
        {
            if (ctrl) continue;
            if (ch >= 32 && ch < 127) TypeChar(e, ch);
        }

        if (KeyRepeat(KEY_ENTER) || KeyRepeat(KEY_KP_ENTER)) TypeNewline(e);
        if (KeyRepeat(KEY_TAB)) Insert(e, "    ", 4);
        if (KeyRepeat(KEY_BACKSPACE) && e->cursor > 0)
        {
            // Backspace over a 4-space indent in one go
            int start = LineStart(e, e->cursor);
            bool spaces = e->cursor - start >= 4;
            for (int i = e->cursor - 4; spaces && i < e->cursor; i++) if (e->text[i] != ' ') spaces = false;
            bool onlyIndent = true;
            for (int i = start; i < e->cursor; i++) if (e->text[i] != ' ') onlyIndent = false;
            if (spaces && onlyIndent) DeleteRange(e, e->cursor - 4, e->cursor);
            else                      DeleteRange(e, e->cursor - 1, e->cursor);
        }
        if (KeyRepeat(KEY_DELETE) && e->cursor < e->len) DeleteRange(e, e->cursor, e->cursor + 1);
    }
    else
    {
        while (GetCharPressed() > 0) {}   // ignore typing while running
    }

    if (ctrl && IsKeyPressed(KEY_C)) SetClipboardText(e->text);   // copy all code

    if (e->cursor != before || changedFlag)
    {
        e->blink = 0;
        KeepCursorVisible(e);
    }
}

// ---------------------------------------------------------------------------
// Drawing with syntax colours
// ---------------------------------------------------------------------------
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

void Editor_Draw(const Editor *e)
{
    Rectangle b = e->bounds;
    DrawRectangleRec(b, (Color){ 20, 24, 36, 255 });
    DrawRectangleLinesEx(b, 2, e->focused ? (Color){ 90, 220, 255, 255 } : (Color){ 60, 200, 255, 160 });
    DrawText("CODE", (int)b.x + 10, (int)b.y + 8, 18, (Color){ 60, 200, 255, 255 });
    if (e->readOnly)
        DrawText("running...", (int)(b.x + b.width - 90), (int)b.y + 10, 14, (Color){ 90, 230, 120, 255 });

    // Gutter
    DrawRectangle((int)b.x + 2, (int)TextTop(e) - 4, GUTTER_WIDTH, (int)(b.height - TITLE_HEIGHT + 2),
                  (Color){ 16, 19, 29, 255 });

    BeginScissorMode((int)b.x + 2, (int)TextTop(e) - 4, (int)b.width - 4, (int)(b.height - TITLE_HEIGHT + 2));

    int lines = LineCount(e);
    int visible = VisibleLines(e);
    int cursorLine = LineOf(e, e->cursor);

    for (int L = e->scrollLine; L < lines && L < e->scrollLine + visible + 1; L++)
    {
        float y = TextTop(e) + (L - e->scrollLine) * e->lineHeight;
        int lineNo = L + 1;

        // highlight bars
        Rectangle bar = { b.x + 2, y - 2, b.width - 4, e->lineHeight };
        if (lineNo == e->errorLine)
            DrawRectangleRec(bar, (Color){ 140, 30, 40, 160 });
        else if (lineNo == e->runLine)
            DrawRectangleRec(bar, (Color){ 140, 120, 20, 150 });
        else if (e->focused && !e->readOnly && L == cursorLine)
            DrawRectangleRec(bar, (Color){ 255, 255, 255, 10 });

        const char *num = TextFormat("%d", lineNo);
        Color numCol = (lineNo == e->runLine) ? (Color){ 255, 220, 90, 255 } :
                       (lineNo == e->errorLine) ? (Color){ 255, 120, 120, 255 } : (Color){ 80, 90, 115, 255 };
        DrawTextEx(e->font, num, (Vector2){ b.x + GUTTER_WIDTH - 6 - MeasureTextEx(e->font, num, e->fontSize - 2, 0).x, y + 1 },
                   e->fontSize - 2, 0, numCol);

        int start = StartOfLine(e, L);
        int end = LineEnd(e, start);
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
