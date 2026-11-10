#include "circuit.h"
#include "levels.h"
#include "save.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { WORLDS=9, STAGES=5 };
static int stages[WORLDS][STAGES], selectedWorld, selectedStage;
static float clockTime;
static const char *worldNames[]={"BOOT SECTOR","REGISTER BANK","CPU CONTROL UNIT","PROCESSING CORE","SYSTEM LIBRARY","RAM BANKS","DATA BUS","ADDRESS NETWORK","USB GATEWAY"};
static const char *topics[]={"Commands / variables","Types / arithmetic","Decisions / logic","Loops / repetition","Functions / scope","Arrays / searching","Characters / strings","Pointers / addresses","Final escape / revision"};
static const char *planned[WORLDS][STAGES]={
 {"Wake Up","Long Corridor","Mind the Gap","Memory Boxes","Fork in the Road"},
 {"Data Types","Digit Peeler","Binary Decoder","Register Updates","Access Code"},
 {"Comparisons","Choose a Path","Multiple Routes","Logical Gates","Control Terminal"},
 {"Counted Loops","Sensor Loop","Try Again","Emergency Stop","Prime Lock"},
 {"First Function","Parameters","Return Values","Local Memory","Repair Toolkit"},
 {"Data Shelf","Array Traversal","Power Audit","Search the Bay","Mirror Mirror"},
 {"Character Codes","End of Message","String Length","Password Terminal","Message Repair"},
 {"Memory Addresses","Follow the Wire","Swap Wires","Pointer Arithmetic","Measure the String"},
 {"Port Controller","Transfer Buffer","Final Firewall","Escape Protocol","Bitty Breaks Free"}
};
static const Color accents[]={
 {100,245,175,255},{255,198,95,255},{255,135,125,255},
 {110,195,255,255},{185,150,255,255},{100,225,220,255},
 {245,145,210,255},{180,235,115,255},{235,242,255,255}
};
static Camera2D View(void)
{
    float scale=fminf(GetScreenWidth()/1280.0f,GetScreenHeight()/720.0f);
    Camera2D c={0}; c.zoom=scale;
    c.offset=(Vector2){(GetScreenWidth()-1280*scale)/2,(GetScreenHeight()-720*scale)/2};
    return c;
}
static Rectangle Chip(int world)
{
    int row=world/3, col=world%3;
    if(row==1)col=2-col; // One continuous snake through the board.
    return (Rectangle){32+col*293.0f,150+row*158.0f,268,132};
}
static Vector2 Node(int world,int stage)
{
    Rectangle r=Chip(world);
    return (Vector2){r.x+34+stage*50,r.y+89};
}
// Existing files retain their identities, so the team's saves remain valid.
// Explicit topic placements bridge the old world numbering to the new nine worlds.
static bool Placement(const char *file,int *world,int *slot)
{
    if(!strcmp(file,"2-0_first_function.txt")){*world=4;*slot=0;return true;}
    if(!strcmp(file,"2-1_digit_peeler.txt")){*world=1;*slot=1;return true;}
    if(!strcmp(file,"2-2_binary_decoder.txt")){*world=1;*slot=2;return true;}
    if(!strcmp(file,"3-1_leap_year_gate.txt")){*world=2;*slot=3;return true;}
    if(!strcmp(file,"4-1_prime_lock.txt")){*world=3;*slot=4;return true;}
    if(!strcmp(file,"5-1_search_the_bay.txt")){*world=5;*slot=3;return true;}
    if(!strcmp(file,"5-2_mirror_mirror.txt")){*world=5;*slot=4;return true;}
    if(!strcmp(file,"8-1_swap_wires.txt")){*world=7;*slot=2;return true;}
    if(!strcmp(file,"8-2_measure_the_string.txt")){*world=7;*slot=4;return true;}
    int w,s;
    if(sscanf(file,"%d-%d",&w,&s)!=2||w<1||w>9||s<1||s>5)return false;
    *world=w-1;*slot=s-1;return true;
}
void Circuit_Init(void)
{
    for(int w=0;w<WORLDS;w++)for(int s=0;s<STAGES;s++)stages[w][s]=-1;
    for(int i=0;i<Levels_Count();i++)
    {
        int w,s;
        if(Placement(Levels_Get(i)->file,&w,&s)&&stages[w][s]<0)stages[w][s]=i;
        else TraceLog(LOG_WARNING,"Circuit: no unique slot for %s",Levels_Get(i)->file);
    }
}
void Circuit_SelectLevel(int level)
{
    for(int w=0;w<WORLDS;w++)for(int s=0;s<STAGES;s++)
        if(stages[w][s]==level){selectedWorld=w;selectedStage=s;return;}
}
static bool Done(int index){return index>=0&&Save_IsDone(Levels_Get(index)->file);}
static void Label(Font font,const char *text,float x,float y,float size,Color c)
{DrawTextEx(font,text,(Vector2){x,y},size,0,c);}
static void FitLabel(Font font,const char *text,float x,float y,float size,float width,Color c)
{
    while(size>10&&MeasureTextEx(font,text,size,0).x>width)size-=1;
    Label(font,text,x,y,size,c);
}
int Circuit_Draw(Font font,float dt)
{
    clockTime+=dt;
    Camera2D camera=View();
    Vector2 mouse=GetScreenToWorld2D(GetMousePosition(),camera);
    bool click=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    int launch=-1, total=0, available=0;
    for(int w=0;w<WORLDS;w++)for(int s=0;s<STAGES;s++)
    {if(stages[w][s]>=0)available++;if(Done(stages[w][s]))total++;}
    ClearBackground((Color){4,10,16,255});
    BeginMode2D(camera);
    DrawRectangleGradientV(0,0,1280,720,(Color){10,35,38,255},(Color){5,15,24,255});
    for(int x=16;x<1280;x+=24)for(int y=16;y<720;y+=24)
        DrawCircle(x,y,1,(Color){22,48,51,255});
    Label(font,"BITTY'S ESCAPE CIRCUIT",32,28,32,(Color){150,255,210,255});
    Label(font,"Trace a route through the computer. Choose a stage to write your escape.",34,73,16,LIGHTGRAY);
    Label(font,TextFormat("%02d / 45 CLEARED   |   %d stages playable",total,available),34,107,17,SKYBLUE);
    // Copper tracks connect the nine major chips.
    for(int w=0;w<8;w++)
    {
        Rectangle a=Chip(w),b=Chip(w+1);
        Vector2 from={a.x+a.width/2,a.y+a.height/2},to={b.x+b.width/2,b.y+b.height/2};
        DrawLineEx(from,to,7,(Color){20,65,67,255});
        DrawLineEx(from,to,2,Fade(accents[w],0.4f));
        float p=fmodf(clockTime*0.2f+w*0.13f,1);
        DrawCircleV((Vector2){from.x+(to.x-from.x)*p,from.y+(to.y-from.y)*p},3,accents[w]);
    }
    for(int w=0;w<WORLDS;w++)
    {
        Rectangle chip=Chip(w); Color accent=accents[w];
        for(int pin=0;pin<10;pin++)
        {
            DrawRectangle((int)chip.x+15+pin*25,(int)chip.y-6,6,6,(Color){135,130,95,255});
            DrawRectangle((int)chip.x+15+pin*25,(int)(chip.y+chip.height),6,6,(Color){135,130,95,255});
        }
        DrawRectangleRounded(chip,0.08f,4,(Color){12,27,36,255});
        DrawRectangleRoundedLinesEx(chip,0.08f,4,selectedWorld==w?2:1,Fade(accent,selectedWorld==w?1:0.45f));
        Label(font,TextFormat("%02d",w+1),chip.x+13,chip.y+13,16,accent);
        FitLabel(font,worldNames[w],chip.x+44,chip.y+13,18,212,WHITE);
        Label(font,topics[w],chip.x+14,chip.y+41,12,(Color){155,178,187,255});
        int worldDone=0;
        for(int s=0;s<STAGES;s++)if(Done(stages[w][s]))worldDone++;
        Label(font,TextFormat("%d/5",worldDone),chip.x+231,chip.y+114,11,accent);
        DrawLineEx(Node(w,0),Node(w,4),2,(Color){45,78,86,255});
        for(int s=0;s<STAGES;s++)
        {
            Vector2 n=Node(w,s); int index=stages[w][s];
            bool done=Done(index),hover=CheckCollisionPointCircle(mouse,n,20);
            Color color=done?(Color){110,245,165,255}:index>=0?accent:(Color){55,72,80,255};
            if(selectedWorld==w&&selectedStage==s)
                DrawCircleLines((int)n.x,(int)n.y,21+2*sinf(clockTime*3),accent);
            if(hover)DrawCircleV(n,21,Fade(accent,0.17f));
            DrawCircleV(n,15,(Color){8,20,29,255});
            DrawCircleLines((int)n.x,(int)n.y,15,color);
            if(index>=0&&!strcmp(Levels_Get(index)->file,Save_GetCurrent()))
                DrawTriangle((Vector2){n.x-5,n.y-27},(Vector2){n.x,n.y-20},(Vector2){n.x+5,n.y-27},WHITE);
            Label(font,done?"+":TextFormat("%d",s+1),n.x-4,n.y-7,16,color);
            if(index<0) // Small padlock, plus explicit "coming soon" in details.
            {
                DrawRectangle((int)n.x+6,(int)n.y+8,7,6,color);
                DrawCircleLines((int)n.x+9,(int)n.y+8,3,color);
            }
            if(hover&&click)
            {
                selectedWorld=w;selectedStage=s;
                if(index>=0)launch=index;
            }
        }
    }
    Rectangle details={935,150,313,448};
    DrawRectangleRounded(details,0.06f,6,(Color){12,26,37,255});
    DrawRectangleRoundedLinesEx(details,0.06f,6,1,Fade(accents[selectedWorld],0.6f));
    int selected=stages[selectedWorld][selectedStage];
    Label(font,"SELECTED CIRCUIT",956,174,15,accents[selectedWorld]);
    FitLabel(font,worldNames[selectedWorld],956,212,23,270,WHITE);
    Label(font,TextFormat("STAGE %d.%d",selectedWorld+1,selectedStage+1),956,254,18,SKYBLUE);
    const char *name=selected>=0?Levels_Get(selected)->title:planned[selectedWorld][selectedStage];
    FitLabel(font,name,956,295,20,270,WHITE);
    Label(font,Done(selected)?"COMPLETED":selected>=0?"READY TO CODE":"COMING SOON",956,340,18,selected>=0?GREEN:GRAY);
    Label(font,selected>=0?"Click a lit node to enter.":"This stage is being built.",956,383,15,LIGHTGRAY);
    Label(font,"Your code and completed stages",956,415,13,LIGHTGRAY);
    Label(font,"are saved automatically.",956,436,13,LIGHTGRAY);
    Rectangle play={956,489,270,48};
    DrawRectangleRounded(play,0.15f,6,selected>=0?(Color){30,100,92,255}:(Color){30,43,52,255});
    Label(font,selected>=0?"ENTER STAGE  >":"NOT AVAILABLE YET",976,505,18,selected>=0?WHITE:GRAY);
    if(selected>=0&&((click&&CheckCollisionPointRec(mouse,play))||IsKeyPressed(KEY_ENTER)))launch=selected;
    Label(font,"USB EXIT",1140,634,16,accents[8]);
    DrawRectangleLinesEx((Rectangle){936,630,185,28},2,accents[8]);
    Label(font,"45 stages to freedom",946,638,12,LIGHTGRAY);
    Label(font,"+ completed     coloured = playable     padlock = coming soon",34,660,15,LIGHTGRAY);
    Label(font,"White pointer = last played   |   Enter opens the selected stage",34,689,13,(Color){120,155,166,255});
    EndMode2D();
    return launch;
}
