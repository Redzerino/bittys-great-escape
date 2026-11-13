#ifndef WORLD_VISUALS_H
#define WORLD_VISUALS_H
#include "raylib.h"
#include "game_api.h"
typedef struct {
    RenderTexture2D target;
    Camera3D camera;
    Font font;
    const TestDataRow *data;
    int count;
    const int *values;
    const unsigned char *kinds;
    const char (*labels)[32];
    int wireCount;
    const Vector3 *wireFrom, *wireTo;
    const char (*wireNames)[16];
    bool transfer;
    Vector3 transferAt;
    int transferValue;
    const int *visited;
    const Vector3 *cells;
    Vector3 bitty;
    int theme, current, best;
    bool moving, happy, victory;
    float victoryTime;
    float time;
} DataScene;
void World_DrawDataScene(const DataScene *scene);
#endif
