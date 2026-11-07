#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include "../include/value.h"
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/interpreter.h"

/* ===== interpreter.c =====
   Lihat interpreter.h untuk catatan arsitektur. */

/* ===== State posisi & kontrol alur eksekusi (dipakai parser.c) ===== */
int pos = 0;
int should_return = 0;
Value return_value;
int should_break = 0;
int should_continue = 0;

/* ===== Error handling ===== */
jmp_buf* errorJumpStack = NULL;
int errorJumpCount = 0, errorJumpCap = 0;
char lastErrorMsg[1024];
int lastErrorLine = 0;

void pushErrorJump(jmp_buf* buf) {
    if (errorJumpCount >= errorJumpCap) {
        errorJumpCap = errorJumpCap ? errorJumpCap*2 : 8;
        errorJumpStack = realloc(errorJumpStack, sizeof(jmp_buf)*errorJumpCap);
    }
    memcpy(&errorJumpStack[errorJumpCount], buf, sizeof(jmp_buf));
    errorJumpCount++;
}
void popErrorJump() { if (errorJumpCount>0) errorJumpCount--; }

void errorAt(int line, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[900];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (errorJumpCount > 0) {
        snprintf(lastErrorMsg, sizeof(lastErrorMsg), "%s", buf);
        lastErrorLine = line;
        longjmp(errorJumpStack[errorJumpCount-1], 1);
    }
    fprintf(stderr, "\n*** ERROR pada baris %d: %s ***\n", line, buf);
    exit(1);
}

/* ===== Pre-scan: daftarkan fungsi & struct sebelum eksekusi ===== */
void preScanFunctions() {
    for (int i=0;i<tokenCount;i++) {
        if (tokens[i].type==TOK_FUNGSI) {
            if (tokens[i+1].type != TOK_NAMA) continue; /* lambda anonim, skip */
            char* fname = tokens[i+1].text;
            int p = i+2;
            if (tokens[p].type==TOK_KURUNG_BUKA) p++;
            char* params[16]; int pc=0;
            while (tokens[p].type != TOK_KURUNG_TUTUP) {
                if (tokens[p].type==TOK_NAMA && pc<16) params[pc++] = strdup(tokens[p].text);
                p++;
            }
            if (findFuncDef(fname)==NULL) addFuncDef(fname, params, pc, p+1);
        }
    }
}

void preScanStructs() {
    for (int i=0;i<tokenCount;i++) {
        if (tokens[i].type==TOK_OBJEK) {
            char* name = tokens[i+1].text;
            int p = i+2;
            if (tokens[p].type==TOK_KURAWAL_BUKA) p++;
            char* fields[64]; int fc=0;
            char* methodNamesLocal[32]; int methodIdxLocal[32]; int mc=0;
            while (tokens[p].type != TOK_KURAWAL_TUTUP) {
                if (tokens[p].type==TOK_METODE) {
                    p++;
                    char* mname = strdup(tokens[p].text);
                    p++;
                    char* params[16]; int pc=0;
                    if (tokens[p].type==TOK_KURUNG_BUKA) p++;
                    while (tokens[p].type != TOK_KURUNG_TUTUP) {
                        if (tokens[p].type==TOK_NAMA && pc<16) params[pc++] = strdup(tokens[p].text);
                        p++;
                    }
                    int bodyStart = p+1;
                    int bodyEnd = findMatchingBrace(bodyStart);
                    char synthName[256];
                    snprintf(synthName, sizeof(synthName), "%s#%s", name, mname);
                    int fidx = addFuncDef(synthName, params, pc, bodyStart);
                    if (mc<32) { methodNamesLocal[mc]=mname; methodIdxLocal[mc]=fidx; mc++; }
                    p = bodyEnd + 1;
                    continue;
                }
                if (tokens[p].type==TOK_NAMA && fc<64) fields[fc++] = strdup(tokens[p].text);
                p++;
            }
            if (findStructDef(name)==NULL) {
                char** fcopy = malloc(sizeof(char*)*(fc>0?fc:1));
                for (int k=0;k<fc;k++) fcopy[k]=fields[k];
                char** mcopy = malloc(sizeof(char*)*(mc>0?mc:1));
                int* micopy = malloc(sizeof(int)*(mc>0?mc:1));
                for (int k=0;k<mc;k++) { mcopy[k]=methodNamesLocal[k]; micopy[k]=methodIdxLocal[k]; }
                addStructDef(name, fcopy, fc, mcopy, micopy, mc);
            }
        }
    }
}

/* ===== Entry point utama ===== */
void run() {
    /* currentScope sudah = globalScope dari initScopes() di main.c; top
       level program harus jalan LANGSUNG di globalScope (bukan
       child-nya), supaya var global benar-benar masuk globalScope dan
       terlihat oleh fungsi bernama lain (yang selalu lexical terhadap
       globalScope). Lihat callUserFunction di parser.c. */
    preScanFunctions();
    preScanStructs();
    pos = 0;
    while (curTok()->type != TOK_AKHIR) parseStatement();
}
