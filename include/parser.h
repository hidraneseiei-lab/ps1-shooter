#ifndef BAHASAKU_PARSER_H
#define BAHASAKU_PARSER_H

#include "value.h"
#include "lexer.h"

/* ===== parser.h =====
   Parser bahasaku: token-stream walking, bukan AST-walking dan bukan
   bytecode/VM. Setiap fungsi parseX() di bawah membaca token dari
   tokens[pos] (lihat lexer.h) dan LANGSUNG MENGEKSEKUSI efeknya sambil
   mem-parse -- tidak ada tahap membangun pohon sintaks dulu. Ini
   sengaja disatukan (bukan dipecah jadi "parser" vs "evaluator"
   terpisah) karena itu memang bagaimana arsitektur aslinya bekerja;
   memaksakan pemisahan AST-style di sini hanya akan menambah lapisan
   tanpa manfaat nyata untuk interpreter sekecil ini.

   Fungsi pemanggilan fungsi (callUserFunction dkk) juga di sini karena
   parseStatement/parseExpression adalah satu-satunya pemanggilnya dan
   mereka saling rekursif erat dengan parser. callFunctionOrBuiltin
   (daftar builtin) ada di builtin.h/builtin.c -- terpisah karena isinya
   murni daftar fungsi bawaan, bukan logika parsing. */

Token* curTok(void);

/* ===== Ekspresi (precedence climbing, tiap level satu fungsi) ===== */
Value parseExpression(void);
Value parseLogicalAnd(void);
Value parseComparison(void);
Value parseBitOr(void);
Value parseBitXor(void);
Value parseBitAnd(void);
Value parseShift(void);
Value parseAdditive(void);
Value parseTerm(void);
Value parseUnary(void);
Value parsePrimary(void);
Value applyPostfixChain(Value v);

/* ===== Statement ===== */
void parseStatement(void);

/* ===== Utilitas posisi token ===== */
int findMatchingBrace(int openPos);
int findAssignSplit(void);
int findCompoundAssign(void);
void skipIfChain(void);
Value* resolveDesignator(int* outLine);

/* ===== Pemanggilan fungsi/closure/metode =====
   callUserFunctionEnvFull adalah inti dari semuanya: parentEnv
   menentukan lexical scope (globalScope untuk fungsi bernama biasa,
   closure->env untuk closure sejati), selfVal mengikat 'Ini' saat
   memanggil metode objek. */
Value callUserFunctionEnvFull(FuncDef* fd, Value* args, int argc, Scope* parentEnv, Value* selfVal);
Value callUserFunctionEnv(FuncDef* fd, Value* args, int argc, Scope* parentEnv);
Value callUserFunction(FuncDef* fd, Value* args, int argc);
Value callCallable(Value fn, Value* args, int argc, int line);
int tryCallMethod(Value obj, const char* methodName, int line, Value* outResult);

/* Didefinisikan di builtin.c, dipanggil dari parser.c (parsePrimary,
   callFunctionOrBuiltin sendiri juga memanggil balik parseExpression
   dkk -- saling rekursif lintas file, karena itu dideklarasikan di
   sini juga). */
Value callFunctionOrBuiltin(char* name, int line);

#endif
