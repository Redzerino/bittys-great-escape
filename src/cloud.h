// cloud.h - accounts and saves that follow a player from PC to PC.
//
// The data lives in a free Firebase Realtime Database. The game talks to it
// with curl (built into Windows 10 and 11), so there is no extra library to
// install. Set it up by putting one line in bitty_cloud.txt next to the game:
//
//     url https://your-project-default-rtdb.europe-west1.firebasedatabase.app
//
// Without that file, accounts still work but only on this PC.
//
// Each player is stored at /users/<name>.json as
//     {"pw":"<sha-256 of name+password>","save":"<base64 of their save file>","updated":<unix time>}
// Passwords are never stored or sent as plain text, only as a hash.
//
// This file deliberately doesn't include raylib.h, so it can include windows.h.

#ifndef CLOUD_H
#define CLOUD_H

#include <stdbool.h>

typedef enum { CLOUD_OK, CLOUD_NOT_FOUND, CLOUD_OFFLINE, CLOUD_ERROR } CloudResult;

void        Cloud_Init(const char *gameFolder);     // reads bitty_cloud.txt
bool        Cloud_Configured(void);
const char *Cloud_Url(void);
const char *Cloud_LabsEnd(void);                     // "labs_end 2026-12-19" line, or "" if not set

// A user record. save is the raw save-file text (already base64-decoded).
typedef struct {
    char  pwHash[65];
    char *save;          // malloc'd, may be NULL; free with Cloud_FreeUser
    long  updated;
} CloudUser;

CloudResult Cloud_GetUser(const char *name, CloudUser *out);           // blocks (a second or so)
CloudResult Cloud_PutUser(const char *name, const CloudUser *user);    // blocks
void        Cloud_PutUserInBackground(const char *name, const CloudUser *user);   // returns at once
void        Cloud_FreeUser(CloudUser *u);

// helpers shared with the login screen
void        Cloud_HashPassword(const char *name, const char *password, char out[65]);
bool        Cloud_ValidName(const char *name);       // 3-16 of a-z 0-9 _ -

#endif
