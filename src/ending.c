#include "ending.h"
#include "world_visuals.h"
#include "customisation.h"
#include <math.h>
#include <string.h>
static float elapsed;
static RenderTexture2D picture;
static bool ready;
void Ending_Reset(void){elapsed=0;}
bool Ending_Draw(Font font,float dt,int completed,int total)
{
    elapsed+=dt;
    if(!ready){picture=LoadRenderTexture(620,430);ready=true;}
    float scale=fminf(GetScreenWidth()/1280.0f,GetScreenHeight()/720.0f);
    Camera2D view={0};view.zoom=scale;
    view.offset=(Vector2){(GetScreenWidth()-1280*scale)/2,(GetScreenHeight()-720*scale)/2};
    Vector2 mouse=GetScreenToWorld2D(GetMousePosition(),view);
    int values[]={0,0},seen[]={1,1};unsigned char kinds[]={5,5};
    char labels[2][32]={"USB PORT","OUTSIDE"};Vector3 cells[]={{-1.2f,0,0},{1.2f,0,0}};
    Camera3D camera={0};camera.position=(Vector3){1.5f,4,6};camera.target=(Vector3){0,0.4f,0};camera.up=(Vector3){0,1,0};camera.fovy=34;camera.projection=CAMERA_PERSPECTIVE;
    float t=fminf(1,elapsed/3.2f);t=t*t*(3-2*t);
    DataScene scene={0};scene.target=picture;scene.camera=camera;scene.font=font;
    scene.count=2;scene.values=values;scene.kinds=kinds;scene.labels=labels;scene.visited=seen;scene.cells=cells;
    scene.bitty=(Vector3){-1.2f+2.4f*t,0,0};scene.theme=9;scene.current=t<0.5f?0:1;scene.best=-1;
    scene.moving=elapsed<3.2f;scene.happy=true;scene.victory=elapsed>=3.2f;scene.victoryTime=fmaxf(0,elapsed-3.2f);scene.time=elapsed;
    World_DrawDataScene(&scene);
    ClearBackground((Color){5,12,23,255});
    BeginMode2D(view);
    DrawRectangleGradientV(0,0,1280,720,(Color){12,29,45,255},(Color){5,12,23,255});
    for(int i=0;i<45;i++)
    {
        float x=fmodf(i*151.0f+elapsed*18,1280),y=40+(i*97)%600;
        DrawCircle((int)x,(int)y,1.5f,Fade(SKYBLUE,0.35f));
    }
    DrawTextEx(font,"USB TRANSFER / COMPLETE",(Vector2){65,85},18,0,SKYBLUE);
    DrawTextEx(font,"BITTY IS",(Vector2){65,165},55,0,WHITE);
    DrawTextEx(font,"FREE!",(Vector2){65,235},84,0,(Color){130,255,180,255});
    DrawTextEx(font,"You coded a way out.",(Vector2){69,350},23,0,WHITE);
    DrawTextEx(font,TextFormat("%d / %d stages completed",completed,total),(Vector2){69,400},20,0,SKYBLUE);
    DrawTextEx(font,"Revisit the circuit to keep learning.",(Vector2){69,438},17,0,LIGHTGRAY);
    DrawTextureRec(picture.texture,(Rectangle){0,0,620,-430},(Vector2){595,130},WHITE);
    Rectangle button={65,530,380,58};bool hover=CheckCollisionPointRec(mouse,button);
    DrawRectangleRounded(button,0.15f,5,hover?(Color){40,120,100,255}:(Color){25,75,72,255});
    DrawTextEx(font,"BACK TO ESCAPE CIRCUIT",(Vector2){85,548},20,0,WHITE);
    DrawTextEx(font,"ENTER / SPACE to continue",(Vector2){69,614},16,0,LIGHTGRAY);
    EndMode2D();
    return elapsed>0.4f&&(IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)||(hover&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)));
}
void Ending_Shutdown(void){if(ready)UnloadRenderTexture(picture);ready=false;}
