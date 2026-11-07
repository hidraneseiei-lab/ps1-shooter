#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "../include/value.h"
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/interpreter.h"
#include "../include/builtin.h"
#include "../include/ast.h"

/* ===== main.c =====
   Entry point bahasaku.
   Mode:
     ./myrun file.nv [arg...]   jalankan file
     ./myrun --parse file.nv    uji parser AST (ast_parser.c), tanpa eksekusi
     ./myrun                    REPL interaktif
   Impor: file digabung sebelum tokenize (preprocessImports). */

#define BAHASAKU_VERSI "v18"
#define REPL_BUF_MAX   65536
#define REPL_LINE_MAX  8192

/* ---------- daftar string ---------- */

typedef struct { char** items; int count; int cap; } StrList;

void strListInit(StrList* l) {
    l->cap = 8;
    l->count = 0;
    l->items = malloc(sizeof(char*) * l->cap);
    if (!l->items) { fprintf(stderr, "*** ERROR: memori habis ***\n"); exit(1); }
}

int strListContains(StrList* l, const char* s) {
    for (int i = 0; i < l->count; i++)
        if (strcmp(l->items[i], s) == 0) return 1;
    return 0;
}

void strListAdd(StrList* l, const char* s) {
    if (l->count >= l->cap) {
        int baru = l->cap * 2;
        char** p = realloc(l->items, sizeof(char*) * baru);
        if (!p) { fprintf(stderr, "*** ERROR: memori habis ***\n"); exit(1); }
        l->items = p;
        l->cap = baru;
    }
    l->items[l->count++] = strdup(s);
}

/* ---------- buffer teks dinamis ---------- */

typedef struct { char* data; size_t len; size_t cap; } Teks;

static void teksReserve(Teks* t, size_t tambah) {
    size_t butuh = t->len + tambah + 1;
    if (butuh <= t->cap) return;
    size_t baru = t->cap ? t->cap : 256;
    while (baru < butuh) baru *= 2;
    char* p = realloc(t->data, baru);
    if (!p) { fprintf(stderr, "*** ERROR: memori habis ***\n"); exit(1); }
    t->data = p;
    t->cap = baru;
}

static void teksAdd(Teks* t, const char* s, size_t n) {
    teksReserve(t, n);
    memcpy(t->data + t->len, s, n);
    t->len += n;
    t->data[t->len] = 0;
}

static void teksAddChar(Teks* t, char c) { teksAdd(t, &c, 1); }

/* ---------- file dan path ---------- */

char* readWholeFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0;
    fclose(f);
    return buf;
}

/* Folder dari path file: "proyek/a.nv" -> "proyek", "a.nv" -> "." */
static char* dirOf(const char* path) {
    const char* slash = strrchr(path, '/');
    if (!slash) return strdup(".");
    if (slash == path) return strdup("/");
    size_t n = (size_t)(slash - path);
    char* d = malloc(n + 1);
    memcpy(d, path, n);
    d[n] = 0;
    return d;
}

/* Path impor dicari relatif ke folder file induk */
static char* resolvePath(const char* baseDir, const char* fname) {
    if (fname[0] == '/') return strdup(fname);
    size_t n = strlen(baseDir) + 1 + strlen(fname) + 1;
    char* p = malloc(n);
    snprintf(p, n, "%s/%s", baseDir, fname);
    return p;
}

/* ---------- Impor ---------- */

char* preprocessImports(const char* src, StrList* included, const char* baseDir);

static void prosesImpor(Teks* out, const char* fname, StrList* included, const char* baseDir) {
    char* path = resolvePath(baseDir, fname);
    char* isi = readWholeFile(path);
    if (!isi) {
        /* cadangan: cari relatif ke folder kerja, seperti perilaku lama */
        free(path);
        path = strdup(fname);
        isi = readWholeFile(path);
    }
    if (!isi) {
        fprintf(stderr, "*** ERROR: tidak bisa impor file '%s' ***\n", fname);
        exit(1);
    }
    if (strListContains(included, path)) { free(isi); free(path); return; }
    strListAdd(included, path);

    char* dir = dirOf(path);
    char* hasil = preprocessImports(isi, included, dir);
    free(dir);
    free(isi);
    free(path);

    teksAdd(out, hasil, strlen(hasil));
    teksAddChar(out, '\n');
    free(hasil);
}

