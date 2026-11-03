// inspector.h - the Inspector panel: live variables, arrays and pointer arrows.

#ifndef INSPECTOR_H
#define INSPECTOR_H

#include "raylib.h"
#include "vm.h"

void Inspector_Draw(const VmVariable *vars, int count, Rectangle bounds, Font font, int currentLine);

#endif
