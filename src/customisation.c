#include "customisation.h"
#include "rlgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// Keep cosmetic settings separate from the world: resetting a level must not
// reset the player's appearance. Save beside the executable, not the working dir.
enum { BODY, GLOW, FACE, ANTENNA, HAT, GLASSES, HEADPHONES, CATEGORY_COUNT };
static int choice[CATEGORY_COUNT];
static unsigned char completed[128];
static char savePath[1024];
static bool saveOk = true;
static const int counts[] = {8, 7, 4, 5, 5, 4, 3};
static const char *labels[] = {"Body colour", "Glow colour", "Face", "Antenna", "Hat", "Glasses", "Headphones"};
static const char *options[CATEGORY_COUNT][8] = {
    {"Slate", "Electric blue", "Purple", "Mint", "Coral", "Gold", "Ice", "Midnight"},
    {"Cyan", "Lime", "Pink", "Amber", "Violet", "White", "Circuit glow"},
    {"Classic", "Happy", "Serious", "Sleepy"},
    {"Standard", "None", "Lightning bolt", "Satellite dish", "Loop Master"},
    {"None", "Cap", "Top hat", "Graduation cap", "Crown"},
    {"None", "Round glasses", "Square glasses", "Sunglasses"},
    {"Classic", "None", "Studio"}
};
static const Color bodies[] = {
    {104,112,128,255}, {55,145,230,255}, {155,90,210,255}, {65,180,110,255},
    {225,100,105,255}, {225,177,60,255}, {195,220,235,255}, {45,48,75,255}
};
static const Color glows[] = {
    {100,215,255,255}, {130,255,130,255}, {255,120,205,255},
    {255,195,75,255}, {180,130,255,255}, {230,245,255,255}, {100,255,200,255}
};
static int CompletionCount(void)
{
    int total = 0;
    for (int i = 0; i < 128; i++) total += completed[i] != 0;
    return total;
}
static int Requirement(int category, int option)
{
    if (category == ANTENNA && option == 4) return 5;
    if (category == GLOW && option == 6) return 10;
    if (category == HAT && option == 4) return 20;
    return 0;
}
static void Save(void)
{
    FILE *f = fopen(savePath, "w");
    if (!f) { saveOk = false; return; }
    bool ok = fprintf(f, "BITTY_STYLE_1\n") > 0;
    for (int i = 0; i < CATEGORY_COUNT; i++)
        if (fprintf(f, "%d ", choice[i]) < 0) ok = false;
    if (fprintf(f, "\n") < 0) ok = false;
    for (int i = 0; i < 128; i++)
        if (fprintf(f, "%d ", completed[i]) < 0) ok = false;
    if (fclose(f) != 0) ok = false;
    saveOk = ok;
}
void Customisation_Init(void)
{
    snprintf(savePath, sizeof savePath, "%sbitty-customisation.save", GetApplicationDirectory());
    FILE *f = fopen(savePath, "r");
    if (!f) return;
    char version[32];
    int loaded[CATEGORY_COUNT], progress[128];
    bool valid = fscanf(f, "%31s", version) == 1 && strcmp(version, "BITTY_STYLE_1") == 0;
    for (int i = 0; valid && i < CATEGORY_COUNT; i++)
        valid = fscanf(f, "%d", &loaded[i]) == 1 && loaded[i] >= 0 && loaded[i] < counts[i];
    for (int i = 0; valid && i < 128; i++)
        valid = fscanf(f, "%d", &progress[i]) == 1 && (progress[i] == 0 || progress[i] == 1);
    fclose(f);
    if (!valid) return; // A malformed save cannot create invalid array indices.
    for (int i = 0; i < 128; i++) completed[i] = (unsigned char)progress[i];
    for (int i = 0; i < CATEGORY_COUNT; i++)
        choice[i] = CompletionCount() >= Requirement(i, loaded[i]) ? loaded[i] : 0;
}
void Customisation_RecordCompletion(int level)
{
    if (level >= 0 && level < 128 && !completed[level])
    {
        completed[level] = 1;
        Save();
    }
}
Color Customisation_Body(void) { return bodies[choice[BODY]]; }
Color Customisation_Highlight(void) { return ColorBrightness(Customisation_Body(), 0.18f); }
Color Customisation_Shadow(void) { return ColorBrightness(Customisation_Body(), -0.25f); }
Color Customisation_Glow(void) { return glows[choice[GLOW]]; }
int Customisation_Face(void) { return choice[FACE]; }
bool Customisation_Headphones(void) { return choice[HEADPHONES] == 0; }

