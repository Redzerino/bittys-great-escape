// talk.h - Bitty talks to the player.
//
// Two kinds of speech:
//
//   A TALK is the scripted chat at the start of a level (the level file's
//   [talk] section). It appears in a box at the bottom of the Memory panel,
//   types out letter by letter with a robot babble voice, and waits for the
//   player (Space, Enter or a click) before moving on. A talk can also make
//   Bitty walk by himself, so he can travel up to a problem and then explain it:
//
//       @walk EAST 3
//       Whoa. A wall!
//       To get past it we need a `for` loop.
//
//   Words inside `backticks` are shown in gold: use them for C code.
//
//   A BARK is a short reaction ("Bonk! That's a wall.") in a little bubble
//   above Bitty's head. It doesn't stop the game and fades by itself.

#ifndef TALK_H
#define TALK_H

#include <stdbool.h>
#include "raylib.h"
#include "game_api.h"

void Talk_Init(void);                     // sets up the voice (after InitWindow)
void Talk_Shutdown(void);

void Talk_Start(const char *script);      // begin a level's [talk] script
void Talk_Skip(void);                     // stop straight away
bool Talk_Active(void);

// Call every frame. 'panel' is the Memory panel (clicks inside it move the talk on).
// Returns true on the frame the talk finishes (or is skipped).
bool Talk_Update(World *w, float dt, Rectangle panel);
void Talk_Draw(Rectangle panel, Font font);

void Talk_Bark(const char *text);         // a short reaction above Bitty's head

#endif
