#include "visual_learning.h"
#include "world_visuals.h"
#include "circuit.h"
#include "customisation.h"
#include "talk.h"
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
static bool inspectFailure;
static int failureIndex;
static LevelTest failedInput;
static char failureCall[128], failureExpected[160], failureActual[200];
static RenderTexture2D canvas;
static int canvasW,canvasH;
// Each frame is an observed source line, with real memory before and after it.
typedef struct {
    TestDisplay before, after;
    char code[128], title[64];
    int line, sourceRow, sourceIndex, destRow, destIndex, value;
    bool token, fromTemp, toTemp;
    char registerName[16];
    bool character;
    char explain[240];      // step mode: what this line does, in plain English
    char written[64];       // variables this line wrote to (even with the same value)
} SwapFrame;
static SwapFrame swapFrames[16];
static int swapCount;

// STEP mode uses this same picture as RUN, one line per press. Every line
// that has run is kept, so BACK can show it again.
#define HISTORY_MAX 160
static bool stepMode;
static SwapFrame history[HISTORY_MAX];
static int historyCount, viewIndex=-1;   // viewIndex: the line on screen
static bool replaying;                   // swapFrames[0] is a replay of history[viewIndex]
static bool nextRequested;
static VmVisualTrace prevTrace;          // the trace before the line being recorded
static float swapTime;
static bool swapMode, replayRequested, walkthroughMode;
static int lastReadRow, lastReadIndex=-1, lastReadValue;
static char functionName[64];
static TestWire previousSwapWires[TEST_MAX_WIRES];
static int previousSwapWireCount;

