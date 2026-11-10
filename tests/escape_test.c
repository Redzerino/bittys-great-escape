// escape_test.c - plays every ESCAPE level's [solution] and checks Bitty reaches the EXIT.
// Build and run with: test_escape.bat   (needs raylib, because it uses the real world code)
//
// For every escape level it checks that:
//   - the [solution] compiles and gets Bitty to the EXIT (so the level is solvable)
//   - the [code] starter compiles but does NOT already reach the EXIT
// The world runs without a window: each move() is finished straight away.

#include "../src/levels.h"
#include "../src/vm.h"
#include "../src/game_api.h"
#include "raylib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int passed = 0, failed = 0;
static World world;

static void Report(bool ok, const char *name, const char *detail)
{
    if (ok) { passed++; printf("[PASS] %s\n", name); }
    else    { failed++; printf("[FAIL] %s\n", name); if (detail && detail[0]) printf("    %s\n", detail); }
}

// The console isn't shown in this test, so printing does nothing
void Console_Print(const char *text) { (void)text; }
static void HostPrint(void *user, const char *text) { (void)user; (void)text; }

// move() and write() happen instantly here, so the VM never has to wait
static bool HostBuiltin(void *user, VmBuiltin id, const int *args, int argc, int *result)
{
    (void)user; (void)argc;
    *result = 0;
    switch (id)
    {
        case VM_BUILTIN_MOVE:
        case VM_BUILTIN_WRITE:
        {
            Action a = { id == VM_BUILTIN_MOVE ? ACT_MOVE : ACT_WRITE, (Direction)args[0], args[0], 0 };
            if (world.state != WORLD_RUNNING) return false;
            World_StartAction(&world, a);
            for (int i = 0; i < 200 && !World_ActionDone(&world); i++) World_Update(&world, 0.05f);
            *result = world.lastResult;
            return false;
        }
        case VM_BUILTIN_READ:  *result = World_Read(&world);                      return false;
        case VM_BUILTIN_SCAN:  *result = World_Scan(&world, (Direction)args[0]);  return false;
        case VM_BUILTIN_GET_X: *result = World_GetX(&world);                      return false;
        case VM_BUILTIN_GET_Y: *result = World_GetY(&world);                      return false;
        default:               return false;
    }
}

// Runs 'code' on the level's map. Returns the world state at the end.
static WorldState Play(const Level *lv, const char *code, char *problem, int size)
{
    const char *rows[LEVEL_MAX_ROWS];
    for (int i = 0; i < lv->mapRows; i++) rows[i] = lv->map[i];
    World_LoadMap(&world, rows, lv->mapRows);
    world.doorCode = lv->doorCode;
    problem[0] = '\0';

    VmHost host = { NULL, HostBuiltin, HostPrint, NULL };
    VmOptions opt = { lv->banIndexing, 2000000 };
    VmBuildResult r;
    Vm *vm = Vm_Build(code, &opt, &host, &r);
    if (!vm) { snprintf(problem, (size_t)size, "doesn't compile (line %d): %s", r.line, r.message); return WORLD_RUNNING; }

    Vm_Start(vm, NULL, NULL, 0);
    VmStatus s = Vm_Run(vm, 2000000);
    for (int guard = 0; s == VM_WAITING && guard < 100000; guard++)
    {
        Vm_ActionDone(vm, world.lastResult);
        s = Vm_Run(vm, 2000000);
    }
    if (s == VM_ERROR) snprintf(problem, (size_t)size, "runtime error: %s", Vm_ErrorMessage(vm));
    Vm_Destroy(vm);
    if (getenv("ESCAPE_DEBUG")) printf("    (ended at %d,%d state %d, status %d)\n", world.bittyX, world.bittyY, world.state, s);
    return world.state;
}

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    printf("=== Escape level tests ===\n\n");
    char err[256];
    int count = Levels_LoadFolder("levels", err, sizeof err);
    Report(count > 0 && !err[0], "levels/levels.txt loads with no problems", err);

    for (int i = 0; i < Levels_Count(); i++)
    {
        const Level *lv = Levels_Get(i);
        if (lv->type != LEVEL_ESCAPE) continue;
        char name[200], problem[400];

        WorldState end = Play(lv, lv->solution, problem, sizeof problem);
        snprintf(name, sizeof name, "%s: solution reaches the EXIT", lv->title);
        Report(lv->solution[0] && end == WORLD_WON, name,
               !lv->solution[0] ? "no [solution] section" : problem[0] ? problem :
               end == WORLD_SEGFAULT ? "Bitty fell into the void" : "the program ended before the EXIT");

        end = Play(lv, lv->starterCode, problem, sizeof problem);
        snprintf(name, sizeof name, "%s: starter code compiles but doesn't win yet", lv->title);
        Report(!strstr(problem, "compile") && end != WORLD_WON, name,
               end == WORLD_WON ? "the starter code already reaches the EXIT" : problem);
    }
    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
