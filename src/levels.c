// levels.c - the first levels of World 1 (Boot Sector).
//
// Map letters:  # wall (protected memory)   . empty memory   E exit
//               x void (unallocated: stepping in = segfault)
//               B Bitty's start             0-9 a data tile holding that value

#include "levels.h"

static const Level LEVELS[] = {
    {
        "1-1  Wake Up",
        "Bitty: \"Where... am I? Everything's made of numbers.\"\n"
        "Bitty can't move on its own. Your code is the only way out.\n"
        "Goal: reach the green EXIT tile.\n",
        {
            "#######",
            "#B..###",
            "###..E#",
            "#######",
        },
        4,
        "// LEVEL 1-1: WAKE UP\n"
        "// Bitty can't move by itself.\n"
        "// Your code is the only way out!\n"
        "// Get Bitty to the green EXIT tile.\n"
        "//\n"
        "//   move(NORTH);   move(EAST);\n"
        "//   move(SOUTH);   move(WEST);\n"
        "\n"
        "move(EAST);\n"
    },
    {
        "1-2  The Long Corridor",
        "Bitty: \"That's a LONG way. My battery won't last if you type every step.\"\n"
        "Goal: reach the EXIT using a loop.\n",
        {
            "##############",
            "#B..........E#",
            "##############",
        },
        3,
        "// LEVEL 1-2: THE LONG CORRIDOR\n"
        "// Typing move(EAST); eleven times\n"
        "// is painful. A for loop repeats\n"
        "// code for you:\n"
        "//\n"
        "//   for (int i = 0; i < 3; i++) {\n"
        "//       move(EAST);  // 3 times\n"
        "//   }\n"
        "\n"
    },
    {
        "1-3  Mind the Gap",
        "Bitty: \"Careful! That black memory isn't allocated. If I step in, I'm gone.\"\n"
        "You don't know how long the corridor is. Use scan() to check before you move.\n"
        "Goal: go east until the wall, then south to the EXIT.\n",
        {
            "#########",
            "#B.....##",
            "#xxxxx.##",
            "######E##",
            "#########",
        },
        5,
        "// LEVEL 1-3: MIND THE GAP\n"
        "// scan(EAST) tells you what's in\n"
        "// the next tile east: TILE_EMPTY,\n"
        "// TILE_WALL, TILE_EXIT, TILE_VOID...\n"
        "//\n"
        "// A while loop repeats while its\n"
        "// condition is true:\n"
        "//   while (scan(EAST) != TILE_WALL)\n"
        "//   {\n"
        "//       ...\n"
        "//   }\n"
        "\n"
    },
};

int Levels_Count(void)
{
    return (int)(sizeof LEVELS / sizeof LEVELS[0]);
}

const Level *Levels_Get(int index)
{
    if (index < 0 || index >= Levels_Count()) return &LEVELS[0];
    return &LEVELS[index];
}
