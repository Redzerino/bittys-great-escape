// login.c - sign-in screen (see login.h)
#include "login.h"
#include "cloud.h"
#include "save.h"
#include "sfx.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FIELD_NAME, FIELD_PASSWORD };
enum { ACT_NONE, ACT_LOGIN, ACT_CREATE };

static Font  font;
static char  folder[512];
static char  name[17], password[33];
static int   field;
static char  status[160];
static Color statusColor;
static int   pending;          // an action waiting one frame, so "Connecting..." gets drawn first
static float clockTime, shake, repeat, done = -1;
static char  user[17];
static char  pwHash[65];
static bool  cloudUser;

// ===========================================================================
// Files on this PC
// ===========================================================================
static const char *Path(const char *file) { return TextFormat("%s%s", folder, file); }

static void SetStatus(const char *text, Color c) { snprintf(status, sizeof status, "%s", text); statusColor = c; }
static void Problem(const char *text) { SetStatus(text, (Color){ 255, 120, 110, 255 }); shake = 0.35f; Sfx_Play(SFX_ERROR); }

// bitty_accounts.txt: "name hash" per line, for signing in without internet
static bool LocalAccount(const char *who, char hashOut[65])
{
    FILE *f = fopen(Path("bitty_accounts.txt"), "r");
    if (!f) return false;
    char n[64], h[65];
    bool found = false;
    while (fscanf(f, "%63s %64s", n, h) == 2)
        if (strcmp(n, who) == 0) { snprintf(hashOut, 65, "%s", h); found = true; }
    fclose(f);
    return found;
}

static void RememberLocalAccount(const char *who, const char *hash)
{
    // rewrite the file without the old line for this name, then add the new one
    char *old = LoadFileText(Path("bitty_accounts.txt"));
    FILE *f = fopen(Path("bitty_accounts.txt"), "w");
    if (!f) { if (old) UnloadFileText(old); return; }
    if (old)
    {
        char *line = strtok(old, "\r\n");
        while (line)
        {
            char n[64];
            if (sscanf(line, "%63s", n) == 1 && strcmp(n, who) != 0) fprintf(f, "%s\n", line);
            line = strtok(NULL, "\r\n");
        }
        UnloadFileText(old);
    }
    fprintf(f, "%s %s\n", who, hash);
    fclose(f);
}

