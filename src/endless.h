// endless.h - ENDLESS MODE: a never-ending run of random challenges.
//
// Every topic from the exam coverage sheet (Exam 1 + Exam 2) has a
// generator here. Each one rolls a twist (a different rule, a different
// pattern, the other end of the array...) and fresh test data, so no two
// challenges are quite the same. There are no hints and no gaps: the player
// writes the function from scratch.
//
// How the tests get their answers: each generator also writes a hidden
// reference solution. Endless_Make() runs it in the VM on the test data and
// records what it returned / printed / left in the arrays. Then it checks the
// challenge is fair: the reference passes every test and the starter code
// fails at least one (if not, it rolls again).

#ifndef ENDLESS_H
#define ENDLESS_H

#include <stdbool.h>
#include "levels.h"

int         Endless_TopicCount(void);
const char *Endless_TopicName(int topic);     // "Leap years", "Linked lists"...
const char *Endless_TopicSource(int topic);   // which exam it comes from

// Build challenge number 'number' (shown in the title) from 'topic'.
// 'seed' picks the twist and the test data. Returns false only if every
// re-roll failed (it shouldn't); 'error' says why.
bool Endless_Make(Level *out, int topic, unsigned seed, int number, char *error, int errorSize);

// Pick a topic, avoiding the last few that were played.
int  Endless_PickTopic(unsigned seed);

// Best streak, kept in its own small file next to the save.
void Endless_LoadBest(const char *path);
int  Endless_Best(void);
void Endless_ReportStreak(int streak);    // saves it if it beats the best

#endif
