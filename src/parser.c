#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <ctype.h>
#include "../include/value.h"
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/interpreter.h"

/* ===== parser.c =====
   Lihat parser.h untuk catatan arsitektur token-stream. */

Token* curTok() { return &tokens[pos]; }

int findMatchingBrace(int openPos) {
    int depth = 0, p = openPos;
    while (tokens[p].type != TOK_AKHIR) {
        if (tokens[p].type == TOK_KURAWAL_BUKA) depth++;
        else if (tokens[p].type == TOK_KURAWAL_TUTUP) { depth--; if (depth==0) return p; }
        p++;
    }
    return p;
}

int findAssignSplit() {
    int depth = 0, p = pos;
    while (tokens[p].type != TOK_TITIK_KOMA && tokens[p].type != TOK_AKHIR) {
        TokenType t = tokens[p].type;
        if (t==TOK_KURUNG_BUKA||t==TOK_SIKU_BUKA||t==TOK_KURAWAL_BUKA) depth++;
        else if (t==TOK_KURUNG_TUTUP||t==TOK_SIKU_TUTUP||t==TOK_KURAWAL_TUTUP) depth--;
        else if (t==TOK_SET && depth==0) return p;
        p++;
    }
    return -1;
}

int findCompoundAssign() {
    int depth = 0, p = pos;
    while (tokens[p].type != TOK_TITIK_KOMA && tokens[p].type != TOK_AKHIR) {
        TokenType t = tokens[p].type;
        if (t==TOK_KURUNG_BUKA||t==TOK_SIKU_BUKA||t==TOK_KURAWAL_BUKA) depth++;
        else if (t==TOK_KURUNG_TUTUP||t==TOK_SIKU_TUTUP||t==TOK_KURAWAL_TUTUP) depth--;
        else if (depth==0 && (t==TOK_TAMBAH_SET||t==TOK_KURANG_SET||t==TOK_KALI_SET||
                               t==TOK_BAGI_SET||t==TOK_SISA_SET)) return p;
        p++;
    }
    return -1;
}

Value* resolveDesignator(int* outLine) {
    Token* t = curTok();
    if (t->type != TOK_NAMA) errorAt(t->line, "diharapkan nama variabel");
    char* name = strdup(t->text);
    int line = t->line;
    pos++;
    Value* target = getVarPtr(name);
    if (!target) errorAt(line, "variabel '%s' tidak ditemukan", name);
    while (curTok()->type==TOK_SIKU_BUKA || curTok()->type==TOK_TITIK) {
        if (curTok()->type==TOK_SIKU_BUKA) {
            pos++;
            Value idxv = parseExpression();
            if (curTok()->type==TOK_SIKU_TUTUP) pos++;
            if (target->type == 2) {
                int idx = (int)idxv.num;
                if (idx<0 || idx>=target->arrLen) errorAt(line, "index array di luar batas (idx=%d, panjang=%d)", idx, target->arrLen);
                target = &target->arr[idx];
            } else if (target->type == 6) {
                if (idxv.type != 1) errorAt(line, "kunci kamus harus berupa teks");
                target = dictGetOrInsertPtr(target, idxv.str ? idxv.str : "");
            } else {
                errorAt(line, "'%s' bukan array atau kamus", name);
            }
        } else {
            pos++;
            char* fname = strdup(curTok()->text); pos++;
            if (target->type == 6) {
                target = dictGetOrInsertPtr(target, fname);
            } else if (target->type == 3) {
                int fi=-1;
                for (int k=0;k<target->fieldCount;k++) if (strcmp(target->fieldNames[k],fname)==0){fi=k;break;}
                if (fi==-1) errorAt(line, "field '%s' tidak ditemukan", fname);
                target = &target->fields[fi];
            } else {
                errorAt(line, "bukan objek atau kamus, tidak bisa akses field '%s'", fname);
            }
            free(fname);
        }
    }
    free(name);
    if (outLine) *outLine = line;
    return target;
}

Value applyPostfixChain(Value v) {
    while (curTok()->type==TOK_SIKU_BUKA || curTok()->type==TOK_TITIK) {
        if (curTok()->type==TOK_SIKU_BUKA) {
            int line = curTok()->line;
            pos++;
            Value idxv = parseExpression();
            if (curTok()->type==TOK_SIKU_TUTUP) pos++;
            if (v.type == 2) {
                int idx = (int)idxv.num;
                if (idx<0 || idx>=v.arrLen) errorAt(line, "index di luar batas");
                v = v.arr[idx];
            } else if (v.type == 6) {
                if (idxv.type != 1) errorAt(line, "kunci kamus harus berupa teks");
                Value* p = dictFindPtr(&v, idxv.str ? idxv.str : "");
                v = p ? *p : makeNull();
            } else {
                errorAt(line, "bukan array atau kamus");
            }
        } else {
            int line = curTok()->line;
            pos++;
            char* fname = curTok()->text; pos++;
            if (v.type == 6) {
                Value* p = dictFindPtr(&v, fname);
                v = p ? *p : makeNull();
                continue;
            }
            if (v.type != 3) errorAt(line, "bukan objek atau kamus");
            if (curTok()->type == TOK_KURUNG_BUKA) {
                Value methodResult;
                if (tryCallMethod(v, fname, line, &methodResult)) { v = methodResult; continue; }
            }
            int fi=-1;
            for (int k=0;k<v.fieldCount;k++) if (strcmp(v.fieldNames[k],fname)==0){fi=k;break;}
            if (fi==-1) errorAt(line, "field tidak ada");
            v = v.fields[fi];
        }
    }
    return v;
}