static bool Click(Rectangle r)
{
    return CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
void Customisation_Draw(Rectangle bounds)
{
    Color panel = {20,24,36,255}, muted = {160,175,200,255};
    DrawRectangleRec(bounds,panel);
    DrawRectangleLinesEx(bounds,2,SKYBLUE);
    // The controls below were laid out for a panel at (10,50). Shift them (and
    // the mouse) so they sit inside whatever panel the window size gives us.
    float shiftX = bounds.x - 10 + (bounds.width - 400) / 2, shiftY = bounds.y - 50;
    rlPushMatrix();
    rlTranslatef(shiftX, shiftY, 0);
    SetMouseOffset((int)-shiftX, (int)-shiftY);
    DrawText("BITTY'S WORKSHOP",28,70,24,SKYBLUE);
    DrawText("Your bot. Your escape style.",28,105,18,muted);
    int total = CompletionCount();
    DrawText(TextFormat("%d levels completed  /  live preview",total),28,136,16,muted);
    for (int i = 0; i < CATEGORY_COUNT; i++)
    {
        int y = 175 + i * 62;
        DrawText(labels[i],28,y,16,muted);
        Rectangle left = {28,(float)y+22,32,28}, right = {360,(float)y+22,32,28};
        DrawRectangleRec(left,(Color){50,65,90,255});
        DrawRectangleRec(right,(Color){50,65,90,255});
        DrawText("<",39,y+27,18,WHITE);
        DrawText(">",371,y+27,18,WHITE);
        int direction = Click(left) ? -1 : Click(right) ? 1 : 0;
        if (direction)
        {
            // Reward cosmetics stay locked; arrows skip locked entries.
            int next = choice[i];
            do { next = (next + direction + counts[i]) % counts[i]; }
            while (total < Requirement(i,next));
            choice[i] = next;
            Save();
        }
        Color ink = i == BODY ? Customisation_Body() : i == GLOW ? Customisation_Glow() : WHITE;
        DrawRectangle(70,y+22,280,28,(Color){32,40,58,255});
        DrawText(options[i][choice[i]],82,y+28,18,ink);
    }
    Rectangle reset = {28,622,170,32};
    DrawRectangleRec(reset,(Color){65,55,95,255});
    DrawText("RESET LOOK",45,630,16,WHITE);
    if (Click(reset)) { memset(choice,0,sizeof choice); Save(); }
    DrawText(saveOk ? "Auto-saved" : "Save failed: folder read-only",210,632,saveOk ? 16 : 10,saveOk ? GREEN : ORANGE);
    DrawText("Rewards: 5 levels = Loop Master",28,666,14,muted);
    DrawText("10 = Circuit glow   /   20 = Crown",28,686,14,muted);
    SetMouseOffset(0, 0);
    rlPopMatrix();
}

// Called within Bitty's local transform: every accessory follows his animation.
static void Rod(Vector3 a, Vector3 b, float radius, Color colour)
{
    DrawCylinderEx(a,b,radius,radius,8,colour);
}
void Customisation_DrawAccessories(float time)
{
    Color glow = Customisation_Glow(), dark = {35,39,58,255};
    Color metal = {180,195,215,255};
    float wob = sinf(time*4)*0.025f;
    // Hats occupy the centre; antenna sits at the back-right to avoid clipping.
    Vector3 base = {0.24f,0.49f,-0.15f};
    Vector3 tip = {0.24f+wob,0.76f,-0.15f};
    int antenna = choice[ANTENNA];
    if (antenna == 0)
    {
        Rod(base,tip,0.012f,metal);
        DrawSphereEx(tip,0.045f,8,10,glow);
    }
    else if (antenna == 2)
    {
        Vector3 a = {0.18f,0.62f,-0.15f}, b = {0.29f,0.64f,-0.15f};
        Rod(base,a,0.018f,glow); Rod(a,b,0.018f,glow); Rod(b,tip,0.018f,glow);
    }
    else if (antenna == 3)
    {
        Rod(base,tip,0.012f,metal);
        DrawCylinderEx(tip,(Vector3){tip.x,tip.y+0.03f,tip.z},0.09f,0.025f,16,metal);
        Rod(tip,(Vector3){tip.x,tip.y+0.13f,tip.z},0.009f,glow);
    }
    else if (antenna == 4)
    {
        Rod(base,tip,0.012f,metal);
        for (int j=0;j<20;j++)
        {
            float a=j*2*PI/20, b=(j+1)*2*PI/20;
            Rod((Vector3){tip.x+cosf(a)*0.065f,tip.y+sinf(a)*0.065f,tip.z},
                (Vector3){tip.x+cosf(b)*0.065f,tip.y+sinf(b)*0.065f,tip.z},0.012f,glow);
        }
    }
    int hat = choice[HAT];
    if (hat == 1)
    {
        DrawSphereEx((Vector3){0,0.54f,0},0.23f,12,16,glow);
        DrawCube((Vector3){0,0.52f,0.18f},0.43f,0.035f,0.25f,glow);
    }
    else if (hat == 2)
    {
        DrawCylinder((Vector3){0,0.52f,0},0.29f,0.29f,0.035f,20,dark);
        DrawCylinder((Vector3){0,0.66f,0},0.18f,0.18f,0.27f,20,dark);
        DrawCylinder((Vector3){0,0.56f,0},0.185f,0.185f,0.05f,20,glow);
    }
    else if (hat == 3)
    {
        DrawCube((Vector3){0,0.55f,0},0.30f,0.10f,0.30f,dark);
        DrawCube((Vector3){0,0.62f,0},0.52f,0.035f,0.52f,dark);
        Rod((Vector3){0,0.65f,0},(Vector3){0.25f,0.65f,0.15f},0.008f,GOLD);
        Rod((Vector3){0.25f,0.65f,0.15f},(Vector3){0.25f,0.48f,0.15f},0.012f,GOLD);
    }
    else if (hat == 4)
    {
        DrawCylinder((Vector3){0,0.55f,0},0.23f,0.23f,0.09f,20,GOLD);
        for(int j=0;j<5;j++)
        {
            float a=j*2*PI/5;
            Vector3 v={cosf(a)*0.20f,0.67f,sinf(a)*0.20f};
            DrawCylinder(v,0,0.06f,0.17f,6,GOLD);
            DrawSphereEx((Vector3){v.x,0.76f,v.z},0.02f,6,6,glow);
        }
    }
    int glasses=choice[GLASSES];
    if (glasses)
    {
        float z=0.313f;
        for(int side=-1;side<=1;side+=2)
        {
            float x=side*0.09f;
            if(glasses==1)
            {
                for(int j=0;j<20;j++)
                {
                    float a=j*2*PI/20,b=(j+1)*2*PI/20;
                    Rod((Vector3){x+cosf(a)*0.065f,0.36f+sinf(a)*0.065f,z},
                        (Vector3){x+cosf(b)*0.065f,0.36f+sinf(b)*0.065f,z},0.008f,metal);
                }
            }
            else
            {
                if(glasses==3) DrawCube((Vector3){x,0.36f,z},0.135f,0.095f,0.015f,dark);
                Color frame=glasses==3?glow:metal;
                Rod((Vector3){x-0.07f,0.41f,z},(Vector3){x+0.07f,0.41f,z},0.008f,frame);
                Rod((Vector3){x-0.07f,0.31f,z},(Vector3){x+0.07f,0.31f,z},0.008f,frame);
                Rod((Vector3){x-0.07f,0.31f,z},(Vector3){x-0.07f,0.41f,z},0.008f,frame);
                Rod((Vector3){x+0.07f,0.31f,z},(Vector3){x+0.07f,0.41f,z},0.008f,frame);
            }
            Rod((Vector3){side*0.16f,0.38f,z},(Vector3){side*0.29f,0.38f,0},0.009f,dark);
        }
        Rod((Vector3){-0.025f,0.37f,z},(Vector3){0.025f,0.37f,z},0.008f,metal);
    }
    if(choice[HEADPHONES]==2)
    {
        for(int side=-1;side<=1;side+=2)
        {
            DrawCube((Vector3){side*0.36f,0.37f,0},0.12f,0.23f,0.19f,dark);
            DrawCube((Vector3){side*0.425f,0.37f,0},0.015f,0.14f,0.10f,glow);
            Rod((Vector3){side*0.34f,0.43f,-0.12f},(Vector3){side*0.25f,0.57f,-0.20f},0.025f,dark);
        }
        Rod((Vector3){-0.25f,0.57f,-0.20f},(Vector3){0.25f,0.57f,-0.20f},0.025f,dark);
    }
}
