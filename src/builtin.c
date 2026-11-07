#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include "../include/value.h"
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/interpreter.h"
#include "../include/builtin.h"

#define MY_PI 3.14159265358979323846
#define MY_E  2.71828182845904523536

/* ===== builtin.c =====
   Lihat builtin.h untuk catatan arsitektur. */

int g_argc = 0;
char** g_argv = NULL;

void skipJSONWS(const char* s, int* p) { while (s[*p] && isspace((unsigned char)s[*p])) (*p)++; }
Value parseJSONValue(const char* s, int* p) {
    skipJSONWS(s, p);
    char c = s[*p];
    if (c == '"') {
        (*p)++;
        char buf[8192]; int bi = 0;
        while (s[*p] && s[*p] != '"') {
            if (s[*p] == '\\' && s[*p+1]) {
                (*p)++;
                char nc = s[*p];
                if (nc=='n') buf[bi++]='\n'; else if (nc=='t') buf[bi++]='\t';
                else if (nc=='r') buf[bi++]='\r'; else if (nc=='\\') buf[bi++]='\\';
                else if (nc=='"') buf[bi++]='"'; else if (nc=='/') buf[bi++]='/';
                else buf[bi++]=nc;
            } else { if (bi<8191) buf[bi++] = s[*p]; }
            (*p)++;
        }
        buf[bi] = 0;
        if (s[*p] == '"') (*p)++;
        return makeString(buf);
    } else if (c == '[') {
        (*p)++; Value arr = makeArray(); skipJSONWS(s, p);
        if (s[*p]==']') { (*p)++; return arr; }
        while (1) {
            arrPush(&arr, parseJSONValue(s, p));
            skipJSONWS(s, p);
            if (s[*p]==',') { (*p)++; continue; } break;
        }
        if (s[*p]==']') (*p)++;
        return arr;
    } else if (c == '{') {
        (*p)++; Value dict = makeDict(); skipJSONWS(s, p);
        if (s[*p]=='}') { (*p)++; return dict; }
        while (1) {
            skipJSONWS(s, p);
            Value key = parseJSONValue(s, p);
            skipJSONWS(s, p);
            if (s[*p]==':') (*p)++;
            Value val = parseJSONValue(s, p);
            dictSet(&dict, key.str ? key.str : "", val);
            skipJSONWS(s, p);
            if (s[*p]==',') { (*p)++; continue; } break;
        }
        if (s[*p]=='}') (*p)++;
        return dict;
    } else if (c=='t' && strncmp(s+*p,"true",4)==0) { *p+=4; return makeNumber(1); }
    else if (c=='f' && strncmp(s+*p,"false",5)==0) { *p+=5; return makeNumber(0); }
    else if (c=='n' && strncmp(s+*p,"null",4)==0) { *p+=4; return makeNull(); }
    else if (c=='-' || isdigit((unsigned char)c)) {
        int start = *p;
        if (c=='-') (*p)++;
        while (isdigit((unsigned char)s[*p])) (*p)++;
        if (s[*p]=='.') { (*p)++; while (isdigit((unsigned char)s[*p])) (*p)++; }
        if (s[*p]=='e'||s[*p]=='E') { (*p)++; if (s[*p]=='+'||s[*p]=='-') (*p)++; while (isdigit((unsigned char)s[*p])) (*p)++; }
        char buf[128]; int l=*p-start; if(l>127)l=127;
        strncpy(buf, s+start, l); buf[l]=0;
        return makeNumber(atof(buf));
    }
    return makeNull();
}

char* valueToJSON(Value v) {
    if (v.type==0) {
        char buf[64];
        if (v.num==(long)v.num) sprintf(buf,"%ld",(long)v.num); else sprintf(buf,"%g",v.num);
        return strdup(buf);
    } else if (v.type==1) {
        const char* s = v.str ? v.str : "";
        size_t cap = strlen(s)*2+3; char* out = malloc(cap); int oi=0;
        out[oi++]='"';
        for (int i=0;s[i];i++) {
            if (oi+4>=(int)cap) { cap*=2; out=realloc(out,cap); }
            if (s[i]=='"') { out[oi++]='\\'; out[oi++]='"'; }
            else if (s[i]=='\\') { out[oi++]='\\'; out[oi++]='\\'; }
            else if (s[i]=='\n') { out[oi++]='\\'; out[oi++]='n'; }
            else if (s[i]=='\t') { out[oi++]='\\'; out[oi++]='t'; }
            else if (s[i]=='\r') { out[oi++]='\\'; out[oi++]='r'; }
            else out[oi++]=s[i];
        }
        out[oi++]='"'; out[oi]=0; return out;
    } else if (v.type==2) {
        size_t cap=256; char* out=malloc(cap); int oi=0; out[oi++]='[';
        for (int i=0;i<v.arrLen;i++) {
            char* item=valueToJSON(v.arr[i]); int il=strlen(item);
            while (oi+il+3>=(int)cap) { cap*=2; out=realloc(out,cap); }
            if (i>0) out[oi++]=',';
            memcpy(out+oi,item,il); oi+=il; free(item);
        }
        out[oi++]=']'; out[oi]=0; return out;
    } else if (v.type==3||v.type==6) {
        size_t cap=256; char* out=malloc(cap); int oi=0; out[oi++]='{';
        int count=(v.type==3)?v.fieldCount:v.dictCount;
        for (int i=0;i<count;i++) {
            const char* key=(v.type==3)?v.fieldNames[i]:v.dictKeys[i];
            Value val=(v.type==3)?v.fields[i]:v.dictVals[i];
            char* kJ=valueToJSON(makeString(key));
            char* vJ=valueToJSON(val);
            int kl=strlen(kJ),vl=strlen(vJ);
            while (oi+kl+vl+4>=(int)cap) { cap*=2; out=realloc(out,cap); }
            if (i>0) out[oi++]=',';
            memcpy(out+oi,kJ,kl); oi+=kl; out[oi++]=':';
            memcpy(out+oi,vJ,vl); oi+=vl; free(kJ); free(vJ);
        }
        out[oi++]='}'; out[oi]=0; return out;
    } else if (v.type==4) { return strdup("null"); }
    else if (v.type==5) { return strdup("\"<fungsi>\""); }
    return strdup("null");
}