Value callUserFunctionEnvFull(FuncDef* fd, Value* args, int argc, Scope* parentEnv, Value* selfVal) {
    int callerPos = pos;
    Scope* savedScope = currentScope;
    currentScope = createScope(parentEnv);

    if (selfVal) declareVar("Ini", *selfVal);

    for (int i=0;i<fd->paramCount;i++) {
        Value a = (i<argc) ? args[i] : makeNumber(0);
        declareVar(fd->params[i], a);
    }
    int bodyStart = fd->bodyStart;
    int bodyEnd = findMatchingBrace(bodyStart);
    int saved_should_return = should_return;
    Value saved_return_value = return_value;
    int saved_should_break = should_break;
    int saved_should_continue = should_continue;
    should_return = 0;
    should_break = 0;
    should_continue = 0;
    pos = bodyStart + 1;
    while (pos < bodyEnd && !should_return) {
        parseStatement();
    }
    Value result = should_return ? return_value : makeNumber(0);
    should_return = saved_should_return;
    return_value = saved_return_value;
    should_break = saved_should_break;
    should_continue = saved_should_continue;

    Scope* callScope = currentScope;
    currentScope = savedScope;
    releaseScope(callScope);

    pos = callerPos;
    return result;
}

Value callUserFunctionEnv(FuncDef* fd, Value* args, int argc, Scope* parentEnv) {
    return callUserFunctionEnvFull(fd, args, argc, parentEnv, NULL);
}

Value callUserFunction(FuncDef* fd, Value* args, int argc) {
    return callUserFunctionEnv(fd, args, argc, globalScope);
}

Value callCallable(Value fn, Value* args, int argc, int line) {
    if (fn.type == 5) {
        if (fn.closure) {
            FuncDef* fd = &funcDefs[fn.closure->funcIndex];
            return callUserFunctionEnv(fd, args, argc, fn.closure->env);
        } else {
            FuncDef* fd = findFuncDef(fn.str);
            if (!fd) errorAt(line, "fungsi '%s' tidak dikenal", fn.str ? fn.str : "?");
            return callUserFunction(fd, args, argc);
        }
    }
    errorAt(line, "nilai yang diberikan bukan fungsi, tidak bisa dipanggil");
    return makeNull();
}

int tryCallMethod(Value obj, const char* methodName, int line, Value* outResult) {
    if (obj.type != 3 || !obj.className) return 0;
    StructDef* sd = findStructDef(obj.className);
    if (!sd) return 0;
    int fi = -1;
    for (int i=0;i<sd->methodCount;i++) {
        if (strcmp(sd->methodNames[i], methodName)==0) { fi = sd->methodFuncIndex[i]; break; }
    }
    if (fi == -1) return 0;
    pos++;
    Value args[64]; int argc = 0;
    if (curTok()->type != TOK_KURUNG_TUTUP) {
        while (1) {
            args[argc++] = parseExpression();
            if (curTok()->type==TOK_KOMA) { pos++; continue; }
            break;
        }
    }
    if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
    FuncDef* fd = &funcDefs[fi];
    *outResult = callUserFunctionEnvFull(fd, args, argc, globalScope, &obj);
    return 1;
}

