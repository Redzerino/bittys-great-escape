// cloud.c - Firebase Realtime Database over curl (see cloud.h)
#include "cloud.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static char folder[512];
static char url[400];
static char labsEnd[32];

// ===========================================================================
// Settings
// ===========================================================================
void Cloud_Init(const char *gameFolder)
{
    snprintf(folder, sizeof folder, "%s", gameFolder ? gameFolder : "");
    url[0] = labsEnd[0] = '\0';
    char path[600];
    snprintf(path, sizeof path, "%sbitty_cloud.txt", folder);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "url ", 4) == 0)
        {
            snprintf(url, sizeof url, "%s", line + 4);
            size_t n = strlen(url);
            while (n > 0 && (url[n - 1] == '/' || url[n - 1] == ' ')) url[--n] = '\0';
            bool local = strncmp(url, "http://127.0.0.1", 16) == 0 || strncmp(url, "http://localhost", 16) == 0;   // for testing
            if (strncmp(url, "https://", 8) != 0 && !local) url[0] = '\0';     // only https, never anything odd
            for (size_t i = 0; i < n; i++)                           // nothing that could break the command line
                if (url[i] == '"' || url[i] == '\'' || url[i] == ' ' || url[i] == '&' || url[i] == '|') { url[0] = '\0'; break; }
        }
        else if (strncmp(line, "labs_end ", 9) == 0)
            snprintf(labsEnd, sizeof labsEnd, "%s", line + 9);
    }
    fclose(f);
}

bool        Cloud_Configured(void) { return url[0] != '\0'; }
const char *Cloud_Url(void)        { return url; }
const char *Cloud_LabsEnd(void)    { return labsEnd; }

bool Cloud_ValidName(const char *name)
{
    size_t n = strlen(name);
    if (n < 3 || n > 16) return false;
    for (size_t i = 0; i < n; i++)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

// ===========================================================================
// SHA-256 (so passwords are never stored as plain text)
// ===========================================================================
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void Sha256(const unsigned char *data, size_t len, unsigned char out[32])
{
    uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    size_t total = ((len + 9 + 63) / 64) * 64;
    unsigned char *m = calloc(total, 1);
    if (!m) { memset(out, 0, 32); return; }
    memcpy(m, data, len);
    m[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) m[total - 1 - i] = (unsigned char)(bits >> (8 * i));
    for (size_t off = 0; off < total; off += 64)
    {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)m[off + 4 * i] << 24 | (uint32_t)m[off + 4 * i + 1] << 16 | (uint32_t)m[off + 4 * i + 2] << 8 | m[off + 4 * i + 3];
        for (int i = 16; i < 64; i++)
        {
            uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++)
        {
            uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    free(m);
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (unsigned char)(h[i] >> (24 - 8 * j));
}

void Cloud_HashPassword(const char *name, const char *password, char out[65])
{
    char buf[256];
    snprintf(buf, sizeof buf, "bitty:%s:%s", name, password);
    unsigned char d[32];
    Sha256((const unsigned char *)buf, strlen(buf), d);
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", d[i]);
    out[64] = '\0';
}

// ===========================================================================
// base64 (the save file goes inside a JSON string)
// ===========================================================================
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *Base64Encode(const char *in, size_t len)
{
    char *out = malloc(((len + 2) / 3) * 4 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t v = (uint32_t)(unsigned char)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)(unsigned char)in[i + 1] << 8;
        if (i + 2 < len) v |= (unsigned char)in[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = i + 1 < len ? B64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? B64[v & 63] : '=';
    }
    out[o] = '\0';
    return out;
}

static char *Base64Decode(const char *in)
{
    size_t len = strlen(in);
    char *out = malloc(len / 4 * 3 + 4);
    if (!out) return NULL;
    size_t o = 0;
    uint32_t v = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++)
    {
        const char *p = strchr(B64, in[i]);
        if (!p || !*p) continue;                // '=' and anything else
        v = (v << 6) | (uint32_t)(p - B64);
        bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (char)((v >> bits) & 0xFF); }
    }
    out[o] = '\0';
    return out;
}

// ===========================================================================
// Running curl without a console window popping up
// ===========================================================================
static int RunCommand(const char *cmd, bool wait)
{
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    char *line = _strdup(cmd);
    if (!line) return -1;
    BOOL ok = CreateProcessA(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(line);
    if (!ok) return -1;
    DWORD code = 0;
    if (wait)
    {
        WaitForSingleObject(pi.hProcess, 20000);
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
#else
    char buf[4400];
    snprintf(buf, sizeof buf, wait ? "%s >/dev/null 2>&1" : "(%s) >/dev/null 2>&1 &", cmd);
    return system(buf);
#endif
}

static const char *Curl(void)
{
#ifdef _WIN32
    return "curl.exe";
#else
    return "curl";
#endif
}

// "HTTP/1.1 200 OK" in the saved headers -> 200 (0 if there's no answer)
static int StatusFrom(const char *headerFile)
{
    FILE *f = fopen(headerFile, "r");
    if (!f) return 0;
    char line[256];
    int status = 0;
    while (fgets(line, sizeof line, f))
        if (strncmp(line, "HTTP/", 5) == 0)
        {
            const char *sp = strchr(line, ' ');
            if (sp) status = atoi(sp + 1);          // the last status line wins (after redirects)
        }
    fclose(f);
    return status;
}

static char *ReadAll(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *s = malloc((size_t)n + 1);
    if (!s) { fclose(f); return NULL; }
    size_t got = fread(s, 1, (size_t)n, f);
    s[got] = '\0';
    fclose(f);
    return s;
}

// value of "key":"..." (or "key":123) in a flat JSON object
static bool JsonString(const char *json, const char *key, char *out, size_t size)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p == '"')
    {
        p++;
        size_t n = 0;
        while (*p && *p != '"' && n + 1 < size) out[n++] = *p++;
        out[n] = '\0';
        return true;
    }
    size_t n = 0;
    while (*p && *p != ',' && *p != '}' && n + 1 < size) out[n++] = *p++;
    out[n] = '\0';
    return true;
}

