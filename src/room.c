// room.c - the memory room.
//
// During a repair test Bitty is beamed INTO the memory of the function being
// tested. Everything the code touches becomes something he can walk up to:
//
//   arrays and strings   a row of crates along the back, one per element, with
//                        the index [0] [1] [2] ... on the floor in front of each
//   plain variables      boxes along the front. An int shows one block per digit,
//                        so you can watch digits being taken off and added on.
//   pointers             a socket with a cable plugged into the crate it points at
//
// Each line of code that runs is turned into a list of jobs for Bitty (see
// World_RoomStep): walk to the crate the line read and scan it, take a copy of a
// value and carry it to the variable it was stored in, and so on. The game
// waits until he has finished before it runs the next line, so the code really
// is what drives him.

#include "game_api.h"
#include "world_internal.h"
#include "customisation.h"
#include "raylib.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BEAM_SECONDS  1.1f    // beaming in or out
#define CELL          1.05f   // crate spacing
#define BACK_Z       -1.2f    // the first row of crates
#define ROW_STEP      1.15f   // each further row sits further back...
#define ROW_RISE      0.42f   // ...and higher, like steps
#define FRONT_Z       1.45f   // the variable boxes
#define DIGIT_W       0.34f
#define ITEM_GAP      0.35f
#define WALK_SPEED    4.2f    // crates per second
#define MAX_LINE_READS 48

// How long each job takes (seconds at 1x speed)
static float JobLength(RoomJobKind kind)
{
    switch (kind)
    {
        case JOB_SCAN:   return 0.45f;
        case JOB_PICK:   return 0.45f;
        case JOB_PUT:    return 0.45f;
        case JOB_ZAP:    return 0.42f;
        case JOB_GLANCE: return 0.3f;
        case JOB_ASK:    return 1.3f;     // long enough to read the question and the answer
    }
    return 0.3f;
}

static const Color WIRE_COLORS[TEST_MAX_WIRES] = {
    { 255, 190, 80, 255 }, { 255, 110, 190, 255 }, { 130, 255, 150, 255 }, { 190, 150, 255, 255 },
};

// ===========================================================================
// Text for values
// ===========================================================================
static const char *CharText(int v)
{
    if (v == 0) return "\\0";
    if (v >= 32 && v < 127) return TextFormat("%c", v);
    return "?";
}

static const char *ValueText(int v, bool isChar)
{
    return isChar ? CharText(v) : TextFormat("%d", v);
}

// The digits of an int, most significant first. A minus sign is stored as -1.
static int DigitsOf(int v, int *out)
{
    int n = 0, tmp[12];
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do { tmp[n++] = (int)(u % 10); u /= 10; } while (u > 0 && n < 11);
    int k = 0;
    if (v < 0) out[k++] = -1;
    while (n > 0) out[k++] = tmp[--n];
    return k;
}

// ===========================================================================
// Layout
// ===========================================================================

// Short rows (like two &values) sit side by side; long ones (arrays, strings) stack up like steps
static bool SideBySide(const TestDisplay *td)
{
    if (td->rowCount < 2) return false;
    int total = td->rowCount - 1;
    for (int r = 0; r < td->rowCount; r++) total += td->rows[r].count;
    return total <= 9;
}

static int LongestRow(const TestDisplay *td)
{
    int n = 0;
    for (int r = 0; r < td->rowCount; r++) if (td->rows[r].count > n) n = td->rows[r].count;
    return n;
}

// The centre of crate i of a row (i can be a fraction, for a sliding plug)
static Vector3 CratePos(const World *w, int row, float i)
{
    const TestDisplay *td = &w->test;
    if (SideBySide(td))
    {
        int total = td->rowCount - 1;
        for (int r = 0; r < td->rowCount; r++) total += td->rows[r].count;
        float x = -(total - 1) * CELL / 2;
        for (int r = 0; r < row; r++) x += (td->rows[r].count + 1) * CELL;
        return (Vector3){ x + i * CELL, 0.3f, BACK_Z };
    }
    // stacked rows line up by index, so dest[3] sits right behind src[3]
    float x0 = -(LongestRow(td) - 1) * CELL / 2;
    return (Vector3){ x0 + i * CELL, 0.3f + row * ROW_RISE, BACK_Z - row * ROW_STEP };
}

// Is this pointer's cable drawn? Before the pointer chapters, an array
// parameter is just "the array" (no cable), so arrays don't look scary.
static bool WireVisible(const World *w, int k)
{
    if (w->theme >= 8) return true;
    const TestWire *wire = &w->test.wires[k];
    const char *label = w->test.rows[wire->row].label;
    if (label[0] == '*') label++;
    return strcmp(label, wire->name) != 0;
}

static int VisibleWires(const World *w)
{
    int n = 0;
    for (int k = 0; k < w->test.wireCount; k++) if (WireVisible(w, k)) n++;
    return n;
}

// Variables go along the front, facing the crates. With no arrays at all they
// go along the back instead, where the crates would be.
static bool BoxesAtBack(const World *w) { return w->test.rowCount == 0; }

static float BoxWidth(const TestRegister *r)
{
    if (r->isBool) return 1.25f;
    if (r->isChar || !r->shownKnown) return 1.0f;
    int digits = r->digits < 1 ? 1 : r->digits;
    float wide = digits * DIGIT_W + 0.4f;
    return wide < 1.0f ? 1.0f : wide;
}

#define SOCKET_W 0.9f

// Left edge of the row of boxes (sockets first, then variables)
static float BoxesLeft(const World *w)
{
    float total = 0;
    int items = 0;
    for (int k = 0; k < w->test.wireCount; k++) if (WireVisible(w, k)) { total += SOCKET_W; items++; }
    for (int i = 0; i < w->test.regCount; i++) { total += BoxWidth(&w->test.regs[i]); items++; }
    if (items > 1) total += (items - 1) * ITEM_GAP;
    return -total / 2;
}

static float BoxesZ(const World *w) { return BoxesAtBack(w) ? BACK_Z : FRONT_Z; }

// Centre of the top of variable box i (where its digit blocks sit)
static Vector3 BoxPos(const World *w, int i)
{
    float x = BoxesLeft(w);
    for (int k = 0; k < w->test.wireCount; k++) if (WireVisible(w, k)) x += SOCKET_W + ITEM_GAP;
    for (int k = 0; k < i; k++) x += BoxWidth(&w->test.regs[k]) + ITEM_GAP;
    return (Vector3){ x + BoxWidth(&w->test.regs[i]) / 2, 0.5f, BoxesZ(w) };
}

static Vector3 SocketPos(const World *w, int k)
{
    float x = BoxesLeft(w);
    for (int j = 0; j < k; j++) if (WireVisible(w, j)) x += SOCKET_W + ITEM_GAP;
    return (Vector3){ x + SOCKET_W / 2, 0.42f, BoxesZ(w) };
}

static Vector3 SlotPos(const World *w, TestSlot s)
{
    if (s.kind == SLOT_REGISTER && s.index < w->test.regCount) return BoxPos(w, s.index);
    if (s.kind == SLOT_DATA && s.row < w->test.rowCount) return CratePos(w, s.row, (float)s.index);
    return (Vector3){ 0, 0.4f, 0 };
}

// Which way Bitty faces to work on something
static float FacingFor(const World *w, TestSlot s)
{
    if (s.kind == SLOT_REGISTER && !BoxesAtBack(w)) return 0;      // turn round to the boxes at the front
    return 180;                                                     // the crates at the back
}

// The ends of the room
static void RoomSpan(const World *w, float *left, float *right)
{
    float l = -1.5f, r = 1.5f;
    const TestDisplay *td = &w->test;
    for (int row = 0; row < td->rowCount; row++)
    {
        if (td->rows[row].count == 0) continue;
        Vector3 a = CratePos(w, row, 0), b = CratePos(w, row, (float)td->rows[row].count - 1);
        float margin = td->rows[row].count > 1 ? 1.7f : 0.8f;      // room for the row's name
        if (a.x - margin < l) l = a.x - margin;
        if (b.x + 0.8f > r) r = b.x + 0.8f;
    }
    float bl = BoxesLeft(w);
    if (bl - 0.4f < l) l = bl - 0.4f;
    if (-bl + 0.4f > r) r = -bl + 0.4f;
    *left = l;
    *right = r;
}

// Where Bitty stands when he arrives: at the start of the first row (index 0)
static float StartX(const World *w)
{
    if (w->test.rowCount > 0 && w->test.rows[0].count > 0) return CratePos(w, 0, 0).x;
    if (w->test.regCount > 0) return BoxPos(w, 0).x;
    return 0;
}

// "arr[2]", "*a", "max": what the code calls this place
static const char *SlotName(const World *w, TestSlot s)
{
    const TestDisplay *td = &w->test;
    if (s.kind == SLOT_REGISTER && s.index < td->regCount) return td->regs[s.index].name;
    if (s.kind == SLOT_DATA && s.row < td->rowCount)
    {
        const TestDataRow *d = &td->rows[s.row];
        const char *name = d->label[0] ? d->label : d->name;
        if (d->count == 1 && d->label[0]) return name;
        return TextFormat("%s[%d]", name[0] == '*' ? name + 1 : name, s.index);
    }
    return "?";
}

static int ShownValue(const World *w, TestSlot s, bool *isChar, bool *known)
{
    const TestDisplay *td = &w->test;
    if (known) *known = true;
    if (s.kind == SLOT_REGISTER && s.index < td->regCount)
    {
        if (isChar) *isChar = td->regs[s.index].isChar;
        if (known) *known = td->regs[s.index].shownKnown;
        return td->regs[s.index].shown;
    }
    if (s.kind == SLOT_DATA && s.row < td->rowCount && s.index < td->rows[s.row].count)
    {
        if (isChar) *isChar = td->rows[s.row].isChar;
        return td->rows[s.row].shown[s.index];
    }
    if (isChar) *isChar = false;
    return 0;
}

static bool SameSlot(TestSlot a, TestSlot b)
{
    return a.kind == b.kind && a.index == b.index && (a.kind != SLOT_DATA || a.row == b.row);
}

static bool ValidSlot(const World *w, TestSlot s)
{
    if (s.kind == SLOT_REGISTER) return s.index >= 0 && s.index < w->test.regCount;
    if (s.kind == SLOT_DATA) return s.row >= 0 && s.row < w->test.rowCount && s.index >= 0 && s.index < w->test.rows[s.row].count;
    return false;
}

