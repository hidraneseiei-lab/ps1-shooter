/*
 * render.c - bagian dari ps1-shooter, dibuat oleh hidraneseiei21
 * Primitif gambar tingkat rendah (segitiga, disc, panel) + double-buffer VRAM.
 * Dipisah dari main.c: modul ini murni geometri, tidak tahu apa-apa soal
 * Enemy/Player/state game. Semua fungsi di sini dipakai oleh main.c lewat
 * render.h.
 */
#include <stdint.h>
#include <stddef.h>
#include "render.h"

/* --- buffer & posisi gambar (definisi asli; main.c pakai lewat extern) --- */
RenderBuffer buffers[2];
uint8_t     *nextpri;
int          active;
int16_t      baseOfs[2][2];
int          primPeak = 0;

/* --- tabel trigonometri --- */
const int8_t sin16[16] = {
    0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117, -127, -117, -90, -49
};
int sinI(int t) { return sin16[t & 15]; }
int cosI(int t) { return sin16[(t + 4) & 15]; }

const int8_t sinTab[32] = {
    0, 24, 48, 70, 89, 105, 116, 124, 127, 124, 116, 105, 89, 70, 48, 24,
    0, -24, -48, -70, -89, -105, -116, -124, -127, -124, -116, -105, -89, -70, -48, -24
};
int sinS(int t) { return sinTab[t & 31]; }

const int8_t sin64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12
};
int sinO(int t) { return sin64[t & 63]; }
int cosO(int t) { return sin64[(t + 16) & 63]; }


/* --- cadangan buffer untuk overlay fade/flash (lihat catatan di primAlloc) --- */
#define PRIM_RESERVE 2048
int primOverlayMode = 0;

void *primAlloc(int size) {
    uint8_t *base = buffers[active].buf;
    int limit = BUFFER_LEN - (primOverlayMode ? 0 : PRIM_RESERVE);
    if (nextpri + size > base + limit) return NULL; /* penuh: lewati gambar daripada merusak memori */
    void *p = nextpri;
    nextpri += size;
    {
        int used = (int)(nextpri - base);
        if (used > primPeak) primPeak = used;
    }
    return p;
}

int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

void rect(int layer, int x, int y, int w, int h, int r, int g, int b) {
    TILE *t = (TILE *)primAlloc(sizeof(TILE));
    if (!t) return;
    setTile(t);
    setXY0(t, x, y);
    setWH(t, w, h);
    setRGB0(t, clamp255(r), clamp255(g), clamp255(b));
    addPrim(&buffers[active].ot[layer], t);
}

void tri(int layer,
                int x0, int y0, int r0, int g0, int b0,
                int x1, int y1, int r1, int g1, int b1,
                int x2, int y2, int r2, int g2, int b2) {
    POLY_G3 *p = (POLY_G3 *)primAlloc(sizeof(POLY_G3));
    if (!p) return;
    setPolyG3(p);
    setXY3(p, x0, y0, x1, y1, x2, y2);
    setRGB0(p, clamp255(r0), clamp255(g0), clamp255(b0));
    setRGB1(p, clamp255(r1), clamp255(g1), clamp255(b1));
    setRGB2(p, clamp255(r2), clamp255(g2), clamp255(b2));
    addPrim(&buffers[active].ot[layer], p);
}

void rectGradV(int layer, int x, int y, int w, int h,
                      int r0, int g0, int b0, int r1, int g1, int b1) {
    POLY_G4 *p = (POLY_G4 *)primAlloc(sizeof(POLY_G4));
    if (!p) return;
    setPolyG4(p);
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    setRGB0(p, clamp255(r0), clamp255(g0), clamp255(b0));
    setRGB1(p, clamp255(r0), clamp255(g0), clamp255(b0));
    setRGB2(p, clamp255(r1), clamp255(g1), clamp255(b1));
    setRGB3(p, clamp255(r1), clamp255(g1), clamp255(b1));
    addPrim(&buffers[active].ot[layer], p);
}

void setBlendMode(int layer, int mode) {
    DR_TPAGE *p = (DR_TPAGE *)primAlloc(sizeof(DR_TPAGE));
    if (!p) return;
    setDrawTPage(p, 0, 1, getTPage(0, mode, 0, 0));
    addPrim(&buffers[active].ot[layer], p);
}

