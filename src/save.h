// save.h - remembers the player's progress between sessions.
//
// Saved in bitty_save.txt next to the game: the code typed for each level,
// which levels are completed, and the level the player was on. Levels are
// identified by their file name, so reordering levels.txt doesn't break saves.

#ifndef SAVE_H
#define SAVE_H

#include <stdbool.h>

void        Save_Load(const char *path);                 // call once at startup (fine if the file doesn't exist)
bool        Save_Write(void);                            // writes everything to the file

const char *Save_GetCode(const char *levelFile);         // NULL if nothing saved for that level
void        Save_SetCode(const char *levelFile, const char *code);

bool        Save_IsDone(const char *levelFile);
void        Save_SetDone(const char *levelFile);

const char *Save_GetCurrent(void);                       // level file the player was on ("" if none)
void        Save_SetCurrent(const char *levelFile);

#endif