static void SetCaption(World *w, const char *text)
{
    snprintf(w->room.caption, sizeof w->room.caption, "%s", text);
    w->room.captionTime = 0;
}

// ===========================================================================
// Beaming in and out
// ===========================================================================
void World_EnterRoom(World *w)
{
    RoomState *rm = &w->room;
    if (rm->mode == ROOM_INSIDE || rm->mode == ROOM_BEAM_IN) { World_RoomNewTest(w); return; }
    memset(rm, 0, sizeof *rm);
    rm->mode = ROOM_BEAM_IN;
    rm->facing = rm->facingShown = 180;
    rm->askTime = -1;
    rm->dropIn = 1.0f;
}

void World_LeaveRoom(World *w)
{
    RoomState *rm = &w->room;
    if (rm->mode == ROOM_OUTSIDE || rm->mode == ROOM_BEAM_OUT) return;
    rm->mode = ROOM_BEAM_OUT;
    rm->beam = 0;
    rm->jobCount = rm->jobAt = 0;
    rm->holding = false;
}

bool World_InRoom(const World *w)
{
    return w->room.mode == ROOM_BEAM_IN || w->room.mode == ROOM_INSIDE;
}

bool World_RoomBusy(const World *w)
{
    const RoomState *rm = &w->room;
    if (rm->mode == ROOM_BEAM_IN || rm->mode == ROOM_BEAM_OUT) return true;
    if (rm->mode != ROOM_INSIDE) return false;
    if (rm->quiz.active) return true;               // waiting for the player's guess
    if (rm->dropIn > 0.25f) return true;
    if (rm->crash > 0 && !rm->crashFall) return true;
    return rm->jobAt < rm->jobCount;
}

void World_RoomNewTest(World *w)
{
    RoomState *rm = &w->room;
    rm->jobCount = rm->jobAt = 0;
    rm->jobTime = 0;
    rm->holding = false;
    rm->result[0] = '\0';
    rm->crash = 0;
    rm->crashFall = false;
    rm->dropIn = 0.7f;
    rm->caption[0] = '\0';
    rm->code[0] = '\0';
    rm->askTime = -1;
    rm->arrowOn = false;
    rm->quiz.active = false;
    memset(rm->digitAnim, 0, sizeof rm->digitAnim);
    memset(rm->takeAnim, 0, sizeof rm->takeAnim);
}

void World_RoomCrash(World *w, bool memoryError)
{
    RoomState *rm = &w->room;
    rm->jobCount = rm->jobAt = 0;
    rm->holding = false;
    if (!memoryError || w->test.rowCount == 0 || w->test.rows[0].count == 0)
    {
        w->shake = 0.5f;
        snprintf(rm->caption, sizeof rm->caption, "Crashed!");
        rm->captionTime = 0;
        return;
    }
    // Out of bounds: walk off the end of the data, into memory that isn't there
    const TestDataRow *d = &w->test.rows[0];
    float left = CratePos(w, 0, 0).x, right = CratePos(w, 0, (float)d->count - 1).x;
    bool goLeft = rm->x < (left + right) / 2 - CELL;
    rm->crashX = goLeft ? left - CELL : right + CELL;
    rm->crash = 0.01f;
    rm->crashFall = false;
    snprintf(rm->caption, sizeof rm->caption, goLeft ? "Index -1?! That's before the start of the array!"
                                                    : "Index %d?! That's past the end of the array!", d->count);
    rm->captionTime = 0;
}

void World_RoomResult(World *w, const char *text)
{
    RoomState *rm = &w->room;
    snprintf(rm->result, sizeof rm->result, "%s", text);
    rm->resultTime = 0;
    rm->holding = false;
    rm->askTime = -1;
    if (strncmp(text, "return ", 7) == 0) SetCaption(w, TextFormat("Done! The answer I give back is %s.", text + 7));
    else SetCaption(w, text);
}

// ===========================================================================
// Turning a line of code into jobs
// ===========================================================================
static void AddJob(RoomState *rm, RoomJobKind kind, TestSlot slot, int value)
{
    if (rm->jobCount >= ROOM_MAX_JOBS) return;
    RoomJob *j = &rm->jobs[rm->jobCount++];
    j->kind = kind;
    j->slot = slot;
    j->value = value;
}

// ---- The question an if / while / for asks, written out with the values put in ----

// The condition from a line of code: "if (arr[i] > max) {" -> "arr[i] > max".
// Sets *loop for while / for / do-while. Returns false if the line has none.
static bool ConditionOf(const char *code, char *out, int size, int *loop)
{
    const char *p = code;
    while (*p == ' ' || *p == '\t' || *p == '}') p++;
    if (strncmp(p, "else", 4) == 0) { p += 4; while (*p == ' ') p++; }
    bool isFor = strncmp(p, "for", 3) == 0, isIf = strncmp(p, "if", 2) == 0;
    bool isWhile = strncmp(p, "while", 5) == 0;
    if (!isFor && !isIf && !isWhile) return false;
    *loop = !isIf;
    const char *open = strchr(p, '(');
    if (!open) return false;
    const char *start = open + 1, *end = NULL;
    int depth = 1, semis = 0;
    for (const char *q = start; *q; q++)
    {
        if (*q == '(') depth++;
        if (*q == ')' && --depth == 0) { end = q; break; }
        if (isFor && *q == ';' && depth == 1)
        {
            semis++;
            if (semis == 1) start = q + 1;          // for (a; CONDITION; c)
            else { end = q; break; }
        }
    }
    if (!end || end <= start) return false;
    while (start < end && *start == ' ') start++;
    while (end > start && end[-1] == ' ') end--;
    snprintf(out, (size_t)size, "%.*s", (int)(end - start), start);
    return out[0] != '\0';
}

