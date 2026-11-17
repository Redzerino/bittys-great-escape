#ifndef ENDING_H
#define ENDING_H
#include "raylib.h"
void Ending_Reset(void);
bool Ending_Draw(Font font,float dt,int completed,int total);
void Ending_Shutdown(void);
#endif
