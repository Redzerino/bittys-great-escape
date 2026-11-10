// circuit.h - the Escape Circuit map (see circuit.c)

#ifndef CIRCUIT_H
#define CIRCUIT_H

#include <stdbool.h>
#include "raylib.h"

void Circuit_Init(void);                 // call after the levels are loaded
void Circuit_SelectLevel(int level);     // highlight this level's node

// Draw and handle the map. Returns a level index when a stage is chosen, otherwise -1.
int  Circuit_Draw(Font font, float dt);

int         Circuit_WorldOf(int level);     // which chip a level is on: 1-9, or 0 for the bootcamp
bool        Circuit_IsBootcamp(int level);  // one of the optional beginner lessons?
const char *Circuit_StageName(int level);   // "Stage 2.3" or "Bootcamp 4"

#endif