Value parsePrimary() {
    Token* t = curTok();
    if (t->type==TOK_ANGKA) { pos++; return makeNumber(t->num); }
    if (t->type==TOK_TEKS) { pos++; return makeString(t->text); }
    if (t->type==TOK_BENAR) { pos++; return makeNumber(1); }
    if (t->type==TOK_SALAH) { pos++; return makeNumber(0); }
    if (t->type==TOK_KURUNG_BUKA) {
        pos++;
        Value v = parseExpression();
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        return applyPostfixChain(v);
    }
    if (t->type==TOK_SIKU_BUKA) {
        pos++;
        Value arr = makeArray();
        if (curTok()->type != TOK_SIKU_TUTUP) {
            while (1) {
                Value item = parseExpression();
                arrPush(&arr, item);
                if (curTok()->type==TOK_KOMA) { pos++; continue; }
                break;
            }
        }
        if (curTok()->type==TOK_SIKU_TUTUP) pos++;
        return applyPostfixChain(arr);
    }
    if (t->type==TOK_KURAWAL_BUKA) {
        pos++;
        Value d = makeDict();
        if (curTok()->type != TOK_KURAWAL_TUTUP) {
            while (1) {
                char* key;
                if (curTok()->type==TOK_TEKS || curTok()->type==TOK_NAMA) {
                    key = strdup(curTok()->text);
                    pos++;
                } else {
                    errorAt(curTok()->line, "kunci kamus harus berupa teks atau nama");
                    key = strdup("");
                }
                if (curTok()->type==TOK_TITIK_DUA) pos++;
                Value val = parseExpression();
                dictSet(&d, key, val);
                free(key);
                if (curTok()->type==TOK_KOMA) { pos++; continue; }
                break;
            }
        }
        if (curTok()->type==TOK_KURAWAL_TUTUP) pos++;
        return applyPostfixChain(d);
    }
    if (t->type==TOK_FUNGSI) {
        pos++;
        char* params[16]; int pc=0;
        if (curTok()->type==TOK_KURUNG_BUKA) pos++;
        while (curTok()->type != TOK_KURUNG_TUTUP) {
            if (curTok()->type==TOK_NAMA && pc<16) params[pc++] = strdup(curTok()->text);
            pos++;
        }
        pos++;
        int bodyStart = pos;
        int bodyEnd = findMatchingBrace(bodyStart);

        int funcIndex = findCachedLambda(bodyStart);
        if (funcIndex == -1) {
            char fname[64];
            sprintf(fname, "__lambda_%d", anonCounter++);
            funcIndex = addFuncDef(fname, params, pc, bodyStart);
            cacheLambda(bodyStart, funcIndex);
        } else {
            for (int i=0;i<pc;i++) free(params[i]);
        }

        pos = bodyEnd + 1;
        Value v = makeClosureValue(funcIndex, currentScope);
        return applyPostfixChain(v);
    }
    if (t->type==TOK_BUAT) {
        pos++;
        if (curTok()->type==TOK_KURUNG_BUKA) pos++;
        char* sname = strdup(curTok()->text);
        int line = curTok()->line;
        pos++;
        StructDef* sd = findStructDef(sname);
        if (!sd) errorAt(line, "objek '%s' tidak dikenal", sname);
        Value obj; memset(&obj,0,sizeof(obj));
        obj.type = 3;
        obj.className = strdup(sname);
        obj.fieldCount = sd->fieldCount;
        obj.fieldNames = malloc(sizeof(char*) * sd->fieldCount);
        obj.fields = malloc(sizeof(Value) * sd->fieldCount);
        for (int i=0;i<sd->fieldCount;i++) obj.fieldNames[i] = strdup(sd->fieldNames[i]);
        int argi = 0;
        while (curTok()->type==TOK_KOMA) {
            pos++;
            Value v = parseExpression();
            if (argi < sd->fieldCount) obj.fields[argi] = v;
            argi++;
        }
        for (int i=argi;i<sd->fieldCount;i++) obj.fields[i] = makeNumber(0);
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        free(sname);
        return applyPostfixChain(obj);
    }
    if (t->type==TOK_NAMA) {
        char* name = strdup(t->text);
        int line = t->line;
        pos++;
        if (curTok()->type==TOK_KURUNG_BUKA) {
            Value result = callFunctionOrBuiltin(name, line);
            free(name);
            return applyPostfixChain(result);
        }
        Value* vptr = getVarPtr(name);
        if (!vptr) {
            FuncDef* fd = findFuncDef(name);
            if (fd) {
                Value fref = makeFunctionRef(name);
                free(name);
                return applyPostfixChain(fref);
            }
            errorAt(line, "variabel '%s' tidak ditemukan", name);
        }
        Value* curPtr = vptr;
        while (curTok()->type==TOK_SIKU_BUKA || curTok()->type==TOK_TITIK) {
            if (curTok()->type==TOK_SIKU_BUKA) {
                pos++;
                Value idxv = parseExpression();
                if (curTok()->type==TOK_SIKU_TUTUP) pos++;
                if (curPtr->type == 2) {
                    int idx = (int)idxv.num;
                    if (idx<0 || idx>=curPtr->arrLen) errorAt(line, "index array di luar batas (idx=%d, panjang=%d)", idx, curPtr->arrLen);
                    curPtr = &curPtr->arr[idx];
                } else if (curPtr->type == 6) {
                    if (idxv.type != 1) errorAt(line, "kunci kamus harus berupa teks");
                    Value* p = dictFindPtr(curPtr, idxv.str ? idxv.str : "");
                    if (p) {
                        curPtr = p;
                    } else {
                        static Value missing;
                        missing = makeNull();
                        curPtr = &missing;
                    }
                } else {
                    errorAt(line, "'%s' bukan array atau kamus", name);
                }
            } else {
                pos++;
                char* fname = curTok()->text; pos++;
                if (curPtr->type == 6) {
                    curPtr = dictGetOrInsertPtr(curPtr, fname);
                    continue;
                }
                if (curPtr->type != 3) errorAt(line, "bukan objek atau kamus, tidak bisa akses field '%s'", fname);
                if (curTok()->type == TOK_KURUNG_BUKA) {
                    Value methodResult;
                    if (tryCallMethod(*curPtr, fname, line, &methodResult)) {
                        free(name);
                        return applyPostfixChain(methodResult);
                    }
                }
                int fi=-1;
                for (int k=0;k<curPtr->fieldCount;k++) if (strcmp(curPtr->fieldNames[k],fname)==0){fi=k;break;}
                if (fi==-1) errorAt(line, "field '%s' tidak ditemukan", fname);
                curPtr = &curPtr->fields[fi];
            }
        }
        free(name);
        Value result = *curPtr;
        if (curTok()->type==TOK_KURUNG_BUKA && (result.type==5)) {
            pos++;
            if (result.closure) {
                Value args[64]; int argc = 0;
                if (curTok()->type != TOK_KURUNG_TUTUP) {
                    while (1) {
                        args[argc++] = parseExpression();
                        if (curTok()->type==TOK_KOMA) { pos++; continue; }
                        break;
                    }
                }
                if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
                FuncDef* fd = &funcDefs[result.closure->funcIndex];
                result = callUserFunctionEnv(fd, args, argc, result.closure->env);
            } else {
                Value args[64]; int argc = 0;
                if (curTok()->type != TOK_KURUNG_TUTUP) {
                    while (1) {
                        args[argc++] = parseExpression();
                        if (curTok()->type==TOK_KOMA) { pos++; continue; }
                        break;
                    }
                }
                if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
                FuncDef* fd = findFuncDef(result.str);
                if (!fd) errorAt(line, "fungsi '%s' tidak dikenal", result.str);
                result = callUserFunction(fd, args, argc);
            }
        }
        return applyPostfixChain(result);
    }
    errorAt(t->line, "token tidak dikenal dalam ekspresi");
    return makeNull();
}

