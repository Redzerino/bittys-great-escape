#ifndef CIRCUIT_H
#define CIRCUIT_H
#include "raylib.h"
void Circuit_Init(void);
void Circuit_SelectLevel(int level);
// Draw and handle the map. Returns a loaded level index when a stage is chosen.
int Circuit_Draw(Font font, float dt);
#endif