void disc(int layer, int cx, int cy, int rad,
                 int cr, int cg, int cb, int er, int eg, int eb) {
    for (int i = 0; i < 16; i++) {
        int x1 = cx + cosI(i)     * rad / 127;
        int y1 = cy + sinI(i)     * rad / 127;
        int x2 = cx + cosI(i + 1) * rad / 127;
        int y2 = cy + sinI(i + 1) * rad / 127;
        tri(layer, cx, cy, cr, cg, cb, x1, y1, er, eg, eb, x2, y2, er, eg, eb);
    }
}

void discLo(int layer, int cx, int cy, int rad,
                   int cr, int cg, int cb, int er, int eg, int eb) {
    for (int i = 0; i < 16; i += 2) {
        int x1 = cx + cosI(i)     * rad / 127;
        int y1 = cy + sinI(i)     * rad / 127;
        int x2 = cx + cosI(i + 2) * rad / 127;
        int y2 = cy + sinI(i + 2) * rad / 127;
        tri(layer, cx, cy, cr, cg, cb, x1, y1, er, eg, eb, x2, y2, er, eg, eb);
    }
}

void glowDisc(int cx, int cy, int rad, int r, int g, int b, int sides) {
    int step = 16 / sides; if (step < 1) step = 1;
    for (int i = 0; i < 16; i += step) {
        int x1 = cx + cosI(i)        * rad / 127;
        int y1 = cy + sinI(i)        * rad / 127;
        int x2 = cx + cosI(i + step) * rad / 127;
        int y2 = cy + sinI(i + step) * rad / 127;
        POLY_G3 *p = (POLY_G3 *)primAlloc(sizeof(POLY_G3));
        if (!p) return;
        setPolyG3(p);
        setSemiTrans(p, 1);
        setXY3(p, cx, cy, x1, y1, x2, y2);
        setRGB0(p, clamp255(r), clamp255(g), clamp255(b));
        setRGB1(p, 0, 0, 0);
        setRGB2(p, 0, 0, 0);
        addPrim(&buffers[active].ot[L_GLOW], p);
    }
}

/* [DETAIL] Versi 64-segmen glowDisc, dipakai untuk glow radius besar (atmosfer
   planet dkk) di mana 8-16 segmen terlihat jelas "berbintang" di tepinya
   (garis lurus antar segitiga Gouraud kentara pada radius besar). Biaya lebih
   mahal (sampai 64 segitiga vs 16), jadi HANYA dipakai untuk elemen besar
   yang jarang (planet latar), bukan efek kecil yang sering (peluru dsb). */
void glowDiscHD(int cx, int cy, int rad, int r, int g, int b) {
    for (int i = 0; i < 64; i += 2) {
        int x1 = cx + cosO(i)     * rad / 127;
        int y1 = cy + sinO(i)     * rad / 127;
        int x2 = cx + cosO(i + 2) * rad / 127;
        int y2 = cy + sinO(i + 2) * rad / 127;
        POLY_G3 *p = (POLY_G3 *)primAlloc(sizeof(POLY_G3));
        if (!p) return;
        setPolyG3(p);
        setSemiTrans(p, 1);
        setXY3(p, cx, cy, x1, y1, x2, y2);
        setRGB0(p, clamp255(r), clamp255(g), clamp255(b));
        setRGB1(p, 0, 0, 0);
        setRGB2(p, 0, 0, 0);
        addPrim(&buffers[active].ot[L_GLOW], p);
    }
}

void burst(int layer, int cx, int cy, int rOut, int rIn, int rot,
                  int cr, int cg, int cb, int er, int eg, int eb) {
    for (int i = 0; i < 8; i++) {
        int a0 = (i * 2 + rot) & 15;
        int a1 = (i * 2 + 1 + rot) & 15;
        int a2 = (i * 2 + 2 + rot) & 15;
        int xo = cx + cosI(a1) * rOut / 127;
        int yo = cy + sinI(a1) * rOut / 127;
        int xa = cx + cosI(a0) * rIn / 127;
        int ya = cy + sinI(a0) * rIn / 127;
        int xb = cx + cosI(a2) * rIn / 127;
        int yb = cy + sinI(a2) * rIn / 127;
        tri(layer, xo, yo, er, eg, eb, xa, ya, cr, cg, cb, xb, yb, cr, cg, cb);
    }
}

