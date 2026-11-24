// ui.c - shared look (see ui.h)
#include "ui.h"
#include "sfx.h"
#include "rlgl.h"

#include <math.h>
#include <string.h>

// A soft round glow. Drawn directly with rlgl because DrawCircleGradient
// changed its arguments between raylib versions (ints in 5.0, a Vector2 in 5.5+).
static void Radial(float cx, float cy, float r, Color in, Color out)
{
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 360; i += 10)
    {
        float a0 = DEG2RAD * i, a1 = DEG2RAD * (i + 10);
        rlColor4ub(in.r, in.g, in.b, in.a);    rlVertex2f(cx, cy);
        rlColor4ub(out.r, out.g, out.b, out.a); rlVertex2f(cx + cosf(a1) * r, cy + sinf(a1) * r);
        rlColor4ub(out.r, out.g, out.b, out.a); rlVertex2f(cx + cosf(a0) * r, cy + sinf(a0) * r);
    }
    rlEnd();
}

static Font small, large, poster;      // UI font at two sizes (sharper text), poster font
static bool ownSmall, ownLarge, ownPoster;

static Font LoadFirst(const char **files, int count, int size, bool *owned)
{
    for (int i = 0; i < count; i++)
        if (FileExists(files[i]))
        {
            Font f = LoadFontEx(files[i], size, NULL, 0);
            if (f.texture.id != 0)
            {
                SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
                *owned = true;
                return f;
            }
        }
    *owned = false;
    return GetFontDefault();
}

void UI_Init(void)
{
    const char *ui[] = { "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/arialbd.ttf",
                         "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf" };
    const char *big[] = { "C:/Windows/Fonts/impact.ttf", "C:/Windows/Fonts/ariblk.ttf", "C:/Windows/Fonts/segoeuib.ttf",
                          "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/System/Library/Fonts/Supplemental/Impact.ttf" };
    small  = LoadFirst(ui, 5, 18, &ownSmall);
    large  = LoadFirst(ui, 5, 40, &ownLarge);
    poster = LoadFirst(big, 5, 72, &ownPoster);
}

void UI_Shutdown(void)
{
    if (ownSmall) UnloadFont(small);
    if (ownLarge) UnloadFont(large);
    if (ownPoster) UnloadFont(poster);
    ownSmall = ownLarge = ownPoster = false;
}

// ===========================================================================
// Text
// ===========================================================================
static Font Pick(float size) { return size <= 20 ? small : large; }
static float Spacing(float size) { return size * 0.04f; }

void UI_Text(const char *text, float x, float y, float size, Color c)
{
    DrawTextEx(Pick(size), text, (Vector2){ roundf(x), roundf(y) }, size, Spacing(size), c);
}
float UI_TextWidth(const char *text, float size) { return MeasureTextEx(Pick(size), text, size, Spacing(size)).x; }

void UI_Title(const char *text, float x, float y, float size, Color c)
{
    float sp = size * 0.05f;
    for (int i = 3; i >= 1; i--)            // soft coloured glow
        DrawTextEx(poster, text, (Vector2){ x, y + i * 0.6f }, size, sp, Fade(c, 0.07f * i));
    DrawTextEx(poster, text, (Vector2){ x + 2, y + 3 }, size, sp, Fade(BLACK, 0.55f));
    DrawTextEx(poster, text, (Vector2){ x, y }, size, sp, c);
}
float UI_TitleWidth(const char *text, float size) { return MeasureTextEx(poster, text, size, size * 0.05f).x; }

Color UI_Lighten(Color c, float a)
{
    return (Color){ (unsigned char)(c.r + (255 - c.r) * a), (unsigned char)(c.g + (255 - c.g) * a),
                    (unsigned char)(c.b + (255 - c.b) * a), c.a };
}
static Color Darken(Color c, float a)
{
    return (Color){ (unsigned char)(c.r * (1 - a)), (unsigned char)(c.g * (1 - a)), (unsigned char)(c.b * (1 - a)), c.a };
}

// raylib's roundness is a fraction of the short side; this takes pixels instead
static float Round(Rectangle r, float px)
{
    float m = fminf(r.width, r.height);
    return m <= 0 ? 0 : fminf(1.0f, 2 * px / m);
}

