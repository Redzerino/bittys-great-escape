// levels.h - the level list.
//
// For now the levels are written in levels.c. Later (feature/levels) they
// will be loaded from text files so new ones can be added without code.

#ifndef LEVELS_H
#define LEVELS_H

#define LEVEL_MAX_ROWS 12

typedef struct {
    const char *title;                 // e.g. "1-1  Wake Up"
    const char *story;                 // shown in the console when the level loads
    const char *map[LEVEL_MAX_ROWS];   // see World_LoadMap for the letters
    int         mapRows;
    const char *starterCode;           // what's in the editor at the start
} Level;

int          Levels_Count(void);
const Level *Levels_Get(int index);

#endif