static int DataSlot(const TestDisplay *td,int address,int *row)
{
    for(int r=0;r<td->rowCount;r++)
        if(address>=td->rows[r].address&&address<td->rows[r].address+td->rows[r].count)
        { *row=r;return address-td->rows[r].address; }
    return -1;
}
void VisualLearning_SetFunction(const char *name)
{
    swapMode=strstr(name,"reverse")!=NULL||strstr(name,"swap")!=NULL;
    walkthroughMode=true;
    snprintf(functionName,sizeof functionName,"%s",name);
}
int VisualLearning_CurrentLine(void)
{
    if(swapCount)return swapFrames[0].line;
    if(stepMode&&viewIndex>=0&&viewIndex<historyCount)return history[viewIndex].line;
    return 0;
}
void VisualLearning_SetStepping(bool on){stepMode=on;}
bool VisualLearning_TakeNext(void){bool n=nextRequested;nextRequested=false;return n;}
void VisualLearning_ExplainLast(const char *text)
{
    if(swapCount)snprintf(swapFrames[swapCount-1].explain,sizeof swapFrames[0].explain,"%s",text);
}
// The frame on screen has finished animating: mark the cells, and in step mode keep it
static void FinishFrame(void)
{
    if(!swapCount)return;
    SwapFrame *f=&swapFrames[0];
    if(f->sourceIndex>=0)visited[f->sourceRow][f->sourceIndex]=1;
    if(f->destIndex>=0)visited[f->destRow][f->destIndex]=2;
    if(stepMode&&!replaying)
    {
        if(historyCount==HISTORY_MAX){memmove(history,history+1,(HISTORY_MAX-1)*sizeof history[0]);historyCount--;}
        history[historyCount++]=*f;
        viewIndex=historyCount-1;
    }
    replaying=false;
    memmove(swapFrames,swapFrames+1,(--swapCount)*sizeof swapFrames[0]);swapTime=0;
}
static void Replay(int index)
{
    viewIndex=index;
    swapFrames[0]=history[index];swapCount=1;swapTime=0;replaying=true;
}
// NEXT: returns true if it was handled here (moving forward through lines already
// seen); false means the game should run the next real line.
bool VisualLearning_StepForward(void)
{
    bool wasReplay=replaying;
    FinishFrame();
    if(viewIndex>=0&&viewIndex<historyCount-1){Replay(viewIndex+1);return true;}
    (void)wasReplay;
    return false;
}
void VisualLearning_StepBack(void)
{
    FinishFrame();
    if(viewIndex>0)Replay(viewIndex-1);
}
bool VisualLearning_TakeReplay(void)
{
    bool requested=replayRequested;replayRequested=false;return requested;
}
void VisualLearning_ObserveLine(const TestDisplay *td,const VmAccess *log,int logCount,int line,const char *code)
{
    if(!walkthroughMode||td->fastForward||line<=0||(logCount<=0&&!stepMode)||swapCount>=16)return;
    SwapFrame *frame=&swapFrames[swapCount++];
    memset(frame,0,sizeof *frame);
    frame->before=*td;frame->after=*td;frame->line=line;
    frame->before.trace=prevTrace;prevTrace=td->trace;
    for(int k=0;k<frame->before.wireCount;k++)
        for(int j=0;j<previousSwapWireCount;j++)
            if(!strcmp(frame->before.wires[k].name,previousSwapWires[j].name))frame->before.wires[k]=previousSwapWires[j];
    memcpy(previousSwapWires,td->wires,sizeof previousSwapWires);
    previousSwapWireCount=td->wireCount;
    frame->sourceIndex=frame->destIndex=-1;
    snprintf(frame->code,sizeof frame->code,"%s",code);
    // Undo this line's writes to reconstruct its actual pre-execution memory.
    for(int i=logCount-1;i>=0;i--)
    {
        if(!log[i].write||log[i].condition)continue;
        int row=0,index=DataSlot(td,log[i].address,&row);
        if(index>=0)frame->before.rows[row].values[index]=log[i].before;
        for(int r=0;r<frame->before.regCount;r++)
            if(frame->before.regs[r].address==log[i].address){frame->before.regs[r].value=log[i].before;frame->before.regs[r].known=log[i].initializedBefore;}
    }
    for(int i=0;i<logCount;i++)
    {
        if(log[i].condition)continue;
        int row=0,index=DataSlot(td,log[i].address,&row);
        if(index>=0)
        {
            if(log[i].write){frame->destRow=row;frame->destIndex=index;frame->value=log[i].value;}
            else {frame->sourceRow=row;frame->sourceIndex=index;frame->value=log[i].value;lastReadRow=row;lastReadIndex=index;lastReadValue=log[i].value;}
            frame->character=td->rows[row].isChar;
        }
        for(int r=0;r<td->regCount;r++)
            if(log[i].write&&td->regs[r].address==log[i].address&&strlen(frame->written)+strlen(td->regs[r].name)<60)
            { strcat(frame->written,td->regs[r].name); strcat(frame->written,","); }
        for(int r=0;r<td->regCount;r++)
            if(td->regs[r].address==log[i].address&&(!strcmp(td->regs[r].name,"temp")||!strcmp(td->regs[r].name,"max")||!strcmp(td->regs[r].name,"total")||!strcmp(td->regs[r].name,"count")))
            {
                if(log[i].write){frame->toTemp=true;frame->value=log[i].value;snprintf(frame->registerName,sizeof frame->registerName,"%s",td->regs[r].name);
                    if(!swapMode&&lastReadIndex>=0){frame->sourceRow=lastReadRow;frame->sourceIndex=lastReadIndex;frame->value=lastReadValue;frame->character=td->rows[lastReadRow].isChar;}
                }
                else if(frame->destIndex<0)frame->fromTemp=true;
            }
    }
    if(!swapMode&&frame->toTemp)frame->fromTemp=false;
    frame->token=(frame->destIndex>=0||frame->toTemp)&&(frame->sourceIndex>=0||frame->fromTemp);
    const char *title=frame->toTemp?"SAVE VALUE IN TEMP":frame->fromTemp&&frame->destIndex>=0?"MOVE TEMP INTO THE OTHER CELL":frame->destIndex>=0?"COPY VALUE ACROSS":strstr(code,"++")||strstr(code,"--")?"MOVE THE POINTERS INWARD":strstr(code,"while")?"CHECK WHETHER POINTERS HAVE MET":"POSITION THE POINTERS";
    if(!swapMode)
        title=frame->toTemp?"UPDATE THE TRACKED VARIABLE":frame->destIndex>=0?"WRITE TO DESTINATION":strstr(code,"return")?"RETURN THE ANSWER":strstr(code,"if")?"CHECK THIS CELL":strstr(code,"while")||strstr(code,"for")?"CHECK THE LOOP CONDITION":"EXECUTE THIS LINE";
    snprintf(frame->title,sizeof frame->title,"%s",title);
    if(swapCount==1)swapTime=0;
}