// ===========================================================================
// Backdrop
// ===========================================================================
void UI_Backdrop(float w, float h)
{
    float t = (float)GetTime();
    DrawRectangleGradientV(0, 0, (int)w, (int)h, (Color){ 13, 17, 32, 255 }, (Color){ 6, 8, 15, 255 });
    // two big slow glows
    Radial(w * 0.25f + sinf(t * 0.11f) * w * 0.08f, h * 0.3f, h * 0.7f, Fade((Color){ 40, 90, 160, 255 }, 0.16f), Fade(BLACK, 0));
    Radial(w * 0.8f + cosf(t * 0.09f) * w * 0.06f, h * 0.85f, h * 0.6f, Fade((Color){ 120, 50, 160, 255 }, 0.12f), Fade(BLACK, 0));
    // faint grid
    for (float x = 0; x < w; x += 32) DrawLineV((Vector2){ x, 0 }, (Vector2){ x, h }, Fade((Color){ 80, 120, 190, 255 }, 0.035f));
    for (float y = 0; y < h; y += 32) DrawLineV((Vector2){ 0, y }, (Vector2){ w, y }, Fade((Color){ 80, 120, 190, 255 }, 0.035f));
}

// ===========================================================================
// Panels
// ===========================================================================
static void Shadow(Rectangle b, float radius)
{
    for (int i = 4; i >= 1; i--)
    {
        Rectangle s = { b.x - i * 1.5f, b.y - i * 1.0f + 4, b.width + i * 3.0f, b.height + i * 3.0f };
        DrawRectangleRounded(s, Round(s, radius + i * 2), 8, Fade(BLACK, 0.09f));
    }
}

void UI_PanelBorder(Rectangle b, Color accent, bool focused)
{
    float t = (float)GetTime();
    if (focused)
    {
        Rectangle g = { b.x - 2, b.y - 2, b.width + 4, b.height + 4 };
        DrawRectangleRoundedLinesEx(g, Round(g, 12), 8, 3, Fade(accent, 0.18f + 0.06f * sinf(t * 3)));
    }
    DrawRectangleRoundedLinesEx(b, Round(b, 10), 8, 1.5f, Fade(accent, focused ? 0.85f : 0.38f));
}

void UI_Panel(Rectangle b, const char *title, Color accent, bool focused)
{
    Shadow(b, 10);
    float rr = Round(b, 10);
    DrawRectangleRounded(b, rr, 8, (Color){ 17, 21, 35, 255 });
    // glassy gradient: slightly lighter at the top
    DrawRectangleGradientV((int)b.x + 1, (int)b.y + UI_HEADER, (int)b.width - 2, (int)(b.height * 0.45f), (Color){ 23, 29, 46, 255 }, (Color){ 17, 21, 35, 255 });
    // title strip
    Rectangle strip = { b.x, b.y, b.width, UI_HEADER };
    DrawRectangleRounded(strip, Round(strip, 10), 8, Fade(Darken(accent, 0.65f), 0.55f));
    DrawRectangle((int)b.x, (int)(b.y + UI_HEADER / 2), (int)b.width, UI_HEADER / 2, Fade(Darken(accent, 0.65f), 0.55f));
    DrawRectangleGradientH((int)b.x + 1, (int)b.y + UI_HEADER - 1, (int)(b.width - 2), 1, Fade(accent, 0.7f), Fade(accent, 0.0f));
    // the accent bar and title
    DrawRectangleRounded((Rectangle){ b.x + 10, b.y + 8, 4, 14 }, 1, 4, accent);
    if (title && title[0]) UI_Text(title, b.x + 21, b.y + 6, 16, UI_Lighten(accent, 0.55f));
    UI_PanelBorder(b, accent, focused);
}

// ===========================================================================
// Buttons
// ===========================================================================
static void Icon(UiIcon icon, Vector2 c, Color col)
{
    switch (icon)
    {
    case UI_ICON_PLAY: DrawTriangle((Vector2){ c.x - 4, c.y - 6 }, (Vector2){ c.x - 4, c.y + 6 }, (Vector2){ c.x + 6, c.y }, col); break;
    case UI_ICON_STEP:
        DrawTriangle((Vector2){ c.x - 6, c.y - 6 }, (Vector2){ c.x - 6, c.y + 6 }, (Vector2){ c.x + 3, c.y }, col);
        DrawRectangle((int)c.x + 4, (int)c.y - 6, 3, 12, col);
        break;
    case UI_ICON_STOP: DrawRectangleRounded((Rectangle){ c.x - 5, c.y - 5, 10, 10 }, 0.3f, 4, col); break;
    case UI_ICON_RESET:
        DrawRing(c, 4, 6.5f, 40, 330, 16, col);
        DrawTriangle((Vector2){ c.x + 3, c.y - 9 }, (Vector2){ c.x + 3, c.y - 1 }, (Vector2){ c.x + 9, c.y - 5 }, col);
        break;
    default: break;
    }
}

