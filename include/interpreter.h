#ifndef BAHASAKU_INTERPRETER_H
#define BAHASAKU_INTERPRETER_H

#include <setjmp.h>
#include "value.h"

/* ===== interpreter.h =====
   Error handling (coba/tangkap via setjmp/longjmp), state eksekusi
   global (pos, should_return/break/continue), dan entry point run()
   yang memindai fungsi/struct lalu mengeksekusi token stream dari awal.

   Catatan arsitektur: bahasaku TIDAK memiliki tahap "evaluator terpisah"
   dalam arti klasik -- parsing dan eksekusi menyatu dalam fungsi-fungsi
   parser.c (parseStatement, parseExpression, dst) yang langsung
   menjalankan efek sambil mem-parse. File ini hanya menaungi state
   global dan mekanisme kontrol alur lintas-fungsi (error jump stack,
   flag return/break/continue) yang dipakai bersama oleh parser.c. */

/* ===== State posisi & kontrol alur, dipakai lintas file ===== */
extern int pos;
extern int should_return;
extern Value return_value;
extern int should_break;
extern int should_continue;

/* ===== Error handling: coba/tangkap via setjmp/longjmp =====
   Ketika errorJumpCount > 0 (sedang di dalam blok coba{}), errorAt()
   melompat ke titik setjmp terdekat alih-alih exit(1). lastErrorMsg
   dan lastErrorLine membawa pesan error ke blok tangkap(e). */
extern jmp_buf* errorJumpStack;
extern int errorJumpCount, errorJumpCap;
extern char lastErrorMsg[1024];
extern int lastErrorLine;

void pushErrorJump(jmp_buf* buf);
void popErrorJump(void);
void errorAt(int line, const char* fmt, ...);

/* ===== Entry point interpreter ===== */
void preScanFunctions(void);
void preScanStructs(void);
void run(void);

#endif
