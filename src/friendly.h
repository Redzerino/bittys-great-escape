// friendly.h - turns compiler errors and warnings into one plain sentence
// from Bitty, points at the exact spot, and (when it's sure) offers a FIX.
//
//   "missing ; at the end of this statement."
//      -> "Every instruction ends with a ; like a full stop. This one needs one."  [FIX: add the ;]
//   "there's no function called Move()"
//      -> "I don't know Move. Did you mean move? Capital letters matter in C!"     [FIX: Move -> move]

#ifndef FRIENDLY_H
#define FRIENDLY_H

#include <stdbool.h>

typedef struct {
    char say[220];          // what Bitty says
    int  line, col;         // where to point (1-based; col 0 = the whole line)
    bool warning;           // the code still runs (e.g. = inside an if)

    bool hasFix;
    char fixLabel[40];      // the button: "Add the ;"
    int  fixStart, fixEnd;  // the bytes of the source the fix replaces (start == end: insert)
    char fixText[40];       // what goes there
} FriendlyError;

// message/line/col come from VmBuildResult. Returns false if there's nothing to say.
bool Friendly_Explain(const char *source, const char *message, int line, int col, FriendlyError *out);

// A warning from VmBuildResult.warnings (the code still runs)
bool Friendly_Warning(const char *source, const char *warning, int line, FriendlyError *out);

// The source with the fix applied (out must hold EDITOR_MAX_TEXT bytes). Returns where the cursor should go.
int Friendly_ApplyFix(const char *source, const FriendlyError *fe, char *out, int size);

#endif
