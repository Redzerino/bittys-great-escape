// login.h - the sign-in screen shown after the boot sequence.
//
// LOG IN / NEW ACCOUNT use the cloud (see cloud.h) when bitty_cloud.txt is set
// up, so the same progress loads on any PC. PLAY OFFLINE keeps the old single
// bitty_save.txt. Each account's progress is kept on this PC in
// bitty_save_<name>.txt as well, so it still works when the internet is down.

#ifndef LOGIN_H
#define LOGIN_H

#include <stdbool.h>
#include "raylib.h"

void        Login_Init(Font font, const char *gameFolder);
bool        Login_Draw(float dt);      // true once the player is in (their save has been loaded)

const char *Login_User(void);          // "" when playing offline as a guest
bool        Login_Cloud(void);         // signed in to a cloud account?

// Sends the current save file to the cloud (no-op for guests / offline).
void        Login_Upload(bool waitForIt);

#endif
