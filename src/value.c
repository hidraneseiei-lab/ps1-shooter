#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/value.h"

/* ===== value.c =====
   Implementasi tipe data inti bahasaku. Lihat value.h untuk catatan
   arsitektur penting (terutama soal kebijakan memori Scope/Closure). */

/* ===== Variabel global ===== */
Scope* globalScope = NULL;
Scope* currentScope = NULL;

FuncDef* funcDefs = NULL;
int funcCount = 0, funcCap = 0;
int anonCounter = 0;

StructDef* structDefs = NULL;
int structDefCount = 0, structDefCap = 0;

LambdaCacheEntry* lambdaCache = NULL;
int lambdaCacheCount = 0, lambdaCacheCap = 0;

/* ===== Konstruktor Value dasar ===== */
Value makeNumber(double n) { Value v; memset(&v,0,sizeof(v)); v.type=0; v.num=n; return v; }
Value makeString(const char* s) { Value v; memset(&v,0,sizeof(v)); v.type=1; v.str=strdup(s?s:""); return v; }
Value makeNull() { Value v; memset(&v,0,sizeof(v)); v.type=4; return v; }
Value makeFunctionRef(const char* s) { Value v; memset(&v,0,sizeof(v)); v.type=5; v.str=strdup(s); return v; }

Value makeArray() {
    Value v; memset(&v,0,sizeof(v));
    v.type=2; v.arrCap=4; v.arr=malloc(sizeof(Value)*v.arrCap); v.arrLen=0;
    return v;
}

void arrPush(Value* a, Value item) {
    if (a->arrLen >= a->arrCap) {
        a->arrCap *= 2;
        a->arr = realloc(a->arr, sizeof(Value) * a->arrCap);
    }
    a->arr[a->arrLen++] = item;
}

Value makeDict() {
    Value v; memset(&v,0,sizeof(v));
    v.type = 6;
    v.dictCap = 4;
    v.dictKeys = malloc(sizeof(char*)*v.dictCap);
    v.dictVals = malloc(sizeof(Value)*v.dictCap);
    v.dictCount = 0;
    return v;
}

int dictFindIndex(Value* d, const char* key) {
    for (int i=0;i<d->dictCount;i++) if (strcmp(d->dictKeys[i], key)==0) return i;
    return -1;
}

void dictSet(Value* d, const char* key, Value val) {
    int idx = dictFindIndex(d, key);
    if (idx != -1) { d->dictVals[idx] = val; return; }
    if (d->dictCount >= d->dictCap) {
        d->dictCap *= 2;
        d->dictKeys = realloc(d->dictKeys, sizeof(char*)*d->dictCap);
        d->dictVals = realloc(d->dictVals, sizeof(Value)*d->dictCap);
    }
    d->dictKeys[d->dictCount] = strdup(key);
    d->dictVals[d->dictCount] = val;
    d->dictCount++;
}

Value* dictFindPtr(Value* d, const char* key) {
    int idx = dictFindIndex(d, key);
    if (idx==-1) return NULL;
    return &d->dictVals[idx];
}

Value* dictGetOrInsertPtr(Value* d, const char* key) {
    int idx = dictFindIndex(d, key);
    if (idx != -1) return &d->dictVals[idx];
    if (d->dictCount >= d->dictCap) {
        d->dictCap *= 2;
        d->dictKeys = realloc(d->dictKeys, sizeof(char*)*d->dictCap);
        d->dictVals = realloc(d->dictVals, sizeof(Value)*d->dictCap);
    }
    d->dictKeys[d->dictCount] = strdup(key);
    d->dictVals[d->dictCount] = makeNull();
    d->dictCount++;
    return &d->dictVals[d->dictCount-1];
}

int dictRemove(Value* d, const char* key) {
    int idx = dictFindIndex(d, key);
    if (idx==-1) return 0;
    free(d->dictKeys[idx]);
    for (int i=idx;i<d->dictCount-1;i++) { d->dictKeys[i]=d->dictKeys[i+1]; d->dictVals[i]=d->dictVals[i+1]; }
    d->dictCount--;
    return 1;
}

int truthy(Value v) {
    if (v.type==0) return v.num != 0;
    if (v.type==1) return v.str && strlen(v.str) > 0;
    if (v.type==2) return v.arrLen > 0;
    if (v.type==3) return 1;
    if (v.type==5) return 1;
    if (v.type==6) return v.dictCount > 0;
    return 0;
}

