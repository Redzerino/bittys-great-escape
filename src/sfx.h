// sfx.h - sound effects for the whole game.
//
// Every sound is made by code when the game starts (little synthesisers:
// sine and square waves, noise and envelopes), so there are no audio files to
// ship or licence. Ctrl+M mutes / unmutes everything.

#ifndef SFX_H
#define SFX_H

#include <stdbool.h>

typedef enum {
    SFX_CLICK,        // any button
    SFX_HOVER,        // soft tick when the mouse moves onto a big button
    SFX_RUN,          // program starts
    SFX_STEP,         // one STEP / NEXT
    SFX_BACK,         // STEP BACK
    SFX_PASS,         // one test passed
    SFX_FAIL,         // a test failed
    SFX_CLEAR,        // stage cleared (fanfare)
    SFX_CRASH,        // segmentation fault / runtime error
    SFX_ERROR,        // compile error
    SFX_MOVE,         // Bitty takes a step
    SFX_WHOOSH,       // screen change (map, labs, login)
    SFX_STREAK,       // endless streak goes up
    SFX_LOGIN,        // logged in
    SFX_TYPE,         // a key typed into a text box
    SFX_SELECT,       // big choice made (C / C++ card)
    SFX_COUNT
} SfxId;

void Sfx_Init(void);              // after the window is open
void Sfx_Shutdown(void);
void Sfx_Play(SfxId id);
void Sfx_PlayPitched(SfxId id, float pitch);
void Sfx_Update(void);            // Ctrl+M toggles mute
bool Sfx_Muted(void);

#endif