bool UI_Button(Rectangle r, const char *text, Color base, bool enabled, UiIcon icon)
{
    return UI_ButtonAt(r, text, base, enabled, icon, GetMousePosition());
}

bool UI_ButtonAt(Rectangle r, const char *text, Color base, bool enabled, UiIcon icon, Vector2 m)
{
    bool hover = enabled && CheckCollisionPointRec(m, r);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color fill = enabled ? base : (Color){ 38, 42, 58, 255 };
    if (hover) fill = UI_Lighten(fill, 0.14f);
    Rectangle b = r;
    if (down) b.y += 1;

    float rr = Round(b, 7);
    if (!down) DrawRectangleRounded((Rectangle){ b.x, b.y + 2, b.width, b.height }, rr, 6, Fade(BLACK, 0.45f));   // drop shadow
    if (hover)                                                                                                   // glow
        DrawRectangleRounded((Rectangle){ b.x - 3, b.y - 3, b.width + 6, b.height + 6 }, Round(b, 10), 6, Fade(UI_Lighten(base, 0.3f), 0.22f));
    DrawRectangleRounded(b, rr, 6, Darken(fill, 0.18f));
    Rectangle top = { b.x, b.y, b.width, b.height * 0.58f };                                                   // glossy top half
    DrawRectangleRounded(top, Round(top, 7), 6, fill);
    DrawRectangleGradientV((int)b.x + 2, (int)(b.y + 1), (int)b.width - 4, (int)(b.height * 0.4f), Fade(WHITE, 0.10f), Fade(WHITE, 0.0f));
    DrawRectangleRoundedLinesEx(b, rr, 6, 1, Fade(UI_Lighten(fill, 0.45f), enabled ? 0.55f : 0.2f));

    Color tc = enabled ? WHITE : (Color){ 105, 112, 132, 255 };
    float size = b.height >= 44 ? 19 : b.height >= 28 ? 15 : 13;
    float tw = UI_TextWidth(text, size);
    float iconW = icon != UI_ICON_NONE ? 17 : 0;
    float x = b.x + (b.width - tw - iconW) / 2;
    if (icon != UI_ICON_NONE) Icon(icon, (Vector2){ x + 6, b.y + b.height / 2 }, tc);
    UI_Text(text, x + iconW, b.y + (b.height - size) / 2 - 1, size, tc);

    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (clicked) Sfx_Play(SFX_CLICK);
    return clicked;
}

void UI_Pill(Rectangle r, const char *text, Color fill, Color textColor)
{
    DrawRectangleRounded(r, 1, 8, fill);
    float size = r.height - 8 > 16 ? 16 : r.height - 8;
    UI_Text(text, r.x + (r.width - UI_TextWidth(text, size)) / 2, r.y + (r.height - size) / 2 - 1, size, textColor);
}

void UI_Glow(Vector2 c, float radius, Color col, float strength)
{
    Radial(c.x, c.y, radius, Fade(col, strength), Fade(col, 0));
}

void UI_Card(Rectangle b, Color accent, bool lit)
{
    Shadow(b, 10);
    if (lit) DrawRectangleRounded((Rectangle){ b.x - 4, b.y - 4, b.width + 8, b.height + 8 }, Round(b, 14), 8, Fade(accent, 0.12f));
    DrawRectangleRounded(b, Round(b, 10), 8, (Color){ 15, 22, 36, 248 });
    DrawRectangleGradientV((int)b.x + 1, (int)b.y + 8, (int)b.width - 2, (int)(b.height * 0.45f), Fade(UI_Lighten((Color){ 15, 22, 36, 255 }, 0.06f), 0.9f), Fade((Color){ 15, 22, 36, 255 }, 0));
    UI_PanelBorder(b, accent, lit);
}