void printValue(Value v) {
    if (v.type==0) {
        if (v.num == (long)v.num) printf("%ld", (long)v.num);
        else printf("%g", v.num);
    } else if (v.type==1) {
        printf("%s", v.str ? v.str : "");
    } else if (v.type==2) {
        printf("[");
        for (int i=0;i<v.arrLen;i++) { printValue(v.arr[i]); if (i<v.arrLen-1) printf(", "); }
        printf("]");
    } else if (v.type==3) {
        printf("%s{", v.className ? v.className : "Objek");
        for (int i=0;i<v.fieldCount;i++) {
            printf("%s=", v.fieldNames[i]);
            printValue(v.fields[i]);
            if (i<v.fieldCount-1) printf(", ");
        }
        printf("}");
    } else if (v.type==5) {
        printf("<Fungsi:%s>", v.str ? v.str : "?");
    } else if (v.type==6) {
        printf("{");
        for (int i=0;i<v.dictCount;i++) {
            printf("\"%s\": ", v.dictKeys[i]);
            printValue(v.dictVals[i]);
            if (i<v.dictCount-1) printf(", ");
        }
        printf("}");
    } else {
        printf("null");
    }
}

void addStructDef(const char* name, char** fields, int fcount, char** methodNames, int* methodFuncIndex, int methodCount) {
    if (structDefCount >= structDefCap) {
        structDefCap = structDefCap ? structDefCap*2 : 8;
        structDefs = realloc(structDefs, sizeof(StructDef)*structDefCap);
    }
    structDefs[structDefCount].name = strdup(name);
    structDefs[structDefCount].fieldNames = fields;
    structDefs[structDefCount].fieldCount = fcount;
    structDefs[structDefCount].methodNames = methodNames;
    structDefs[structDefCount].methodFuncIndex = methodFuncIndex;
    structDefs[structDefCount].methodCount = methodCount;
    structDefCount++;
}

StructDef* findStructDef(const char* name) {
    for (int i=0;i<structDefCount;i++) if (strcmp(structDefs[i].name,name)==0) return &structDefs[i];
    return NULL;
}

Scope* createScope(Scope* parent) {
    Scope* s = calloc(1, sizeof(Scope));
    if (!s) { fprintf(stderr, "Gagal alokasi Scope\n"); exit(1); }
    s->vars = malloc(sizeof(Var)*8);
    s->cap = 8;
    s->count = 0;
    s->parent = parent;
    s->refCount = 1;
    if (parent) parent->refCount++;
    return s;
}

void releaseScope(Scope* s) {
    if (!s) return;
    if (s->refCount > 0) s->refCount--;
}

void initScopes() {
    globalScope = createScope(NULL);
    currentScope = globalScope;
}

void pushScope() {
    currentScope = createScope(currentScope);
}

void popScope() {
    Scope* old = currentScope;
    currentScope = old->parent;
    releaseScope(old);
}

Var* findInScope(Scope* s, const char* name) {
    for (int i=s->count-1;i>=0;i--) if (strcmp(s->vars[i].name,name)==0) return &s->vars[i];
    return NULL;
}

Value* getVarPtr(const char* name) {
    Scope* s = currentScope;
    while (s) {
        Var* v = findInScope(s, name);
        if (v) return &v->value;
        s = s->parent;
    }
    return NULL;
}

int isVarConst(const char* name) {
    Scope* s = currentScope;
    while (s) {
        Var* v = findInScope(s, name);
        if (v) return v->isConst;
        s = s->parent;
    }
    return 0;
}

void declareVar(const char* name, Value val) {
    Scope* top = currentScope;
    Var* v = findInScope(top, name);
    if (v) { v->value = val; return; }
    if (top->count >= top->cap) { top->cap *= 2; top->vars = realloc(top->vars, sizeof(Var)*top->cap); }
    top->vars[top->count].name = strdup(name);
    top->vars[top->count].value = val;
    top->vars[top->count].isConst = 0;
    top->count++;
}

void declareConst(const char* name, Value val) {
    Scope* top = currentScope;
    Var* v = findInScope(top, name);
    if (v) { v->value = val; v->isConst = 1; return; }
    if (top->count >= top->cap) { top->cap *= 2; top->vars = realloc(top->vars, sizeof(Var)*top->cap); }
    top->vars[top->count].name = strdup(name);
    top->vars[top->count].value = val;
    top->vars[top->count].isConst = 1;
    top->count++;
}