// "arr[i] > max" -> "9 > 3", using what Bitty just looked at
static void ConditionValues(const World *w, const char *cond, const TestSlot *seen, int seenCount, char *out, int size)
{
    const TestDisplay *td = &w->test;
    int n = 0;
    out[0] = '\0';
    for (const char *p = cond; *p && n < size - 12; )
    {
        bool deref = (*p == '*' && (p[1] == '_' || (p[1] >= 'a' && p[1] <= 'z') || (p[1] >= 'A' && p[1] <= 'Z')) &&
                      (p == cond || strchr(" (!&|=<>", p[-1])));
        if (deref) p++;
        if (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
        {
            char name[32];
            int k = 0;
            const char *q = p;
            while ((*q == '_' || (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9')) && k < 31)
                name[k++] = *q++;
            name[k] = '\0';
            bool indexed = (*q == '[');
            const char *after = q;
            if (indexed)
            {
                int depth = 0;
                for (; *after; after++)
                {
                    if (*after == '[') depth++;
                    if (*after == ']' && --depth == 0) { after++; break; }
                }
            }

            const char *value = NULL;
            if (indexed || deref)
            {
                // arr[i] or *p: the crate Bitty just looked at in that row
                int row = -1;
                if (indexed)
                    for (int r = 0; r < td->rowCount; r++)
                    {
                        const char *label = td->rows[r].label[0] == '*' ? td->rows[r].label + 1 : td->rows[r].label;
                        if (strcmp(label, name) == 0) row = r;
                    }
                else
                    for (int k2 = 0; k2 < td->wireCount; k2++)
                        if (strcmp(td->wires[k2].name, name) == 0)
                        {
                            row = td->wires[k2].row;
                            int i = td->wires[k2].index;
                            if (i < td->rows[row].count) value = ValueText(td->rows[row].shown[i], td->rows[row].isChar);
                        }
                for (int k2 = seenCount - 1; k2 >= 0 && !value && row >= 0; k2--)
                    if (seen[k2].kind == SLOT_DATA && seen[k2].row == row)
                        value = ValueText(td->rows[row].shown[seen[k2].index], td->rows[row].isChar);
                if (value && td->rows[row >= 0 ? row : 0].isChar && strcmp(value, "\\0") != 0) value = TextFormat("'%s'", value);
                else if (value && td->rows[row >= 0 ? row : 0].isChar) value = "'\\0'";
            }
            else
            {
                for (int pass = 0; pass < 2 && !value; pass++)
                for (int r = 0; r < td->regCount && !value; r++)
                    if ((td->regs[r].inScope || pass == 1) && strcmp(td->regs[r].name, name) == 0 && td->regs[r].shownKnown)
                    {
                        const TestRegister *g = &td->regs[r];
                        value = g->isBool ? (g->shown ? "true" : "false") :
                                g->isChar ? TextFormat("'%s'", CharText(g->shown)) : TextFormat("%d", g->shown);
                    }
                if (!value && strcmp(name, "true") == 0) value = "true";
                if (!value && strcmp(name, "false") == 0) value = "false";
            }

            if (!value)     // don't know it: leave the code as it is
            {
                if (deref) out[n++] = '*';
                value = TextFormat("%.*s", (int)(after - p), p);
            }
            n += snprintf(out + n, (size_t)(size - n), "%s", value);
            if (n >= size) n = size - 1;
            p = after;
            continue;
        }
        out[n++] = *p++;
        out[n] = '\0';
    }
    out[n] = '\0';
}

// ---- Guess first (step mode) ----
static void AddOption(RoomQuiz *q, const char *text)
{
    if (q->optionCount >= 3) return;
    for (int i = 0; i < q->optionCount; i++) if (strcmp(q->options[i], text) == 0) return;
    snprintf(q->options[q->optionCount++], sizeof q->options[0], "%s", text);
}

// Before Bitty acts out the line, ask the player what it will do:
// the answer to an if / while question, or the new value of a box
static void MakeQuiz(World *w)
{
    RoomState *rm = &w->room;
    RoomQuiz *q = &rm->quiz;
    memset(q, 0, sizeof *q);
    q->chosen = -1;

    for (int i = rm->jobAt; i < rm->jobCount; i++)
    {
        const RoomJob *j = &rm->jobs[i];
        if (j->kind == JOB_ASK)
        {
            snprintf(q->question, sizeof q->question, rm->questionKind ? "Will the loop keep going?" : "Is the answer YES or NO?");
            snprintf(q->detail, sizeof q->detail, "%s ?", rm->questionValues);
            AddOption(q, "YES");
            AddOption(q, "NO");
            q->correct = j->value ? 0 : 1;
            q->active = true;
            return;
        }
    }
    for (int i = rm->jobAt; i < rm->jobCount; i++)
    {
        const RoomJob *j = &rm->jobs[i];
        if (j->kind != JOB_PUT && j->kind != JOB_ZAP) continue;
        bool isChar, known;
        int old = ShownValue(w, j->slot, &isChar, &known);
        const TestRegister *g = j->slot.kind == SLOT_REGISTER ? &w->test.regs[j->slot.index] : NULL;
        if (g && g->isBool) return;                     // true / false boxes: the if questions cover those
        snprintf(q->question, sizeof q->question, "What will %s be after this line?", SlotName(w, j->slot));
        snprintf(q->detail, sizeof q->detail, "%s", rm->code);

        // the right answer, and two that a beginner might think
        char right[28], a[28], b[28];
        snprintf(right, sizeof right, "%s", isChar && j->value ? TextFormat("'%s'", CharText(j->value)) : ValueText(j->value, isChar));
        int wrong1 = known && old != j->value ? old : j->value + 1;
        int wrong2 = j->value - 1;
        if (rm->holding && rm->held != j->value) wrong2 = rm->held;
        for (int k = rm->jobAt; k < rm->jobCount && wrong2 == j->value - 1; k++)
            if ((rm->jobs[k].kind == JOB_PICK) && rm->jobs[k].value != j->value && rm->jobs[k].value != wrong1)
                wrong2 = rm->jobs[k].value;
        if (wrong2 == wrong1 || wrong2 == j->value) wrong2 = j->value + 2;
        snprintf(a, sizeof a, "%s", isChar && wrong1 > 0 ? TextFormat("'%s'", CharText(wrong1)) : ValueText(wrong1, isChar));
        snprintf(b, sizeof b, "%s", isChar && wrong2 > 0 ? TextFormat("'%s'", CharText(wrong2)) : ValueText(wrong2, isChar));

        // shuffle, but the same way each time for the same line
        int order = (j->value * 7 + rm->codeLine * 3) % 3;
        for (int k = 0; k < 3; k++)
        {
            int pick = (order + k) % 3;
            AddOption(q, pick == 0 ? right : pick == 1 ? a : b);
        }
        for (int k = 0; k < q->optionCount; k++) if (strcmp(q->options[k], right) == 0) q->correct = k;
        q->active = q->optionCount >= 2;
        return;
    }
}

void World_RoomGuess(World *w, int option)
{
    RoomQuiz *q = &w->room.quiz;
    if (!q->active || q->chosen >= 0 || option < 0 || option >= q->optionCount) return;
    q->chosen = option;
    q->feedback = 1.6f;
    w->room.guesses++;
    if (option == q->correct) w->room.guessesRight++;
}

void World_RoomStep(World *w, const TestAccess *list, int count, bool animate, int line, const char *code)
{
    RoomState *rm = &w->room;
    if (rm->mode != ROOM_INSIDE && rm->mode != ROOM_BEAM_IN) return;
    if (rm->jobAt >= rm->jobCount) rm->jobAt = rm->jobCount = 0;
    if (!animate) return;       // fast-forward: the values just catch up (see SyncShown)

    // the line being acted out, for the banner
    rm->codeLine = line;
    const char *c = code ? code : "";
    while (*c == ' ' || *c == '\t') c++;
    snprintf(rm->code, sizeof rm->code, "%s", c);

    TestSlot reads[MAX_LINE_READS], seen[MAX_LINE_READS];
    int readCount = 0, seenCount = 0;

    for (int i = 0; i < count; i++)
    {
        const TestAccess *a = &list[i];

        // an if / while / for question was answered: look at what it used, then show it
        if (a->condition)
        {
            for (int k = 0; k < readCount; k++)
                AddJob(rm, reads[k].kind == SLOT_DATA ? JOB_SCAN : JOB_GLANCE, reads[k], 0);
            readCount = 0;
            int loop = 0;
            if (ConditionOf(rm->code, rm->question, sizeof rm->question, &loop))
            {
                ConditionValues(w, rm->question, seen, seenCount, rm->questionValues, sizeof rm->questionValues);
                rm->questionKind = loop;
                rm->answer = a->answer;
                TestSlot none = { SLOT_NONE, 0, 0 };
                AddJob(rm, JOB_ASK, none, a->answer);
            }
            continue;
        }

        if (!ValidSlot(w, a->slot)) continue;
        if (!a->write)
        {
            if (readCount < MAX_LINE_READS) reads[readCount++] = a->slot;
            if (seenCount < MAX_LINE_READS) seen[seenCount++] = a->slot;
            continue;
        }
        // ---- a write: where did the value come from? ----
        TestSlot dest = a->slot;
        int source = -1, carry = 0;
        bool digitTake = false;
        bool destKnown;
        int old = ShownValue(w, dest, NULL, &destKnown);
        // 1. something that held exactly this value (a copy: max = arr[i], *a = *b)
        for (int k = readCount - 1; k >= 0 && source < 0; k--)
            if (!SameSlot(reads[k], dest) && ShownValue(w, reads[k], NULL, NULL) == a->value) source = k;
        // 2. the last digit of a variable (digit = n % 10), maybe added straight
        //    onto the end of another number (rev = rev * 10 + n % 10)
        int digit = a->value;
        if (dest.kind == SLOT_REGISTER && destKnown && old > 0 && a->value / 10 == old) digit = a->value % 10;
        for (int k = readCount - 1; k >= 0 && source < 0; k--)
        {
            int v = ShownValue(w, reads[k], NULL, NULL);
            if (!SameSlot(reads[k], dest) && reads[k].kind == SLOT_REGISTER && v > 0 && v % 10 == digit && (v >= 10 || digit != a->value))
            {
                source = k;
                digitTake = true;
                carry = digit;
            }
        }
        // 3. added to (or taken from, or times) the old value: sum += arr[i], total -= cost
        //    (crates first, then variables). Anything else, like i++ or n = n / 10,
        //    is changed where it stands.
        for (int pass = 0; pass < 2 && source < 0; pass++)
            for (int k = readCount - 1; k >= 0 && source < 0; k--)
            {
                if (SameSlot(reads[k], dest) || (reads[k].kind == SLOT_DATA) != (pass == 0)) continue;
                int v = ShownValue(w, reads[k], NULL, NULL);
                if (destKnown && v != 0 && (a->value == old + v || a->value == old - v || (v != 1 && a->value == old * v)))
                    source = k;
            }

        for (int k = 0; k < readCount; k++)
        {
            if (k == source)
                AddJob(rm, JOB_PICK, reads[k], digitTake ? carry : ShownValue(w, reads[k], NULL, NULL));
            else if (reads[k].kind == SLOT_DATA && !SameSlot(reads[k], dest))
                AddJob(rm, JOB_SCAN, reads[k], 0);
            else if (!SameSlot(reads[k], dest))
                AddJob(rm, JOB_GLANCE, reads[k], 0);
        }
        readCount = 0;

        // crates are always walked to; a variable changed in place (i++, n = n / 10) is zapped from where he stands
        if (source >= 0 || dest.kind == SLOT_DATA) AddJob(rm, JOB_PUT, dest, a->value);
        else AddJob(rm, JOB_ZAP, dest, a->value);
    }

    // reads that weren't used for a write: comparisons, conditions, return
    for (int k = 0; k < readCount; k++)
        AddJob(rm, reads[k].kind == SLOT_DATA ? JOB_SCAN : JOB_GLANCE, reads[k], 0);

    if (w->test.askGuesses) MakeQuiz(w);
}

// ===========================================================================
// Acting the jobs out
// ===========================================================================

// Is a job still waiting to put a new value into this slot? (Until it has,
// the room keeps showing the old value.)
static bool PendingWrite(const RoomState *rm, TestSlot s)
{
    for (int i = rm->jobAt; i < rm->jobCount; i++)
        if ((rm->jobs[i].kind == JOB_PUT || rm->jobs[i].kind == JOB_ZAP) && SameSlot(rm->jobs[i].slot, s)) return true;
    return false;
}

// Everything not waiting for Bitty shows its real value
static void SyncShown(World *w)
{
    TestDisplay *td = &w->test;
    RoomState *rm = &w->room;
    for (int r = 0; r < td->rowCount; r++)
        for (int i = 0; i < td->rows[r].count; i++)
        {
            TestSlot s = { SLOT_DATA, r, i };
            if (!PendingWrite(rm, s)) td->rows[r].shown[i] = td->rows[r].values[i];
        }
    for (int i = 0; i < td->regCount; i++)
    {
        TestSlot s = { SLOT_REGISTER, 0, i };
        TestRegister *g = &td->regs[i];
        if (!PendingWrite(rm, s))
        {
            g->shown = g->value;
            g->shownKnown = g->known;
        }
        int d[12];
        int n = g->shownKnown ? DigitsOf(g->shown, d) : 1;
        if (n > g->digits) g->digits = n;
    }
}

// A value lands in a slot (Bitty put it there, or zapped it)
static void Apply(World *w, TestSlot s, int value)
{
    TestDisplay *td = &w->test;
    RoomState *rm = &w->room;
    Vector3 at = SlotPos(w, s);
    if (s.kind == SLOT_DATA)
    {
        td->rows[s.row].shown[s.index] = value;
        td->rows[s.row].writeFlash[s.index] = 0.6f;
        Burst_Sparks((Vector3){ at.x, at.y + 0.3f, at.z }, 10, 0.9f, (Color){ 255, 190, 110, 255 });
        return;
    }
    TestRegister *g = &td->regs[s.index];
    int old = g->shown;
    bool wasKnown = g->shownKnown;
    g->shown = value;
    g->shownKnown = true;
    g->flash = 0.6f;
    rm->digitKind[s.index] = 0;
    if (wasKnown && !g->isChar && !g->isBool)
    {
        if (old != value && old > 0 && value == old / 10)
        {
            rm->digitKind[s.index] = 1;                  // n = n / 10: the last digit drops off
            rm->digitOld[s.index] = old;
            rm->digitAnim[s.index] = 0.7f;
        }
        else if (old != 0 && old > 0 && value > old && (value - old * 10) >= 0 && (value - old * 10) <= 9 && value / 10 == old)
        {
            rm->digitKind[s.index] = 2;                  // rev = rev * 10 + d: a digit is added on the end
            rm->digitOld[s.index] = old;
            rm->digitAnim[s.index] = 0.5f;
        }
    }
    Burst_Sparks((Vector3){ at.x, at.y + 0.25f, at.z }, 8, 0.8f, (Color){ 255, 220, 120, 255 });
}


// What Bitty says while he does each job: short, in his own words, so
// someone who has never coded can follow along.
static void StartJob(World *w, RoomJob *j)
{
    RoomState *rm = &w->room;
    bool isChar = false, known = true;
    int v = 0;
    if (j->kind != JOB_ASK) v = ShownValue(w, j->slot, &isChar, &known);
    char where[40];
    snprintf(where, sizeof where, "%s", j->kind == JOB_ASK ? "" : SlotName(w, j->slot));
    const char *vt = ValueText(v, isChar);
    if (isChar && v != 0) vt = TextFormat("'%s'", CharText(v));

    switch (j->kind)
    {
        case JOB_SCAN:
            rm->raySlot = j->slot;
            rm->ray = JobLength(j->kind);
            rm->rayZap = false;
            if (j->slot.kind == SLOT_DATA) w->test.rows[j->slot.row].readFlash[j->slot.index] = 0.6f;
            if (isChar && v == 0) SetCaption(w, TextFormat("%s holds \\0. That's the end of the word!", where));
            else SetCaption(w, TextFormat("I look in %s. It holds %s!", where, vt));
            break;

        case JOB_PICK:
        {
            rm->raySlot = j->slot;
            rm->ray = JobLength(j->kind);
            rm->rayZap = false;
            if (j->slot.kind == SLOT_DATA) w->test.rows[j->slot.row].readFlash[j->slot.index] = 0.6f;
            bool digit = j->slot.kind == SLOT_REGISTER && !isChar && v >= 10 && j->value == v % 10;
            if (digit)
            {
                rm->takeAnim[j->slot.index] = JobLength(j->kind) + 0.3f;
                SetCaption(w, TextFormat("I take the last digit of %s: the %d. (%d %% 10 is %d)", where, j->value, v, j->value));
            }
            else SetCaption(w, TextFormat("I copy the %s from %s.", ValueText(j->value, isChar), where));
            break;
        }

        case JOB_PUT:
        {
            bool destChar;
            ShownValue(w, j->slot, &destChar, NULL);
            if (!rm->holding)
            {
                // nothing picked up (e.g. arr[i] = 0): the value appears in his hands
                rm->holding = true;
                rm->held = j->value;
                rm->heldChar = destChar;
                rm->heldPop = 0.25f;
            }
            const char *nv = destChar && j->value ? TextFormat("'%s'", CharText(j->value)) : ValueText(j->value, destChar);
            if (j->slot.kind == SLOT_REGISTER && known && !isChar && v > 0 && j->value / 10 == v && j->value > v)
                SetCaption(w, TextFormat("I stick the %d on the end of %s. Now it's %d!", j->value % 10, where, j->value));
            else if (j->slot.kind == SLOT_REGISTER && known && j->value != rm->held)
                SetCaption(w, TextFormat("%s was %s. Now it's %s!", where, vt, nv));
            else
                SetCaption(w, TextFormat("I put %s into %s.", nv, where));
            break;
        }

        case JOB_ZAP:
        {
            rm->raySlot = j->slot;
            rm->ray = JobLength(j->kind);
            rm->rayZap = true;
            const TestRegister *g = j->slot.kind == SLOT_REGISTER ? &w->test.regs[j->slot.index] : NULL;
            const char *nv = (g && g->isBool) ? (j->value ? "true" : "false") : ValueText(j->value, isChar);
            if (!known)
                SetCaption(w, TextFormat("New box: %s. It starts at %s.", where, nv));
            else if (g && !isChar && !g->isBool && v > 0 && j->value == v / 10)
                SetCaption(w, TextFormat("%s loses its last digit. %d becomes %d!", where, v, j->value));
            else if (g && !g->isBool && j->value == v + 1)
                SetCaption(w, TextFormat("%s goes up by one: %d, %d!", where, v, j->value));
            else if (g && !g->isBool && j->value == v - 1)
                SetCaption(w, TextFormat("%s goes down by one: %d, %d!", where, v, j->value));
            else
                SetCaption(w, TextFormat("%s is now %s.", where, nv));
            break;
        }

        case JOB_ASK:
            rm->askTime = 0;
            if (rm->questionKind == 0)
                SetCaption(w, j->value ? "YES! So I do the code inside the { }." : "NO. So I skip the code inside the { }.");
            else
                SetCaption(w, j->value ? "YES! So round the loop I go again." : "NO. So the loop is finished!");
            break;

        case JOB_GLANCE:
            break;
    }
}

static void FinishJob(World *w, RoomJob *j)
{
    RoomState *rm = &w->room;
    switch (j->kind)
    {
        case JOB_PICK:
        {
            bool isChar;
            ShownValue(w, j->slot, &isChar, NULL);
            rm->holding = true;
            rm->held = j->value;
            rm->heldChar = isChar;
            rm->heldPop = 0.3f;
            rm->arrowFrom = j->slot;        // an arrow shows where it's going
            rm->arrowOn = true;
            break;
        }
        case JOB_PUT:
            Apply(w, j->slot, j->value);
            rm->holding = false;
            rm->arrowOn = false;
            break;
        case JOB_ZAP:
            Apply(w, j->slot, j->value);
            break;
        default:
            break;
    }
}

static void RunJobs(World *w, float dt)
{
    RoomState *rm = &w->room;
    while (rm->jobAt < rm->jobCount)
    {
        RoomJob *j = &rm->jobs[rm->jobAt];

        if (j->kind == JOB_GLANCE)
        {
            // all the glances in a row happen together: a quick look at each
            int last = rm->jobAt;
            while (last + 1 < rm->jobCount && rm->jobs[last + 1].kind == JOB_GLANCE) last++;
            if (rm->jobTime == 0)
            {
                char names[80] = "";
                for (int k = rm->jobAt; k <= last; k++)
                {
                    TestSlot s = rm->jobs[k].slot;
                    if (s.kind == SLOT_REGISTER) rm->glance[s.index] = JobLength(JOB_GLANCE) + 0.1f;
                    size_t len = strlen(names);
                    bool dup = false;
                    for (int q = rm->jobAt; q < k; q++) if (SameSlot(rm->jobs[q].slot, s)) dup = true;
                    if (!dup && len < 60) snprintf(names + len, sizeof names - len, "%s%s", len ? " and " : "", SlotName(w, s));
                }
                // straight after looking at a crate: it's a comparison, e.g. if (arr[i] > max)
                const RoomJob *prev = rm->jobAt > 0 ? &rm->jobs[rm->jobAt - 1] : NULL;
                TestSlot first = rm->jobs[rm->jobAt].slot;
                if (prev && prev->kind == JOB_SCAN && last == rm->jobAt)
                {
                    bool c1, c2;
                    int a = ShownValue(w, prev->slot, &c1, NULL), b = ShownValue(w, first, &c2, NULL);
                    char left[40];
                    snprintf(left, sizeof left, "%s", SlotName(w, prev->slot));
                    SetCaption(w, TextFormat("I compare %s (%s) with %s (%s)...", left, ValueText(a, c1), SlotName(w, first), ValueText(b, c2)));
                }
                else SetCaption(w, TextFormat("I check %s...", names));
            }
            rm->jobTime += dt;
            if (rm->jobTime < JobLength(JOB_GLANCE)) return;
            rm->jobAt = last + 1;
            rm->jobTime = 0;
            continue;
        }

        if (j->kind != JOB_ZAP && j->kind != JOB_ASK)
        {
            float tx = SlotPos(w, j->slot).x;
            float d = tx - rm->x;
            if (fabsf(d) > 0.001f)
            {
                // walk there, hopping from crate to crate (long walks are quicker)
                float speed = WALK_SPEED * (1.0f + fabsf(d) * 0.12f);
                float step = speed * dt;
                if (step > fabsf(d)) step = fabsf(d);
                rm->x += d > 0 ? step : -step;
                rm->walked += step;
                rm->walking = true;
                rm->facing = d > 0 ? 75 : -75;
                return;
            }
        }
        rm->walking = false;
        if (j->kind == JOB_ASK) rm->facing = 0;      // turns to us to answer the question
        else rm->facing = FacingFor(w, j->slot);

        if (rm->jobTime == 0) StartJob(w, j);
        rm->jobTime += dt;
        if (rm->jobTime < JobLength(j->kind)) return;
        FinishJob(w, j);
        rm->jobAt++;
        rm->jobTime = 0;
    }
    rm->walking = false;
}

void Room_Update(World *w, float dt)
{
    RoomState *rm = &w->room;
    TestDisplay *td = &w->test;
    float speed = w->speed > 0 ? w->speed : 1.0f;
    if (td->quick) speed *= 1.8f;

    // beaming: Bitty stretches into light, the picture switches, he comes back together
    if (rm->mode == ROOM_BEAM_IN || rm->mode == ROOM_BEAM_OUT)
    {
        float s = speed > 3 ? 3 : speed;
        rm->beam += dt * s / BEAM_SECONDS;
        float k = rm->beam < 0.5f ? rm->beam / 0.42f : 1 - (rm->beam - 0.58f) / 0.42f;
        w->teleport = Clamp01(k);
        if (rm->mode == ROOM_BEAM_IN && rm->beam >= 0.5f && !rm->placed)
        {
            rm->placed = true;
            rm->x = StartX(w);
        }
        if (rm->beam >= 1)
        {
            rm->mode = (rm->mode == ROOM_BEAM_IN) ? ROOM_INSIDE : ROOM_OUTSIDE;
            w->teleport = 0;
            if (rm->mode == ROOM_OUTSIDE) memset(rm, 0, sizeof *rm);
        }
    }
    if (rm->mode == ROOM_OUTSIDE) return;

    // fade the flashes
    for (int r = 0; r < td->rowCount; r++)
        for (int i = 0; i < td->rows[r].count; i++)
        {
            if (td->rows[r].readFlash[i]  > 0) td->rows[r].readFlash[i]  -= dt;
            if (td->rows[r].writeFlash[i] > 0) td->rows[r].writeFlash[i] -= dt;
        }
    for (int i = 0; i < TEST_MAX_REGISTERS; i++)
    {
        if (i < td->regCount && td->regs[i].flash > 0) td->regs[i].flash -= dt;
        if (rm->glance[i] > 0) rm->glance[i] -= dt * speed;
        if (rm->digitAnim[i] > 0) rm->digitAnim[i] -= dt * speed;
        if (rm->takeAnim[i] > 0) rm->takeAnim[i] -= dt * speed;
    }
    for (int i = 0; i < td->wireCount; i++)
    {
        TestWire *wire = &td->wires[i];
        wire->shownIndex += (wire->index - wire->shownIndex) * (1 - expf(-dt * 10 * speed));
        if (wire->replug > 0) wire->replug -= dt;
    }
    if (rm->ray > 0) rm->ray -= dt * speed;
    if (rm->heldPop > 0) rm->heldPop -= dt;
    if (rm->dropIn > 0) rm->dropIn -= dt * (speed > 2 ? 2 : speed);
    rm->resultTime += dt;
    rm->captionTime += dt;
    if (rm->askTime >= 0) rm->askTime += dt;

    // turn smoothly
    float diff = rm->facing - rm->facingShown;
    while (diff > 180) diff -= 360;
    while (diff < -180) diff += 360;
    rm->facingShown += diff * (1 - expf(-dt * 12));

    SyncShown(w);
    if (rm->mode != ROOM_INSIDE) return;

    // a memory error: walk off the end of the data, teeter, fall
    if (rm->crash > 0)
    {
        if (!rm->crashFall)
        {
            float d = rm->crashX - rm->x;
            if (fabsf(d) > 0.001f)
            {
                float step = WALK_SPEED * 1.4f * dt * (speed > 2 ? 2 : speed);
                if (step > fabsf(d)) step = fabsf(d);
                rm->x += d > 0 ? step : -step;
                rm->walked += step;
                rm->walking = true;
                rm->facing = d > 0 ? 75 : -75;
                return;
            }
            rm->walking = false;
            rm->crashFall = true;
            rm->crash = 0.01f;
            Burst_Sparks((Vector3){ rm->x, 0.4f, 0 }, 18, 1.2f, (Color){ 255, 90, 150, 255 });
        }
        else rm->crash += dt;
        return;
    }

    // a guess: nothing happens until the player has picked, and seen if they were right
    if (rm->quiz.active)
    {
        if (rm->quiz.chosen < 0) return;
        rm->quiz.feedback -= dt;
        if (rm->quiz.feedback > 0) return;
        rm->quiz.active = false;
    }

    RunJobs(w, dt * speed);
}

// ===========================================================================
// Drawing
// ===========================================================================

// Bitty drawn at his spot in the room. DrawBitty works in tiles, so this makes
// a copy of the world with his position and pose set for the room.
static void DrawRoomBitty(const World *w, float t)
{
    const RoomState *rm = &w->room;
    static World tmp;
    tmp = *w;
    memset(tmp.tilePress, 0, sizeof tmp.tilePress);
    tmp.bittyX = tmp.bittyY = 0;
    tmp.drawX = rm->x + (tmp.cols - 1) / 2.0f;
    tmp.drawY = (tmp.rows - 1) / 2.0f;
    tmp.facingShown = rm->facingShown / 0.6f;     // DrawBitty turns him by 0.6 x facing
    tmp.spawn = 0;
    tmp.state = WORLD_RUNNING;
    tmp.current.type = ACT_NONE;
    tmp.land = 0;
    tmp.bittyScale = 0.72f;                        // a bit smaller, so he doesn't hide the crates

    if (rm->walking)
    {
        float hops = rm->walked / CELL;
        tmp.current.type = ACT_MOVE;
        tmp.current.dir = rm->facing > 0 ? EAST : WEST;
        tmp.lastResult = 1;
        tmp.actionLength = 1;
        tmp.actionTimer = 1 - (hops - floorf(hops));
    }
    else if (rm->jobAt < rm->jobCount && rm->jobTime > 0)
    {
        const RoomJob *j = &rm->jobs[rm->jobAt];
        float len = JobLength(j->kind);
        tmp.actionLength = 1;
        tmp.actionTimer = 1 - Clamp01(rm->jobTime / len);
        if (j->kind == JOB_PUT) tmp.current.type = ACT_WRITE;
        else if (j->kind == JOB_SCAN || j->kind == JOB_PICK) tmp.current.type = ACT_READ;
        else if (j->kind == JOB_ASK && rm->jobTime > len * 0.35f)
        {
            // the answer: a nod for YES, a shake of the head for NO
            float k = (rm->jobTime - len * 0.35f) / (len * 0.65f);
            tmp.actionTimer = 1 - fmodf(k * 2, 1.0f);
            tmp.current.type = j->value ? ACT_READ : ACT_SCAN;
            tmp.current.dir = NORTH;
            if (j->value) tmp.mood = MOOD_HAPPY;
        }
    }
    if (rm->crashFall)
    {
        tmp.state = WORLD_SEGFAULT;
        tmp.glitch = FALL_SECONDS - rm->crash;
        if (tmp.glitch < 0) tmp.glitch = 0;
    }
    if (rm->result[0] && rm->resultTime > 0.2f && w->mood == MOOD_HAPPY)
    {
        tmp.state = WORLD_WON;
        tmp.winTime = rm->resultTime + 0.6f;      // happy little hops
    }
    DrawBitty(&tmp, t);
}

// Where the value Bitty is carrying floats (over his head, or on its way down into a slot)
static Vector3 HeldPos(const World *w, float t)
{
    const RoomState *rm = &w->room;
    Vector3 head = { rm->x, 1.25f + sinf(t * 4) * 0.04f, 0.0f };
    if (rm->jobAt < rm->jobCount && rm->jobs[rm->jobAt].kind == JOB_PUT && !rm->walking && rm->jobTime > 0)
    {
        float k = Smooth(rm->jobTime / JobLength(JOB_PUT));
        Vector3 to = SlotPos(w, rm->jobs[rm->jobAt].slot);
        to.y += 0.45f;
        float lift = sinf(k * PI_F) * 0.4f;
        return (Vector3){ head.x + (to.x - head.x) * k, head.y + (to.y - head.y) * k + lift, head.z + (to.z - head.z) * k };
    }
    return head;
}

static Color CrateColor(const TestDataRow *d, int i)
{
    if (d->isChar && d->shown[i] == 0) return (Color){ 150, 52, 74, 255 };     // '\0': the end of the string
    return d->isChar ? (Color){ 48, 128, 140, 255 } : (Color){ 52, 92, 178, 255 };
}

// How far a crate is still dropping into place (new test data)
static float CrateDrop(const World *w, int row, int i)
{
    float k = w->room.dropIn - 0.04f * i - 0.1f * row;
    if (k <= 0) return 0;
    return k * k * 6;
}

static void DrawFloor(const World *w)
{
    const Theme *th = ThemeOf(w);
    const TestDisplay *td = &w->test;
    float left, right;
    RoomSpan(w, &left, &right);
    Color floorA = { 92, 118, 168, 255 }, floorB = { 84, 108, 156, 255 };
    Color under = { 52, 50, 88, 255 };

    // the walkway Bitty walks along
    int tiles = (int)ceilf((right - left) / CELL);
    const RoomState *rm = &w->room;
    for (int k = 0; k < tiles; k++)
    {
        float x = left + (k + 0.5f) * CELL;
        if (rm->crash > 0 && fabsf(x - rm->crashX) < CELL * 0.75f) continue;    // out of bounds: there's nothing here
        DrawStone((Vector3){ x, -0.12f, 0 }, (Vector3){ CELL - 0.04f, 0.2f, 1.0f }, (k % 2) ? floorA : floorB);
        DrawCubeV((Vector3){ x, -0.9f, 0 }, (Vector3){ CELL - 0.1f, 1.4f, 0.9f }, under);
    }
    // a strip of light down the middle of it
    DrawCubeV((Vector3){ (left + right) / 2, 0.005f, 0.42f }, (Vector3){ right - left - 0.1f, 0.01f, 0.03f }, Alpha(th->a, 255));

    // a platform under each row of crates (exactly as long as the array: there is nothing past the end)
    for (int r = 0; r < td->rowCount; r++)
    {
        const TestDataRow *d = &td->rows[r];
        for (int i = 0; i < d->count; i++)
        {
            Vector3 p = CratePos(w, r, (float)i);
            float top = p.y - 0.3f;
            DrawStone((Vector3){ p.x, top - 0.12f, p.z + 0.05f }, (Vector3){ CELL - 0.06f, 0.2f, 1.05f }, (i % 2) ? floorA : floorB);
            DrawCubeV((Vector3){ p.x, top - 0.9f, p.z + 0.05f }, (Vector3){ CELL - 0.12f, 1.4f, 0.95f }, under);
        }
    }

    // the floor under the variable boxes
    if (td->regCount + VisibleWires(w) > 0)
    {
        float bl = BoxesLeft(w);
        int n = (int)ceilf((-bl * 2 + 0.6f) / CELL);
        if (n < 1) n = 1;
        float x0 = -(n * CELL) / 2;
        for (int k = 0; k < n; k++)
        {
            float x = x0 + (k + 0.5f) * CELL;
            DrawStone((Vector3){ x, -0.12f, BoxesZ(w) }, (Vector3){ CELL - 0.04f, 0.2f, 1.0f }, (k % 2) ? floorB : floorA);
            DrawCubeV((Vector3){ x, -0.9f, BoxesZ(w) }, (Vector3){ CELL - 0.1f, 1.4f, 0.9f }, under);
        }
    }
}

static void DrawCrates(const World *w)
{
    const TestDisplay *td = &w->test;
    if (w->room.crash > 0 && td->rowCount > 0)
    {
        Vector3 g = CratePos(w, 0, 0);
        g.x = w->room.crashX;
        DrawCubeWiresV(g, (Vector3){ 0.8f, 0.56f, 0.62f }, (Color){ 220, 90, 255, 255 });
    }
    for (int r = 0; r < td->rowCount; r++)
    {
        const TestDataRow *d = &td->rows[r];
        for (int i = 0; i < d->count; i++)
        {
            Vector3 p = CratePos(w, r, (float)i);
            p.y += CrateDrop(w, r, i);
            Color c = CrateColor(d, i);
            c = Mix(c, (Color){ 110, 230, 255, 255 }, Clamp01(d->readFlash[i] / 0.5f));
            c = Mix(c, (Color){ 255, 160, 70, 255 }, Clamp01(d->writeFlash[i] / 0.6f));
            float bump = d->writeFlash[i] > 0 ? sinf(Clamp01(d->writeFlash[i] / 0.6f) * PI_F) * 0.08f : 0;
            DrawRoundedBox((Vector3){ p.x, p.y + bump, p.z }, (Vector3){ 0.8f, 0.56f, 0.62f }, 0.07f, c);
            // a lighter lid
            DrawCubeV((Vector3){ p.x, p.y + bump + 0.285f, p.z }, (Vector3){ 0.62f, 0.02f, 0.46f }, Mix(c, WHITE, 0.25f));
        }
    }
}

static void DrawDigitBlock(Vector3 p, Color c, float scale)
{
    DrawRoundedBox(p, (Vector3){ 0.3f * scale, 0.36f * scale, 0.3f * scale }, 0.04f * scale, c);
}

// x of digit k (of n) in a box centred on cx
static float DigitX(float cx, int k, int n) { return cx + (k - (n - 1) / 2.0f) * DIGIT_W; }

static void DrawBoxes(const World *w, float t)
{
    const TestDisplay *td = &w->test;
    const RoomState *rm = &w->room;
    Color plinth = { 60, 62, 100, 255 };

    for (int i = 0; i < td->regCount; i++)
    {
        const TestRegister *g = &td->regs[i];
        Vector3 p = BoxPos(w, i);
        float wide = BoxWidth(g);
        Color edge = Mix(plinth, (Color){ 255, 214, 110, 255 }, Clamp01(g->flash / 0.6f));
        if (!g->inScope) edge = (Color){ 40, 40, 56, 255 };
        DrawStone((Vector3){ p.x, 0.12f, p.z }, (Vector3){ wide, 0.24f, 0.72f }, edge);

        float top = 0.27f + 0.18f;
        if (!g->shownKnown)
        {
            DrawDigitBlock((Vector3){ p.x, top, p.z }, (Color){ 120, 70, 80, 255 }, 1);
            continue;
        }
        if (g->isBool)
        {
            Color lamp = g->shown ? (Color){ 110, 240, 150, 255 } : (Color){ 110, 50, 60, 255 };
            DrawSphere((Vector3){ p.x, top + 0.05f, p.z }, 0.2f, lamp);
            continue;
        }
        if (g->isChar)
        {
            DrawDigitBlock((Vector3){ p.x, top, p.z }, (Color){ 48, 150, 160, 255 }, 1.15f);
            continue;
        }

        int d[12];
        int n = DigitsOf(g->shown, d);
        Color gold = g->inScope ? (Color){ 214, 168, 72, 255 } : (Color){ 90, 86, 96, 255 };
        for (int k = 0; k < n; k++)
        {
            float y = top;
            Color c = gold;
            if (rm->digitKind[i] == 2 && rm->digitAnim[i] > 0 && k == n - 1)
                y += Smooth(rm->digitAnim[i] / 0.5f) * 0.9f;             // the new digit drops in
            if (rm->takeAnim[i] > 0 && k == n - 1)
            {
                y += sinf(Clamp01(rm->takeAnim[i] / 0.6f) * PI_F) * 0.35f;  // the last digit lifts out
                c = (Color){ 255, 236, 150, 255 };
            }
            c = Mix(c, (Color){ 255, 236, 160, 255 }, Clamp01(g->flash / 0.6f) * 0.6f);
            DrawDigitBlock((Vector3){ DigitX(p.x, k, n), y, p.z }, d[k] < 0 ? (Color){ 150, 150, 170, 255 } : c, 1);
        }
        // n = n / 10: the old last digit falls off the end and shrinks away
        if (rm->digitKind[i] == 1 && rm->digitAnim[i] > 0)
        {
            float k = 1 - rm->digitAnim[i] / 0.7f;
            float x = DigitX(p.x, n, n + 1) + k * 0.5f;
            DrawDigitBlock((Vector3){ x, top - k * k * 1.6f + sinf(k * PI_F) * 0.3f, p.z + k * 0.3f }, gold, 1 - k * 0.7f);
        }
    }
    (void)t;
}

// The cable for wire k: from the top of its socket, up over the walkway, down into the crate
static Vector3 PlugPos(const World *w, int k)
{
    const TestWire *wire = &w->test.wires[k];
    Vector3 p = CratePos(w, wire->row, wire->shownIndex);
    return (Vector3){ p.x, p.y + 0.34f, p.z + 0.12f };
}

static Vector3 WirePoint(const World *w, int k, float u)
{
    Vector3 p0 = SocketPos(w, k), p3 = PlugPos(w, k);
    p0.y += 0.3f;
    Vector3 c1 = { p0.x, p0.y + 1.9f, p0.z - 0.4f }, c2 = { p3.x, p3.y + 1.7f, p3.z + 0.4f };
    float v = 1 - u;
    float b0 = v * v * v, b1 = 3 * v * v * u, b2 = 3 * v * u * u, b3 = u * u * u;
    return (Vector3){ b0 * p0.x + b1 * c1.x + b2 * c2.x + b3 * p3.x,
                      b0 * p0.y + b1 * c1.y + b2 * c2.y + b3 * p3.y,
                      b0 * p0.z + b1 * c1.z + b2 * c2.z + b3 * p3.z };
}

static void DrawWires(const World *w)
{
    const TestDisplay *td = &w->test;
    for (int k = 0; k < td->wireCount; k++)
    {
        if (!WireVisible(w, k)) continue;
        Color col = WIRE_COLORS[k];
        Color dark = { (unsigned char)(col.r * 0.55f), (unsigned char)(col.g * 0.55f), (unsigned char)(col.b * 0.55f), 255 };
        Vector3 s = SocketPos(w, k), plug = PlugPos(w, k);
        DrawStone((Vector3){ s.x, 0.2f, s.z }, (Vector3){ SOCKET_W, 0.4f, 0.6f }, (Color){ 40, 40, 70, 255 });
        DrawCubeV((Vector3){ s.x, 0.48f, s.z }, (Vector3){ 0.3f, 0.16f, 0.3f }, col);

        Vector3 prev = WirePoint(w, k, 0);
        for (int i = 1; i <= 20; i++)
        {
            Vector3 next = WirePoint(w, k, i / 20.0f);
            DrawCylinderEx(prev, next, 0.04f, 0.04f, 6, dark);
            prev = next;
        }
        DrawCubeV(plug, (Vector3){ 0.26f, 0.14f, 0.2f }, col);
        DrawCubeV((Vector3){ plug.x, plug.y + 0.1f, plug.z }, (Vector3){ 0.1f, 0.08f, 0.1f }, (Color){ 230, 235, 250, 255 });
    }
}

static void DrawHeld(const World *w, float t)
{
    const RoomState *rm = &w->room;
    if (!rm->holding) return;
    Vector3 p = HeldPos(w, t);
    float pop = 1 + (rm->heldPop > 0 ? sinf(rm->heldPop / 0.3f * PI_F) * 0.3f : 0);
    Color c = rm->heldChar ? (Color){ 60, 170, 180, 255 } : (Color){ 230, 180, 80, 255 };
    DrawRoundedBox(p, (Vector3){ 0.42f * pop, 0.34f * pop, 0.34f * pop }, 0.05f, c);
}

// A beam from Bitty's eye to a slot
static void Beam(Camera3D cam, Vector3 from, Vector3 to, Color c, float strength)
{
    for (int i = 0; i <= 12; i++)
    {
        float u = i / 12.0f;
        Vector3 q = { from.x + (to.x - from.x) * u, from.y + (to.y - from.y) * u, from.z + (to.z - from.z) * u };
        Glow(cam, q, 0.12f + u * 0.3f, Alpha(c, strength * (90 + 80 * u)));
    }
    Glow(cam, to, 1.1f, Alpha(c, strength * 110));
}

static Vector3 EyePos(const World *w)
{
    const RoomState *rm = &w->room;
    float a = rm->facingShown * PI_F / 180;
    return (Vector3){ rm->x + sinf(a) * 0.22f, 0.45f, cosf(a) * 0.22f };
}

static void DrawRoomGlows(const World *w, Camera3D cam, float t)
{
    const TestDisplay *td = &w->test;
    const RoomState *rm = &w->room;
    const Theme *th = ThemeOf(w);

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    // Bitty's glow
    Color m = MoodColor(w);
    Glow(cam, (Vector3){ rm->x, 0.3f, 0 }, 2.4f, Alpha(m, 45));

    // crates glow softly; a '\0' glows red so the end of a string stands out
    for (int r = 0; r < td->rowCount; r++)
    {
        const TestDataRow *d = &td->rows[r];
        for (int i = 0; i < d->count; i++)
        {
            Vector3 p = CratePos(w, r, (float)i);
            p.y += CrateDrop(w, r, i);
            Color c = (d->isChar && d->shown[i] == 0) ? (Color){ 255, 80, 110, 255 } : th->a;
            Glow(cam, (Vector3){ p.x, p.y - 0.25f, p.z + 0.3f }, 1.0f, Alpha(c, 30));
            if (d->readFlash[i] > 0) Glow(cam, p, 1.6f, Alpha((Color){ 110, 230, 255, 255 }, 160 * d->readFlash[i] / 0.5f));
            if (d->writeFlash[i] > 0) Glow(cam, p, 1.8f, Alpha((Color){ 255, 170, 80, 255 }, 190 * d->writeFlash[i] / 0.6f));
        }
    }

    // variables flash when they change
    for (int i = 0; i < td->regCount; i++)
    {
        Vector3 p = BoxPos(w, i);
        if (td->regs[i].flash > 0) Glow(cam, p, 1.8f, Alpha((Color){ 255, 220, 120, 255 }, 170 * td->regs[i].flash / 0.6f));
        if (rm->glance[i] > 0) Beam(cam, EyePos(w), p, (Color){ 150, 200, 255, 255 }, 0.5f * Clamp01(rm->glance[i] / 0.2f));
    }

    // the beam from his eye: cyan to look, yellow to change something
    if (rm->ray > 0 && ValidSlot(w, rm->raySlot))
    {
        Color c = rm->rayZap ? (Color){ 255, 220, 100, 255 } : (Color){ 110, 235, 255, 255 };
        Vector3 to = SlotPos(w, rm->raySlot);
        Beam(cam, EyePos(w), to, c, Clamp01(rm->ray / 0.15f));
    }

    // cables: a pulse of current runs along each, and the plug glows (brighter when it has just moved)
    for (int k = 0; k < td->wireCount; k++)
    {
        if (!WireVisible(w, k)) continue;
        Color col = WIRE_COLORS[k];
        float u = fmodf(t * 0.6f + k * 0.31f, 1.0f);
        Glow(cam, WirePoint(w, k, u), 0.3f, Alpha(col, 170));
        for (int i = 0; i <= 10; i++) Glow(cam, WirePoint(w, k, i / 10.0f), 0.18f, Alpha(col, 50));
        float moved = td->wires[k].replug > 0 ? td->wires[k].replug / 0.45f : 0;
        Glow(cam, PlugPos(w, k), 0.6f + moved * 0.9f, Alpha(col, 120 + 135 * moved));
    }

    // the value he's carrying
    if (rm->holding) Glow(cam, HeldPos(w, t), 0.9f, (Color){ 255, 230, 150, 120 });

    // carrying a copy: a dotted arrow from where it came from to where it's going
    if (rm->arrowOn && ValidSlot(w, rm->arrowFrom))
    {
        int put = -1;
        for (int i = rm->jobAt; i < rm->jobCount && put < 0; i++) if (rm->jobs[i].kind == JOB_PUT) put = i;
        if (put >= 0 && ValidSlot(w, rm->jobs[put].slot))
        {
            Vector3 a = SlotPos(w, rm->arrowFrom), b = SlotPos(w, rm->jobs[put].slot);
            a.y += 0.45f;
            b.y += 0.5f;
            Color c = { 255, 220, 110, 255 };
            float d = sqrtf((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
            int dots = 10 + (int)(d * 4);
            for (int i = 0; i <= dots; i++)
            {
                float u = (i + fmodf(t * 2.5f, 1.0f)) / (dots + 1);
                Vector3 q = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u + sinf(u * PI_F) * (1.0f + d * 0.15f), a.z + (b.z - a.z) * u };
                Glow(cam, q, 0.22f, Alpha(c, 200));
            }
            // the arrow head, pointing down into where it goes
            DrawCylinderEx((Vector3){ b.x, b.y + 0.45f, b.z }, (Vector3){ b.x, b.y + 0.05f, b.z }, 0.2f, 0.0f, 10, Alpha(c, 230));
            Glow(cam, (Vector3){ b.x, b.y, b.z }, 1.2f + 0.2f * sinf(t * 8), Alpha(c, 110));
        }
    }

    // out of bounds: a hole into the void where the array should carry on
    if (rm->crash > 0)
    {
        float throb = 0.7f + 0.3f * sinf(t * 4);
        Glow(cam, (Vector3){ rm->crashX, -0.4f, 0 }, 1.8f, Alpha((Color){ 160, 60, 230, 255 }, 160 * throb));
        Glow(cam, (Vector3){ rm->crashX, 0.3f, BACK_Z }, 1.4f, Alpha((Color){ 200, 90, 255, 255 }, 110 * throb));
    }

    // beaming in or out
    if (w->teleport > 0)
    {
        float k = Smooth(w->teleport);
        Color g = Customisation_Glow();
        DrawCylinderEx((Vector3){ rm->x, 0, 0 }, (Vector3){ rm->x, 6.0f, 0 }, 0.12f + 0.3f * k, 0.12f + 0.3f * k, 14,
                       Alpha(g, 150 * k));
        Glow(cam, (Vector3){ rm->x, 0.5f, 0 }, 2.6f * k, Alpha(WHITE, 160 * k));
    }

    // drifting bits of memory in the dark around the room
    float left, right;
    RoomSpan(w, &left, &right);
    for (int i = 0; i < 30; i++)
    {
        float k = fmodf(t * (0.05f + (i % 5) * 0.012f) + i * 0.137f, 1.0f);
        float x = left - 2 + fmodf(i * 7.31f, (right - left) + 4);
        float z = -4.5f + fmodf(i * 3.17f, 8.0f);
        Glow(cam, (Vector3){ x, -1.5f + k * 4.0f, z }, 0.12f + (i % 3) * 0.04f, Alpha((i % 3) ? th->a : th->b, sinf(k * PI_F) * 140));
    }

    DrawParticles(cam);

    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

// A label with a dark rounded backing, so it can be read over anything
static void Tag(Vector3 at, Camera3D cam, int vw, int vh, const char *text, int size, Color col, Color back)
{
    Vector2 s = GetWorldToScreenEx(at, cam, vw, vh);
    int tw = MeasureText(text, size);
    DrawRectangleRounded((Rectangle){ s.x - tw / 2.0f - 5, s.y - size / 2.0f - 3, tw + 10.0f, size + 6.0f }, 0.4f, 6, back);
    DrawText(text, (int)(s.x - tw / 2.0f), (int)(s.y - size / 2.0f), size, col);
}

static void DrawRoomLabels(const World *w, Camera3D cam, int vw, int vh, float t)
{
    const TestDisplay *td = &w->test;
    const RoomState *rm = &w->room;
    Color back = { 8, 10, 24, 170 };

    // crates: the value on the front, the index on the floor in front, the name at the start of the row
    for (int r = 0; r < td->rowCount; r++)
    {
        const TestDataRow *d = &td->rows[r];
        if (d->count == 0) continue;
        for (int i = 0; i < d->count; i++)
        {
            Vector3 p = CratePos(w, r, (float)i);
            p.y += CrateDrop(w, r, i);
            const char *v = ValueText(d->shown[i], d->isChar);
            DrawLabel((Vector3){ p.x, p.y, p.z + 0.34f }, cam, vw, vh, v, 30, WHITE);
            if (d->count > 1)
                DrawLabel((Vector3){ p.x, p.y - 0.29f, p.z + 0.6f }, cam, vw, vh, TextFormat("[%d]", i), 15, (Color){ 190, 210, 255, 255 });
        }
        Vector3 first = CratePos(w, r, 0);
        const char *name = d->label[0] ? d->label : d->name;
        if (d->count == 1)
            Tag((Vector3){ first.x, first.y + 0.62f, first.z }, cam, vw, vh, name, 20, (Color){ 220, 230, 255, 255 }, back);
        else
            Tag((Vector3){ first.x - 1.0f, first.y + 0.05f, first.z }, cam, vw, vh, name[0] == '*' ? name + 1 : name, 20,
                (Color){ 220, 230, 255, 255 }, back);
    }

    // variables: the name above, the value in the blocks
    for (int i = 0; i < td->regCount; i++)
    {
        const TestRegister *g = &td->regs[i];
        Vector3 p = BoxPos(w, i);
        float top = 0.45f;
        Tag((Vector3){ p.x, 1.05f, p.z }, cam, vw, vh, g->name, 20, g->inScope ? (Color){ 255, 230, 160, 255 } : (Color){ 140, 140, 160, 255 }, back);
        if (!g->inScope) DrawLabel((Vector3){ p.x, 0.02f, p.z + 0.45f }, cam, vw, vh, g->waiting ? "waiting" : "out of scope", 10, (Color){ 150, 150, 175, 255 });
        if (!g->shownKnown) { DrawLabel((Vector3){ p.x, top, p.z + 0.16f }, cam, vw, vh, "?", 20, (Color){ 255, 150, 150, 255 }); continue; }
        if (g->isBool) { DrawLabel((Vector3){ p.x, top + 0.38f, p.z + 0.1f }, cam, vw, vh, g->shown ? "true" : "false", 14, WHITE); continue; }
        if (g->isChar) { DrawLabel((Vector3){ p.x, top, p.z + 0.2f }, cam, vw, vh, TextFormat("'%s'", CharText(g->shown)), 20, WHITE); continue; }

        int d[12];
        int n = DigitsOf(g->shown, d);
        for (int k = 0; k < n; k++)
        {
            float y = top;
            if (rm->digitKind[i] == 2 && rm->digitAnim[i] > 0 && k == n - 1) y += Smooth(rm->digitAnim[i] / 0.5f) * 0.9f;
            if (rm->takeAnim[i] > 0 && k == n - 1) y += sinf(Clamp01(rm->takeAnim[i] / 0.6f) * PI_F) * 0.35f;
            DrawLabel((Vector3){ DigitX(p.x, k, n), y, p.z + 0.17f }, cam, vw, vh, d[k] < 0 ? "-" : TextFormat("%d", d[k]), 22,
                      (Color){ 40, 26, 6, 255 });
        }
        if (rm->digitKind[i] == 1 && rm->digitAnim[i] > 0)
        {
            int od[12];
            int on = DigitsOf(rm->digitOld[i], od);
            float k = 1 - rm->digitAnim[i] / 0.7f;
            float x = DigitX(p.x, n, n + 1) + k * 0.5f;
            if (k < 0.7f)
                DrawLabel((Vector3){ x, top - k * k * 1.6f + sinf(k * PI_F) * 0.3f, p.z + 0.17f + k * 0.3f }, cam, vw, vh,
                          TextFormat("%d", od[on - 1]), 18, (Color){ 40, 26, 6, 255 });
        }
    }

    // sockets: the pointer's name, and what it points at
    for (int k = 0; k < td->wireCount; k++)
    {
        if (!WireVisible(w, k)) continue;
        const TestWire *wire = &td->wires[k];
        Vector3 s = SocketPos(w, k);
        Tag((Vector3){ s.x, 0.95f, s.z }, cam, vw, vh, wire->name, 16, WIRE_COLORS[k], back);
        const TestDataRow *d = &td->rows[wire->row];
        const char *name = d->label[0] ? d->label : d->name;
        if (name[0] == '*') name++;
        const char *where = d->count > 1 ? TextFormat("-> %s[%d]", name, wire->index) : TextFormat("-> %s", name);
        DrawLabel((Vector3){ s.x, 0.2f, s.z + 0.32f }, cam, vw, vh, where, 12, WIRE_COLORS[k]);
    }

    // the element that isn't there
    if (rm->crash > 0 && td->rowCount > 0)
    {
        const TestDataRow *d = &td->rows[0];
        const char *name = d->label[0] ? d->label : d->name;
        if (name[0] == '*') name++;
        int index = rm->crashX < CratePos(w, 0, 0).x ? -1 : d->count;
        Vector3 g = CratePos(w, 0, 0);
        g.x = rm->crashX;
        DrawLabel((Vector3){ g.x, g.y, g.z + 0.34f }, cam, vw, vh, "???", 20, (Color){ 230, 150, 255, 255 });
        Tag((Vector3){ g.x, g.y + 0.75f, g.z }, cam, vw, vh, TextFormat("%s[%d] doesn't exist", name, index), 14,
            (Color){ 240, 170, 255, 255 }, (Color){ 30, 8, 40, 220 });
    }

    // the value he's carrying
    if (rm->holding)
    {
        Vector3 p = HeldPos(w, t);
        DrawLabel((Vector3){ p.x, p.y, p.z + 0.2f }, cam, vw, vh, ValueText(rm->held, rm->heldChar), 20, (Color){ 30, 20, 5, 255 });
    }

    // what the function gave back
    if (rm->result[0] && rm->resultTime > 0.15f)
    {
        Vector3 p = { rm->x, 1.9f + sinf(t * 3) * 0.05f, 0 };
        Color c = w->mood == MOOD_ERROR ? (Color){ 255, 130, 140, 255 } : (Color){ 150, 255, 190, 255 };
        Tag(p, cam, vw, vh, rm->result, 18, c, (Color){ 8, 10, 24, 220 });
    }
}

// Bitty's speech bubble: what he's doing, in words. For an if / while / for
// it holds the question instead, with the values put in, and the answer.
static bool Asking(const RoomState *rm)
{
    return rm->jobAt < rm->jobCount && rm->jobs[rm->jobAt].kind == JOB_ASK && rm->question[0];
}

static void DrawBubble(Rectangle box, Vector2 head, float pop)
{
    Color paper = { 250, 248, 240, (unsigned char)(250 * pop) };
    float tip = head.x;
    if (tip < box.x + 14) tip = box.x + 14;
    if (tip > box.x + box.width - 14) tip = box.x + box.width - 14;
    DrawTriangle((Vector2){ tip - 9, box.y + box.height - 1 }, (Vector2){ head.x, box.y + box.height + 13 },
                 (Vector2){ tip + 9, box.y + box.height - 1 }, paper);
    DrawRectangleRounded(box, 0.35f, 8, paper);
}

static void DrawSpeech(const World *w, Camera3D cam, int vw, int vh)
{
    const RoomState *rm = &w->room;
    if (w->teleport > 0) return;
    Vector2 head = GetWorldToScreenEx((Vector3){ rm->x, rm->holding ? 1.75f : 1.25f, 0 }, cam, vw, vh);
    Color ink = { 30, 32, 48, 255 };

    if (Asking(rm))
    {
        float tJob = rm->jobTime;
        bool same = strcmp(rm->question, rm->questionValues) == 0;
        const char *q1 = TextFormat("%s %s ?", rm->questionKind ? "Keep going while" : "Is", rm->question);
        const char *q2 = TextFormat("%s ?", rm->questionValues);
        const char *ans = rm->answer ? "YES!" : "NO!";
        int s1 = 17, s2 = 30, s3 = 28;
        while (s1 > 12 && MeasureText(q1, s1) > vw - 60) s1--;
        while (s2 > 14 && MeasureText(q2, s2) + 120 > vw - 60) s2--;
        int w1 = MeasureText(q1, s1), w2 = same ? 0 : MeasureText(q2, s2), w3 = MeasureText("YES!", s3) + 26;
        int row2 = w2 + w3 + (same ? 0 : 18);
        float bw = (float)(w1 > row2 ? w1 : row2) + 28, bh = s1 + s3 + 34.0f;
        float bx = head.x - bw / 2, by = head.y - bh - 16;
        if (bx < 8) bx = 8;
        if (bx + bw > vw - 8) bx = vw - 8 - bw;
        if (by < 98) by = 98;
        float pop = tJob < 0.12f ? tJob / 0.12f : 1;
        DrawBubble((Rectangle){ bx, by, bw, bh }, head, pop);
        DrawText(q1, (int)(bx + (bw - w1) / 2), (int)by + 9, s1, (Color){ 90, 96, 130, 255 });
        float x2 = bx + (bw - row2) / 2, y2 = by + 16 + s1;
        if (!same) DrawText(q2, (int)x2, (int)(y2 + (s3 + 8 - s2) / 2), s2, ink);
        if (tJob > 0.5f)    // the answer pops up after a moment
        {
            Color c = rm->answer ? (Color){ 50, 190, 100, 255 } : (Color){ 220, 66, 76, 255 };
            float k = tJob - 0.5f < 0.12f ? (tJob - 0.5f) / 0.12f : 1;
            Rectangle pill = { x2 + (same ? 0 : w2 + 18), y2 - (1 - k) * 8, (float)w3, s3 + 8.0f };
            DrawRectangleRounded(pill, 0.5f, 8, c);
            DrawText(ans, (int)(pill.x + (w3 - MeasureText(ans, s3)) / 2), (int)pill.y + 4, s3, WHITE);
        }
        return;
    }

    if (!rm->caption[0]) return;
    if (rm->jobAt >= rm->jobCount && rm->captionTime > 3.0f && rm->crash <= 0 && !w->test.waitingForStep && !rm->result[0]) return;
    int size = 19;
    while (size > 13 && MeasureText(rm->caption, size) > vw - 60) size--;
    int tw = MeasureText(rm->caption, size);
    float bw = tw + 26.0f, bh = size + 18.0f;
    float bx = head.x - bw / 2, by = head.y - bh - 16;
    if (bx < 8) bx = 8;
    if (bx + bw > vw - 8) bx = vw - 8 - bw;
    if (by < 98) by = 98;
    float pop = rm->captionTime < 0.12f ? rm->captionTime / 0.12f : 1;
    DrawBubble((Rectangle){ bx, by, bw, bh }, head, pop);
    DrawText(rm->caption, (int)(bx + 13), (int)(by + 9), size, (Color){ ink.r, ink.g, ink.b, (unsigned char)(255 * pop) });
}

// The line of code Bitty is acting out, along the bottom; and in step mode, what to press
static void DrawCodeBanner(const World *w, int vw, int vh)
{
    const RoomState *rm = &w->room;
    if (rm->code[0] && rm->codeLine > 0)
    {
        const char *label = TextFormat("Line %d", rm->codeLine);
        int ls = 16, cs = 20;
        while (cs > 12 && MeasureText(rm->code, cs) + MeasureText(label, ls) + 60 > vw - 20) cs--;
        float bw = MeasureText(label, ls) + MeasureText(rm->code, cs) + 50.0f;
        Rectangle box = { (vw - bw) / 2, vh - 42.0f, bw, 34 };
        DrawRectangleRounded(box, 0.4f, 8, (Color){ 10, 12, 26, 235 });
        DrawRectangleRoundedLinesEx(box, 0.4f, 8, 2, (Color){ 120, 170, 255, 200 });
        DrawText(label, (int)box.x + 14, (int)box.y + 10, ls, (Color){ 130, 160, 220, 255 });
        DrawText(rm->code, (int)box.x + 30 + MeasureText(label, ls), (int)box.y + (34 - cs) / 2, cs, (Color){ 255, 236, 170, 255 });
    }
    if (w->test.waitingForStep)
    {
        const char *hint = "Press STEP (F10) for the next line,  or RUN to go by itself";
        int hs = 16, hw = MeasureText(hint, hs);
        float pulse = 0.65f + 0.35f * sinf((float)GetTime() * 5);
        Rectangle box = { (vw - hw) / 2.0f - 12, vh - 76.0f, hw + 24.0f, 26 };
        DrawRectangleRounded(box, 0.5f, 8, (Color){ 40, 110, 170, (unsigned char)(230 * pulse) });
        DrawText(hint, (vw - hw) / 2, (int)box.y + 5, hs, WHITE);
    }
}

static Camera3D RoomCamera(const World *w, float aspect)
{
    const RoomState *rm = &w->room;
    const TestDisplay *td = &w->test;
    float left, right;
    RoomSpan(w, &left, &right);

    // how many units across we want to see: the whole room if it fits, otherwise follow Bitty
    float rows = SideBySide(td) ? 1.0f : (float)(td->rowCount > 0 ? td->rowCount : 1);
    float wantW = right - left + 0.6f;
    if (wantW > 11.5f) wantW = 11.5f;
    if (wantW < 7.0f) wantW = 7.0f;
    float dist = wantW / (2 * tanf(22.5f * PI_F / 180) * aspect) * 1.02f;
    float depth = 5.4f + (rows - 1) * 1.1f;
    if (dist < depth) dist = depth;

    float half = wantW / 2 - 0.3f;
    float want = (left + right) / 2;
    if (right - left + 0.6f > wantW)
    {
        want = rm->x;
        if (want < left + half) want = left + half;
        if (want > right - half) want = right - half;
    }
    static float camX = 0;
    static bool camSet = false;
    if (!camSet || rm->mode == ROOM_BEAM_IN) { camX = want; camSet = true; }
    camX += (want - camX) * (1 - expf(-GetFrameTime() * 3.0f));

    float t = (float)GetTime();
    float midZ = (BACK_Z - (rows - 1) * ROW_STEP + FRONT_Z) / 2;
    Camera3D cam = { 0 };
    cam.target = (Vector3){ camX, 0.2f + (rows - 1) * 0.25f, midZ - 0.15f };
    float steep = td->rowCount > 0 ? 0.84f : 0.7f;
    cam.position = (Vector3){ camX + sinf(t * 0.23f) * 0.15f, dist * steep, midZ + dist * (1.44f - steep) };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 45;
    cam.projection = CAMERA_PERSPECTIVE;
    if (w->shake > 0) cam.position.x += sinf(t * 55) * 0.05f * (w->shake / 0.35f);
    return cam;
}

void Room_Draw(const World *w, int vw, int vh)
{
    const RoomState *rm = &w->room;
    const Theme *th = ThemeOf(w);
    float t = (float)GetTime();
    Camera3D cam = RoomCamera(w, (float)vw / vh);

    World_SetBittyScreen(w, (Vector3){ rm->x, 0.95f, 0 }, cam, vw, vh);
    World_DrawBackground(w, vw, vh);
    BeginMode3D(cam);

    // lights: Bitty, and a few hanging over the crates and the boxes
    Light lights[MAX_LIGHTS];
    int n = 0;
    Color cool = { 200, 225, 255, 255 };
    lights[n++] = (Light){ (Vector3){ rm->x, 1.0f, 0.3f }, Mix(cool, MoodColor(w), 0.45f), 1.9f, 4.6f };
    float left, right;
    RoomSpan(w, &left, &right);
    int across = (int)((right - left) / 3.2f) + 1;
    if (across > 5) across = 5;
    for (int i = 0; i < across && n < MAX_LIGHTS - 1; i++)
    {
        float x = left + (right - left) * (i + 0.5f) / across;
        lights[n++] = (Light){ (Vector3){ x, 2.2f, BACK_Z }, th->a, 0.9f, 4.2f };
    }
    if (w->test.regCount > 0 && n < MAX_LIGHTS)
        lights[n++] = (Light){ (Vector3){ 0, 2.0f, BoxesZ(w) + 0.4f }, (Color){ 255, 220, 160, 255 }, 0.9f, 4.5f };

    World_BeginLit(w, lights, n, cam, t, false);
    DrawFloor(w);
    DrawCrates(w);
    DrawBoxes(w, t);
    DrawWires(w);
    World_EndLit();

    DrawRoomBitty(w, t);
    DrawHeld(w, t);
    DrawRoomGlows(w, cam, t);
    EndMode3D();

    DrawRoomLabels(w, cam, vw, vh, t);
    DrawSpeech(w, cam, vw, vh);
    DrawCodeBanner(w, vw, vh);
}
