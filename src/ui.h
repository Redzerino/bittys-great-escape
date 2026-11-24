// ui.h - the shared look of the game: fonts, glassy panels, glowing buttons
// and the animated backdrop. Everything is drawn with shapes (no image files).
//
// Fonts come from the computer (Segoe UI / Impact on Windows, DejaVu on Linux).
// If none are found the game falls back to raylib's built-in font.

#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "raylib.h"

#define UI_HEADER 30            // height of a panel's title strip (content starts below it)

typedef enum { UI_ICON_NONE, UI_ICON_PLAY, UI_ICON_STEP, UI_ICON_STOP, UI_ICON_RESET } UiIcon;

void  UI_Init(void);            // after InitWindow
void  UI_Shutdown(void);

// Text in the clean UI font (bold sans) or the big poster font
void  UI_Text(const char *text, float x, float y, float size, Color c);
float UI_TextWidth(const char *text, float size);
void  UI_Title(const char *text, float x, float y, float size, Color c);     // poster font with a soft glow
float UI_TitleWidth(const char *text, float size);

// Background for the whole screen: deep gradient, faint grid, drifting light
void  UI_Backdrop(float width, float height);

// A panel: drop shadow, glassy gradient, title strip with an accent bar.
// Call before drawing the panel's contents.
void  UI_Panel(Rectangle b, const char *title, Color accent, bool focused);
// Just the outline (for panels whose contents cover the whole rectangle)
void  UI_PanelBorder(Rectangle b, Color accent, bool focused);

// A glossy button. Returns true when clicked (and plays the click sound).
bool  UI_Button(Rectangle r, const char *text, Color base, bool enabled, UiIcon icon);

bool  UI_ButtonAt(Rectangle r, const char *text, Color base, bool enabled, UiIcon icon, Vector2 mouse);  // mouse in the same space as r

// A soft round glow of light (for nodes, eyes, lamps)
void  UI_Glow(Vector2 centre, float radius, Color c, float strength);
// A floating glass card without a title strip (lit = glowing edge)
void  UI_Card(Rectangle b, Color accent, bool lit);

// Small pill / tag with text
void  UI_Pill(Rectangle r, const char *text, Color fill, Color textColor);

Color UI_Lighten(Color c, float amount);   // 0..1 towards white

#endif
