// save.c - remembers the player's progress. See save.h.
//
// File format (plain text, so it's easy to look at):
//
//   BITTY SAVE 1
//   current 2-5_digit_peeler.txt
//   done 1-1_wake_up.txt
//   code 2-5_digit_peeler.txt 57        <- the next 57 bytes are the code
//   int Reversed(int number) { ... }

#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAVE_MAX_LEVELS 128
#define SAVE_NAME_MAX   64

typedef struct {
    char  file[SAVE_NAME_MAX];
    bool  done;
    char *code;     // malloc'd, or NULL
} SaveEntry;

static SaveEntry entries[SAVE_MAX_LEVELS];
static int       entryCount = 0;
static char      current[SAVE_NAME_MAX] = "";
static char      savePath[512] = "bitty_save.txt";

// Level files that were renamed when the levels were put in teaching order.
// Old saves still use the old names, so they're translated when loaded.
static const char *RENAMED[][2] = {
    { "2-0_first_function.txt",     "2-1_first_function.txt" },
    { "2-1_data_types.txt",         "2-2_data_types.txt" },
    { "2-4_register_updates.txt",   "2-3_register_updates.txt" },
    { "2-5_access_code.txt",        "2-4_access_code.txt" },
    { "2-1_digit_peeler.txt",       "2-5_digit_peeler.txt" },
    { "2-2_binary_decoder.txt",     "2-6_binary_decoder.txt" },
    { "3-1_leap_year_gate.txt",     "3-4_leap_year_gate.txt" },
    { "4-1_prime_lock.txt",         "4-5_prime_lock.txt" },
    { "5-1_search_the_bay.txt",     "6-4_search_the_bay.txt" },
    { "5-2_mirror_mirror.txt",      "6-5_mirror_mirror.txt" },
    { "8-1_swap_wires.txt",         "8-3_swap_wires.txt" },
    { "8-2_measure_the_string.txt", "8-5_measure_the_string.txt" },
};

static void NewName(char *name, size_t size)
{
    for (size_t i = 0; i < sizeof RENAMED / sizeof RENAMED[0]; i++)
        if (strcmp(name, RENAMED[i][0]) == 0) { snprintf(name, size, "%s", RENAMED[i][1]); return; }
}

static SaveEntry *Find(const char *file, bool create)
{
    for (int i = 0; i < entryCount; i++)
        if (strcmp(entries[i].file, file) == 0) return &entries[i];
    if (!create || entryCount >= SAVE_MAX_LEVELS) return NULL;

    SaveEntry *e = &entries[entryCount++];
    memset(e, 0, sizeof *e);
    snprintf(e->file, sizeof e->file, "%s", file);
    return e;
}

static char *CopyString(const char *s, size_t n)
{
    char *c = malloc(n + 1);
    if (!c) return NULL;
    memcpy(c, s, n);
    c[n] = '\0';
    return c;
}

void Save_Load(const char *path)
{
    snprintf(savePath, sizeof savePath, "%s", path);

    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 4000000) { fclose(f); return; }
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return; }
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);

    const char *p = buf, *end = buf + got;
    while (p < end)
    {
        // one line
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl) nl = end;
        char line[256];
        size_t n = (size_t)(nl - p);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        if (n > 0 && line[n - 1] == '\r') line[n - 1] = '\0';
        p = (nl < end) ? nl + 1 : end;

        char name[SAVE_NAME_MAX];
        long bytes;
        if (sscanf(line, "current %63s", name) == 1)
        {
            NewName(name, sizeof name);
            snprintf(current, sizeof current, "%s", name);
        }
        else if (sscanf(line, "done %63s", name) == 1)
        {
            NewName(name, sizeof name);
            SaveEntry *e = Find(name, true);
            if (e) e->done = true;
        }
        else if (sscanf(line, "code %63s %ld", name, &bytes) == 2)
        {
            if (bytes < 0 || bytes > end - p) break;   // damaged file: keep what we have
            NewName(name, sizeof name);
            SaveEntry *e = Find(name, true);
            if (e)
            {
                free(e->code);
                e->code = CopyString(p, (size_t)bytes);
            }
            p += bytes;
            if (p < end && *p == '\n') p++;
        }
    }
    free(buf);
}

bool Save_Write(void)
{
    FILE *f = fopen(savePath, "wb");
    if (!f) return false;

    fprintf(f, "BITTY SAVE 1\n");
    if (current[0]) fprintf(f, "current %s\n", current);
    for (int i = 0; i < entryCount; i++)
        if (entries[i].done) fprintf(f, "done %s\n", entries[i].file);
    for (int i = 0; i < entryCount; i++)
    {
        if (!entries[i].code) continue;
        size_t len = strlen(entries[i].code);
        fprintf(f, "code %s %lu\n", entries[i].file, (unsigned long)len);
        fwrite(entries[i].code, 1, len, f);
        fputc('\n', f);
    }
    fclose(f);
    return true;
}

const char *Save_GetCode(const char *levelFile)
{
    SaveEntry *e = Find(levelFile, false);
    return e ? e->code : NULL;
}

void Save_SetCode(const char *levelFile, const char *code)
{
    SaveEntry *e = Find(levelFile, true);
    if (!e) return;
    free(e->code);
    e->code = CopyString(code, strlen(code));
}

bool Save_IsDone(const char *levelFile)
{
    SaveEntry *e = Find(levelFile, false);
    return e && e->done;
}

void Save_SetDone(const char *levelFile)
{
    SaveEntry *e = Find(levelFile, true);
    if (e) e->done = true;
}

const char *Save_GetCurrent(void)
{
    return current;
}

void Save_SetCurrent(const char *levelFile)
{
    snprintf(current, sizeof current, "%s", levelFile);
}