Value parseUnary() {
    if (curTok()->type==TOK_KURANG) { pos++; Value v=parseUnary(); return makeNumber(-v.num); }
    if (curTok()->type==TOK_TIDAK) { pos++; Value v=parseUnary(); return makeNumber(truthy(v)?0:1); }
    if (curTok()->type==TOK_BALIK) { pos++; Value v=parseUnary(); return makeNumber((double)(~((long)v.num))); }
    return parsePrimary();
}

Value parseTerm() {
    Value left = parseUnary();
    while (curTok()->type==TOK_KALI||curTok()->type==TOK_BAGI||curTok()->type==TOK_SISA) {
        TokenType op = curTok()->type; int line = curTok()->line; pos++;
        Value right = parseUnary();
        if (op==TOK_KALI) left = makeNumber(left.num * right.num);
        else if (op==TOK_BAGI) {
            if (right.num==0) errorAt(line, "pembagian dengan nol");
            left = makeNumber(left.num / right.num);
        } else left = makeNumber(fmod(left.num, right.num));
    }
    return left;
}

Value parseAdditive() {
    Value left = parseTerm();
    while (curTok()->type==TOK_TAMBAH||curTok()->type==TOK_KURANG) {
        TokenType op = curTok()->type; pos++;
        Value right = parseTerm();
        if (op==TOK_TAMBAH) {
            if (left.type==1 || right.type==1) {
                left = concatValues(left, right);
            } else {
                left = makeNumber(left.num + right.num);
            }
        } else {
            left = makeNumber(left.num - right.num);
        }
    }
    return left;
}

Value parseShift() {
    Value left = parseAdditive();
    while (curTok()->type==TOK_GESER_KIRI||curTok()->type==TOK_GESER_KANAN) {
        TokenType op = curTok()->type; pos++;
        Value right = parseAdditive();
        long a=(long)left.num, b=(long)right.num;
        left = makeNumber((double)(op==TOK_GESER_KIRI ? (a<<b) : (a>>b)));
    }
    return left;
}
Value parseBitAnd() {
    Value left = parseShift();
    while (curTok()->type==TOK_DAN_BIT) { pos++; Value r=parseShift(); left=makeNumber((double)(((long)left.num)&((long)r.num))); }
    return left;
}
Value parseBitXor() {
    Value left = parseBitAnd();
    while (curTok()->type==TOK_XOR) { pos++; Value r=parseBitAnd(); left=makeNumber((double)(((long)left.num)^((long)r.num))); }
    return left;
}
Value parseBitOr() {
    Value left = parseBitXor();
    while (curTok()->type==TOK_ATAU_BIT) { pos++; Value r=parseBitXor(); left=makeNumber((double)(((long)left.num)|((long)r.num))); }
    return left;
}

