#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>
#include <psxgpu.h>

/* [MODUL RENDER] Primitif gambar tingkat rendah + double-buffer VRAM.
   Dipisah dari main.c supaya logika game (musuh, pemain, state) dan lapisan
   grafis (segitiga, disc, panel) tidak tercampur di satu file raksasa.
   Modul ini TIDAK tahu apa-apa soal Enemy/Player/dll - murni geometri. */

#define OT_LEN     10
#define BUFFER_LEN 131072
#define SCREEN_W   320
#define SCREEN_H   240

/* Urutan layer OT (over-draw): index tinggi = digambar duluan (paling belakang),
   index rendah = digambar terakhir (paling depan). Ini bagian dari kontrak
   rendering (dipakai primitif glowDisc dkk untuk tahu ke layer mana glow
   masuk), jadi didefinisikan di sini, bukan di main.c. */
enum { L_OVERLAY = 0, L_HUD_TOP = 1, L_HUD_BASE = 2, L_GLOW = 3, L_FX = 4, L_PLAYER = 5,
       L_BULLET = 6, L_ENEMY = 7, L_PLANET = 8, L_BG = 9 };

typedef struct {
    DISPENV  disp;
    DRAWENV  draw;
    uint32_t ot[OT_LEN];
    uint8_t  buf[BUFFER_LEN];
} RenderBuffer;

/* Buffer & posisi gambar saat ini. Didefinisikan di render.c, dipakai juga
   langsung oleh main.c (initVideo, flip, fxApplyShake dkk butuh akses ini). */
extern RenderBuffer buffers[2];
extern uint8_t     *nextpri;
extern int          active;
extern int16_t      baseOfs[2][2];   /* offset dasar DRAWENV tiap buffer, dipakai shake */

/* Statistik debug: pemakaian puncak buffer primitif (byte), untuk dipantau
   kalau suatu saat adegan terlalu padat dan primAlloc mulai menolak gambar. */
extern int primPeak;
extern int primOverlayMode;   /* 1 = sedang menggambar overlay fade/flash, boleh pakai cadangan buffer */

/* --- tabel trigonometri (dipakai luas oleh logika game juga: gerakan musuh dsb) --- */
int sinI(int t);  /* 16-segmen, -127..127 */
int cosI(int t);
int sinS(int t);  /* 32-segmen */
int sinO(int t);  /* 64-segmen, presisi lebih tinggi (planet, orbiter) */
int cosO(int t);

/* --- alokasi primitif dari buffer aktif --- */
void *primAlloc(int size);
int   clamp255(int v);

/* --- primitif dasar --- */
void rect(int layer, int x, int y, int w, int h, int r, int g, int b);
void tri(int layer,
         int x0, int y0, int r0, int g0, int b0,
         int x1, int y1, int r1, int g1, int b1,
         int x2, int y2, int r2, int g2, int b2);
void rectGradV(int layer, int x, int y, int w, int h,
               int r0, int g0, int b0, int r1, int g1, int b1);
void setBlendMode(int layer, int mode);
void disc(int layer, int cx, int cy, int rad,
          int cr, int cg, int cb, int er, int eg, int eb);
void discLo(int layer, int cx, int cy, int rad,
            int cr, int cg, int cb, int er, int eg, int eb);
void glowDisc(int cx, int cy, int rad, int r, int g, int b, int sides);
void glowDiscHD(int cx, int cy, int rad, int r, int g, int b);
void burst(int layer, int cx, int cy, int rOut, int rIn, int rot,
           int cr, int cg, int cb, int er, int eg, int eb);
void beamQuad(int layer, int cx, int cy, int angle, int len, int thick,
              int r, int g, int b, int trans);
int  beamHit(int bx, int by, int angle, int len, int halfThick, int px, int py);
void aimVector(int fromX, int fromY, int toX, int toY, int speed, int *outVx, int *outVy);

/* --- UI melengkung --- */
void flatRect(int layer, int x, int y, int w, int h, int r, int g, int b, int trans);
void quarterDisc(int layer, int cx, int cy, int rad, int q, int r, int g, int b, int trans);
void roundedPanel(int layer, int x, int y, int w, int h, int rad, int r, int g, int b, int trans);

#endif
