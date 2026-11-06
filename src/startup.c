#include "startup.h"
#include "game_api.h"
#include "customisation.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

static float elapsed;
static bool title, muted, audioOwned, soundReady;
static Font uiFont;
static Sound bootSound;
static World actor;
static RenderTexture2D preview;
static const float INTRO_SECONDS = 7.5f;
static const Color ink = {8,12,24,255};
static const Color cyan = {90,225,255,255};

static float Clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float Ease(float x) { x=Clamp01(x); return x*x*(3-2*x); }
static void Text(const char *s,float x,float y,float size,Color c)
{
    DrawTextEx(uiFont,s,(Vector2){x,y},size,1,c);
}
static void BootChime(void)
{
    // Original generated notes: no external music or downloaded assets.
    if (!IsAudioDeviceReady()) { InitAudioDevice(); audioOwned=IsAudioDeviceReady(); }
    if (!IsAudioDeviceReady()) return;
    const int rate=22050, frames=rate;
    short *samples=MemAlloc(frames*sizeof(short));
    if (!samples) return;
    const float notes[]={261.63f,329.63f,392.0f,523.25f};
    for(int i=0;i<frames;i++)
    {
        float t=(float)i/rate;
        int n=(int)(t/0.25f); if(n>3)n=3;
        float local=fmodf(t,0.25f);
        float envelope=fminf(local/0.015f,1.0f)*(1-local/0.25f);
        samples[i]=(short)(sinf(2*PI*notes[n]*t)*envelope*5000);
    }
    Wave wave={(unsigned int)frames,(unsigned int)rate,16,1,samples};
    bootSound=LoadSoundFromWave(wave);
    soundReady=bootSound.frameCount>0;
    MemFree(samples);
    if(soundReady) PlaySound(bootSound);
}
void Startup_Init(Font font)
{
    uiFont=font;
    elapsed=0; title=false;
    World_Init(&actor,1,1);
    preview=LoadRenderTexture(650,510);
    BootChime();
}
bool Startup_Update(float dt)
{
    if(dt>0.1f)dt=0.1f;
    elapsed+=dt;
    if(IsKeyPressed(KEY_M))
    {
        muted=!muted;
        if(soundReady) SetSoundVolume(bootSound,muted?0:1);
    }
    if(!title)
    {
        if(elapsed>=INTRO_SECONDS || IsKeyPressed(KEY_ENTER) ||
           IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ESCAPE))
            title=true;
        return false; // Skipping never also dismisses the title screen.
    }
    if(IsKeyPressed(KEY_R))
    {
        elapsed=0; title=false;
        if(soundReady) PlaySound(bootSound);
        return false;
    }
    Rectangle start={64,500,350,62};
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
        (CheckCollisionPointRec(GetMousePosition(),start) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
}
static void CircuitBackground(void)
{
    ClearBackground(ink);
    DrawRectangleGradientV(0,0,GetScreenWidth(),GetScreenHeight(),(Color){14,25,46,255},ink);
    for(int i=0;i<15;i++)
    {
        float y=65+i*43;
        float bend=560+(i%5)*62;
        Color wire={28,55,73,255};
        DrawLineEx((Vector2){0,y},(Vector2){bend,y},1,wire);
        DrawLineEx((Vector2){bend,y},(Vector2){bend+75,y+60},1,wire);
        DrawLineEx((Vector2){bend+75,y+60},(Vector2){1280,y+60},1,wire);
        float x=fmodf(elapsed*(45+i*3)+i*131,1280);
        float py=y+(x>bend?fminf((x-bend)*0.8f,60):0);
        DrawCircleV((Vector2){x,py},2,Fade(cyan,0.4f));
    }
    for(int i=0;i<6;i++)
    {
        int x=80+i*210;
        DrawRectangleLinesEx((Rectangle){(float)x,610,115,48},1,(Color){25,51,68,255});
        for(int pin=0;pin<6;pin++) DrawRectangle(x+8+pin*18,602,4,8,(Color){35,65,80,255});
    }
}
static void ActorScene(float wake,float run,bool celebrate)
{
    Camera3D camera={0};
    camera.position=(Vector3){3.0f,2.4f,5.5f};
    camera.target=(Vector3){0,0.45f,0}; camera.up=(Vector3){0,1,0};
    camera.fovy=32; camera.projection=CAMERA_PERSPECTIVE;
    BeginTextureMode(preview);
    ClearBackground((Color){0,0,0,0});
    BeginMode3D(camera);
    // CPU platform and circuit runway.
    DrawCube((Vector3){0,-0.18f,0},2.6f,0.25f,2.4f,(Color){25,35,55,255});
    DrawCubeWires((Vector3){0,-0.18f,0},2.6f,0.25f,2.4f,Fade(cyan,0.65f));
    for(int i=0;i<9;i++)
    {
        float x=-2.8f+i*0.65f;
        DrawCube((Vector3){x,-0.34f,0},0.5f,0.08f,0.85f,(Color){30,55,72,255});
        DrawCube((Vector3){x,-0.29f,0},0.42f,0.015f,0.04f,Customisation_Glow());
    }
    rlPushMatrix();
    float x=run>0 ? -2.0f+2.0f*Ease(run) : 0;
    float jump=run>0 ? fabsf(sinf(run*PI*6))*0.18f : 0;
    rlTranslatef(x,jump-(1-wake)*0.25f,0);
    rlScalef(1,0.45f+0.55f*wake,1);
    actor.mood=celebrate?MOOD_HAPPY:MOOD_IDLE;
    World_DrawBittyPreview(&actor,elapsed);
    rlPopMatrix();
    EndMode3D();
    EndTextureMode();
    DrawTextureRec(preview.texture,(Rectangle){0,0,650,-510},(Vector2){610,100},WHITE);
}
void Startup_Draw(void)
{
    CircuitBackground();
    Text("BOOT / SECTOR 01",64,40,16,cyan);
    Text("A CODE-POWERED ESCAPE",64,68,14,(Color){140,165,185,255});
    if(!title && elapsed<2)
    {
        Text("INITIALISING...",64,172,38,WHITE);
        const char *lines[]={"> checking memory banks","> restoring circuit pathways","> locating Bitty","> consciousness online"};
        for(int i=0;i<4;i++)
            if(elapsed>i*0.4f)Text(lines[i],68,260+i*38,20,i==3?cyan:LIGHTGRAY);
        DrawRectangle(64,455,490,5,(Color){35,55,70,255});
        DrawRectangle(64,455,(int)(490*Clamp01(elapsed/2)),5,cyan);
    }
    else if(!title && elapsed<6)
    {
        bool running=elapsed>=4;
        Text(running?"FIND A WAY OUT.":"WAKE UP, BITTY.",64,178,38,WHITE);
        Text("SYSTEM SHUTDOWN INITIATED",64,255,22,(Color){255,125,135,255});
        Text(running?"Your code is his only escape.":"One tiny bot. One enormous computer.",64,310,19,LIGHTGRAY);
        ActorScene(Ease((elapsed-2)/0.8f),running?(elapsed-4)/2:0,false);
    }
    else
    {
        float reveal=title?1:Ease((elapsed-6)/0.7f);
        float offset=(1-reveal)*35;
        Text("BITTY'S",64,177+offset,66,Fade(WHITE,reveal));
        Text("GREAT",64,247+offset,78,Fade(cyan,reveal));
        Text("ESCAPE",64,330+offset,78,Fade(cyan,reveal));
        Text("WRITE CODE. BREAK FREE.",68,430,21,Fade(LIGHTGRAY,reveal));
        ActorScene(1,0,true);
        if(title)
        {
            float pulse=0.7f+0.3f*sinf(elapsed*3);
            Rectangle button={64,500,350,62};
            bool hover=CheckCollisionPointRec(GetMousePosition(),button);
            DrawRectangleRounded(button,0.18f,8,hover?(Color){35,85,105,255}:(Color){20,50,68,255});
            DrawRectangleRoundedLinesEx(button,0.18f,8,2,Fade(cyan,pulse));
            Text("PRESS ENTER TO START",85,520,22,WHITE);
            Text("R  replay intro",68,582,15,(Color){140,165,185,255});
        }
    }
    Text(title?"YOUR CODE IS THE ONLY WAY OUT":"ENTER / SPACE  skip intro",64,671,16,(Color){140,165,185,255});
    Text(muted?"M  sound off":"M  sound on",1080,671,15,(Color){140,165,185,255});
}
void Startup_Shutdown(void)
{
    if(soundReady) UnloadSound(bootSound);
    UnloadRenderTexture(preview);
    if(audioOwned) CloseAudioDevice();
}
