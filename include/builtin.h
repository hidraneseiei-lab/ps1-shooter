#ifndef BAHASAKU_BUILTIN_H
#define BAHASAKU_BUILTIN_H

#include "value.h"

/* ===== builtin.h =====
   Semua fungsi bawaan bahasaku (string, array, matematika, kamus,
   JSON, file I/O, dll) hidup di callFunctionOrBuiltin() -- satu fungsi
   besar berisi rangkaian if/strcmp per nama builtin. Dipanggil dari
   parser.c (parsePrimary) saat menemukan IDENT diikuti '('.

   Variabel global g_argc/g_argv (argumen command-line program .nv,
   BUKAN argv dari interpreter sendiri) didefinisikan di main.c dan
   dipakai builtin Argumen()/ArgumenJumlah(). */

extern int g_argc;
extern char** g_argv;

/* callFunctionOrBuiltin sendiri sudah dideklarasikan di parser.h,
   karena parser.c (parsePrimary) juga memanggilnya -- didefinisikan
   di builtin.c. */

/* Konversi Value <-> JSON, dan util format string ala printf sederhana;
   dipakai builtin KeJSON/DariJSON/Format. */
char* valueToJSON(Value v);
Value parseJSONValue(const char* s, int* p);
char* formatString(const char* fmt, Value* args, int argc);

#endif
