#ifndef VISUAL_LEARNING_H
#define VISUAL_LEARNING_H
#include "raylib.h"
#include "game_api.h"
#include "levels.h"
#include "repair.h"
void VisualLearning_Reset(void);
void VisualLearning_SetFunction(const char *name);
bool VisualLearning_TakeReplay(void);
int VisualLearning_CurrentLine(void);
void VisualLearning_ObserveLine(const TestDisplay *td, const VmAccess *log, int count, int line, const char *code);
void VisualLearning_Enter(void);
void VisualLearning_Check(int index, bool passed, const char *detail);
void VisualLearning_ShowFailure(int index, const LevelTest *test, const TestOutcome *outcome);
void VisualLearning_Update(float dt, float speed);
bool VisualLearning_Busy(void);
bool VisualLearning_HasData(const LevelTest *test);
void VisualLearning_Access(const TestDisplay *display, int row, int index, bool write);
void VisualLearning_Draw(Rectangle panel, Font font, const Level *level,
                         const TestDisplay *live, int testIndex, bool running, bool won, float victoryTime);
void VisualLearning_Shutdown(void);
#endif
