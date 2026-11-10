// explain.h - says what a line of C does, in plain English.
// Used when the mouse rests on a line in the Code panel:
//
//   for (int i = 0; i < 11; i++) {   ->  "Repeat the code in the { } 11 times. i counts 0, 1, 2... up to 10."
//   if (read() > 5) {                ->  "Only run the code in the { } if the number on my tile is bigger than 5."

#ifndef EXPLAIN_H
#define EXPLAIN_H

#include <stdbool.h>

// 'source' is the whole program (to tell pointers from numbers), 'line' is 1-based.
// Returns false for lines with nothing to say (blank lines, comments).
bool Explain_Line(const char *source, int line, char *out, int size);

#endif