char* formatString(const char* fmt, Value* args, int argc) {
    size_t cap = strlen(fmt)*2+256; char* out = malloc(cap); int oi=0; int ai=0;
    for (int i=0;fmt[i];i++) {
        if (fmt[i]=='%' && fmt[i+1]) {
            i++; char spec=fmt[i]; char tmp[128];
            if (spec=='s') {
                const char* s="";
                if (ai<argc && args[ai].type==1) s = args[ai].str ? args[ai].str : "";
                else if (ai<argc && args[ai].type==0) {
                    if (args[ai].num==(long)args[ai].num) sprintf(tmp,"%ld",(long)args[ai].num);
                    else sprintf(tmp,"%g",args[ai].num);
                    s=tmp;
                }
                ai++;
                while (oi+(int)strlen(s)+1>=(int)cap) { cap*=2; out=realloc(out,cap); }
                memcpy(out+oi,s,strlen(s)); oi+=strlen(s);
            } else if (spec=='d') {
                long n=(ai<argc)?(long)args[ai].num:0; ai++;
                sprintf(tmp,"%ld",n);
                while (oi+(int)strlen(tmp)+1>=(int)cap) { cap*=2; out=realloc(out,cap); }
                memcpy(out+oi,tmp,strlen(tmp)); oi+=strlen(tmp);
            } else if (spec=='f') {
                double n=(ai<argc)?args[ai].num:0; ai++;
                sprintf(tmp,"%f",n);
                while (oi+(int)strlen(tmp)+1>=(int)cap) { cap*=2; out=realloc(out,cap); }
                memcpy(out+oi,tmp,strlen(tmp)); oi+=strlen(tmp);
            } else if (spec=='g') {
                double n=(ai<argc)?args[ai].num:0; ai++;
                sprintf(tmp,"%g",n);
                while (oi+(int)strlen(tmp)+1>=(int)cap) { cap*=2; out=realloc(out,cap); }
                memcpy(out+oi,tmp,strlen(tmp)); oi+=strlen(tmp);
            } else if (spec=='%') {
                if (oi+1>=(int)cap) { cap*=2; out=realloc(out,cap); }
                out[oi++]='%';
            }
        } else {
            if (oi+1>=(int)cap) { cap*=2; out=realloc(out,cap); }
            out[oi++]=fmt[i];
        }
    }
    out[oi]=0; return out;
}