static void WriteText(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

// Load this player's save into the game
static void Enter(const char *who, const char *hash, bool cloud, const char *welcome)
{
    SetStatus(welcome, (Color){ 120, 255, 180, 255 });     // first: welcome may be a TextFormat buffer
    snprintf(user, sizeof user, "%s", who);
    snprintf(pwHash, sizeof pwHash, "%s", hash ? hash : "");
    cloudUser = cloud;
    Save_Clear();
    Save_Load(who[0] ? Path(TextFormat("bitty_save_%s.txt", who)) : Path("bitty_save.txt"));
    if (who[0]) WriteText(Path("bitty_last_user.txt"), who);
    Sfx_Play(SFX_LOGIN);
    done = 1.1f;            // show the welcome for a moment
}

// ===========================================================================
// The two buttons
// ===========================================================================
static bool CheckFields(void)
{
    if (!Cloud_ValidName(name)) { Problem("Names are 3-16 letters, numbers, _ or -"); field = FIELD_NAME; return false; }
    if (strlen(password) < 4) { Problem("Passwords need at least 4 characters"); field = FIELD_PASSWORD; return false; }
    return true;
}

static void DoLogin(void)
{
    char hash[65], local[65];
    Cloud_HashPassword(name, password, hash);
    bool haveLocal = LocalAccount(name, local);

    if (Cloud_Configured())
    {
        CloudUser u;
        CloudResult r = Cloud_GetUser(name, &u);
        if (r == CLOUD_OK)
        {
            if (strcmp(u.pwHash, hash) != 0) { Cloud_FreeUser(&u); Problem("Wrong password"); return; }
            if (u.save && u.save[0]) WriteText(Path(TextFormat("bitty_save_%s.txt", name)), u.save);
            Cloud_FreeUser(&u);
            RememberLocalAccount(name, hash);
            Enter(name, hash, true, TextFormat("Welcome back, %s! Progress synced.", name));
            return;
        }
        if (r == CLOUD_NOT_FOUND) { Problem("No account with that name yet. Press NEW ACCOUNT."); return; }
        // can't reach the cloud: fall back to this PC's copy
        if (haveLocal && strcmp(local, hash) == 0)
        {
            Enter(name, hash, true, "Offline: using this PC's copy. It syncs next time.");
            return;
        }
        Problem(haveLocal ? "Wrong password" : "Can't reach the cloud. Check the internet or PLAY OFFLINE.");
        return;
    }

    if (!haveLocal) { Problem("No account with that name on this PC. Press NEW ACCOUNT."); return; }
    if (strcmp(local, hash) != 0) { Problem("Wrong password"); return; }
    Enter(name, hash, false, TextFormat("Welcome back, %s!", name));
}

static void DoCreate(void)
{
    char hash[65], local[65];
    Cloud_HashPassword(name, password, hash);

    // A new account starts with the progress already on this PC, so nobody loses their levels
    char *start = LoadFileText(Path("bitty_save.txt"));

    if (Cloud_Configured())
    {
        CloudUser u;
        CloudResult r = Cloud_GetUser(name, &u);
        Cloud_FreeUser(&u);
        if (r == CLOUD_OK) { Problem("That name is taken. Try another."); if (start) UnloadFileText(start); return; }
        if (r != CLOUD_NOT_FOUND) { Problem("Can't reach the cloud to make the account. Try again or PLAY OFFLINE."); if (start) UnloadFileText(start); return; }
        CloudUser fresh = { 0 };
        snprintf(fresh.pwHash, sizeof fresh.pwHash, "%s", hash);
        fresh.save = start;
        CloudResult put = Cloud_PutUser(name, &fresh);
        if (put != CLOUD_OK)
        {
            Problem(put == CLOUD_OFFLINE ? "Can't reach the cloud. Try again or PLAY OFFLINE." : "The cloud said no (check the database rules).");
            if (start) UnloadFileText(start);
            return;
        }
    }
    else if (LocalAccount(name, local)) { Problem("That name is taken on this PC."); if (start) UnloadFileText(start); return; }

    if (start) { WriteText(Path(TextFormat("bitty_save_%s.txt", name)), start); UnloadFileText(start); }
    RememberLocalAccount(name, hash);
    Enter(name, hash, Cloud_Configured(), Cloud_Configured() ? TextFormat("Account made! Welcome, %s.", name)
                                                             : TextFormat("Account made on this PC. Welcome, %s!", name));
}

// ===========================================================================
// Drawing
// ===========================================================================
static Camera2D View(void)
{
    float scale = fminf(GetScreenWidth() / 1280.0f, GetScreenHeight() / 720.0f);
    Camera2D c = { 0 };
    c.zoom = scale;
    c.offset = (Vector2){ (GetScreenWidth() - 1280 * scale) / 2, (GetScreenHeight() - 720 * scale) / 2 };
    return c;
}

static void Text(const char *s, float x, float y, float size, Color c) { DrawTextEx(font, s, (Vector2){ x, y }, size, size / 12.0f, c); }
static float Width(const char *s, float size) { return MeasureTextEx(font, s, size, size / 12.0f).x; }

static bool Button(Rectangle r, const char *label, Color base, Vector2 mouse, bool click, bool enabled)
{
    bool hover = enabled && CheckCollisionPointRec(mouse, r);
    Color c = hover ? (Color){ (unsigned char)fminf(255, base.r + 30), (unsigned char)fminf(255, base.g + 30), (unsigned char)fminf(255, base.b + 30), 255 } : base;
    DrawRectangleRounded(r, 0.25f, 6, enabled ? c : (Color){ 40, 44, 52, 255 });
    Text(label, r.x + r.width / 2 - Width(label, 18) / 2, r.y + r.height / 2 - 9, 18, enabled ? WHITE : GRAY);
    if (hover && click) { Sfx_Play(SFX_CLICK); return true; }
    return false;
}

static void Field(Rectangle r, const char *label, const char *value, bool secret, bool active, Vector2 mouse, bool click, int which)
{
    if (click && CheckCollisionPointRec(mouse, r)) { field = which; Sfx_Play(SFX_CLICK); }
    Text(label, r.x, r.y - 24, 15, active ? (Color){ 120, 240, 210, 255 } : LIGHTGRAY);
    DrawRectangleRounded(r, 0.15f, 6, (Color){ 8, 16, 24, 255 });
    DrawRectangleRoundedLinesEx(r, 0.15f, 6, active ? 2 : 1, active ? (Color){ 90, 240, 210, 255 } : (Color){ 60, 80, 96, 255 });
    char shown[40] = "";
    if (secret) { size_t n = strlen(value); for (size_t i = 0; i < n && i < 32; i++) shown[i] = '*'; shown[n < 32 ? n : 32] = '\0'; }
    else snprintf(shown, sizeof shown, "%s", value);
    Text(shown, r.x + 14, r.y + 12, 22, WHITE);
    if (active && fmodf(clockTime, 1.0f) < 0.55f)
        DrawRectangle((int)(r.x + 16 + Width(shown, 22)), (int)r.y + 10, 2, 26, (Color){ 90, 240, 210, 255 });
}

static void Typing(void)
{
    char *buf = field == FIELD_NAME ? name : password;
    size_t cap = field == FIELD_NAME ? sizeof name : sizeof password;
    int c;
    while ((c = GetCharPressed()) != 0)
    {
        size_t n = strlen(buf);
        if (field == FIELD_NAME)
        {
            c = tolower(c);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) continue;
        }
        else if (c < 32 || c > 126) continue;
        if (n + 1 < cap) { buf[n] = (char)c; buf[n + 1] = '\0'; Sfx_PlayPitched(SFX_TYPE, 0.9f + (c % 7) * 0.04f); }
    }
    bool back = IsKeyPressed(KEY_BACKSPACE);
    if (IsKeyDown(KEY_BACKSPACE)) { repeat += GetFrameTime(); if (repeat > 0.45f) { back = true; repeat = 0.4f; } }
    else repeat = 0;
    size_t n = strlen(buf);
    if (back && n > 0) { buf[n - 1] = '\0'; Sfx_PlayPitched(SFX_TYPE, 0.75f); }
    if (IsKeyPressed(KEY_TAB) || (IsKeyPressed(KEY_ENTER) && field == FIELD_NAME && name[0]))
    {
        field = field == FIELD_NAME ? FIELD_PASSWORD : FIELD_NAME;
        if (IsKeyPressed(KEY_ENTER)) field = FIELD_PASSWORD;
        Sfx_Play(SFX_CLICK);
    }
}

