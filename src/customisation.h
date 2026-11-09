#ifndef CUSTOMISATION_H
#define CUSTOMISATION_H
#include "raylib.h"
void Customisation_Init(void);
void Customisation_RecordCompletion(int level);
Color Customisation_Body(void);
Color Customisation_Highlight(void);
Color Customisation_Shadow(void);
Color Customisation_Glow(void);
int Customisation_Face(void);
bool Customisation_Headphones(void);
void Customisation_DrawAccessories(float time);
void Customisation_Draw(Rectangle panel);   // panel = where the Code panel normally sits
#endif