char* preprocessImports(const char* src, StrList* included, const char* baseDir) {
    Teks out = {0};
    size_t len = strlen(src);
    size_t i = 0;
    int dalamString = 0;

    while (i < len) {
        char c = src[i];

        if (dalamString) {
            if (c == '\\' && i + 1 < len) {       /* escape: salin apa adanya, termasuk \" */
                teksAdd(&out, src + i, 2);
                i += 2;
                continue;
            }
            if (c == '"') dalamString = 0;
            teksAddChar(&out, c);
            i++;
            continue;
        }

        if (c == '/' && i + 1 < len && src[i + 1] == '/') {   /* komentar sampai akhir baris */
            while (i < len && src[i] != '\n') { teksAddChar(&out, src[i]); i++; }
            continue;
        }

        if (c == '"') {
            dalamString = 1;
            teksAddChar(&out, c);
            i++;
            continue;
        }

        if (isalpha((unsigned char)c) || c == '_') {
            size_t start = i, j = i;
            while (j < len && (isalnum((unsigned char)src[j]) || src[j] == '_')) j++;
            size_t wlen = j - start;

            if (wlen == 5 && strncmp(src + start, "Impor", 5) == 0) {
                size_t k = j;
                while (k < len && isspace((unsigned char)src[k])) k++;
                if (k < len && src[k] == '"') {
                    k++;
                    size_t fstart = k;
                    while (k < len && src[k] != '"') k++;
                    size_t flen = k - fstart;
                    char* fname = malloc(flen + 1);
                    memcpy(fname, src + fstart, flen);
                    fname[flen] = 0;
                    if (k < len) k++;                 /* lewati '"' penutup */
                    while (k < len && isspace((unsigned char)src[k])) k++;
                    if (k < len && src[k] == ';') k++;

                    prosesImpor(&out, fname, included, baseDir);
                    free(fname);
                    i = k;
                    continue;
                }
            }

            teksAdd(&out, src + start, wlen);
            i = j;
            continue;
        }

        teksAddChar(&out, c);
        i++;
    }
    return out.data ? out.data : strdup("");
}

/* ---------- mode-mode ---------- */

static int jalankanParse(const char* path) {
    char* src = readWholeFile(path);
    if (!src) { printf("Tidak bisa membuka file: %s\n", path); return 1; }

    char* dir = dirOf(path);
    StrList included;
    strListInit(&included);
    char* processed = preprocessImports(src, &included, dir);
    free(src);
    free(dir);

    tokenize(processed);
    Pernyataan* prog = uraiProgram(0);
    printf("Parse OK: %d statement di level atas\n", prog->as.block.count);

    free(processed);
    return 0;
}

static int jalankanFile(const char* path) {
    char* src = readWholeFile(path);
    if (!src) { printf("Tidak bisa membuka file: %s\n", path); return 1; }

    char* dir = dirOf(path);
    StrList included;
    strListInit(&included);
    char* processed = preprocessImports(src, &included, dir);
    free(src);
    free(dir);

    tokenize(processed);
    run();
    free(processed);
    return 0;
}

static int jalankanRepl(void) {
    static char buffer[REPL_BUF_MAX];
    char line[REPL_LINE_MAX];
    int exitRequested = 0;

    printf("Mode interaktif %s. Ketik 'Keluar' untuk berhenti.\n", BAHASAKU_VERSI);
    initTokens();

    while (!exitRequested) {
        size_t used = 0;
        int depth = 0, inString = 0, first = 1, gotLine = 0, terlalu = 0;
        buffer[0] = 0;

        while (1) {
            printf(first ? "bahasaku> " : "........> ");
            fflush(stdout);
            if (!fgets(line, sizeof line, stdin)) { exitRequested = 1; break; }

            size_t l = strlen(line);
            if (l > 0 && line[l - 1] == '\n') line[--l] = 0;

            if (first) {
                char* p = line;
                while (*p == ' ' || *p == '\t') p++;
                if (strcmp(p, "Keluar") == 0) { exitRequested = 1; break; }
                if (*p == 0) break;
            }

            if (used + l + 2 > REPL_BUF_MAX) { terlalu = 1; break; }

            memcpy(buffer + used, line, l);
            used += l;
            buffer[used++] = '\n';
            buffer[used] = 0;
            gotLine = 1;

            for (size_t k = 0; k < l; k++) {
                char c = line[k];
                if (inString) {
                    if (c == '\\') { k++; continue; }
                    if (c == '"') inString = 0;
                } else {
                    if (c == '"') inString = 1;
                    else if (c == '{') depth++;
                    else if (c == '}') depth--;
                }
            }

            first = 0;
            if (depth <= 0) break;
        }

        if (exitRequested) break;
        if (terlalu) {
            fprintf(stderr, "*** ERROR: input melebihi %d byte, dibuang ***\n", REPL_BUF_MAX);
            continue;
        }
        if (!gotLine) continue;

        int chunkStart = tokenCount;
        tokenizeAppend(buffer);
        preScanFunctions();
        preScanStructs();
        pos = chunkStart;
        while (curTok()->type != TOK_AKHIR) parseStatement();
    }
    return 0;
}

/* ---------- entry point ---------- */

int main(int argc, char** argv) {
    srand((unsigned int)time(NULL));
    initScopes();
    return_value = makeNumber(0);

    if (argc >= 3 && strcmp(argv[1], "--parse") == 0)
        return jalankanParse(argv[2]);

    if (argc >= 2) {
        g_argc = argc - 2;
        g_argv = (g_argc > 0) ? argv + 2 : NULL;
        return jalankanFile(argv[1]);
    }

    g_argc = 0;
    g_argv = NULL;
    return jalankanRepl();
}