void Login_Init(Font f, const char *gameFolder)
{
    font = f;
    snprintf(folder, sizeof folder, "%s", gameFolder);
    name[0] = password[0] = user[0] = '\0';
    char *last = LoadFileText(Path("bitty_last_user.txt"));
    if (last)
    {
        snprintf(name, sizeof name, "%s", last);
        name[strcspn(name, "\r\n ")] = '\0';
        if (!Cloud_ValidName(name)) name[0] = '\0';
        UnloadFileText(last);
    }
    field = name[0] ? FIELD_PASSWORD : FIELD_NAME;
    SetStatus(Cloud_Configured() ? "Sign in and your progress follows you to any PC."
                                 : "Accounts are saved on this PC (cloud sync isn't set up).", LIGHTGRAY);
}

bool Login_Draw(float dt)
{
    clockTime += dt;
    Camera2D cam = View();
    Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), cam);
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool busy = pending != ACT_NONE || done >= 0;

    // run an action that was asked for last frame (so "Connecting..." was already on screen)
    if (pending != ACT_NONE)
    {
        int act = pending;
        pending = ACT_NONE;
        if (act == ACT_LOGIN) DoLogin(); else DoCreate();
        busy = done >= 0;
    }
    bool enterLogin = !busy && IsKeyPressed(KEY_ENTER) && field == FIELD_PASSWORD;   // before Typing moves the box
    if (!busy) Typing();

    ClearBackground((Color){ 4, 10, 16, 255 });
    BeginMode2D(cam);

    // the same circuit-board look as the map, with a slow scanning beam
    DrawRectangleGradientV(0, 0, 1280, 720, (Color){ 10, 35, 38, 255 }, (Color){ 5, 15, 24, 255 });
    for (int x = 16; x < 1280; x += 24)
        for (int y = 16; y < 720; y += 24) DrawCircle(x, y, 1, (Color){ 22, 48, 51, 255 });
    float beam = fmodf(clockTime * 160, 900) - 90;
    DrawRectangleGradientV(0, (int)beam, 1280, 90, Fade((Color){ 90, 240, 210, 255 }, 0), Fade((Color){ 90, 240, 210, 255 }, 0.07f));

    const char *title = "BITTY'S GREAT ESCAPE";
    Text(title, 640 - Width(title, 44) / 2 + 3, 70 + 3, 44, Fade(BLACK, 0.6f));
    Text(title, 640 - Width(title, 44) / 2, 70, 44, (Color){ 150, 255, 210, 255 });
    const char *sub = "SIGN IN";
    Text(sub, 640 - Width(sub, 20) / 2, 128, 20, LIGHTGRAY);

    float sx = shake > 0 ? sinf(clockTime * 80) * 10 * shake : 0;
    shake = fmaxf(0, shake - dt);
    Rectangle panel = { 400 + sx, 170, 480, 420 };
    DrawRectangleRounded((Rectangle){ panel.x + 10, panel.y + 12, panel.width, panel.height }, 0.06f, 6, Fade(BLACK, 0.45f));
    DrawRectangleRounded(panel, 0.06f, 6, (Color){ 12, 26, 37, 250 });
    DrawRectangleRoundedLinesEx(panel, 0.06f, 6, 2, Fade((Color){ 90, 240, 210, 255 }, 0.6f + 0.2f * sinf(clockTime * 2)));

    // cloud badge
    bool cloud = Cloud_Configured();
    Rectangle badge = { panel.x + 24, panel.y + 22, cloud ? 176 : 150, 26 };
    DrawRectangleRounded(badge, 0.5f, 6, cloud ? (Color){ 20, 90, 70, 255 } : (Color){ 90, 70, 20, 255 });
    DrawCircle((int)badge.x + 15, (int)badge.y + 13, 5, cloud ? (Color){ 120, 255, 180, 255 } : (Color){ 255, 200, 90, 255 });
    Text(cloud ? "CLOUD SYNC ON" : "THIS PC ONLY", badge.x + 28, badge.y + 6, 14, WHITE);

    Field((Rectangle){ panel.x + 24, panel.y + 92, 432, 46 }, "NAME", name, false, field == FIELD_NAME && !busy, mouse, click && !busy, FIELD_NAME);
    Field((Rectangle){ panel.x + 24, panel.y + 178, 432, 46 }, "PASSWORD", password, true, field == FIELD_PASSWORD && !busy, mouse, click && !busy, FIELD_PASSWORD);

    bool wantLogin = Button((Rectangle){ panel.x + 24, panel.y + 250, 210, 48 }, "LOG IN", (Color){ 30, 120, 100, 255 }, mouse, click, !busy);
    bool wantNew   = Button((Rectangle){ panel.x + 246, panel.y + 250, 210, 48 }, "NEW ACCOUNT", (Color){ 70, 50, 120, 255 }, mouse, click, !busy);
    bool wantGuest = Button((Rectangle){ panel.x + 24, panel.y + 310, 432, 38 }, "PLAY OFFLINE (no account)", (Color){ 36, 44, 56, 255 }, mouse, click, !busy);
    if (enterLogin) wantLogin = true;

    if ((wantLogin || wantNew) && CheckFields())
    {
        pending = wantLogin ? ACT_LOGIN : ACT_CREATE;
        SetStatus(cloud ? "Connecting to the cloud..." : "Checking...", SKYBLUE);
    }
    if (wantGuest) Enter("", NULL, false, "Playing offline. Progress saves on this PC.");

    // status line (wrapped to the panel)
    if (pending != ACT_NONE)
    {
        for (int d = 0; d < 3; d++)
            DrawCircle((int)(panel.x + 34 + d * 14), (int)(panel.y + 378), 4, Fade(SKYBLUE, fmodf(clockTime * 3 - d * 0.3f, 1.0f)));
        Text(status, panel.x + 80, panel.y + 370, 16, statusColor);
    }
    else Text(status, panel.x + 24, panel.y + 370, Width(status, 16) > 432 ? 13 : 16, statusColor);

    Text("Tab switches box   |   Enter logs in   |   Ctrl+M mutes sound", 640 - Width("Tab switches box   |   Enter logs in   |   Ctrl+M mutes sound", 13) / 2, 620, 13, (Color){ 120, 155, 166, 255 });
    EndMode2D();

    if (done >= 0)
    {
        done -= dt;
        if (done < 0) { memset(password, 0, sizeof password); return true; }
    }
    return false;
}

const char *Login_User(void)  { return user; }
bool        Login_Cloud(void) { return cloudUser && user[0] && Cloud_Configured(); }

void Login_Upload(bool waitForIt)
{
    if (!Login_Cloud()) return;
    char *text = LoadFileText(Save_Path());
    if (!text) return;
    CloudUser u = { 0 };
    snprintf(u.pwHash, sizeof u.pwHash, "%s", pwHash);
    u.save = text;
    if (waitForIt) Cloud_PutUser(user, &u);
    else Cloud_PutUserInBackground(user, &u);
    u.save = NULL;
    UnloadFileText(text);
}