static char *JsonStringAlloc(const char *json, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p += strlen(pat);
    const char *e = strchr(p, '"');
    if (!e) return NULL;
    char *out = malloc((size_t)(e - p) + 1);
    if (!out) return NULL;
    memcpy(out, p, (size_t)(e - p));
    out[e - p] = '\0';
    return out;
}

// ===========================================================================
// Users
// ===========================================================================
void Cloud_FreeUser(CloudUser *u) { if (u) { free(u->save); u->save = NULL; } }

CloudResult Cloud_GetUser(const char *name, CloudUser *out)
{
    memset(out, 0, sizeof *out);
    if (!Cloud_Configured()) return CLOUD_OFFLINE;
    if (!Cloud_ValidName(name)) return CLOUD_ERROR;

    char body[600], head[600], cmd[4096];
    snprintf(body, sizeof body, "%sbitty_cloud_in.json", folder);
    snprintf(head, sizeof head, "%sbitty_cloud_in.hdr", folder);
    remove(body);
    remove(head);
    snprintf(cmd, sizeof cmd, "%s -s --max-time 12 -D \"%s\" -o \"%s\" \"%s/users/%s.json\"", Curl(), head, body, url, name);
    RunCommand(cmd, true);

    int status = StatusFrom(head);
    remove(head);
    if (status == 0) { remove(body); return CLOUD_OFFLINE; }
    char *json = ReadAll(body);
    remove(body);
    if (status != 200 || !json) { free(json); return CLOUD_ERROR; }
    if (strncmp(json, "null", 4) == 0) { free(json); return CLOUD_NOT_FOUND; }

    JsonString(json, "pw", out->pwHash, sizeof out->pwHash);
    char updated[32] = "0";
    JsonString(json, "updated", updated, sizeof updated);
    out->updated = atol(updated);
    char *b64 = JsonStringAlloc(json, "save");
    if (b64) { out->save = Base64Decode(b64); free(b64); }
    free(json);
    return out->pwHash[0] ? CLOUD_OK : CLOUD_ERROR;
}

static bool WriteBody(const char *path, const CloudUser *user)
{
    char *b64 = Base64Encode(user->save ? user->save : "", user->save ? strlen(user->save) : 0);
    if (!b64) return false;
    FILE *f = fopen(path, "wb");
    if (!f) { free(b64); return false; }
    fprintf(f, "{\"pw\":\"%s\",\"save\":\"%s\",\"updated\":%ld}", user->pwHash, b64, (long)time(NULL));
    fclose(f);
    free(b64);
    return true;
}

static CloudResult Put(const char *name, const CloudUser *user, bool wait)
{
    if (!Cloud_Configured()) return CLOUD_OFFLINE;
    if (!Cloud_ValidName(name)) return CLOUD_ERROR;
    char body[600], head[600], cmd[4096];
    // background uploads get their own file, so a new save can't overwrite one still being sent
    snprintf(body, sizeof body, "%sbitty_cloud_%s.json", folder, wait ? "out" : "bg");
    snprintf(head, sizeof head, "%sbitty_cloud_out.hdr", folder);
    if (!WriteBody(body, user)) return CLOUD_ERROR;
    if (!wait)
    {
        snprintf(cmd, sizeof cmd, "%s -s --max-time 20 -X PUT --data-binary \"@%s\" -o \"%sbitty_cloud_bg.txt\" \"%s/users/%s.json\"",
                 Curl(), body, folder, url, name);
        RunCommand(cmd, false);
        return CLOUD_OK;
    }
    remove(head);
    snprintf(cmd, sizeof cmd, "%s -s --max-time 12 -X PUT --data-binary \"@%s\" -D \"%s\" -o \"%sbitty_cloud_out.txt\" \"%s/users/%s.json\"",
             Curl(), body, head, folder, url, name);
    RunCommand(cmd, true);
    int status = StatusFrom(head);
    remove(head);
    remove(body);
    char done[600];
    snprintf(done, sizeof done, "%sbitty_cloud_out.txt", folder);
    remove(done);
    if (status == 0) return CLOUD_OFFLINE;
    return status == 200 ? CLOUD_OK : CLOUD_ERROR;
}

CloudResult Cloud_PutUser(const char *name, const CloudUser *user) { return Put(name, user, true); }
void Cloud_PutUserInBackground(const char *name, const CloudUser *user) { Put(name, user, false); }
