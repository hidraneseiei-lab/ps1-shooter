#ifndef BAHASAKU_VALUE_H
#define BAHASAKU_VALUE_H

/* ===== value.h =====
   Tipe data inti bahasaku: Value (nilai runtime), Scope (environment
   lexical, heap-allocated dengan parent chain sejak v0.13), Closure
   (instance fungsi + environment tertangkap), FuncDef (definisi kode
   fungsi/metode), StructDef (definisi objek + daftar metodenya).

   Catatan arsitektur penting: Scope dan Closure TIDAK PERNAH dibebaskan
   (lihat releaseScope/releaseClosure di value.c) -- ini keputusan sadar
   sejak v0.13 demi menghindari use-after-free/double-free, karena jalur
   penyalinan Value (assignment, argumen, return, penyimpanan dalam
   array/objek/kamus) belum diaudit penuh untuk reference counting.
   Jangan aktifkan pembebasan memori di sini tanpa audit penuh itu dulu. */

typedef struct Scope Scope;
typedef struct Closure Closure;

typedef struct Value {
    int type; /* 0=angka 1=teks 2=array 3=objek 4=null 5=fungsi 6=kamus */
    double num;
    char* str;
    struct Value* arr;
    int arrLen, arrCap;
    char** fieldNames;
    struct Value* fields;
    int fieldCount;
    char* className;
    Closure* closure; /* non-NULL jika Value ini sebuah closure sejati */
    char** dictKeys;
    struct Value* dictVals;
    int dictCount, dictCap;
} Value;

typedef struct { char* name; Value value; int isConst; } Var;

/* v0.13: Scope heap-allocated, bukan array flat global, dengan parent
   chain untuk lexical scoping sejati. */
struct Scope {
    Var* vars;
    int count;
    int cap;
    Scope* parent;
    int refCount;
};

/* v0.13: Closure memisahkan "definisi kode" (FuncDef, di bawah) dari
   "instance fungsi + environment tertangkap". Satu FuncDef (misal satu
   lambda dalam source code) bisa punya banyak Closure instance berbeda,
   masing-masing dengan env sendiri. */
struct Closure {
    int funcIndex; /* index ke funcDefs[] */
    Scope* env;    /* environment tempat lambda ini dibuat */
    int refCount;
};

typedef struct {
    char* name;
    char* params[16];
    int paramCount;
    int bodyStart; /* posisi token '{' pembuka tubuh fungsi */
} FuncDef;

/* v0.17: StructDef menyimpan field DAN daftar metode (nama + index ke
   funcDefs[], metode disimpan sebagai FuncDef biasa dengan nama sintetis
   "NamaStruct#NamaMetode"). */
typedef struct {
    char* name;
    char** fieldNames;
    int fieldCount;
    char** methodNames;
    int* methodFuncIndex;
    int methodCount;
} StructDef;

/* ===== Variabel global bersama (didefinisikan di value.c) ===== */
extern Scope* globalScope;
extern Scope* currentScope;

extern FuncDef* funcDefs;
extern int funcCount, funcCap;
extern int anonCounter;

extern StructDef* structDefs;
extern int structDefCount, structDefCap;

typedef struct { int bodyStart; int funcIndex; } LambdaCacheEntry;
extern LambdaCacheEntry* lambdaCache;
extern int lambdaCacheCount, lambdaCacheCap;

/* ===== Konstruktor Value ===== */
Value makeNumber(double n);
Value makeString(const char* s);
Value makeNull(void);
Value makeFunctionRef(const char* s);
Value makeArray(void);
void arrPush(Value* a, Value item);
Value makeClosureValue(int funcIndex, Scope* env);

/* ===== Kamus (dictionary/map) ===== */
Value makeDict(void);
int dictFindIndex(Value* d, const char* key);
void dictSet(Value* d, const char* key, Value val);
Value* dictFindPtr(Value* d, const char* key);
Value* dictGetOrInsertPtr(Value* d, const char* key);
int dictRemove(Value* d, const char* key);

/* ===== Operasi umum Value ===== */
int truthy(Value v);
void printValue(Value v);
int valueCompareDefault(Value a, Value b);
Value deepCopyValue(Value v);
Value concatValues(Value left, Value right);

/* ===== Scope (environment lexical) ===== */
Scope* createScope(Scope* parent);
void releaseScope(Scope* s);
void initScopes(void);
void pushScope(void);
void popScope(void);
Var* findInScope(Scope* s, const char* name);
Value* getVarPtr(const char* name);
int isVarConst(const char* name);
void declareVar(const char* name, Value val);
void declareConst(const char* name, Value val);

/* ===== Closure ===== */
Closure* createClosure(int funcIndex, Scope* env);
void retainClosure(Closure* c);
void releaseClosure(Closure* c);

/* ===== FuncDef registry ===== */
int addFuncDef(const char* name, char** params, int pc, int bodyStart);
FuncDef* findFuncDef(const char* name);
int findCachedLambda(int bodyStart);
void cacheLambda(int bodyStart, int funcIndex);

/* ===== StructDef registry ===== */
void addStructDef(const char* name, char** fields, int fcount, char** methodNames, int* methodFuncIndex, int methodCount);
StructDef* findStructDef(const char* name);

#endif
