#ifndef BAHASAKU_LEXER_H
#define BAHASAKU_LEXER_H

/* ===== lexer.h =====
   Tokenizer bahasaku: mengubah source code (.nv) jadi array token.

   Catatan arsitektur: bahasaku adalah TOKEN-STREAM INTERPRETER, bukan
   AST-walking dan bukan bytecode/VM. Array tokens[] global ini dibaca
   langsung oleh parser (lihat parser.h), yang menggerakkan index pos
   sebagai semacam program counter sambil langsung mengeksekusi -- tidak
   ada tahap "bangun AST dulu, baru jalankan". Token array ini juga tidak
   pernah dibebaskan/direset selama program jalan, sehingga FuncDef
   (lihat value.h) bisa aman menyimpan index posisi token (bodyStart)
   sebagai referensi ke tubuh fungsi, karena tokennya tidak akan pindah
   atau hilang. */

typedef enum {
    TOK_ANGKA, TOK_TEKS, TOK_NAMA,
    TOK_TAMBAH, TOK_KURANG, TOK_KALI, TOK_BAGI, TOK_SISA,
    TOK_SET, TOK_SAMA, TOK_TIDAK_SAMA, TOK_KURANG_DARI, TOK_LEBIH_DARI, TOK_KURANG_SAMA, TOK_LEBIH_SAMA,
    TOK_KURUNG_BUKA, TOK_KURUNG_TUTUP, TOK_KURAWAL_BUKA, TOK_KURAWAL_TUTUP, TOK_SIKU_BUKA, TOK_SIKU_TUTUP,
    TOK_TITIK_KOMA, TOK_KOMA, TOK_TITIK, TOK_TITIK_DUA,
    TOK_DAN, TOK_ATAU, TOK_TIDAK,
    TOK_DAN_BIT, TOK_ATAU_BIT, TOK_XOR, TOK_BALIK, TOK_GESER_KIRI, TOK_GESER_KANAN,
    TOK_TAMBAH_SET, TOK_KURANG_SET, TOK_KALI_SET, TOK_BAGI_SET, TOK_SISA_SET,
    TOK_CETAK, TOK_VAR, TOK_TETAP, TOK_JIKA, TOK_LAINNYA, TOK_SELAMA, TOK_UNTUK,
    TOK_FUNGSI, TOK_KEMBALI, TOK_BENAR, TOK_SALAH, TOK_OBJEK, TOK_BUAT,
    TOK_BERHENTI, TOK_LANJUT, TOK_COBA, TOK_TANGKAP, TOK_LEMPAR,
    TOK_PILIH, TOK_KASUS, TOK_BAWAAN, TOK_DALAM, TOK_UNTUK_SETIAP, TOK_METODE,
    TOK_AKHIR
} TokenType;

typedef struct {
    TokenType type;
    char* text;
    double num;
    int line;
} Token;

/* Array token global -- dibaca parser.c lewat pos dan curTok(). */
extern Token* tokens;
extern int tokenCount;
extern int tokenCap;

void initTokens(void);
void addToken(TokenType t, const char* text, double num, int line);
void tokenizeAppend(const char* src);
void tokenize(const char* src);

#endif
