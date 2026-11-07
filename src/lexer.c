#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/lexer.h"

/* ===== lexer.c =====
   Lihat lexer.h untuk catatan arsitektur token-stream. */

Token* tokens = NULL;
int tokenCount = 0;
int tokenCap = 0;

void initTokens() {
    tokenCap = 4096;
    tokens = malloc(sizeof(Token) * tokenCap);
    tokenCount = 0;
}

void addToken(TokenType t, const char* text, double num, int line) {
    if (tokenCount >= tokenCap) {
        tokenCap *= 2;
        tokens = realloc(tokens, sizeof(Token) * tokenCap);
    }
    tokens[tokenCount].type = t;
    tokens[tokenCount].text = text ? strdup(text) : NULL;
    tokens[tokenCount].num = num;
    tokens[tokenCount].line = line;
    tokenCount++;
}

void tokenizeAppend(const char* src) {
    int i = 0, line = 1;
    int len = strlen(src);
    while (i < len) {
        char c = src[i];
        if (c == '\n') { line++; i++; continue; }
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c=='/' && i+1<len && src[i+1]=='/') { while (i<len && src[i]!='\n') i++; continue; }
        if (isdigit((unsigned char)c)) {
            int start = i;
            if (c=='0' && i+1<len && (src[i+1]=='x'||src[i+1]=='X')) {
                i += 2;
                while (i<len && isxdigit((unsigned char)src[i])) i++;
                char buf[128]; int l=i-start; if(l>127)l=127;
                strncpy(buf, src+start, l); buf[l]=0;
                addToken(TOK_ANGKA, NULL, (double)strtol(buf,NULL,16), line);
                continue;
            }
            if (c=='0' && i+1<len && (src[i+1]=='b'||src[i+1]=='B')) {
                i += 2;
                int vstart = i;
                while (i<len && (src[i]=='0'||src[i]=='1')) i++;
                char buf[128]; int l=i-vstart; if(l>127)l=127;
                strncpy(buf, src+vstart, l); buf[l]=0;
                addToken(TOK_ANGKA, NULL, (double)strtol(buf,NULL,2), line);
                continue;
            }
            while (i<len && (isdigit((unsigned char)src[i]) || src[i]=='.')) i++;
            char buf[128]; int l=i-start; if(l>127)l=127;
            strncpy(buf, src+start, l); buf[l]=0;
            addToken(TOK_ANGKA, NULL, atof(buf), line);
            continue;
        }
        if (c == '"') {
            i++;
            char buf[8192]; int bi=0;
            while (i<len && src[i]!='"') {
                char ch = src[i];
                if (ch=='\\' && i+1<len) {
                    i++;
                    char nc = src[i];
                    if (nc=='n') ch='\n';
                    else if (nc=='t') ch='\t';
                    else if (nc=='r') ch='\r';
                    else ch=nc;
                }
                if (bi<8191) buf[bi++]=ch;
                i++;
            }
            buf[bi]=0;
            if (i<len) i++;
            addToken(TOK_TEKS, buf, 0, line);
            continue;
        }
        if (isalpha((unsigned char)c) || c=='_') {
            int start=i;
            while (i<len && (isalnum((unsigned char)src[i])||src[i]=='_')) i++;
            char buf[256]; int l=i-start; if(l>255)l=255;
            strncpy(buf, src+start, l); buf[l]=0;
            if (strcmp(buf,"Cetak")==0) addToken(TOK_CETAK,buf,0,line);
            else if (strcmp(buf,"Var")==0) addToken(TOK_VAR,buf,0,line);
            else if (strcmp(buf,"Tetap")==0) addToken(TOK_TETAP,buf,0,line);
            else if (strcmp(buf,"Jika")==0) addToken(TOK_JIKA,buf,0,line);
            else if (strcmp(buf,"Lainnya")==0) addToken(TOK_LAINNYA,buf,0,line);
            else if (strcmp(buf,"Selama")==0) addToken(TOK_SELAMA,buf,0,line);
            else if (strcmp(buf,"Untuk")==0) addToken(TOK_UNTUK,buf,0,line);
            else if (strcmp(buf,"Fungsi")==0) addToken(TOK_FUNGSI,buf,0,line);
            else if (strcmp(buf,"Metode")==0) addToken(TOK_METODE,buf,0,line);
            else if (strcmp(buf,"Kembali")==0) addToken(TOK_KEMBALI,buf,0,line);
            else if (strcmp(buf,"Dan")==0) addToken(TOK_DAN,buf,0,line);
            else if (strcmp(buf,"Atau")==0) addToken(TOK_ATAU,buf,0,line);
            else if (strcmp(buf,"Benar")==0) addToken(TOK_BENAR,buf,0,line);
            else if (strcmp(buf,"Salah")==0) addToken(TOK_SALAH,buf,0,line);
            else if (strcmp(buf,"Objek")==0) addToken(TOK_OBJEK,buf,0,line);
            else if (strcmp(buf,"Buat")==0) addToken(TOK_BUAT,buf,0,line);
            else if (strcmp(buf,"Berhenti")==0) addToken(TOK_BERHENTI,buf,0,line);
            else if (strcmp(buf,"Lanjut")==0) addToken(TOK_LANJUT,buf,0,line);
            else if (strcmp(buf,"Coba")==0) addToken(TOK_COBA,buf,0,line);
            else if (strcmp(buf,"Tangkap")==0) addToken(TOK_TANGKAP,buf,0,line);
            else if (strcmp(buf,"Lempar")==0) addToken(TOK_LEMPAR,buf,0,line);
            else if (strcmp(buf,"Pilih")==0) addToken(TOK_PILIH,buf,0,line);
            else if (strcmp(buf,"Kasus")==0) addToken(TOK_KASUS,buf,0,line);
            else if (strcmp(buf,"Bawaan")==0) addToken(TOK_BAWAAN,buf,0,line);
            else if (strcmp(buf,"Dalam")==0) addToken(TOK_DALAM,buf,0,line);
            else if (strcmp(buf,"UntukSetiap")==0) addToken(TOK_UNTUK_SETIAP,buf,0,line);
            else addToken(TOK_NAMA,buf,0,line);
            continue;
        }
        switch (c) {
            case '+':
                if (i+1<len && src[i+1]=='=') { addToken(TOK_TAMBAH_SET,"+=",0,line); i+=2; }
                else { addToken(TOK_TAMBAH,"+",0,line); i++; }
                break;
            case '-':
                if (i+1<len && src[i+1]=='=') { addToken(TOK_KURANG_SET,"-=",0,line); i+=2; }
                else { addToken(TOK_KURANG,"-",0,line); i++; }
                break;
            case '*':
                if (i+1<len && src[i+1]=='=') { addToken(TOK_KALI_SET,"*=",0,line); i+=2; }
                else { addToken(TOK_KALI,"*",0,line); i++; }
                break;
            case '/':
                if (i+1<len && src[i+1]=='=') { addToken(TOK_BAGI_SET,"/=",0,line); i+=2; }
                else { addToken(TOK_BAGI,"/",0,line); i++; }
                break;
            case '%':
                if (i+1<len && src[i+1]=='=') { addToken(TOK_SISA_SET,"%=",0,line); i+=2; }
                else { addToken(TOK_SISA,"%",0,line); i++; }
                break;
            case '(': addToken(TOK_KURUNG_BUKA,"(",0,line); i++; break;
            case ')': addToken(TOK_KURUNG_TUTUP,")",0,line); i++; break;
            case '{': addToken(TOK_KURAWAL_BUKA,"{",0,line); i++; break;
            case '}': addToken(TOK_KURAWAL_TUTUP,"}",0,line); i++; break;
            case '[': addToken(TOK_SIKU_BUKA,"[",0,line); i++; break;
            case ']': addToken(TOK_SIKU_TUTUP,"]",0,line); i++; break;
            case ';': addToken(TOK_TITIK_KOMA,";",0,line); i++; break;
            case ',': addToken(TOK_KOMA,",",0,line); i++; break;
            case '.': addToken(TOK_TITIK,".",0,line); i++; break;
            case ':': addToken(TOK_TITIK_DUA,":",0,line); i++; break;
            case '~': addToken(TOK_BALIK,"~",0,line); i++; break;
            case '^': addToken(TOK_XOR,"^",0,line); i++; break;
            case '=':
                if (i+1<len && src[i+1]=='=') { addToken(TOK_SAMA,"==",0,line); i+=2; }
                else { addToken(TOK_SET,"=",0,line); i++; }
                break;
            case '!':
                if (i+1<len && src[i+1]=='=') { addToken(TOK_TIDAK_SAMA,"!=",0,line); i+=2; }
                else { addToken(TOK_TIDAK,"!",0,line); i++; }
                break;
            case '<':
                if (i+1<len && src[i+1]=='=') { addToken(TOK_KURANG_SAMA,"<=",0,line); i+=2; }
                else if (i+1<len && src[i+1]=='<') { addToken(TOK_GESER_KIRI,"<<",0,line); i+=2; }
                else { addToken(TOK_KURANG_DARI,"<",0,line); i++; }
                break;
            case '>':
                if (i+1<len && src[i+1]=='=') { addToken(TOK_LEBIH_SAMA,">=",0,line); i+=2; }
                else if (i+1<len && src[i+1]=='>') { addToken(TOK_GESER_KANAN,">>",0,line); i+=2; }
                else { addToken(TOK_LEBIH_DARI,">",0,line); i++; }
                break;
            case '&':
                if (i+1<len && src[i+1]=='&') { addToken(TOK_DAN,"&&",0,line); i+=2; }
                else { addToken(TOK_DAN_BIT,"&",0,line); i++; }
                break;
            case '|':
                if (i+1<len && src[i+1]=='|') { addToken(TOK_ATAU,"||",0,line); i+=2; }
                else { addToken(TOK_ATAU_BIT,"|",0,line); i++; }
                break;
            default: i++; break;
        }
    }
    addToken(TOK_AKHIR,"EOF",0,line);
}

void tokenize(const char* src) {
    initTokens();
    tokenizeAppend(src);
}