static const Color cyan={90,220,255,255}, gold={255,205,90,255};

static Vector3 Cell(int index,int count)
{
    int columns=count<6?count:6;
    if(columns<1)columns=1;
    return (Vector3){(index%columns-(columns-1)*0.5f)*1.1f,0,(index/columns)*1.25f};
}
// A comparison reads as a question with a YES/NO answer ("9 > 3?  YES"),
// because "= 1" means nothing to a beginner. Maths keeps its "= result".
static const char *OperationText(const VmVisualTrace *t)
{
    static const char *questions[]={"<",">","<=",">=","==","!=","&&","||"};
    for(int i=0;i<(int)(sizeof questions/sizeof questions[0]);i++)
        if(!strcmp(t->operation,questions[i]))
            return TextFormat("LAST CHECK   %d %s %d ?   %s",t->left,t->operation,t->right,t->result?"YES (true)":"NO (false)");
    return TextFormat("LAST OPERATION   %d %s %d = %d",t->left,t->operation,t->right,t->result);
}

void VisualLearning_Reset(void)
{
    swapCount=0;swapTime=0;swapMode=false;walkthroughMode=false;replayRequested=false;
    historyCount=0;viewIndex=-1;replaying=false;nextRequested=false;memset(&prevTrace,0,sizeof prevTrace);previousSwapWireCount=0;lastReadIndex=-1;inspectFailure=false;failureCall[0]=0;
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
void VisualLearning_ShowFailure(int index,const LevelTest *test,const TestOutcome *outcome)
{
    failureIndex=index;failedInput=*test;inspectFailure=false;
    Repair_Describe(test,failureCall,sizeof failureCall,failureExpected,sizeof failureExpected);
    snprintf(failureActual,sizeof failureActual,"%s",outcome->summary);
}
void VisualLearning_Update(float dt,float speed)
{
    timeNow+=dt;
    entrance+=dt;
    if(paused&&!stepMode)return;
    if(swapCount)
    {
        swapTime+=dt*speed;
        if(swapTime>=(swapFrames[0].before.rowCount>0?1.8f:0.85f))FinishFrame();
    }
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
bool VisualLearning_Busy(void){return (paused&&!stepMode)||queued>0||swapCount>0;}
bool VisualLearning_HasData(const LevelTest *test)
{
    if(!test)return false;
    for(int i=0;i<test->argc;i++)if(test->args[i].kind!=VALUE_INT)return true;
    return false;
}
void VisualLearning_Access(const TestDisplay *td,int row,int index,bool write)
{
    if(walkthroughMode)return;
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
// Word-wrapped text, up to maxLines lines; returns how many it used
static int Wrapped(Font font,const char *s,float x,float y,float size,Color color,float width,int maxLines)
{
    int lines=0;
    const char *p=s;
    while(*p&&lines<maxLines)
    {
        int len=(int)strlen(p),fit=len;
        char buf[256];
        while(fit>0)
        {
            snprintf(buf,sizeof buf,"%.*s",fit,p);
            if(MeasureTextEx(font,buf,size,0).x<=width)break;
            int cut=fit-1;
            while(cut>0&&p[cut]!=' ')cut--;
            fit=cut>0?cut:fit-1;
        }
        if(fit<=0)fit=1;
        if(lines==maxLines-1&&fit<len)
        {   // last line: shrink to keep the rest rather than lose it
            snprintf(buf,sizeof buf,"%.*s",len>250?250:len,p);
            float sz=size;
            while(sz>10&&MeasureTextEx(font,buf,sz,0).x>width)sz--;
            DrawTextEx(font,buf,(Vector2){x,y+lines*(size+4)},sz,0,color);
            return lines+1;
        }
        snprintf(buf,sizeof buf,"%.*s",fit,p);
        DrawTextEx(font,buf,(Vector2){x,y+lines*(size+4)},size,0,color);
        lines++;
        p+=fit;
        while(*p==' ')p++;
    }
    return lines;
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
    // Exam revision stages
    if(strstr(f,"Triangle"))return "PATTERN PRINTER";
    if(!strcmp(f,"isAPalindrome"))return "PALINDROME CHECK";
    if(strstr(f,"2ndLargest"))return "RUNNER-UP SEARCH";
    if(!strcmp(f,"frequencyCount"))return "FREQUENCY COUNTER";
    if(!strcmp(f,"countDuplicates"))return "DUPLICATE HUNTER";
    if(!strcmp(f,"wordCounter"))return "WORD COUNTER";
    if(strstr(f,"FindAllVariables"))return "CODE SCANNER";
    if(strstr(f,"vowels"))return "VOWEL SCANNER";
    if(!strcmp(f,"rotateLeft"))return "CONVEYOR ROTATION";
    if(!strcmp(f,"insertElement"))return "MAKE ROOM: SHIFT RIGHT";
    if(!strcmp(f,"deleteElement"))return "CLOSE THE GAP: SHIFT LEFT";
    if(!strcmp(f,"twoMovies"))return "PAIR FINDER";
    if(!strcmp(f,"countInGrid")||!strcmp(f,"PrintArray"))return "VIDEO MEMORY GRID";
    if(!strcmp(f,"ReplaceWithMinusOne"))return "CLUSTER SCANNER";
    if(!strcmp(f,"FallDownAndReplace"))return "GRAVITY DROP";
    if(!strncmp(f,"test",4))return "HEAP NODES / LINKED LIST";
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
    const LevelTest *test=inspectFailure?&failedInput:&level->tests[testIndex];
    TestDisplay sample;
    bool actual=!inspectFailure&&(live->active||live->rowCount>0||live->regCount>0);
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
    // The line on screen: the one animating, or (step mode) the one the player stopped on
    const SwapFrame *cur=!walkthroughMode||!actual?NULL:swapCount?&swapFrames[0]:
                         (stepMode&&viewIndex>=0&&viewIndex<historyCount)?&history[viewIndex]:NULL;
    float ft=swapCount?swapTime:99.0f;
    if(cur)td=ft<1.55f?&cur->before:&cur->after;
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
    if(cur)
    {
        const SwapFrame *frame=cur;
        if(frame->sourceIndex>=0){row=frame->sourceRow;index=frame->sourceIndex;}
        if(ft>0.9f&&frame->destIndex>=0){row=frame->destRow;index=frame->destIndex;}
    }
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
    for(int k=0;k<td->wireCount&&wires<12;k++)
    {
        const TestWire *wire=&td->wires[k];int r=wire->row,at=wire->index;
        if(swapMode&&!strcmp(wire->name,"arr"))continue;
        if(r<0||r>=td->rowCount||at<0||at>td->rows[r].count)continue;
        const TestDataRow *d=&td->rows[r];
        wireTo[wires]=positions[offsets[r]+(at<d->count?at:d->count-1)];
        if(at==d->count)wireTo[wires].x+=1.1f;
        if(cur)
            for(int j=0;j<cur->after.wireCount;j++)
            {
                const TestWire *next=&cur->after.wires[j];
                if(strcmp(next->name,wire->name)||next->row!=r||next->index<0||next->index>=d->count)continue;
                Vector3 end=positions[offsets[r]+next->index];
                float t=fminf(1,ft/1.5f);
                wireTo[wires].x+=(end.x-wireTo[wires].x)*t;
                wireTo[wires].z+=(end.z-wireTo[wires].z)*t;
            }
        wireTo[wires].y=0.3f;
        wireFrom[wires]=wireTo[wires];wireFrom[wires].y=1.35f+(k%2)*0.25f;wireFrom[wires].z-=0.5f;
        snprintf(wireNames[wires],16,"%.6s[%u]%s",wire->name,(unsigned)at%100u,at==d->count?" END":"");wires++;
    }
    int theme=dataScene?6:5;
    for(int i=0;i<Levels_Count();i++)if(Levels_Get(i)==level){theme=Circuit_WorldOf(i);break;}
    DataScene scene={0};scene.target=canvas;scene.camera=camera;scene.font=font;
    scene.count=count;scene.values=values;scene.kinds=kinds;scene.labels=labels;
    scene.visited=seen;scene.cells=positions;scene.bitty=shown;scene.theme=theme;
    scene.current=target;scene.best=best;scene.moving=queued>0;
    scene.happy=!checkFailed&&won;scene.time=timeNow;scene.victory=won;scene.victoryTime=victoryTime;
    scene.wireCount=wires;scene.wireFrom=wireFrom;scene.wireTo=wireTo;scene.wireNames=wireNames;
    if(!swapMode&&queued&&queue[0].write)
    {
        scene.transfer=true;scene.transferValue=queue[0].value;scene.transferAt=aim;
        scene.transferAt.y=0.35f+1.2f*fmaxf(0,travel/0.65f);
    }
    World_DrawDataScene(&scene);
    DrawTextureRec(canvas.texture,(Rectangle){0,0,(float)width,(float)-height},(Vector2){panel.x,panel.y},WHITE);
    DrawRectangleLinesEx(panel,2,cyan);
    Text(font,SceneTitle(test->func),panel.x+12,panel.y+10,19,cyan,panel.width-24);
    bool stepping=stepMode&&running&&actual;
    Text(font,stepping?"STEP MODE  -  NEXT runs one line. BACK shows the line before.":actual?"WATCH MODE  -  RUN plays your code. STEP goes one line at a time.":"WRITE YOUR CODE  -  RUN to watch it, STEP to go line by line",panel.x+12,panel.y+36,13,stepping?gold:LIGHTGRAY,panel.width-24);
    if(dataScene&&running&&!stepping&&Button((Rectangle){panel.x+panel.width-84,panel.y+8,72,26},paused?"PLAY":"PAUSE",font))paused=!paused;
    if(!actual&&entrance>=1&&entrance<6&&!Talk_Active())   // Bitty's intro talk uses this space
    {
        Rectangle speech={panel.x+12,panel.y+86,panel.width-24,76};
        Card(font,speech,"BITTY / YOUR MISSION",test->func,Customisation_Glow());
        Text(font,maxLevel?"Find the largest. I'll follow YOUR code.":copy?"Move the characters between these buffers.":wires?"Follow the addresses. Change the real memory.":dataScene?"Explore these cells with your code.":"Program this machine. Watch its values change.",speech.x+10,speech.y+54,13,WHITE,speech.width-20);
    }
    // A real branch from bytecode, never a guessed outcome from the task answer.
    if((!walkthroughMode||!dataScene)&&actual&&td->trace.branchKnown)
    {
        float bw=(panel.width-36)/2;
        Card(font,(Rectangle){panel.x+12,panel.y+58,bw,52},"LAST CONDITION",td->trace.branchValue?"TRUE":"FALSE",td->trace.branchValue?GREEN:ORANGE);
        Card(font,(Rectangle){panel.x+24+bw,panel.y+58,bw,52},"SOURCE LINE",TextFormat("%d",td->trace.branchLine),cyan);
    }
    if(walkthroughMode&&dataScene)
    {
        Rectangle tempBox={panel.x+panel.width-156,panel.y+119,143,64};
        const TestRegister *temp=NULL;
        const char *trackedName=swapMode?"temp":strstr(test->func,"Max")||strstr(test->func,"max")?"max":strstr(test->func,"count")||strstr(test->func,"Vowels")?"count":"total";
        for(int i=0;i<td->regCount;i++)if(!strcmp(td->regs[i].name,trackedName))temp=&td->regs[i];
        bool letters=strstr(test->func,"char")!=NULL;   // swap_chars: show 'A', not 65
        if(temp||swapMode)   // only when the code really has that variable
            Card(font,tempBox,swapMode?"TEMP / SPARE STORAGE":trackedName,temp&&temp->known?(letters?TextFormat("'%c'",temp->value):TextFormat("%d",temp->value)):"empty",gold);
        if(swapCount)
        {
            SwapFrame *frame=&swapFrames[0];
            if(!stepping)
            {
                Rectangle explain={panel.x+12,panel.y+57,panel.width-24,57};
                DrawRectangleRounded(explain,0.1f,4,(Color){9,24,38,245});
                Text(font,frame->title,explain.x+9,explain.y+7,14,gold,explain.width-18);
                Text(font,TextFormat("LINE %d: %s",frame->line,frame->code),explain.x+9,explain.y+31,15,WHITE,explain.width-18);
            }
            if(frame->token)
            {
                Vector2 start={tempBox.x+tempBox.width/2,tempBox.y+tempBox.height/2};
                Vector2 end=start;
                if(!frame->fromTemp&&frame->sourceIndex>=0)
                {
                    Vector3 p=positions[offsets[frame->sourceRow]+frame->sourceIndex];p.y=0.5f;
                    start=GetWorldToScreenEx(p,camera,width,height);start.x+=panel.x;start.y+=panel.y;
                }
                if(!frame->toTemp&&frame->destIndex>=0)
                {
                    Vector3 p=positions[offsets[frame->destRow]+frame->destIndex];p.y=0.5f;
                    end=GetWorldToScreenEx(p,camera,width,height);end.x+=panel.x;end.y+=panel.y;
                }
                float progress=fminf(1,fmaxf(0,(swapTime-0.35f)/1.1f));
                Vector2 previous=start;
                for(int step=1;step<=24;step++)
                {
                    float t=step/24.0f;
                    Vector2 p={start.x+(end.x-start.x)*t,start.y+(end.y-start.y)*t-sinf(t*PI)*48};
                    DrawLineEx(previous,p,2,Fade(gold,0.5f));previous=p;
                }
                Vector2 p={start.x+(end.x-start.x)*progress,start.y+(end.y-start.y)*progress-sinf(progress*PI)*48};
                DrawCircleV(p,20,(Color){120,80,22,255});DrawCircleLines((int)p.x,(int)p.y,20,gold);
                Text(font,frame->character?TextFormat("'%c'",frame->value):TextFormat("%d",frame->value),p.x-14,p.y-10,20,WHITE,40);
            }
        }
        else if(won&&!stepping)
            Text(font,swapMode?"Pointers met. Every swap is complete.":"Every check passed. Nice work!",panel.x+12,panel.y+74,15,GREEN,panel.width-24);
    }

    if(won&&Button((Rectangle){panel.x+panel.width-140,panel.y+8,128,29},"REPLAY CODE",font))replayRequested=true;
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
    if(checkFailed&&failureCall[0])
    {
        Rectangle error={panel.x+12,panel.y+55,panel.width-24,119};
        DrawRectangleRounded(error,0.1f,5,(Color){56,22,28,250});
        Text(font,TextFormat("CHECK %d FAILED / %s",failureIndex+1,inspectFailure?"FAILING INPUT PREVIEW":"DIFFERENT INPUT"),error.x+10,error.y+8,14,ORANGE,error.width-20);
        Text(font,failureCall,error.x+10,error.y+33,14,WHITE,error.width-20);
        Text(font,TextFormat("EXPECTED: %s",failureExpected),error.x+10,error.y+57,14,GREEN,error.width-20);
        Text(font,TextFormat("ACTUAL: %s",failureActual),error.x+10,error.y+79,14,ORANGE,error.width-20);
        if(Button((Rectangle){error.x+error.width-157,error.y+91,147,24},inspectFailure?"MAIN ARRAY":"FAILING INPUT",font))inspectFailure=!inspectFailure;
    }
    float bottomH=stepping?204:142;
    float bottom=panel.y+panel.height-bottomH;
    DrawRectangle((int)panel.x+3,(int)bottom-4,(int)panel.width-6,(int)bottomH+1,(Color){8,18,30,245});
    float cardW=(panel.width-36)/2;
    const char *focusName=dataScene?"CURRENT CELL":"FUNCTION",*focusValue=dataScene?TextFormat("%s = %d",labels[target],values[target]):test->func;
    if(dataScene&&strstr(test->func,"char")&&values[target]>=32&&values[target]<127)focusValue=TextFormat("%s = '%c'",labels[target],values[target]);
    if(maxLevel&&best>=0){focusName="LARGEST VISITED";focusValue=TextFormat("%d at %s",values[best],labels[best]);}
    Card(font,(Rectangle){panel.x+12,bottom,cardW,59},focusName,focusValue,maxLevel?gold:cyan);
    const TestRegister *tracked=NULL;
    const char *preferred=maxLevel?"max":!strcmp(test->func,"addPoints")?"score":strstr(test->func,"count")||strstr(test->func,"Vowels")?"count":strstr(test->func,"reverse")||strstr(test->func,"swap")?"temp":"total";
    for(int i=0;i<td->regCount;i++)if(!strcmp(td->regs[i].name,preferred))tracked=&td->regs[i];
    if(!tracked&&td->regCount)tracked=&td->regs[td->regCount-1];
    Card(font,(Rectangle){panel.x+24+cardW,bottom,cardW,59},tracked?tracked->name:"YOUR VARIABLE",tracked&&tracked->known?TextFormat("%d",tracked->value):"not set",tracked&&tracked->flash>0?gold:cyan);
    if(stepping)
    {
        // WHAT'S HAPPENING: the line, what it does in plain English, and what changed
        Rectangle box={panel.x+12,bottom+65,panel.width-24,96};
        DrawRectangleRounded(box,0.1f,4,(Color){44,38,16,250});
        DrawRectangleRoundedLinesEx(box,0.1f,4,1,Fade(gold,0.8f));
        if(cur)
        {
            Text(font,TextFormat("LINE %d   %s",cur->line,cur->title),box.x+10,box.y+7,13,gold,box.width-20);
            const char *what=cur->explain[0]?cur->explain:cur->code;
            Wrapped(font,what,box.x+10,box.y+27,14,WHITE,box.width-20,2);
            // what changed: a check's answer, a value written, a variable updated
            char changed[200]="";
            const TestDisplay *b=&cur->before,*a=&cur->after;
            const VmVisualTrace *ta=&a->trace,*tb=&b->trace;
            if(ta->operationKnown&&(!tb->operationKnown||ta->left!=tb->left||ta->right!=tb->right||ta->result!=tb->result||strcmp(ta->operation,tb->operation)))
                snprintf(changed,sizeof changed,"%s",OperationText(&a->trace));
            for(int r=0;r<a->regCount&&strlen(changed)<120;r++)
            {
                const TestRegister *ra=&a->regs[r],*rb=NULL;
                for(int k=0;k<b->regCount;k++)if(!strcmp(b->regs[k].name,ra->name))rb=&b->regs[k];
                if(!ra->known)continue;
                char tag[24];snprintf(tag,sizeof tag,"%s,",ra->name);
                bool wrote=strstr(cur->written,tag)!=NULL;
                if(rb&&rb->known&&rb->value==ra->value)
                {
                    if(wrote){size_t l=strlen(changed);snprintf(changed+l,sizeof changed-l,"%s%s = %d (same as before)",l?"    ":"",ra->name,ra->value);}
                    continue;
                }
                size_t len=strlen(changed);
                if(rb&&rb->known)snprintf(changed+len,sizeof changed-len,"%s%s: %d -> %d",len?"    ":"",ra->name,rb->value,ra->value);
                else snprintf(changed+len,sizeof changed-len,"%s%s = %d",len?"    ":"",ra->name,ra->value);
            }
            if(cur->destIndex>=0&&cur->destRow<a->rowCount)
            {
                size_t len=strlen(changed);
                int was=b->rows[cur->destRow].values[cur->destIndex],now=a->rows[cur->destRow].values[cur->destIndex];
                bool ch=a->rows[cur->destRow].isChar&&now>=32&&now<127&&was>=32&&was<127;
                const char *name=copy?(cur->destRow==0?"dest":"src"):a->rowCount>1?(cur->destRow==0?"a":"b"):"";
                if(ch)snprintf(changed+len,sizeof changed-len,"%s%s[%d]: '%c' -> '%c'",len?"    ":"",name,cur->destIndex,was,now);
                else snprintf(changed+len,sizeof changed-len,"%s%s[%d]: %d -> %d",len?"    ":"",name,cur->destIndex,was,now);
            }
            if(!changed[0])snprintf(changed,sizeof changed,"No boxes in the picture changed on this line.");
            Text(font,changed,box.x+10,box.y+71,14,(Color){255,232,160,255},box.width-20);
        }
        else
        {
            Text(font,"READY",box.x+10,box.y+7,13,gold,box.width-20);
            Wrapped(font,"The test data is in memory and your function is about to start. Press NEXT to run the first line.",box.x+10,box.y+27,14,WHITE,box.width-20,3);
        }
        // BACK / NEXT and where you are
        float by=bottom+168;
        bool canBack=viewIndex>0||(swapCount&&replaying&&viewIndex>0);
        Rectangle back={panel.x+12,by,92,28},next={panel.x+112,by,92,28};
        DrawRectangleRounded(back,0.2f,5,canBack?(Color){40,60,80,255}:(Color){24,32,42,255});
        Text(font,"< BACK",back.x+16,back.y+6,15,canBack?WHITE:GRAY,back.width-20);
        if(canBack&&CheckCollisionPointRec(GetMousePosition(),back)&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT))VisualLearning_StepBack();
        bool hoverNext=CheckCollisionPointRec(GetMousePosition(),next);
        DrawRectangleRounded(next,0.2f,5,hoverNext?(Color){70,130,190,255}:(Color){50,110,170,255});
        Text(font,"NEXT >",next.x+16,next.y+6,15,WHITE,next.width-20);
        if(hoverNext&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT))nextRequested=true;
        const char *where=viewIndex<0?"Step 0":viewIndex<historyCount-1?TextFormat("Step %d of %d (back)",viewIndex+1,historyCount):TextFormat("Step %d",viewIndex+1);
        float dx=panel.x+panel.width-18-level->testCount*16;
        Text(font,where,panel.x+216,by+7,14,viewIndex<historyCount-1?gold:LIGHTGRAY,dx-panel.x-216-14);
        for(int i=0;i<level->testCount;i++)
        {
            Color c=checks[i]>0?GREEN:checks[i]<0?ORANGE:(Color){65,85,105,255};
            DrawCircle((int)(dx+i*16),(int)(by+14),5,c);
        }
        return;
    }
    Rectangle operation={panel.x+12,bottom+65,panel.width-24,42};
    DrawRectangleRounded(operation,0.12f,4,(Color){20,37,54,255});
    const VmVisualTrace *trace=&td->trace;
    Text(font,actual&&trace->operationKnown?OperationText(trace):"WRITE YOUR SOLUTION / RUN TO SEE IT EXECUTE",operation.x+10,operation.y+13,15,WHITE,operation.width-20);
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