Value parseComparison() {
    Value left = parseBitOr();
    while (curTok()->type==TOK_SAMA||curTok()->type==TOK_TIDAK_SAMA||curTok()->type==TOK_KURANG_DARI||
           curTok()->type==TOK_LEBIH_DARI||curTok()->type==TOK_KURANG_SAMA||curTok()->type==TOK_LEBIH_SAMA) {
        TokenType op = curTok()->type; pos++;
        Value right = parseBitOr();
        int result = 0;
        if (left.type==1 && right.type==1) {
            int c = strcmp(left.str, right.str);
            switch(op) {
                case TOK_SAMA: result=(c==0); break;
                case TOK_TIDAK_SAMA: result=(c!=0); break;
                case TOK_KURANG_DARI: result=(c<0); break;
                case TOK_LEBIH_DARI: result=(c>0); break;
                case TOK_KURANG_SAMA: result=(c<=0); break;
                case TOK_LEBIH_SAMA: result=(c>=0); break;
                default: break;
            }
        } else {
            double a=left.num, b=right.num;
            switch(op) {
                case TOK_SAMA: result=(a==b); break;
                case TOK_TIDAK_SAMA: result=(a!=b); break;
                case TOK_KURANG_DARI: result=(a<b); break;
                case TOK_LEBIH_DARI: result=(a>b); break;
                case TOK_KURANG_SAMA: result=(a<=b); break;
                case TOK_LEBIH_SAMA: result=(a>=b); break;
                default: break;
            }
        }
        left = makeNumber(result ? 1 : 0);
    }
    return left;
}

Value parseLogicalAnd() {
    Value left = parseComparison();
    while (curTok()->type==TOK_DAN) {
        pos++;
        Value right = parseComparison();
        left = makeNumber((truthy(left) && truthy(right)) ? 1 : 0);
    }
    return left;
}

Value parseExpression() {
    Value left = parseLogicalAnd();
    while (curTok()->type==TOK_ATAU) {
        pos++;
        Value right = parseLogicalAnd();
        left = makeNumber((truthy(left) || truthy(right)) ? 1 : 0);
    }
    return left;
}

void skipIfChain() {
    pos++;
    if (curTok()->type==TOK_KURUNG_BUKA) pos++;
    int depth=0;
    while (!(curTok()->type==TOK_KURUNG_TUTUP && depth==0)) {
        if (curTok()->type==TOK_KURUNG_BUKA) depth++;
        if (curTok()->type==TOK_KURUNG_TUTUP) depth--;
        pos++;
    }
    pos++;
    int bodyStart = pos;
    int bodyEnd = findMatchingBrace(bodyStart);
    pos = bodyEnd + 1;
    if (curTok()->type==TOK_LAINNYA) {
        pos++;
        if (curTok()->type==TOK_JIKA) {
            skipIfChain();
        } else {
            int eb = pos;
            int ee = findMatchingBrace(eb);
            pos = ee + 1;
        }
    }
}

Value parseCaseLabelValue() {
    int neg = 0;
    if (curTok()->type==TOK_KURANG) { neg = 1; pos++; }
    Token* t = curTok();
    Value v;
    if (t->type==TOK_ANGKA) { v = makeNumber(neg ? -t->num : t->num); pos++; }
    else if (t->type==TOK_TEKS) { v = makeString(t->text); pos++; }
    else if (t->type==TOK_BENAR) { v = makeNumber(1); pos++; }
    else if (t->type==TOK_SALAH) { v = makeNumber(0); pos++; }
    else { errorAt(t->line, "label 'Kasus' harus berupa nilai literal (angka/teks/Benar/Salah)"); v = makeNull(); }
    return v;
}
int valuesEqualSimple(Value a, Value b) {
    if (a.type != b.type) return 0;
    if (a.type==0) return a.num==b.num;
    if (a.type==1) return strcmp(a.str?a.str:"", b.str?b.str:"")==0;
    return 0;
}