Closure* createClosure(int funcIndex, Scope* env) {
    Closure* c = malloc(sizeof(Closure));
    if (!c) { fprintf(stderr, "Gagal alokasi Closure\n"); exit(1); }
    c->funcIndex = funcIndex;
    c->env = env;
    c->refCount = 1;
    if (env) env->refCount++;
    return c;
}

void retainClosure(Closure* c) { if (c) c->refCount++; }

void releaseClosure(Closure* c) {
    if (!c) return;
    if (c->refCount > 0) c->refCount--;
}

Value makeClosureValue(int funcIndex, Scope* env) {
    Value v; memset(&v,0,sizeof(v));
    v.type = 5;
    v.closure = createClosure(funcIndex, env);
    return v;
}

int addFuncDef(const char* name, char** params, int pc, int bodyStart) {
    if (funcCount >= funcCap) { funcCap = funcCap ? funcCap*2 : 16; funcDefs = realloc(funcDefs, sizeof(FuncDef)*funcCap); }
    funcDefs[funcCount].name = strdup(name);
    for (int i=0;i<pc;i++) funcDefs[funcCount].params[i] = params[i];
    funcDefs[funcCount].paramCount = pc;
    funcDefs[funcCount].bodyStart = bodyStart;
    int idx = funcCount;
    funcCount++;
    return idx;
}

FuncDef* findFuncDef(const char* name) {
    for (int i=0;i<funcCount;i++) if (strcmp(funcDefs[i].name,name)==0) return &funcDefs[i];
    return NULL;
}

int findCachedLambda(int bodyStart) {
    for (int i=0;i<lambdaCacheCount;i++) if (lambdaCache[i].bodyStart==bodyStart) return lambdaCache[i].funcIndex;
    return -1;
}

void cacheLambda(int bodyStart, int funcIndex) {
    if (lambdaCacheCount >= lambdaCacheCap) {
        lambdaCacheCap = lambdaCacheCap ? lambdaCacheCap*2 : 16;
        lambdaCache = realloc(lambdaCache, sizeof(LambdaCacheEntry)*lambdaCacheCap);
    }
    lambdaCache[lambdaCacheCount].bodyStart = bodyStart;
    lambdaCache[lambdaCacheCount].funcIndex = funcIndex;
    lambdaCacheCount++;
}

int valueCompareDefault(Value a, Value b) {
    if (a.type==1 && b.type==1) {
        return strcmp(a.str ? a.str : "", b.str ? b.str : "");
    }
    double x = a.num, y = b.num;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

Value deepCopyValue(Value v) {
    if (v.type==2) {
        Value r = makeArray();
        for (int i=0;i<v.arrLen;i++) arrPush(&r, deepCopyValue(v.arr[i]));
        return r;
    } else if (v.type==3) {
        Value r; memset(&r,0,sizeof(r));
        r.type=3;
        r.className = v.className ? strdup(v.className) : NULL;
        r.fieldCount = v.fieldCount;
        r.fieldNames = malloc(sizeof(char*)*v.fieldCount);
        r.fields = malloc(sizeof(Value)*v.fieldCount);
        for (int i=0;i<v.fieldCount;i++) {
            r.fieldNames[i] = strdup(v.fieldNames[i]);
            r.fields[i] = deepCopyValue(v.fields[i]);
        }
        return r;
    } else if (v.type==6) {
        Value r = makeDict();
        for (int i=0;i<v.dictCount;i++) dictSet(&r, v.dictKeys[i], deepCopyValue(v.dictVals[i]));
        return r;
    } else if (v.type==1) {
        return makeString(v.str ? v.str : "");
    }
    return v;
}

Value concatValues(Value left, Value right) {
    char tmp1[64], tmp2[64];
    const char *s1, *s2;
    if (left.type==1) s1=left.str; else { if(left.num==(long)left.num) sprintf(tmp1,"%ld",(long)left.num); else sprintf(tmp1,"%g",left.num); s1=tmp1; }
    if (right.type==1) s2=right.str; else { if(right.num==(long)right.num) sprintf(tmp2,"%ld",(long)right.num); else sprintf(tmp2,"%g",right.num); s2=tmp2; }
    char* combined = malloc(strlen(s1)+strlen(s2)+1);
    sprintf(combined,"%s%s",s1,s2);
    Value v = makeString(combined);
    free(combined);
    return v;
}