Value callFunctionOrBuiltin(char* name, int line) {
    Value* maybeVar = getVarPtr(name);
    char* actualName = name;
    Closure* calledClosure = NULL;
    if (maybeVar && maybeVar->type == 5) {
        if (maybeVar->closure) {
            calledClosure = maybeVar->closure;
        } else {
            actualName = maybeVar->str;
        }
    }

    pos++;

    if (calledClosure) {
        Value args[64]; int argc = 0;
        if (curTok()->type != TOK_KURUNG_TUTUP) {
            while (1) {
                args[argc++] = parseExpression();
                if (curTok()->type==TOK_KOMA) { pos++; continue; }
                break;
            }
        }
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        FuncDef* fd = &funcDefs[calledClosure->funcIndex];
        return callUserFunctionEnv(fd, args, argc, calledClosure->env);
    }

    if (strcmp(actualName,"Tambah")==0) {
        Value* target = resolveDesignator(NULL);
        if (curTok()->type==TOK_KOMA) pos++;
        Value val = parseExpression();
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        if (target->type != 2) errorAt(line, "Tambah() hanya untuk array");
        arrPush(target, val);
        return makeNumber(target->arrLen);
    }
    if (strcmp(actualName,"HapusAkhir")==0) {
        Value* target = resolveDesignator(NULL);
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        if (target->type != 2) errorAt(line, "HapusAkhir() hanya untuk array");
        if (target->arrLen==0) errorAt(line, "array kosong, tidak bisa HapusAkhir()");
        Value last = target->arr[target->arrLen-1];
        target->arrLen--;
        return last;
    }
    if (strcmp(actualName,"HapusKunci")==0) {
        Value* target = resolveDesignator(NULL);
        if (curTok()->type==TOK_KOMA) pos++;
        Value keyv = parseExpression();
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        if (target->type != 6) errorAt(line, "HapusKunci() hanya untuk kamus");
        if (keyv.type != 1) errorAt(line, "kunci harus berupa teks");
        int removed = dictRemove(target, keyv.str ? keyv.str : "");
        return makeNumber(removed);
    }
    if (strcmp(actualName,"Sisipkan")==0) {
        Value* target = resolveDesignator(NULL);
        if (curTok()->type==TOK_KOMA) pos++;
        Value idxv = parseExpression();
        if (curTok()->type==TOK_KOMA) pos++;
        Value val = parseExpression();
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        if (target->type != 2) errorAt(line, "Sisipkan() hanya untuk array");
        int idx=(int)idxv.num; if(idx<0) idx=0; if(idx>target->arrLen) idx=target->arrLen;
        if (target->arrLen>=target->arrCap) { target->arrCap*=2; target->arr=realloc(target->arr,sizeof(Value)*target->arrCap); }
        for (int i=target->arrLen;i>idx;i--) target->arr[i]=target->arr[i-1];
        target->arr[idx]=val; target->arrLen++;
        return makeNumber(target->arrLen);
    }
    if (strcmp(actualName,"HapusPada")==0) {
        Value* target = resolveDesignator(NULL);
        if (curTok()->type==TOK_KOMA) pos++;
        Value idxv = parseExpression();
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        if (target->type != 2) errorAt(line, "HapusPada() hanya untuk array");
        int idx=(int)idxv.num;
        if (idx<0||idx>=target->arrLen) errorAt(line, "index di luar batas");
        Value removed = target->arr[idx];
        for (int i=idx;i<target->arrLen-1;i++) target->arr[i]=target->arr[i+1];
        target->arrLen--;
        return removed;
    }
    if (strcmp(actualName,"SisipAwal")==0) {
        Value* target = resolveDesignator(NULL);
        if (curTok()->type==TOK_KOMA) pos++;
        Value val = parseExpression();
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        if (target->type != 2) errorAt(line, "SisipAwal() hanya untuk array");
        if (target->arrLen>=target->arrCap) { target->arrCap*=2; target->arr=realloc(target->arr,sizeof(Value)*target->arrCap); }
        for (int i=target->arrLen;i>0;i--) target->arr[i]=target->arr[i-1];
        target->arr[0]=val; target->arrLen++;
        return makeNumber(target->arrLen);
    }
    if (strcmp(actualName,"HapusAwal")==0) {
        Value* target = resolveDesignator(NULL);
        if (curTok()->type==TOK_KURUNG_TUTUP) pos++;
        if (target->type != 2) errorAt(line, "HapusAwal() hanya untuk array");
        if (target->arrLen==0) errorAt(line, "array kosong");
        Value removed = target->arr[0];
        for (int i=0;i<target->arrLen-1;i++) target->arr[i]=target->arr[i+1];
        target->arrLen--;
        return removed;
    }

    Value args[64]; int argc = 0;
    if (curTok()->type != TOK_KURUNG_TUTUP) {
        while (1) {
            args[argc++] = parseExpression();
            if (curTok()->type==TOK_KOMA) { pos++; continue; }
            break;
        }
    }
    if (curTok()->type==TOK_KURUNG_TUTUP) pos++;

    if (strcmp(actualName,"Panjang")==0) {
        if (args[0].type==1) return makeNumber(strlen(args[0].str));
        if (args[0].type==2) return makeNumber(args[0].arrLen);
        if (args[0].type==6) return makeNumber(args[0].dictCount);
        return makeNumber(0);
    }
    if (strcmp(actualName,"KeAngka")==0) return makeNumber(atof(args[0].str ? args[0].str : ""));
    if (strcmp(actualName,"KeTeks")==0) {
        char buf[128];
        if (args[0].type==0) {
            if (args[0].num == (long)args[0].num) sprintf(buf,"%ld",(long)args[0].num);
            else sprintf(buf,"%g",args[0].num);
        } else sprintf(buf,"%s", args[0].str ? args[0].str : "");
        return makeString(buf);
    }
    if (strcmp(actualName,"Baca")==0) {
        char buf[4096];
        if (fgets(buf,sizeof(buf),stdin)) {
            int l=strlen(buf);
            if (l>0 && buf[l-1]=='\n') buf[l-1]=0;
        } else buf[0]=0;
        return makeString(buf);
    }
    if (strcmp(actualName,"Akar")==0) return makeNumber(sqrt(args[0].num));
    if (strcmp(actualName,"Pangkat")==0) return makeNumber(pow(args[0].num, args[1].num));
    if (strcmp(actualName,"Acak")==0) {
        int lo=(int)args[0].num, hi=(int)args[1].num;
        return makeNumber(lo + rand()%(hi-lo+1));
    }
    if (strcmp(actualName,"KeBiner")==0) {
        unsigned long un=(unsigned long)(long)args[0].num;
        char tmp[128]; int ti=0;
        if (un==0) tmp[ti++]='0';
        while (un>0) { tmp[ti++]=(un%2)+'0'; un/=2; }
        char buf[128]; int bi=0;
        for (int k=ti-1;k>=0;k--) buf[bi++]=tmp[k];
        buf[bi]=0;
        return makeString(buf);
    }
    if (strcmp(actualName,"KeHex")==0) {
        char buf[64]; sprintf(buf,"%lx",(long)args[0].num);
        return makeString(buf);
    }
    if (strcmp(actualName,"TulisFile")==0) {
        FILE* f = fopen(args[0].str,"w");
        if (!f) return makeNumber(0);
        fprintf(f,"%s", args[1].str ? args[1].str : "");
        fclose(f);
        return makeNumber(1);
    }
    if (strcmp(actualName,"TambahFile")==0) {
        FILE* f = fopen(args[0].str,"a");
        if (!f) return makeNumber(0);
        fprintf(f,"%s", args[1].str ? args[1].str : "");
        fclose(f);
        return makeNumber(1);
    }
    if (strcmp(actualName,"BacaFile")==0) {
        FILE* f = fopen(args[0].str,"r");
        if (!f) return makeString("");
        char* buf = malloc(1); buf[0]=0;
        size_t len=0; char chunk[4096]; size_t n;
        while ((n=fread(chunk,1,sizeof(chunk),f))>0) {
            buf = realloc(buf, len+n+1);
            memcpy(buf+len, chunk, n);
            len += n;
            buf[len]=0;
        }
        fclose(f);
        Value v = makeString(buf);
        free(buf);
        return v;
    }
    if (strcmp(actualName,"AdaFile")==0) {
        FILE* f = fopen(args[0].str,"r");
        if (f) { fclose(f); return makeNumber(1); }
        return makeNumber(0);
    }

    if (strcmp(actualName,"Pisah")==0) {
        const char* src = args[0].str ? args[0].str : "";
        const char* sep = args[1].str ? args[1].str : "";
        Value result = makeArray();
        int sepLen = strlen(sep);
        if (sepLen == 0) {
            for (int k=0; src[k]; k++) {
                char buf[2] = { src[k], 0 };
                arrPush(&result, makeString(buf));
            }
        } else {
            const char* start = src;
            const char* p = strstr(start, sep);
            while (p) {
                int l = p - start;
                char* piece = malloc(l+1);
                memcpy(piece, start, l); piece[l]=0;
                arrPush(&result, makeString(piece));
                free(piece);
                start = p + sepLen;
                p = strstr(start, sep);
            }
            arrPush(&result, makeString(start));
        }
        return result;
    }
    if (strcmp(actualName,"Gabung")==0) {
        if (args[0].type != 2) errorAt(line, "Gabung() butuh array sebagai argumen pertama");
        const char* sep = args[1].str ? args[1].str : "";
        size_t cap = 256, outLen = 0;
        char* out = malloc(cap); out[0]=0;
        for (int k=0; k<args[0].arrLen; k++) {
            char tmp[128];
            const char* piece;
            Value* item = &args[0].arr[k];
            if (item->type==1) piece = item->str ? item->str : "";
            else if (item->type==0) {
                if (item->num==(long)item->num) sprintf(tmp,"%ld",(long)item->num);
                else sprintf(tmp,"%g",item->num);
                piece = tmp;
            } else piece = "";
            size_t pl = strlen(piece);
            size_t sl = (k>0) ? strlen(sep) : 0;
            while (outLen+pl+sl+1 >= cap) { cap*=2; out=realloc(out,cap); }
            if (k>0) { memcpy(out+outLen, sep, sl); outLen+=sl; }
            memcpy(out+outLen, piece, pl); outLen+=pl;
            out[outLen]=0;
        }
        Value v = makeString(out);
        free(out);
        return v;
    }
    if (strcmp(actualName,"Ganti")==0) {
        const char* src = args[0].str ? args[0].str : "";
        const char* oldS = args[1].str ? args[1].str : "";
        const char* newS = args[2].str ? args[2].str : "";
        int oldLen = strlen(oldS);
        if (oldLen == 0) return makeString(src);
        size_t cap = strlen(src)*2 + strlen(newS) + 16, outLen = 0;
        char* out = malloc(cap); out[0]=0;
        const char* start = src;
        const char* p = strstr(start, oldS);
        while (p) {
            int l = p - start;
            while (outLen+l+strlen(newS)+1 >= cap) { cap*=2; out=realloc(out,cap); }
            memcpy(out+outLen, start, l); outLen+=l;
            memcpy(out+outLen, newS, strlen(newS)); outLen+=strlen(newS);
            out[outLen]=0;
            start = p + oldLen;
            p = strstr(start, oldS);
        }
        int rem = strlen(start);
        while (outLen+rem+1 >= cap) { cap*=2; out=realloc(out,cap); }
        memcpy(out+outLen, start, rem); outLen+=rem;
        out[outLen]=0;
        Value v = makeString(out);
        free(out);
        return v;
    }
    if (strcmp(actualName,"Potong")==0) {
        const char* src = args[0].str ? args[0].str : "";
        int len = strlen(src);
        int a = (int)args[1].num;
        int b = (argc >= 3) ? (int)args[2].num : len;
        if (a < 0) a = 0;
        if (b > len) b = len;
        if (a > len) a = len;
        if (b < a) b = a;
        int l = b - a;
        char* buf = malloc(l+1);
        memcpy(buf, src+a, l); buf[l]=0;
        Value v = makeString(buf);
        free(buf);
        return v;
    }
    if (strcmp(actualName,"Rapikan")==0) {
        const char* src = args[0].str ? args[0].str : "";
        int len = strlen(src);
        int a = 0, b = len;
        while (a < b && isspace((unsigned char)src[a])) a++;
        while (b > a && isspace((unsigned char)src[b-1])) b--;
        int l = b - a;
        char* buf = malloc(l+1);
        memcpy(buf, src+a, l); buf[l]=0;
        Value v = makeString(buf);
        free(buf);
        return v;
    }
    if (strcmp(actualName,"HurufBesar")==0) {
        const char* src = args[0].str ? args[0].str : "";
        char* buf = strdup(src);
        for (int k=0; buf[k]; k++) buf[k] = toupper((unsigned char)buf[k]);
        Value v = makeString(buf);
        free(buf);
        return v;
    }
    if (strcmp(actualName,"HurufKecil")==0) {
        const char* src = args[0].str ? args[0].str : "";
        char* buf = strdup(src);
        for (int k=0; buf[k]; k++) buf[k] = tolower((unsigned char)buf[k]);
        Value v = makeString(buf);
        free(buf);
        return v;
    }
    if (strcmp(actualName,"Mengandung")==0) {
        const char* src = args[0].str ? args[0].str : "";
        const char* cari = args[1].str ? args[1].str : "";
        return makeNumber(strstr(src, cari) != NULL ? 1 : 0);
    }
    if (strcmp(actualName,"DimulaiDengan")==0) {
        const char* src = args[0].str ? args[0].str : "";
        const char* awalan = args[1].str ? args[1].str : "";
        int al = strlen(awalan);
        return makeNumber((int)strlen(src) >= al && strncmp(src, awalan, al)==0 ? 1 : 0);
    }
    if (strcmp(actualName,"DiakhiriDengan")==0) {
        const char* src = args[0].str ? args[0].str : "";
        const char* akhiran = args[1].str ? args[1].str : "";
        int sl = strlen(src), el = strlen(akhiran);
        if (el > sl) return makeNumber(0);
        return makeNumber(strcmp(src+sl-el, akhiran)==0 ? 1 : 0);
    }
    if (strcmp(actualName,"Posisi")==0) {
        const char* s=args[0].str?args[0].str:""; const char* sub=args[1].str?args[1].str:"";
        const char* p=strstr(s,sub); return makeNumber(p?(double)(p-s):-1);
    }
    if (strcmp(actualName,"PosisiTerakhir")==0) {
        const char* s=args[0].str?args[0].str:""; const char* sub=args[1].str?args[1].str:"";
        int sl=strlen(sub); if(sl==0) return makeNumber(strlen(s));
        int last=-1; const char* p=strstr(s,sub);
        while(p){ last=(int)(p-s); p=strstr(p+1,sub); }
        return makeNumber(last);
    }
    if (strcmp(actualName,"Ulangi")==0) {
        const char* s=args[0].str?args[0].str:""; int n=(int)args[1].num; if(n<0)n=0;
        int sl=strlen(s);
        char* buf=malloc((size_t)sl*n+1); buf[0]=0;
        for (int i=0;i<n;i++) memcpy(buf+i*sl,s,sl);
        buf[sl*n]=0;
        Value v=makeString(buf); free(buf); return v;
    }
    if (strcmp(actualName,"Karakter")==0) {
        const char* s=args[0].str?args[0].str:""; int idx=(int)args[1].num;
        int sl=strlen(s); if(idx<0||idx>=sl) return makeString("");
        char b[2]={s[idx],0}; return makeString(b);
    }
    if (strcmp(actualName,"KodeKarakter")==0) {
        const char* s=args[0].str?args[0].str:""; int idx=(argc>=2)?(int)args[1].num:0;
        int sl=strlen(s); if(idx<0||idx>=sl) return makeNumber(0);
        return makeNumber((double)(unsigned char)s[idx]);
    }
    if (strcmp(actualName,"DariKode")==0) {
        int code=(int)args[0].num; char b[2]={(char)code,0}; return makeString(b);
    }
    if (strcmp(actualName,"BalikTeks")==0) {
        const char* s=args[0].str?args[0].str:""; int sl=strlen(s);
        char* buf=malloc(sl+1);
        for (int i=0;i<sl;i++) buf[i]=s[sl-1-i];
        buf[sl]=0;
        Value v=makeString(buf); free(buf); return v;
    }
    if (strcmp(actualName,"Bandingkan")==0) {
        const char* a=args[0].str?args[0].str:""; const char* b=args[1].str?args[1].str:"";
        int c=strcmp(a,b); return makeNumber(c<0?-1:(c>0?1:0));
    }
    if (strcmp(actualName,"HitungKata")==0) {
        const char* s=args[0].str?args[0].str:""; int count=0,inWord=0;
        for(int i=0;s[i];i++){ if(isspace((unsigned char)s[i])) inWord=0; else if(!inWord){count++;inWord=1;} }
        return makeNumber(count);
    }

    if (strcmp(actualName,"Mutlak")==0) return makeNumber(fabs(args[0].num));
    if (strcmp(actualName,"BulatBawah")==0) return makeNumber(floor(args[0].num));
    if (strcmp(actualName,"BulatAtas")==0) return makeNumber(ceil(args[0].num));
    if (strcmp(actualName,"Bulat")==0) return makeNumber(round(args[0].num));
    if (strcmp(actualName,"Sinus")==0) return makeNumber(sin(args[0].num));
    if (strcmp(actualName,"Kosinus")==0) return makeNumber(cos(args[0].num));
    if (strcmp(actualName,"Tangen")==0) return makeNumber(tan(args[0].num));
    if (strcmp(actualName,"Ln")==0) return makeNumber(log(args[0].num));
    if (strcmp(actualName,"Log10")==0) return makeNumber(log10(args[0].num));
    if (strcmp(actualName,"Eksponen")==0) return makeNumber(exp(args[0].num));
    if (strcmp(actualName,"Pi")==0) return makeNumber(MY_PI);
    if (strcmp(actualName,"E")==0) return makeNumber(MY_E);
    if (strcmp(actualName,"Maks")==0) return makeNumber(args[0].num > args[1].num ? args[0].num : args[1].num);
    if (strcmp(actualName,"Min")==0) return makeNumber(args[0].num < args[1].num ? args[0].num : args[1].num);
    if (strcmp(actualName,"Genap")==0) return makeNumber(((long)args[0].num)%2==0 ? 1 : 0);
    if (strcmp(actualName,"Ganjil")==0) return makeNumber(((long)args[0].num)%2!=0 ? 1 : 0);
    if (strcmp(actualName,"FPB")==0) {
        long a=labs((long)args[0].num), b=labs((long)args[1].num);
        while (b) { long t=b; b=a%b; a=t; }
        return makeNumber((double)a);
    }
    if (strcmp(actualName,"KPK")==0) {
        long x=labs((long)args[0].num), y=labs((long)args[1].num);
        if (x==0||y==0) return makeNumber(0);
        long g=x, h=y;
        while (h) { long t=h; h=g%h; g=t; }
        return makeNumber((double)(x/g*y));
    }

    if (strcmp(actualName,"Balik")==0) {
        if (args[0].type != 2) errorAt(line, "Balik() hanya untuk larik");
        Value r = makeArray();
        for (int k=args[0].arrLen-1;k>=0;k--) arrPush(&r, args[0].arr[k]);
        return r;
    }
    if (strcmp(actualName,"Maksimum")==0) {
        if (args[0].type != 2) errorAt(line, "Maksimum() hanya untuk larik");
        if (args[0].arrLen == 0) errorAt(line, "Maksimum() tidak bisa untuk larik kosong");
        Value best = args[0].arr[0];
        for (int k=1;k<args[0].arrLen;k++) if (valueCompareDefault(args[0].arr[k], best) > 0) best = args[0].arr[k];
        return best;
    }
    if (strcmp(actualName,"Minimum")==0) {
        if (args[0].type != 2) errorAt(line, "Minimum() hanya untuk larik");
        if (args[0].arrLen == 0) errorAt(line, "Minimum() tidak bisa untuk larik kosong");
        Value best = args[0].arr[0];
        for (int k=1;k<args[0].arrLen;k++) if (valueCompareDefault(args[0].arr[k], best) < 0) best = args[0].arr[k];
        return best;
    }
    if (strcmp(actualName,"Jumlah")==0) {
        if (args[0].type != 2) errorAt(line, "Jumlah() hanya untuk larik");
        double s=0;
        for (int k=0;k<args[0].arrLen;k++) s += args[0].arr[k].num;
        return makeNumber(s);
    }
    if (strcmp(actualName,"RataRata")==0) {
        if (args[0].type != 2) errorAt(line, "RataRata() hanya untuk larik");
        if (args[0].arrLen == 0) errorAt(line, "RataRata() tidak bisa untuk larik kosong");
        double s=0;
        for (int k=0;k<args[0].arrLen;k++) s += args[0].arr[k].num;
        return makeNumber(s / args[0].arrLen);
    }
    if (strcmp(actualName,"Indeks")==0) {
        if (args[0].type != 2) errorAt(line, "Indeks() hanya untuk larik");
        for (int k=0;k<args[0].arrLen;k++) {
            Value* it = &args[0].arr[k];
            if (it->type == args[1].type) {
                if (it->type==1 && strcmp(it->str?it->str:"", args[1].str?args[1].str:"")==0) return makeNumber(k);
                if (it->type==0 && it->num == args[1].num) return makeNumber(k);
            }
        }
        return makeNumber(-1);
    }
    if (strcmp(actualName,"MengandungLarik")==0) {
        if (args[0].type != 2) errorAt(line, "MengandungLarik() hanya untuk larik");
        for (int k=0;k<args[0].arrLen;k++) {
            Value* it = &args[0].arr[k];
            if (it->type == args[1].type) {
                if (it->type==1 && strcmp(it->str?it->str:"", args[1].str?args[1].str:"")==0) return makeNumber(1);
                if (it->type==0 && it->num == args[1].num) return makeNumber(1);
            }
        }
        return makeNumber(0);
    }
    if (strcmp(actualName,"Urutkan")==0) {
        if (args[0].type != 2) errorAt(line, "Urutkan() hanya untuk larik");
        Value r = makeArray();
        for (int k=0;k<args[0].arrLen;k++) arrPush(&r, args[0].arr[k]);
        int useCustom = (argc >= 2 && args[1].type == 5);
        for (int ii=1; ii<r.arrLen; ii++) {
            Value key = r.arr[ii];
            int jj = ii-1;
            while (jj>=0) {
                int cmp;
                if (useCustom) {
                    Value cargs[2]; cargs[0]=r.arr[jj]; cargs[1]=key;
                    Value cres = callCallable(args[1], cargs, 2, line);
                    cmp = (cres.num > 0) ? 1 : (cres.num < 0 ? -1 : 0);
                } else {
                    cmp = valueCompareDefault(r.arr[jj], key);
                }
                if (cmp > 0) { r.arr[jj+1] = r.arr[jj]; jj--; }
                else break;
            }
            r.arr[jj+1] = key;
        }
        return r;
    }
    if (strcmp(actualName,"Peta")==0) {
        if (args[0].type != 2) errorAt(line, "Peta() hanya untuk larik");
        if (argc < 2 || args[1].type != 5) errorAt(line, "Peta() butuh fungsi sebagai argumen kedua");
        Value r = makeArray();
        for (int k=0;k<args[0].arrLen;k++) {
            Value cargs[2]; cargs[0]=args[0].arr[k]; cargs[1]=makeNumber(k);
            Value res = callCallable(args[1], cargs, 2, line);
            arrPush(&r, res);
        }
        return r;
    }
    if (strcmp(actualName,"Saring")==0) {
        if (args[0].type != 2) errorAt(line, "Saring() hanya untuk larik");
        if (argc < 2 || args[1].type != 5) errorAt(line, "Saring() butuh fungsi sebagai argumen kedua");
        Value r = makeArray();
        for (int k=0;k<args[0].arrLen;k++) {
            Value cargs[2]; cargs[0]=args[0].arr[k]; cargs[1]=makeNumber(k);
            Value res = callCallable(args[1], cargs, 2, line);
            if (truthy(res)) arrPush(&r, args[0].arr[k]);
        }
        return r;
    }
    if (strcmp(actualName,"Reduksi")==0) {
        if (args[0].type != 2) errorAt(line, "Reduksi() hanya untuk larik");
        if (argc < 2 || args[1].type != 5) errorAt(line, "Reduksi() butuh fungsi sebagai argumen kedua");
        Value acc = (argc >= 3) ? args[2] : makeNumber(0);
        for (int k=0;k<args[0].arrLen;k++) {
            Value cargs[3]; cargs[0]=acc; cargs[1]=args[0].arr[k]; cargs[2]=makeNumber(k);
            acc = callCallable(args[1], cargs, 3, line);
        }
        return acc;
    }
    if (strcmp(actualName,"Salin")==0) {
        return deepCopyValue(args[0]);
    }
    if (strcmp(actualName,"Kloning")==0) {
        return deepCopyValue(args[0]);
    }
    if (strcmp(actualName,"Potongan")==0) {
        if (args[0].type != 2) errorAt(line, "Potongan() hanya untuk larik");
        int len = args[0].arrLen;
        int a = (int)args[1].num;
        int b = (argc>=3) ? (int)args[2].num : len;
        if (a<0) a=0; if (b>len) b=len; if (a>len) a=len; if (b<a) b=a;
        Value r = makeArray();
        for (int k=a;k<b;k++) arrPush(&r, args[0].arr[k]);
        return r;
    }
    if (strcmp(actualName,"Unik")==0) {
        if (args[0].type != 2) errorAt(line, "Unik() hanya untuk larik");
        Value r = makeArray();
        for (int k=0;k<args[0].arrLen;k++) {
            int found=0;
            for (int j=0;j<r.arrLen;j++) {
                if (r.arr[j].type==args[0].arr[k].type) {
                    if (r.arr[j].type==1 && strcmp(r.arr[j].str?r.arr[j].str:"",args[0].arr[k].str?args[0].arr[k].str:"")==0) { found=1; break; }
                    if (r.arr[j].type==0 && r.arr[j].num==args[0].arr[k].num) { found=1; break; }
                }
            }
            if (!found) arrPush(&r, args[0].arr[k]);
        }
        return r;
    }
    if (strcmp(actualName,"Ratakan")==0) {
        if (args[0].type != 2) errorAt(line, "Ratakan() hanya untuk larik");
        Value r = makeArray();
        for (int k=0;k<args[0].arrLen;k++) {
            if (args[0].arr[k].type==2) {
                for (int j=0;j<args[0].arr[k].arrLen;j++) arrPush(&r, args[0].arr[k].arr[j]);
            } else arrPush(&r, args[0].arr[k]);
        }
        return r;
    }
    if (strcmp(actualName,"SemuaCocok")==0) {
        if (args[0].type != 2) errorAt(line, "SemuaCocok() hanya untuk larik");
        if (argc<2||args[1].type!=5) errorAt(line, "SemuaCocok() butuh fungsi");
        for (int k=0;k<args[0].arrLen;k++) {
            Value cargs[2]={args[0].arr[k],makeNumber(k)};
            Value res = callCallable(args[1],cargs,2,line);
            if (!truthy(res)) return makeNumber(0);
        }
        return makeNumber(1);
    }
    if (strcmp(actualName,"AdaCocok")==0) {
        if (args[0].type != 2) errorAt(line, "AdaCocok() hanya untuk larik");
        if (argc<2||args[1].type!=5) errorAt(line, "AdaCocok() butuh fungsi");
        for (int k=0;k<args[0].arrLen;k++) {
            Value cargs[2]={args[0].arr[k],makeNumber(k)};
            Value res = callCallable(args[1],cargs,2,line);
            if (truthy(res)) return makeNumber(1);
        }
        return makeNumber(0);
    }
    if (strcmp(actualName,"CariPertama")==0) {
        if (args[0].type != 2) errorAt(line, "CariPertama() hanya untuk larik");
        if (argc<2||args[1].type!=5) errorAt(line, "CariPertama() butuh fungsi");
        for (int k=0;k<args[0].arrLen;k++) {
            Value cargs[2]={args[0].arr[k],makeNumber(k)};
            Value res = callCallable(args[1],cargs,2,line);
            if (truthy(res)) return args[0].arr[k];
        }
        return makeNull();
    }

    if (strcmp(actualName,"Kamus")==0) {
        return makeDict();
    }
    if (strcmp(actualName,"Kunci")==0) {
        if (args[0].type != 6) errorAt(line, "Kunci() hanya untuk kamus");
        Value r = makeArray();
        for (int k=0;k<args[0].dictCount;k++) arrPush(&r, makeString(args[0].dictKeys[k]));
        return r;
    }
    if (strcmp(actualName,"Nilai")==0) {
        if (args[0].type != 6) errorAt(line, "Nilai() hanya untuk kamus");
        Value r = makeArray();
        for (int k=0;k<args[0].dictCount;k++) arrPush(&r, args[0].dictVals[k]);
        return r;
    }
    if (strcmp(actualName,"AdaKunci")==0) {
        if (args[0].type != 6) errorAt(line, "AdaKunci() hanya untuk kamus");
        if (args[1].type != 1) errorAt(line, "kunci harus berupa teks");
        return makeNumber(dictFindIndex(&args[0], args[1].str ? args[1].str : "") != -1 ? 1 : 0);
    }

    if (strcmp(actualName,"KeJSON")==0) {
        char* j = valueToJSON(args[0]);
        Value v = makeString(j);
        free(j);
        return v;
    }
    if (strcmp(actualName,"DariJSON")==0) {
        int p = 0;
        return parseJSONValue(args[0].str ? args[0].str : "", &p);
    }
    if (strcmp(actualName,"Format")==0) {
        if (argc < 1) return makeString("");
        Value restArgs[64]; int rc=0;
        for (int k=1;k<argc;k++) restArgs[rc++] = args[k];
        char* f = formatString(args[0].str ? args[0].str : "", restArgs, rc);
        Value v = makeString(f);
        free(f);
        return v;
    }

    if (strcmp(actualName,"JenisNilai")==0) {
        const char* tn;
        switch (args[0].type) {
            case 0: tn="angka"; break;
            case 1: tn="teks"; break;
            case 2: tn="larik"; break;
            case 3: tn="objek"; break;
            case 4: tn="null"; break;
            case 5: tn="fungsi"; break;
            case 6: tn="kamus"; break;
            default: tn="?"; break;
        }
        return makeString(tn);
    }
    if (strcmp(actualName,"IsAngka")==0) return makeNumber(args[0].type==0 ? 1 : 0);
    if (strcmp(actualName,"IsTeks")==0) return makeNumber(args[0].type==1 ? 1 : 0);
    if (strcmp(actualName,"IsLarik")==0) return makeNumber(args[0].type==2 ? 1 : 0);
    if (strcmp(actualName,"IsObjek")==0) return makeNumber(args[0].type==3 ? 1 : 0);
    if (strcmp(actualName,"IsNull")==0) return makeNumber(args[0].type==4 ? 1 : 0);
    if (strcmp(actualName,"IsFungsi")==0) return makeNumber(args[0].type==5 ? 1 : 0);
    if (strcmp(actualName,"IsKamus")==0) return makeNumber(args[0].type==6 ? 1 : 0);
    if (strcmp(actualName,"TipeSama")==0) return makeNumber(args[0].type==args[1].type ? 1 : 0);

    if (strcmp(actualName,"Argumen")==0) {
        int idx = (int)args[0].num;
        if (idx<0 || idx>=g_argc) return makeString("");
        return makeString(g_argv[idx]);
    }
    if (strcmp(actualName,"ArgumenJumlah")==0) return makeNumber(g_argc);
    if (strcmp(actualName,"Waktu")==0) return makeNumber((double)time(NULL));
    if (strcmp(actualName,"Tunda")==0) { usleep((useconds_t)(args[0].num*1000)); return makeNumber(1); }
    if (strcmp(actualName,"Sistem")==0) { int rc = system(args[0].str ? args[0].str : ""); return makeNumber(rc); }

    FuncDef* fd = findFuncDef(actualName);
    if (fd) return callUserFunction(fd, args, argc);

    errorAt(line, "fungsi '%s' tidak dikenal", actualName);
    return makeNull();
}