void beamQuad(int layer, int cx, int cy, int angle, int len, int thick,
                     int r, int g, int b, int trans) {
    int dx = cosI(angle), dy = sinI(angle);
    int nx = -dy, ny = dx;
    int hx1 = cx + dx * len / 127, hy1 = cy + dy * len / 127;
    int hx2 = cx - dx * len / 127, hy2 = cy - dy * len / 127;
    int ox = nx * thick / 254, oy = ny * thick / 254;
    POLY_F4 *p = (POLY_F4 *)primAlloc(sizeof(POLY_F4));
    if (!p) return;
    setPolyF4(p);
    if (trans) setSemiTrans(p, 1);
    setXY4(p, hx1 + ox, hy1 + oy, hx1 - ox, hy1 - oy, hx2 + ox, hy2 + oy, hx2 - ox, hy2 - oy);
    setRGB0(p, clamp255(r), clamp255(g), clamp255(b));
    addPrim(&buffers[active].ot[layer], p);
}

int beamHit(int bx, int by, int angle, int len, int halfThick, int px, int py) {
    int dx = cosI(angle), dy = sinI(angle);
    int rx = px - bx, ry = py - by;
    int along = (rx * dx + ry * dy) / 127;
    if (along < -len || along > len) return 0;
    int cross = rx * dy - ry * dx;
    if (cross < 0) cross = -cross;
    return (cross / 127) < halfThick;
}

/* [BARU] normalisasi arah tanpa sqrt (Chebyshev-scaled) - dipakai musuh
   Turret & Absorber untuk menembak terarah ke posisi pemain */
void aimVector(int fromX, int fromY, int toX, int toY, int speed, int *outVx, int *outVy) {
    int dx = toX - fromX, dy = toY - fromY;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int m = adx > ady ? adx : ady;
    if (m == 0) { *outVx = 0; *outVy = speed; return; }
    *outVx = dx * speed / m;
    *outVy = dy * speed / m;
}

/* ---------- UI melengkung ---------- */

void flatRect(int layer, int x, int y, int w, int h,
                     int r, int g, int b, int trans) {
    POLY_F4 *p = (POLY_F4 *)primAlloc(sizeof(POLY_F4));
    if (!p) return;
    setPolyF4(p);
    if (trans) setSemiTrans(p, 1);
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    setRGB0(p, clamp255(r), clamp255(g), clamp255(b));
    addPrim(&buffers[active].ot[layer], p);
}

void quarterDisc(int layer, int cx, int cy, int rad, int q,
                        int r, int g, int b, int trans) {
    int base = q * 4;
    for (int i = 0; i < 4; i++) {
        int x1 = cx + cosI(base + i)     * rad / 127;
        int y1 = cy + sinI(base + i)     * rad / 127;
        int x2 = cx + cosI(base + i + 1) * rad / 127;
        int y2 = cy + sinI(base + i + 1) * rad / 127;
        POLY_G3 *p = (POLY_G3 *)primAlloc(sizeof(POLY_G3));
        if (!p) return;
        setPolyG3(p);
        if (trans) setSemiTrans(p, 1);
        setXY3(p, cx, cy, x1, y1, x2, y2);
        setRGB0(p, clamp255(r), clamp255(g), clamp255(b));
        setRGB1(p, clamp255(r), clamp255(g), clamp255(b));
        setRGB2(p, clamp255(r), clamp255(g), clamp255(b));
        addPrim(&buffers[active].ot[layer], p);
    }
}

void roundedPanel(int layer, int x, int y, int w, int h, int rad,
                         int r, int g, int b, int trans) {
    flatRect(layer, x + rad, y, w - 2 * rad, h, r, g, b, trans);
    flatRect(layer, x, y + rad, rad, h - 2 * rad, r, g, b, trans);
    flatRect(layer, x + w - rad, y + rad, rad, h - 2 * rad, r, g, b, trans);
    quarterDisc(layer, x + rad,       y + rad,       rad, 2, r, g, b, trans);
    quarterDisc(layer, x + w - rad,   y + rad,       rad, 3, r, g, b, trans);
    quarterDisc(layer, x + rad,       y + h - rad,   rad, 1, r, g, b, trans);
    quarterDisc(layer, x + w - rad,   y + h - rad,   rad, 0, r, g, b, trans);
}

