// endless_test.c - every endless topic makes fair challenges, for lots of seeds
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/endless.h"
#include "../src/levels.h"

static Level lv;

int main(int argc, char **argv)
{
    int per = argc > 1 ? atoi(argv[1]) : 60;
    int bad = 0, made = 0;
    printf("=== Endless mode: %d topics x %d seeds ===\n", Endless_TopicCount(), per);
    for (int t = 0; t < Endless_TopicCount(); t++)
    {
        int fails = 0; char first[300] = "";
        for (int seed = 1; seed <= per; seed++)
        {
            char err[300];
            if (Endless_Make(&lv, t, (unsigned)(seed * 7919 + t), seed, err, sizeof err)) made++;
            else { fails++; if (!first[0]) snprintf(first, sizeof first, "%s", err); }
        }
        printf("  %s %-22s %d/%d%s%s\n", fails ? "FAIL" : "PASS", Endless_TopicName(t), per - fails, per, first[0] ? "   " : "", first);
        bad += fails;
    }
    printf("\n%d challenges made, %d failed\n", made, bad);
    return bad ? 1 : 0;
}
