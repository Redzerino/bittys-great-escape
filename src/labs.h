// labs.h - CLASS LABS: for a limited time, the questions from the Advanced
// Programming Lab 1 (C) and Lab 2 (C++) sheets, picked from two big
// cinematic cards and played as Bitty levels.
//
// The offer ends on a date (labs_end in bitty_cloud.txt, 19 Dec 2026 if not set).
// After that the CLASS LABS button and the chooser disappear on their own.

#ifndef LABS_H
#define LABS_H

#include <stdbool.h>
#include "raylib.h"

#define LABS_TO_MAP (-2)

void Labs_Init(const char *endDate);   // "YYYY-MM-DD", or "" for the default (after the window opens)
void Labs_Shutdown(void);
bool Labs_Active(void);                 // still before the end date?
int  Labs_DaysLeft(void);
void Labs_Open(bool showChooser);       // chooser cards, or straight to the last lab's question list

// Draws the labs screen. Returns a level index to play, LABS_TO_MAP, or -1.
int  Labs_Draw(Font font, float dt);

#endif
