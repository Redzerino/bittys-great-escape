#ifndef STARTUP_H
#define STARTUP_H
#include "raylib.h"
void Startup_Init(Font font);
bool Startup_Update(float dt); // True once the player chooses to enter the game.
void Startup_Draw(void);
void Startup_Shutdown(void);
#endif