void parseStatement() {
    Token* t = curTok();

    if (t->type==TOK_CETAK) {
        pos++;
        if (curTok()->type==TOK_KURUNG_BUKA) pos++;
        if (curTok()->type != TOK_KURUNG_TUTUP) {
            while (1) {
                Value v = parseExpression();
                printValue(v);
                if (curTok()->type==TOK_KOMA) { pos++; printf(" "); continue; }
                break;
            }
        }
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        if (curTok()->type==TOK_TITIK_KOMA) pos++;
        printf("\n");
        return;
    }

    if (t->type==TOK_VAR) {
        pos++;
        char* name = strdup(curTok()->text);
        pos++;
        Value v = makeNumber(0);
        if (curTok()->type==TOK_SET) { pos++; v = parseExpression(); }
        if (curTok()->type==TOK_TITIK_KOMA) pos++;
        declareVar(name, v);
        free(name);
        return;
    }

    if (t->type==TOK_TETAP) {
        pos++;
        char* name = strdup(curTok()->text);
        pos++;
        Value v = makeNumber(0);
        if (curTok()->type==TOK_SET) { pos++; v = parseExpression(); }
        if (curTok()->type==TOK_TITIK_KOMA) pos++;
        declareConst(name, v);
        free(name);
        return;
    }

    if (t->type==TOK_OBJEK) {
        pos++; pos++;
        if (curTok()->type==TOK_KURAWAL_BUKA) {
            int braceStart = pos;
            int braceEnd = findMatchingBrace(braceStart);
            pos = braceEnd + 1;
        }
        return;
    }

    if (t->type==TOK_FUNGSI) {
        pos++; pos++;
        if (curTok()->type==TOK_KURUNG_BUKA) pos++;
        while (curTok()->type != TOK_KURUNG_TUTUP) pos++;
        pos++;
        int bodyStart = pos;
        int bodyEnd = findMatchingBrace(bodyStart);
        pos = bodyEnd + 1;
        return;
    }

    if (t->type==TOK_KEMBALI) {
        pos++;
        if (curTok()->type != TOK_TITIK_KOMA) return_value = parseExpression();
        else return_value = makeNumber(0);
        if (curTok()->type==TOK_TITIK_KOMA) pos++;
        should_return = 1;
        return;
    }

    if (t->type==TOK_BERHENTI) {
        pos++;
        if (curTok()->type==TOK_TITIK_KOMA) pos++;
        should_break = 1;
        return;
    }

    if (t->type==TOK_LANJUT) {
        pos++;
        if (curTok()->type==TOK_TITIK_KOMA) pos++;
        should_continue = 1;
        return;
    }

    if (t->type==TOK_LEMPAR) {
        int line = t->line;
        pos++;
        Value v = parseExpression();
        if (curTok()->type==TOK_TITIK_KOMA) pos++;
        char buf[900];
        if (v.type==1) snprintf(buf, sizeof(buf), "%s", v.str ? v.str : "");
        else if (v.type==0) {
            if (v.num==(long)v.num) snprintf(buf,sizeof(buf),"%ld",(long)v.num);
            else snprintf(buf,sizeof(buf),"%g",v.num);
        } else snprintf(buf, sizeof(buf), "(nilai dilempar)");
        errorAt(line, "%s", buf);
        return;
    }

    if (t->type==TOK_COBA) {
        pos++;
        int tryStart = pos;
        int tryEnd = findMatchingBrace(tryStart);

        int afterTry = tryEnd + 1;
        int catchVarPos = -1, catchBodyStart = -1, catchBodyEnd = -1;
        int scanPos = afterTry;
        if (tokens[scanPos].type==TOK_TANGKAP) {
            scanPos++;
            if (tokens[scanPos].type==TOK_KURUNG_BUKA) {
                scanPos++;
                if (tokens[scanPos].type==TOK_NAMA) catchVarPos = scanPos;
                scanPos++;
                if (tokens[scanPos].type==TOK_KURUNG_TUTUP) scanPos++;
            }
            catchBodyStart = scanPos;
            catchBodyEnd = findMatchingBrace(catchBodyStart);
        }

        jmp_buf jb;
        Scope* savedScopeOnEntry = currentScope;
        int caught = setjmp(jb);
        if (!caught) {
            pushErrorJump(&jb);
            pos = tryStart + 1;
            while (pos < tryEnd && !should_return && !should_break && !should_continue) {
                parseStatement();
            }
            popErrorJump();
            pos = tryEnd + 1;
            if (catchBodyStart != -1) pos = catchBodyEnd + 1;
            return;
        } else {
            popErrorJump();
            while (currentScope != savedScopeOnEntry && currentScope->parent) popScope();
            if (catchBodyStart == -1) {
                errorAt(lastErrorLine, "%s", lastErrorMsg);
                return;
            }
            pushScope();
            if (catchVarPos != -1) {
                declareVar(tokens[catchVarPos].text, makeString(lastErrorMsg));
            }
            pos = catchBodyStart + 1;
            while (pos < catchBodyEnd && !should_return && !should_break && !should_continue) {
                parseStatement();
            }
            popScope();
            pos = catchBodyEnd + 1;
            return;
        }
    }

    if (t->type==TOK_JIKA) {
        pos++;
        if (curTok()->type==TOK_KURUNG_BUKA) pos++;
        Value cond = parseExpression();
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        int bodyStart = pos;
        int bodyEnd = findMatchingBrace(bodyStart);
        int tookBranch = 0;
        if (truthy(cond)) {
            pos = bodyStart + 1;
            while (pos < bodyEnd && !should_return && !should_break && !should_continue) parseStatement();
            pos = bodyEnd + 1;
            tookBranch = 1;
        } else {
            pos = bodyEnd + 1;
        }
        if (!should_return && !should_break && !should_continue && curTok()->type==TOK_LAINNYA) {
            pos++;
            if (curTok()->type==TOK_JIKA) {
                if (tookBranch) skipIfChain();
                else parseStatement();
            } else {
                int elseStart = pos;
                int elseEnd = findMatchingBrace(elseStart);
                if (!tookBranch) {
                    pos = elseStart + 1;
                    while (pos < elseEnd && !should_return && !should_break && !should_continue) parseStatement();
                    pos = elseEnd + 1;
                } else {
                    pos = elseEnd + 1;
                }
            }
        }
        return;
    }

    if (t->type==TOK_SELAMA) {
        pos++;
        if (curTok()->type==TOK_KURUNG_BUKA) pos++;
        int condStart = pos;
        while (1) {
            pos = condStart;
            Value cond = parseExpression();
            if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
            int bodyStart = pos;
            int bodyEnd = findMatchingBrace(bodyStart);
            if (truthy(cond)) {
                pos = bodyStart + 1;
                while (pos < bodyEnd && !should_return && !should_break && !should_continue) parseStatement();
                if (should_return) { pos = bodyEnd + 1; return; }
                if (should_break) { should_break = 0; pos = bodyEnd + 1; break; }
                if (should_continue) { should_continue = 0; }
                pos = bodyEnd + 1;
                continue;
            } else {
                pos = bodyEnd + 1;
                break;
            }
        }
        return;
    }

    if (t->type==TOK_UNTUK) {
        pos++;
        if (curTok()->type==TOK_KURUNG_BUKA) pos++;
        parseStatement();
        int condStart = pos;
        while (1) {
            pos = condStart;
            Value cond = parseExpression();
            if (curTok()->type==TOK_TITIK_KOMA) pos++;
            int updateStart = pos;
            int depth = 0, p = updateStart;
            while (!(tokens[p].type==TOK_KURUNG_TUTUP && depth==0)) {
                if (tokens[p].type==TOK_KURUNG_BUKA) depth++;
                if (tokens[p].type==TOK_KURUNG_TUTUP) depth--;
                p++;
            }
            int updateEnd = p;
            int bodyStart = updateEnd + 1;
            int bodyEnd = findMatchingBrace(bodyStart);
            if (truthy(cond)) {
                pos = bodyStart + 1;
                while (pos < bodyEnd && !should_return && !should_break && !should_continue) parseStatement();
                if (should_return) { pos = bodyEnd + 1; return; }
                int doBreak = 0;
                if (should_break) { should_break = 0; doBreak = 1; }
                if (should_continue) { should_continue = 0; }
                if (!doBreak) {
                    pos = updateStart;
                    parseStatement();
                    pos = bodyEnd + 1;
                    continue;
                } else {
                    pos = bodyEnd + 1;
                    break;
                }
            } else {
                pos = bodyEnd + 1;
                break;
            }
        }
        return;
    }

    if (t->type==TOK_UNTUK_SETIAP) {
        pos++;
        if (curTok()->type==TOK_KURUNG_BUKA) pos++;
        char* var1 = NULL; char* var2 = NULL;
        if (curTok()->type != TOK_NAMA) errorAt(curTok()->line, "diharapkan nama variabel setelah 'UntukSetiap('");
        var1 = strdup(curTok()->text); pos++;
        if (curTok()->type==TOK_KOMA) {
            pos++;
            if (curTok()->type != TOK_NAMA) errorAt(curTok()->line, "diharapkan nama variabel kedua setelah koma");
            var2 = strdup(curTok()->text); pos++;
        }
        if (curTok()->type==TOK_DALAM) pos++;
        else errorAt(curTok()->line, "diharapkan 'Dalam' pada UntukSetiap(...)");
        Value source = parseExpression();
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        int bodyStart = pos;
        int bodyEnd = findMatchingBrace(bodyStart);

        if (source.type==2) {
            for (int idx=0; idx<source.arrLen; idx++) {
                pushScope();
                if (var2) { declareVar(var1, makeNumber(idx)); declareVar(var2, source.arr[idx]); }
                else { declareVar(var1, source.arr[idx]); }
                pos = bodyStart + 1;
                while (pos < bodyEnd && !should_return && !should_break && !should_continue) parseStatement();
                popScope();
                if (should_return) { pos = bodyEnd + 1; free(var1); if (var2) free(var2); return; }
                if (should_break) { should_break = 0; break; }
                if (should_continue) { should_continue = 0; }
            }
        } else if (source.type==6) {
            for (int idx=0; idx<source.dictCount; idx++) {
                pushScope();
                if (var2) { declareVar(var1, makeString(source.dictKeys[idx])); declareVar(var2, source.dictVals[idx]); }
                else { declareVar(var1, makeString(source.dictKeys[idx])); }
                pos = bodyStart + 1;
                while (pos < bodyEnd && !should_return && !should_break && !should_continue) parseStatement();
                popScope();
                if (should_return) { pos = bodyEnd + 1; free(var1); if (var2) free(var2); return; }
                if (should_break) { should_break = 0; break; }
                if (should_continue) { should_continue = 0; }
            }
        } else {
            errorAt(curTok()->line, "UntukSetiap hanya bisa dipakai pada larik atau kamus");
        }
        pos = bodyEnd + 1;
        free(var1);
        if (var2) free(var2);
        return;
    }

    if (t->type==TOK_PILIH) {
        pos++;
        if (curTok()->type==TOK_KURUNG_BUKA) pos++;
        Value subject = parseExpression();
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        if (curTok()->type != TOK_KURAWAL_BUKA) errorAt(curTok()->line, "diharapkan '{' setelah Pilih(...)");
        int bodyStart = pos;
        int bodyEnd = findMatchingBrace(bodyStart);

        int executeFrom = -1;
        int defaultPos = -1;
        int depth = 0;
        int p = bodyStart + 1;
        while (p < bodyEnd) {
            TokenType tt = tokens[p].type;
            if (tt==TOK_KURAWAL_BUKA) { depth++; p++; continue; }
            if (tt==TOK_KURAWAL_TUTUP) { depth--; p++; continue; }
            if (depth==0 && tt==TOK_KASUS) {
                pos = p+1;
                Value caseVal = parseCaseLabelValue();
                int afterVal = pos;
                if (tokens[afterVal].type==TOK_TITIK_DUA) afterVal++;
                if (executeFrom==-1 && valuesEqualSimple(subject, caseVal)) executeFrom = afterVal;
                p = afterVal;
                continue;
            }
            if (depth==0 && tt==TOK_BAWAAN) {
                int afterColon = p+1;
                if (tokens[afterColon].type==TOK_TITIK_DUA) afterColon++;
                if (defaultPos==-1) defaultPos = afterColon;
                p = afterColon;
                continue;
            }
            p++;
        }

        if (executeFrom == -1) executeFrom = defaultPos;

        if (executeFrom != -1) {
            pos = executeFrom;
            while (pos < bodyEnd && !should_return && !should_break && !should_continue) parseStatement();
            if (should_break) should_break = 0;
        }
        pos = bodyEnd + 1;
        return;
    }

    if (t->type==TOK_KURAWAL_BUKA) {
        pos++;
        while (curTok()->type != TOK_KURAWAL_TUTUP && !should_return && !should_break && !should_continue) parseStatement();
        if (curTok()->type==TOK_KURAWAL_TUTUP) pos++;
        return;
    }

    if (t->type==TOK_TITIK_KOMA) { pos++; return; }

    if (t->type==TOK_NAMA) {
        int splitPos = findAssignSplit();
        int compoundPos = findCompoundAssign();
        if (splitPos != -1) {
            int identLine = t->line;
            char* identName = t->text;
            int isSimple = (tokens[pos+1].type != TOK_SIKU_BUKA && tokens[pos+1].type != TOK_TITIK);
            if (isSimple && isVarConst(identName)) errorAt(identLine, "tidak bisa mengubah nilai Tetap '%s'", identName);
            Value* target = resolveDesignator(NULL);
            if (curTok()->type==TOK_SET) pos++;
            Value rhs = parseExpression();
            if (curTok()->type==TOK_TITIK_KOMA) pos++;
            *target = rhs;
        } else if (compoundPos != -1) {
            int identLine = t->line;
            char* identName = t->text;
            int isSimple = (tokens[pos+1].type != TOK_SIKU_BUKA && tokens[pos+1].type != TOK_TITIK);
            if (isSimple && isVarConst(identName)) errorAt(identLine, "tidak bisa mengubah nilai Tetap '%s'", identName);
            Value* target = resolveDesignator(NULL);
            TokenType op = curTok()->type;
            pos++;
            Value rhs = parseExpression();
            if (curTok()->type==TOK_TITIK_KOMA) pos++;
            Value newVal;
            if (op==TOK_TAMBAH_SET) {
                if (target->type==1 || rhs.type==1) newVal = concatValues(*target, rhs);
                else newVal = makeNumber(target->num + rhs.num);
            } else if (op==TOK_KURANG_SET) {
                newVal = makeNumber(target->num - rhs.num);
            } else if (op==TOK_KALI_SET) {
                newVal = makeNumber(target->num * rhs.num);
            } else if (op==TOK_BAGI_SET) {
                if (rhs.num==0) errorAt(identLine, "pembagian dengan nol");
                newVal = makeNumber(target->num / rhs.num);
            } else {
                newVal = makeNumber(fmod(target->num, rhs.num));
            }
            *target = newVal;
        } else {
            parseExpression();
            if (curTok()->type==TOK_TITIK_KOMA) pos++;
        }
        return;
    }

    parseExpression();
    if (curTok()->type==TOK_TITIK_KOMA) pos++;
}
