#include "visual_learning.h"
#include "world_visuals.h"
#include "circuit.h"
#include "customisation.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { int row,index; bool write; int value, before; } Visit;
static Visit queue[64];
static int queued, currentRow, currentIndex, visited[TEST_MAX_DATA_ROWS][TEST_ROW_MAX];
static float travel, timeNow;
static Vector3 shown;
static bool hasPosition;
static bool paused;
static float entrance=6;
static int checks[LEVEL_MAX_TESTS];
static char checkDetail[240];
static bool checkFailed;
static RenderTexture2D canvas;
static int canvasW,canvasH;
static const Color cyan={90,220,255,255}, gold={255,205,90,255};

static Vector3 Cell(int index,int count)
{
    int columns=count<6?count:6;
    if(columns<1)columns=1;
    return (Vector3){(index%columns-(columns-1)*0.5f)*1.1f,0,(index/columns)*1.25f};
}
void VisualLearning_Reset(void)
{
    queued=0; currentRow=0; currentIndex=0; travel=0;hasPosition=false;paused=false;
    memset(visited,0,sizeof visited);
    memset(checks,0,sizeof checks);checkDetail[0]=0;checkFailed=false;entrance=6;
}
void VisualLearning_Enter(void)
{
    VisualLearning_Reset();
    entrance=0;
}
void VisualLearning_Check(int index,bool passed,const char *detail)
{
    if(index>=0&&index<LEVEL_MAX_TESTS)checks[index]=passed?1:-1;
    if(!passed){checkFailed=true;snprintf(checkDetail,sizeof checkDetail,"Check %d: %.220s",index+1,detail);}
}
void VisualLearning_Update(float dt,float speed)
{
    timeNow+=dt;
    entrance+=dt;
    if(paused)return;
    if(queued)
    {
        travel-=dt*speed;
        if(travel<=0)
        {
            currentRow=queue[0].row;currentIndex=queue[0].index;
            visited[currentRow][currentIndex]=queue[0].write?2:1;
            memmove(queue,queue+1,(--queued)*sizeof queue[0]);
            travel=queued?0.65f:0;
        }
    }
}
bool VisualLearning_Busy(void){return paused||queued>0;}
bool VisualLearning_HasData(const LevelTest *test)
{
    if(!test)return false;
    for(int i=0;i<test->argc;i++)if(test->args[i].kind!=VALUE_INT)return true;
    return false;
}
void VisualLearning_Access(const TestDisplay *td,int row,int index,bool write)
{
    if(row<0||row>=td->rowCount||index<0||index>=td->rows[row].count||index>=TEST_ROW_MAX)return;
    if(queued&&queue[queued-1].row==row&&queue[queued-1].index==index&&!write)return;
    if(queued>=64)return;
    queue[queued++]=(Visit){row,index,write,td->rows[row].values[index],td->rows[row].previous[index]};
    if(queued==1)travel=0.65f;
}
static void Text(Font font,const char *s,float x,float y,float size,Color color,float width)
{
    while(size>10&&MeasureTextEx(font,s,size,0).x>width)size--;
    DrawTextEx(font,s,(Vector2){x,y},size,0,color);
}
static void Preview(const LevelTest *test,TestDisplay *out)
{
    memset(out,0,sizeof *out);
    for(int k=0;k<test->argc&&out->rowCount<TEST_MAX_DATA_ROWS;k++)
    {
        const LevelValue *arg=&test->args[k];
        if(arg->kind==VALUE_INT)continue;
        TestDataRow *d=&out->rows[out->rowCount++];
        snprintf(d->name,sizeof d->name,"arg %d",k+1);
        d->isChar=arg->kind==VALUE_STRING;
        d->count=arg->kind==VALUE_ARRAY?arg->count:d->isChar?(arg->capacity>(int)strlen(arg->text)+1?arg->capacity:(int)strlen(arg->text)+1):1;
        if(d->count>TEST_ROW_MAX)d->count=TEST_ROW_MAX;
        for(int i=0;i<d->count;i++)d->values[i]=arg->kind==VALUE_ARRAY?arg->values[i]:d->isChar?(i<=(int)strlen(arg->text)?(unsigned char)arg->text[i]:0):arg->value;
    }
}
static bool Button(Rectangle r,const char *name,Font font)
{
    bool hover=CheckCollisionPointRec(GetMousePosition(),r);
    DrawRectangleRounded(r,0.15f,5,hover?(Color){45,90,110,255}:(Color){25,55,75,255});
    Text(font,name,r.x+7,r.y+6,14,WHITE,r.width-14);
    return hover&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static const char *SceneTitle(const char *f)
{
    if(strstr(f,"Max")||strstr(f,"max"))return "MAXIMUM SEARCH";
    if(!strcmp(f,"sumArray")||!strcmp(f,"sum_array"))return "ARRAY ACCUMULATOR";
    if(!strcmp(f,"countAbove"))return "THRESHOLD SCANNER";
    if(!strcmp(f,"find"))return "SEARCH TERMINAL";
    if(strstr(f,"reverse")||strstr(f,"swap"))return "VALUE EXCHANGE";
    if(strstr(f,"copy"))return "SOURCE / DESTINATION TRANSFER";
    if(strstr(f,"concat"))return "MESSAGE ASSEMBLER";
    if(strstr(f,"Password")||strstr(f,"compare_strings"))return "CHARACTER COMPARISON";
    if(strstr(f,"Vowels"))return "VOWEL SCANNER";
    if(strstr(f,"Strlen")||strstr(f,"strlen"))return "STRING LENGTH SCANNER";
    if(!strcmp(f,"doubleValue")||!strcmp(f,"addPoints"))return "CALLER MEMORY / LOCAL COPY";
    if(!strcmp(f,"Reversed"))return "DIGIT REVERSER";
    if(strstr(f,"Binary"))return "BINARY PLACE VALUES";
    if(!strcmp(f,"sumDigits"))return "DIGIT ACCUMULATOR";
    if(!strcmp(f,"countDigits"))return "DIGIT COUNTER";
    if(!strcmp(f,"average"))return "SENSOR AVERAGER";
    if(!strcmp(f,"grade"))return "GRADE ROUTER";
    if(!strcmp(f,"daysInMonth"))return "MONTH SWITCH";
    if(!strcmp(f,"isLeapYear"))return "LEAP-YEAR LOGIC";
    if(!strcmp(f,"biggest"))return "COMPARISON JUNCTION";
    if(!strcmp(f,"isEven"))return "EVEN / ODD SORTER";
    if(!strcmp(f,"power"))return "MULTIPLICATION LOOP";
    if(!strcmp(f,"smallestDivisor"))return "DIVISOR SCANNER";
    if(!strcmp(f,"isAPrimeNumber"))return "PRIME CHECKER";
    if(!strcmp(f,"factorial"))return "RECURSIVE CALL STACK";
    if(!strcmp(f,"clamp"))return "RANGE LIMITER";
    if(!strcmp(f,"toFahrenheit"))return "TEMPERATURE CONVERTER";
    if(!strcmp(f,"toUpper"))return "CHARACTER CONVERTER";
    return "FUNCTION TERMINAL";
}
static void Card(Font font,Rectangle box,const char *label,const char *value,Color accent)
{
    DrawRectangleRounded(box,0.13f,5,(Color){12,28,43,245});
    DrawRectangleRoundedLinesEx(box,0.13f,5,1,Fade(accent,0.7f));
    Text(font,label,box.x+10,box.y+7,12,accent,box.width-20);
    Text(font,value,box.x+10,box.y+27,20,WHITE,box.width-20);
}
void VisualLearning_Draw(Rectangle panel,Font font,const Level *level,
                         const TestDisplay *live,int testIndex,bool running,bool won,float victoryTime)
{
    if(level->testCount<1)return;
    if(testIndex<0)testIndex=0;
    if(testIndex>=level->testCount)testIndex=level->testCount-1;
    const LevelTest *test=&level->tests[testIndex];
    TestDisplay sample;
    bool actual=live->active||live->rowCount>0||live->regCount>0;
    if(!actual)
    {
        Preview(test,&sample);
        for(int k=0;k<test->argc&&sample.regCount<TEST_MAX_REGISTERS;k++)
            if(test->args[k].kind==VALUE_INT)
            {
                TestRegister *r=&sample.regs[sample.regCount++];
                snprintf(r->name,sizeof r->name,"input %d",k+1);
                r->value=test->args[k].value;r->known=true;
                r->isChar=test->args[k].display==SHOW_CHAR;
            }
    }
    const TestDisplay *td=actual?live:&sample;
    int offsets[TEST_MAX_DATA_ROWS]={0};
    Vector3 positions[TEST_MAX_DATA_ROWS*TEST_ROW_MAX];
    int values[TEST_MAX_DATA_ROWS*TEST_ROW_MAX],seen[TEST_MAX_DATA_ROWS*TEST_ROW_MAX];
    unsigned char kinds[TEST_MAX_DATA_ROWS*TEST_ROW_MAX];
    char labels[TEST_MAX_DATA_ROWS*TEST_ROW_MAX][32];
    int count=0;float z=0;
    bool copy=strstr(test->func,"copy")||strstr(test->func,"concat");
    for(int r=0;r<td->rowCount;r++)
    {
        const TestDataRow *d=&td->rows[r];offsets[r]=count;
        bool ended=false;
        for(int i=0;i<d->count&&i<TEST_ROW_MAX;i++,count++)
        {
            positions[count]=Cell(i,d->count);positions[count].z+=z;
            values[count]=d->values[i];seen[count]=visited[r][i];
            kinds[count]=d->isChar?(ended&&d->values[i]==0?2:1):0;
            if(d->isChar&&d->values[i]==0)ended=true;
            const char *name=copy?(r==0?"dest":"src"):td->rowCount>1?(r==0?"a":"b"):"";
            snprintf(labels[count],32,"%s[%d]",name,i);
        }
        z+=((d->count+5)/6)*1.25f+0.75f;
    }
    bool dataScene=count>0;
    if(!dataScene)
    {
        count=td->regCount>0?td->regCount:1;
        for(int i=0;i<count;i++)
        {
            positions[i]=Cell(i,count);seen[i]=0;
            values[i]=td->regCount?td->regs[i].value:0;
            kinds[i]=td->regCount?(td->regs[i].known?(td->regs[i].isChar?1:td->regs[i].isBool?4:0):3):3;
            snprintf(labels[i],32,"%s",td->regCount?td->regs[i].name:"return");
        }
        z=1.25f;
    }
    int row=queued?queue[0].row:currentRow;
    if(row>=td->rowCount)row=0;
    int index=queued?queue[0].index:currentIndex;
    if(td->rowCount&&index>=td->rows[row].count)index=0;
    int target=dataScene?offsets[row]+index:0;
    if(queued&&queue[0].write&&travel>0.15f)values[target]=queue[0].before;
    Vector3 aim=positions[target];
    if(!actual&&entrance<1.5f)aim.x-=2.2f*(1-entrance/1.5f);
    if(!hasPosition){shown=positions[0];if(entrance<1.5f)shown.x-=2.2f;hasPosition=true;}
    float ease=1-expf(-GetFrameTime()*9);
    shown.x+=(aim.x-shown.x)*ease;shown.z+=(aim.z-shown.z)*ease;
    int width=(int)panel.width,height=(int)panel.height;
    if(width<100||height<200)return;
    if(width!=canvasW||height!=canvasH)
    {
        if(canvasW)UnloadRenderTexture(canvas);
        canvas=LoadRenderTexture(width,height);canvasW=width;canvasH=height;
    }
    Camera3D camera={0};
    camera.position=(Vector3){1.6f,5+z*0.65f,5+z*0.85f};
    camera.target=(Vector3){0,0,(z-1.25f)*0.45f};camera.up=(Vector3){0,1,0};
    camera.projection=CAMERA_PERSPECTIVE;camera.fovy=36;
    bool maxLevel=strstr(test->func,"Max")||strstr(test->func,"max");
    int best=-1;
    if(maxLevel&&actual)for(int i=0;i<count;i++)if(seen[i]&&(best<0||values[i]>values[best]))best=i;
    Vector3 wireFrom[12],wireTo[12];char wireNames[12][16];int wires=0;
    for(int r=0;r<td->rowCount;r++)for(int k=0;k<td->rows[r].markerCount&&wires<12;k++)
    {
        const TestDataRow *d=&td->rows[r];int at=d->markerIndex[k];
        if(at<0||at>d->count)continue;
        wireTo[wires]=positions[offsets[r]+(at<d->count?at:d->count-1)];
        if(at==d->count)wireTo[wires].x+=1.1f;
        wireTo[wires].y=0.23f;
        wireFrom[wires]=(Vector3){positions[offsets[r]].x-0.55f-k*0.45f,0.9f,positions[offsets[r]].z-0.75f};
        snprintf(wireNames[wires],16,"%.10s%s",d->markerName[k],at==d->count?" END":"");wires++;
    }
    int theme=dataScene?6:5;
    for(int i=0;i<Levels_Count();i++)if(Levels_Get(i)==level){theme=Circuit_WorldOf(i);break;}
    DataScene scene={0};scene.target=canvas;scene.camera=camera;scene.font=font;
    scene.count=count;scene.values=values;scene.kinds=kinds;scene.labels=labels;
    scene.visited=seen;scene.cells=positions;scene.bitty=shown;scene.theme=theme;
    scene.current=target;scene.best=best;scene.moving=queued>0;
    scene.happy=!checkFailed&&won;scene.time=timeNow;scene.victory=won;scene.victoryTime=victoryTime;
    scene.wireCount=wires;scene.wireFrom=wireFrom;scene.wireTo=wireTo;scene.wireNames=wireNames;
    if(queued&&queue[0].write)
    {
        scene.transfer=true;scene.transferValue=queue[0].value;scene.transferAt=aim;
        scene.transferAt.y=0.35f+1.2f*fmaxf(0,travel/0.65f);
    }
    World_DrawDataScene(&scene);
    DrawTextureRec(canvas.texture,(Rectangle){0,0,(float)width,(float)-height},(Vector2){panel.x,panel.y},WHITE);
    DrawRectangleLinesEx(panel,2,cyan);
    Text(font,SceneTitle(test->func),panel.x+12,panel.y+10,19,cyan,panel.width-24);
    Text(font,actual?"LIVE EXECUTION / YOUR CODE":"YOUR CHALLENGE / WRITE CODE THEN RUN",panel.x+12,panel.y+36,13,LIGHTGRAY,panel.width-24);
    if(dataScene&&running&&Button((Rectangle){panel.x+panel.width-84,panel.y+55,72,26},paused?"PLAY":"PAUSE",font))paused=!paused;
    if(!actual&&entrance>=1&&entrance<6)
    {
        Rectangle speech={panel.x+12,panel.y+86,panel.width-24,76};
        Card(font,speech,"BITTY / YOUR MISSION",test->func,Customisation_Glow());
        Text(font,maxLevel?"Find the largest. I'll follow YOUR code.":copy?"Move the characters between these buffers.":wires?"Follow the addresses. Change the real memory.":dataScene?"Explore these cells with your code.":"Program this machine. Watch its values change.",speech.x+10,speech.y+54,13,WHITE,speech.width-20);
    }
    // A real branch from bytecode, never a guessed outcome from the task answer.
    if(actual&&td->trace.branchKnown)
    {
        float bw=(panel.width-36)/2;
        Card(font,(Rectangle){panel.x+12,panel.y+58,bw,52},"LAST CONDITION",td->trace.branchValue?"TRUE":"FALSE",td->trace.branchValue?GREEN:ORANGE);
        Card(font,(Rectangle){panel.x+24+bw,panel.y+58,bw,52},"SOURCE LINE",TextFormat("%d",td->trace.branchLine),cyan);
    }
    // Stage-specific diagrams: inputs and actual returned values are distinct.
    float diagramY=panel.y+125;
    if(!strcmp(test->func,"grade"))
    {
        const char *grades[]={"A / 70+","B / 60+","C / 50+","D / 40+","F / <40"};
        const int letters[]={'A','B','C','D','F'};
        float width=(panel.width-32)/5;
        for(int i=0;i<5;i++)
        {
            bool chosen=actual&&td->trace.returnKnown&&td->trace.returnValue==letters[i];
            Rectangle box={panel.x+12+i*width,diagramY,width-4,44};
            DrawRectangleRounded(box,0.14f,4,chosen?(Color){45,115,80,245}:(Color){15,30,45,235});
            Text(font,grades[i],box.x+5,box.y+14,13,chosen?GREEN:LIGHTGRAY,box.width-10);
        }
    }
    else if(!strcmp(test->func,"daysInMonth"))
    {
        float width=(panel.width-32)/6;
        for(int i=0;i<12;i++)
        {
            Rectangle box={panel.x+12+(i%6)*width,diagramY+(i/6)*29,width-4,25};
            bool chosen=test->args[0].value==i+1;
            DrawRectangleRounded(box,0.1f,4,chosen?(Color){55,95,115,245}:(Color){15,30,45,235});
            Text(font,TextFormat("%d",i+1),box.x+8,box.y+5,14,chosen?gold:GRAY,box.width-12);
        }
    }
    else if(!strcmp(test->func,"clamp")&&test->argc>=3)
    {
        int low=test->args[1].value,high=test->args[2].value,input=test->args[0].value;
        float left=panel.x+35,right=panel.x+panel.width-35;
        DrawLineEx((Vector2){left,diagramY+25},(Vector2){right,diagramY+25},4,cyan);
        Text(font,TextFormat("LOW %d",low),left,diagramY+37,13,cyan,100);
        Text(font,TextFormat("HIGH %d",high),right-80,diagramY+37,13,cyan,80);
        double span=(double)high-low;
        double position=span?((double)input-low)/span:0.5;
        if(position<0)position=0;
        if(position>1)position=1;
        DrawCircle((int)(left+(right-left)*position),(int)(diagramY+25),8,gold);
        Text(font,TextFormat("INPUT %d",input),left,diagramY-3,13,gold,panel.width-70);
    }
    else if(!strcmp(test->func,"toUpper"))
    {
        Card(font,(Rectangle){panel.x+12,diagramY,(panel.width-36)/2,59},"INPUT CHARACTER",TextFormat("'%c' / %d",test->args[0].value,test->args[0].value),cyan);
        Card(font,(Rectangle){panel.x+24+(panel.width-36)/2,diagramY,(panel.width-36)/2,59},"ACTUAL RETURN",actual&&td->trace.returnKnown?TextFormat("'%c' / %d",td->trace.returnValue,td->trace.returnValue):"waiting",gold);
    }
    else if(!dataScene&&!strcmp(test->func,"factorial")) { /* call frames drawn below */ }
    else if(!dataScene&&strcmp(test->func,"Reversed")&&!strstr(test->func,"Digits")&&!strstr(test->func,"Binary"))
    {
        float width=(panel.width-48)/3;
        Card(font,(Rectangle){panel.x+12,diagramY,width,59},"INPUT",test->argc?TextFormat("%d",test->args[0].value):"none",cyan);
        Card(font,(Rectangle){panel.x+24+width,diagramY,width,59},"FUNCTION",test->func,cyan);
        Card(font,(Rectangle){panel.x+36+width*2,diagramY,width,59},"ACTUAL RETURN",actual&&td->trace.returnKnown?TextFormat("%d",td->trace.returnValue):"waiting",gold);
        DrawLineEx((Vector2){panel.x+12+width,diagramY+34},(Vector2){panel.x+24+width,diagramY+34},2,cyan);
        DrawLineEx((Vector2){panel.x+24+width*2,diagramY+34},(Vector2){panel.x+36+width*2,diagramY+34},2,cyan);
    }

    if(!strcmp(test->func,"factorial")&&actual)
    {
        for(int i=0;i<td->trace.frameCount;i++)
            Card(font,(Rectangle){panel.x+panel.width-180,panel.y+126+i*48,168,44},i==0?"CURRENT CALL":"WAITING CALL",TextFormat("%s(%d)",td->trace.frameName[i],td->trace.frameArg[i]),i==0?gold:cyan);
        Text(font,TextFormat("Actual call depth: %d",td->trace.depth),panel.x+12,panel.y+124,14,gold,panel.width-210);
    }
    // Numeric representations use the actual current input, not a worked solution.
    if(!dataScene&&td->regCount&&(!strcmp(test->func,"Reversed")||strstr(test->func,"Digits")||strstr(test->func,"Binary")))
    {
        int value=td->regs[0].value;char digits[32];snprintf(digits,sizeof digits,"%d",value);
        int length=(int)strlen(digits);float box=fminf(48,(panel.width-30)/fmaxf(length,1));
        for(int i=0;i<length;i++)
        {
            char digit[2]={digits[i],0};
            Card(font,(Rectangle){panel.x+12+i*box,panel.y+125,box-3,56},strstr(test->func,"Binary")?TextFormat("%d",1<<(length-i-1)):"digit",digit,cyan);
        }
    }
    float bottom=panel.y+panel.height-142;
    DrawRectangle((int)panel.x+3,(int)bottom-4,(int)panel.width-6,143,(Color){8,18,30,245});
    float cardW=(panel.width-36)/2;
    const char *focusName="CURRENT CELL",*focusValue=dataScene?TextFormat("%s = %d",labels[target],values[target]):test->func;
    if(maxLevel&&best>=0){focusName="LARGEST VISITED";focusValue=TextFormat("%d at %s",values[best],labels[best]);}
    Card(font,(Rectangle){panel.x+12,bottom,cardW,59},focusName,focusValue,maxLevel?gold:cyan);
    const TestRegister *tracked=NULL;
    const char *preferred=maxLevel?"max":!strcmp(test->func,"addPoints")?"score":strstr(test->func,"count")||strstr(test->func,"Vowels")?"count":strstr(test->func,"reverse")||strstr(test->func,"swap")?"temp":"total";
    for(int i=0;i<td->regCount;i++)if(!strcmp(td->regs[i].name,preferred))tracked=&td->regs[i];
    if(!tracked&&td->regCount)tracked=&td->regs[td->regCount-1];
    Card(font,(Rectangle){panel.x+24+cardW,bottom,cardW,59},tracked?tracked->name:"YOUR VARIABLE",tracked&&tracked->known?TextFormat("%d",tracked->value):"not set",tracked&&tracked->flash>0?gold:cyan);
    Rectangle operation={panel.x+12,bottom+65,panel.width-24,42};
    DrawRectangleRounded(operation,0.12f,4,(Color){20,37,54,255});
    const VmVisualTrace *trace=&td->trace;
    Text(font,actual&&trace->operationKnown?TextFormat("LAST OPERATION   %d %s %d = %d",trace->left,trace->operation,trace->right,trace->result):"WRITE YOUR SOLUTION / RUN TO SEE IT EXECUTE",operation.x+10,operation.y+13,15,WHITE,operation.width-20);
    float x=panel.x+15;
    for(int i=0;i<level->testCount;i++)
    {
        Color c=checks[i]>0?GREEN:checks[i]<0?ORANGE:(Color){65,85,105,255};
        DrawCircle((int)(x+i*19),(int)(bottom+124),5,c);
    }
    const char *status=checkFailed?checkDetail:won?"LEVEL COMPLETE":actual?(td->state==TEST_FAILED?td->result:td->state==TEST_PASSED?"VISIBLE CASE PASSED":"YOUR CODE IS RUNNING") :"SOLUTION CHECKS";
    Text(font,status,x+level->testCount*19+8,bottom+117,13,checkFailed?ORANGE:GREEN,panel.width-level->testCount*19-40);
}
void VisualLearning_Shutdown(void)
{
    if(canvasW)UnloadRenderTexture(canvas);
    canvasW=canvasH=0;
}
