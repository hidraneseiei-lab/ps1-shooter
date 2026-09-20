#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>

#define OT_LEN     9
#define BUFFER_LEN 98304
#define SCREEN_W   320
#define SCREEN_H   240

typedef struct {
    DISPENV  disp;
    DRAWENV  draw;
    uint32_t ot[OT_LEN];
    uint8_t  buf[BUFFER_LEN];
} RenderBuffer;

static RenderBuffer buffers[2];
static uint8_t     *nextpri;
static int          active;
static uint8_t      padbuf[2][34];

/* DrawOTag menggambar dari index TINGGI ke RENDAH.
   Index tinggi = digambar duluan = paling BELAKANG.
   Dalam satu layer, primitif yang ditambahkan BELAKANGAN digambar DULUAN.

   L_GLOW adalah layer khusus semi-transparency (additive blend).
   Primitif di layer ini WAJIB ditambahkan lewat glowDisc(), dan
   setBlendMode(L_GLOW, 1) HARUS dipanggil PALING TERAKHIR tiap frame
   (persis sebelum flip()) supaya mode blend aktif sebelum GPU
   memproses primitif glow di layer itu. */
enum { L_HUD_TOP = 0, L_HUD_BASE = 1, L_GLOW = 2, L_FX = 3, L_PLAYER = 4,
       L_BULLET = 5, L_ENEMY = 6, L_PLANET = 7, L_BG = 8 };

typedef struct { int x, y, alive, hp, type, t; } Enemy;
typedef struct { int x, y, alive, dx; } Bullet;
typedef struct { int x, y, alive, timer, big; } Explosion;
typedef struct { int x, y, vx, vy, life, maxlife, r, g, b, alive, size; } Spark;
typedef struct { int x, y, speed, layer, phase; } Star;

#define MAX_BULLETS    16
#define MAX_ENEMIES    10
#define MAX_EXPLOSIONS 8
#define MAX_SPARKS     40
#define NUM_STARS      50
#define START_LIVES    3
#define EXPLOSION_LEN  20

#define HUD_H   30
#define TEXT_Y  12

enum { STATE_MENU, STATE_PLAY, STATE_GAMEOVER };
enum { E_DRONE = 0, E_ZIGZAG = 1, E_TANK = 2, E_BOSS = 3 };

static Bullet    bullets[MAX_BULLETS];
static Enemy     enemies[MAX_ENEMIES];
static Explosion explosions[MAX_EXPLOSIONS];
static Spark     sparks[MAX_SPARKS];
static Star      stars[NUM_STARS];

static int shootX = -100, shootY = 0, shootT = 0;

/* Sin/cos 16 langkah (skala 0..127), untuk lingkaran dan gerak */
static const int8_t sin16[16] = {
    0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117, -127, -117, -90, -49
};
static int sinI(int t) { return sin16[t & 15]; }
static int cosI(int t) { return sin16[(t + 4) & 15]; }

/* Tabel sinus halus 32 langkah untuk gerak zig-zag dan denyut */
static const int8_t sinTab[32] = {
    0, 24, 48, 70, 89, 105, 116, 124, 127, 124, 116, 105, 89, 70, 48, 24,
    0, -24, -48, -70, -89, -105, -116, -124, -127, -124, -116, -105, -89, -70, -48, -24
};
static int sinS(int t) { return sinTab[t & 31]; }

static void initVideo(void) {
    ResetGraph(0);

    SetDefDispEnv(&buffers[0].disp, 0, 0,        SCREEN_W, SCREEN_H);
    SetDefDrawEnv(&buffers[0].draw, 0, SCREEN_H, SCREEN_W, SCREEN_H);
    SetDefDispEnv(&buffers[1].disp, 0, SCREEN_H, SCREEN_W, SCREEN_H);
    SetDefDrawEnv(&buffers[1].draw, 0, 0,        SCREEN_W, SCREEN_H);

    for (int i = 0; i < 2; i++) {
        setRGB0(&buffers[i].draw, 0, 0, 0);
        buffers[i].draw.isbg = 1;
        buffers[i].draw.dtd  = 1;
    }

    active = 0;
    PutDispEnv(&buffers[0].disp);
    PutDrawEnv(&buffers[0].draw);
    SetDispMask(1);
}

static void flip(void) {
    DrawSync(0);
    VSync(0);

    PutDispEnv(&buffers[active].disp);
    PutDrawEnv(&buffers[active].draw);
    DrawOTag(&buffers[active].ot[OT_LEN - 1]);

    active ^= 1;
    ClearOTagR(buffers[active].ot, OT_LEN);
    nextpri = buffers[active].buf;
}

/* ---------- Primitif dasar ---------- */

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void rect(int layer, int x, int y, int w, int h,
                 int r, int g, int b) {
    TILE *t = (TILE *)nextpri;
    setTile(t);
    setXY0(t, x, y);
    setWH(t, w, h);
    setRGB0(t, clamp255(r), clamp255(g), clamp255(b));
    addPrim(&buffers[active].ot[layer], t);
    nextpri += sizeof(TILE);
}

static void tri(int layer,
                int x0, int y0, int r0, int g0, int b0,
                int x1, int y1, int r1, int g1, int b1,
                int x2, int y2, int r2, int g2, int b2) {
    POLY_G3 *p = (POLY_G3 *)nextpri;
    setPolyG3(p);
    setXY3(p, x0, y0, x1, y1, x2, y2);
    setRGB0(p, clamp255(r0), clamp255(g0), clamp255(b0));
    setRGB1(p, clamp255(r1), clamp255(g1), clamp255(b1));
    setRGB2(p, clamp255(r2), clamp255(g2), clamp255(b2));
    addPrim(&buffers[active].ot[layer], p);
    nextpri += sizeof(POLY_G3);
}

static void rectGradV(int layer, int x, int y, int w, int h,
                      int r0, int g0, int b0, int r1, int g1, int b1) {
    POLY_G4 *p = (POLY_G4 *)nextpri;
    setPolyG4(p);
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    setRGB0(p, clamp255(r0), clamp255(g0), clamp255(b0));
    setRGB1(p, clamp255(r0), clamp255(g0), clamp255(b0));
    setRGB2(p, clamp255(r1), clamp255(g1), clamp255(b1));
    setRGB3(p, clamp255(r1), clamp255(g1), clamp255(b1));
    addPrim(&buffers[active].ot[layer], p);
    nextpri += sizeof(POLY_G4);
}

/* Ganti mode blending GPU untuk sebuah layer.
   mode 0 = (B+F)/2  -> transparan lembut, cocok panel kaca
   mode 1 = B+F      -> aditif terang, cocok glow/neon/api
   mode 2 = B-F      -> jarang dipakai (invert)
   mode 3 = B+F/4    -> aditif halus, cocok kabut/atmosfer
   CATATAN: nama fungsi setDrawMode/getTPage & urutan parameter bisa
   sedikit beda antar versi PSn00bSDK. Kalau gagal compile, cek
   psxgpu.h yang terpasang dan sesuaikan. */
static void setBlendMode(int layer, int mode) {
    DR_MODE *p = (DR_MODE *)nextpri;
    setDrawMode(p, 0, 0, getTPage(0, mode, 0, 0), NULL);
    addPrim(&buffers[active].ot[layer], p);
    nextpri += sizeof(DR_MODE);
}

/* Lingkaran solid 16 sisi (halus, dipakai untuk objek besar: planet, kilau) */
static void disc(int layer, int cx, int cy, int rad,
                 int cr, int cg, int cb, int er, int eg, int eb) {
    for (int i = 0; i < 16; i++) {
        int x1 = cx + cosI(i)     * rad / 127;
        int y1 = cy + sinI(i)     * rad / 127;
        int x2 = cx + cosI(i + 1) * rad / 127;
        int y2 = cy + sinI(i + 1) * rad / 127;
        tri(layer, cx, cy, cr, cg, cb, x1, y1, er, eg, eb, x2, y2, er, eg, eb);
    }
}

/* Lingkaran solid 8 sisi (murah, dipakai objek kecil: bintang, titik indikator) */
static void discLo(int layer, int cx, int cy, int rad,
                   int cr, int cg, int cb, int er, int eg, int eb) {
    for (int i = 0; i < 16; i += 2) {
        int x1 = cx + cosI(i)     * rad / 127;
        int y1 = cy + sinI(i)     * rad / 127;
        int x2 = cx + cosI(i + 2) * rad / 127;
        int y2 = cy + sinI(i + 2) * rad / 127;
        tri(layer, cx, cy, cr, cg, cb, x1, y1, er, eg, eb, x2, y2, er, eg, eb);
    }
}

/* Lingkaran GLOW: 8 segitiga semi-transparan aditif, pusat terang -> tepi
   memudar ke hitam. Digabung sama setBlendMode(L_GLOW,1) hasilnya jadi
   cahaya neon lembut, jauh lebih murah & lebih bagus dari disc() gradient
   biasa. Selalu masuk ke layer L_GLOW. */
static void glowDisc(int cx, int cy, int rad, int r, int g, int b, int sides) {
    int step = 16 / sides; if (step < 1) step = 1;
    for (int i = 0; i < 16; i += step) {
        int x1 = cx + cosI(i)        * rad / 127;
        int y1 = cy + sinI(i)        * rad / 127;
        int x2 = cx + cosI(i + step) * rad / 127;
        int y2 = cy + sinI(i + step) * rad / 127;
        POLY_G3 *p = (POLY_G3 *)nextpri;
        setPolyG3(p);
        setSemiTrans(p, 1);
        setXY3(p, cx, cy, x1, y1, x2, y2);
        setRGB0(p, clamp255(r), clamp255(g), clamp255(b));
        setRGB1(p, 0, 0, 0);
        setRGB2(p, 0, 0, 0);
        addPrim(&buffers[active].ot[L_GLOW], p);
        nextpri += sizeof(POLY_G3);
    }
}

/* Bintang bergerigi (untuk ledakan) */
static void burst(int layer, int cx, int cy, int rOut, int rIn, int rot,
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

/* ---------- UI melengkung (rounded panel / glassmorphism) ---------- */

static void flatRect(int layer, int x, int y, int w, int h,
                     int r, int g, int b, int trans) {
    POLY_F4 *p = (POLY_F4 *)nextpri;
    setPolyF4(p);
    if (trans) setSemiTrans(p, 1);
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    setRGB0(p, clamp255(r), clamp255(g), clamp255(b));
    addPrim(&buffers[active].ot[layer], p);
    nextpri += sizeof(POLY_F4);
}

/* q = kuadran (0..3). Kalau arahnya kebalik pas dites, tukar angka q
   di pemanggilan roundedPanel() sampai sudutnya pas. */
static void quarterDisc(int layer, int cx, int cy, int rad, int q,
                        int r, int g, int b, int trans) {
    int base = q * 4;
    for (int i = 0; i < 4; i++) {
        int x1 = cx + cosI(base + i)     * rad / 127;
        int y1 = cy + sinI(base + i)     * rad / 127;
        int x2 = cx + cosI(base + i + 1) * rad / 127;
        int y2 = cy + sinI(base + i + 1) * rad / 127;
        POLY_G3 *p = (POLY_G3 *)nextpri;
        setPolyG3(p);
        if (trans) setSemiTrans(p, 1);
        setXY3(p, cx, cy, x1, y1, x2, y2);
        setRGB0(p, clamp255(r), clamp255(g), clamp255(b));
        setRGB1(p, clamp255(r), clamp255(g), clamp255(b));
        setRGB2(p, clamp255(r), clamp255(g), clamp255(b));
        addPrim(&buffers[active].ot[layer], p);
        nextpri += sizeof(POLY_G3);
    }
}

static void roundedPanel(int layer, int x, int y, int w, int h, int rad,
                         int r, int g, int b, int trans) {
    flatRect(layer, x + rad, y, w - 2 * rad, h, r, g, b, trans);
    flatRect(layer, x, y + rad, rad, h - 2 * rad, r, g, b, trans);
    flatRect(layer, x + w - rad, y + rad, rad, h - 2 * rad, r, g, b, trans);
    quarterDisc(layer, x + rad,       y + rad,       rad, 2, r, g, b, trans);
    quarterDisc(layer, x + w - rad,   y + rad,       rad, 3, r, g, b, trans);
    quarterDisc(layer, x + rad,       y + h - rad,   rad, 1, r, g, b, trans);
    quarterDisc(layer, x + w - rad,   y + h - rad,   rad, 0, r, g, b, trans);
}

/* ---------- Planet dengan pencahayaan semu (fake sphere lighting) ---------- */

static void planetSphere(int cx, int cy, int rad,
                         int litR, int litG, int litB,
                         int darkR, int darkG, int darkB) {
    for (int i = 0; i < 16; i++) {
        int x1 = cx + cosI(i)     * rad / 127;
        int y1 = cy + sinI(i)     * rad / 127;
        int x2 = cx + cosI(i + 1) * rad / 127;
        int y2 = cy + sinI(i + 1) * rad / 127;
        int litAmt = cosI(i) + 127; /* 0..254: terang di satu sisi, gelap di sisi lain */
        int r = darkR + (litR - darkR) * litAmt / 254;
        int g = darkG + (litG - darkG) * litAmt / 254;
        int b = darkB + (litB - darkB) * litAmt / 254;
        tri(L_PLANET, cx, cy, r, g, b,
                      x1, y1, r / 3, g / 3, b / 3,
                      x2, y2, r / 3, g / 3, b / 3);
    }
    /* atmosfer tipis mengikuti sisi terang */
    glowDisc(cx - rad / 4, cy - rad / 4, rad + 6, litR, litG, litB, 8);
}

/* ---------- Starfield: parallax 3 lapis + kelip + shooting star ---------- */

static void initStars(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].x = rand() % SCREEN_W;
        stars[i].y = rand() % SCREEN_H;
        stars[i].layer = i % 3;
        stars[i].speed = 1 + stars[i].layer;
        stars[i].phase = rand() % 32;
    }
}

static void drawStars(int frame) {
    for (int i = 0; i < NUM_STARS; i++) {
        int tw   = 140 + sinS(frame + stars[i].phase) / 2;
        int rad  = stars[i].layer + 1;
        int tint = (stars[i].layer == 2) ? 40 : 0;
        discLo(L_PLANET, stars[i].x, stars[i].y, rad,
               tw, tw, tw + tint, tw / 3, tw / 3, tw / 3);
    }
}

static void updateShootingStar(int frame) {
    if (shootT <= 0 && frame % 180 == 0) {
        shootX = rand() % (SCREEN_W - 60);
        shootY = HUD_H;
        shootT = 20;
    }
    if (shootT > 0) {
        shootX += 6;
        shootY += 4;
        shootT--;
    }
}

static void drawShootingStar(void) {
    if (shootT <= 0) return;
    glowDisc(shootX, shootY, 3, 255, 255, 255, 8);
    tri(L_PLANET, shootX, shootY, 255, 255, 255,
                  shootX - 14, shootY - 9, 0, 0, 0,
                  shootX - 2, shootY - 1, 120, 120, 160);
}

/* ---------- Latar ---------- */

static void drawBackground(int frame) {
    int py = (frame / 5) % (SCREEN_H + 140) - 70;
    int px = 250;

    tri(L_PLANET, px - 48, py + 3, 210, 180, 230,  px + 48, py - 3, 210, 180, 230,  px, py + 10, 100, 70, 140);
    tri(L_PLANET, px - 48, py + 3, 210, 180, 230,  px + 48, py - 3, 210, 180, 230,  px, py - 8, 150, 120, 190);
    planetSphere(px, py, 30, 255, 210, 150, 40, 25, 60);
    disc(L_PLANET, px - 8, py - 8, 12, 255, 240, 200, 255, 200, 120);

    int py2 = (frame / 8 + 100) % (SCREEN_H + 80) - 40;
    planetSphere(40, py2, 14, 220, 245, 255, 20, 50, 120);

    int pulse = 14 + sinS(frame / 6) / 14;
    disc(L_BG, 70, 80, 70,  pulse + 40, 10, pulse + 60,  16, 6, 34);
    disc(L_BG, 110, 60, 50, pulse + 50, 20, pulse + 70,  16, 6, 34);
    disc(L_BG, 260, 160, 70, 8, pulse + 45, pulse + 55,  6, 10, 34);
    disc(L_BG, 230, 180, 50, 12, pulse + 55, pulse + 60, 6, 10, 34);

    rectGradV(L_BG, 0, 0, SCREEN_W, SCREEN_H / 2, 20, 6, 44, 6, 8, 38);
    rectGradV(L_BG, 0, SCREEN_H / 2, SCREEN_W, SCREEN_H / 2, 6, 8, 38, 2, 4, 20);
}

/* ---------- Pesawat pemain ---------- */

static void drawPlayer(int x, int y, int frame) {
    int cx = x + 8;
    int fl = 4 + (frame / 2) % 4;

    glowDisc(cx - 4, y + 18, 4 + fl / 2, 255, 150, 40, 8);
    glowDisc(cx + 4, y + 18, 4 + fl / 2, 255, 150, 40, 8);
    tri(L_PLAYER, cx - 5, y + 16, 255, 255, 220,  cx - 2, y + 16, 255, 255, 220,  cx - 4, y + 16 + fl, 255, 60, 0);
    tri(L_PLAYER, cx + 2, y + 16, 255, 255, 220,  cx + 5, y + 16, 255, 255, 220,  cx + 4, y + 16 + fl, 255, 60, 0);

    tri(L_PLAYER, cx - 3, y + 4,  90, 200, 255,  x - 8,  y + 18, 20, 60, 200,  cx - 3, y + 15, 30, 100, 230);
    tri(L_PLAYER, cx + 3, y + 4,  90, 200, 255,  x + 24, y + 18, 20, 60, 200,  cx + 3, y + 15, 30, 100, 230);
    rect(L_PLAYER, x - 8,  y + 16, 3, 3, 255, 240, 80);
    rect(L_PLAYER, x + 21, y + 16, 3, 3, 255, 240, 80);

    tri(L_PLAYER, cx, y - 4, 255, 255, 255,  cx - 6, y + 16, 60, 120, 230,  cx + 6, y + 16, 60, 120, 230);
    rect(L_PLAYER, cx - 1, y + 6, 3, 8, 255, 150, 30);

    tri(L_PLAYER, cx, y + 1, 220, 255, 255,  cx - 3, y + 9, 40, 160, 230,  cx + 3, y + 9, 40, 160, 230);
}

/* ---------- Musuh ---------- */

static void drawEnemy(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 8;
    int glow = 160 + ((frame / 3) & 1) * 90;

    switch (e->type) {
    case E_DRONE:
        glowDisc(cx, y + 4, 4, glow, glow / 2, 0, 8);
        tri(L_ENEMY, x - 3,  y,      200, 40, 60,   x + 6,  y + 6,  120, 10, 40,  x + 4,  y + 13, 160, 20, 60);
        tri(L_ENEMY, x + 19, y,      200, 40, 60,   x + 10, y + 6,  120, 10, 40,  x + 12, y + 13, 160, 20, 60);
        tri(L_ENEMY, cx,     y + 17, 255, 140, 140, cx - 7, y,      170, 25, 60,  cx + 7, y,      170, 25, 60);
        break;

    case E_ZIGZAG:
        glowDisc(cx, y + 5, 4, 255, 255, 120, 8);
        tri(L_ENEMY, x - 4,  y + 4,  60, 255, 140,  x + 6,  y + 2,  10, 120, 60,  x + 6,  y + 12, 20, 160, 90);
        tri(L_ENEMY, x + 20, y + 4,  60, 255, 140,  x + 10, y + 2,  10, 120, 60,  x + 10, y + 12, 20, 160, 90);
        tri(L_ENEMY, cx,     y + 18, 200, 255, 220, cx - 6, y,      30, 190, 110, cx + 6, y,      30, 190, 110);
        break;

    case E_TANK:
        glowDisc(cx - 3, y + 7, 3, glow, 60, glow, 8);
        glowDisc(cx + 4, y + 7, 3, glow, 60, glow, 8);
        tri(L_ENEMY, x - 6,  y + 2,  190, 100, 255, x + 8,  y + 8,  90, 40, 160, x + 4,  y + 18, 120, 60, 200);
        tri(L_ENEMY, x + 22, y + 2,  190, 100, 255, x + 8,  y + 8,  90, 40, 160, x + 12, y + 18, 120, 60, 200);
        tri(L_ENEMY, cx,     y + 22, 230, 180, 255, cx - 10, y,     110, 50, 190, cx + 10, y,     110, 50, 190);
        break;

    case E_BOSS: {
        int bx = x, by = y;
        rect(L_FX, bx - 6, by - 8, 60, 4, 50, 10, 10);
        int w = e->hp * 60 / 30;
        if (w < 0) w = 0;
        rect(L_FX, bx - 6, by - 8, w, 4, 255, 60, 60);

        glowDisc(bx + 19, by + 15, 5, glow, 40, 0, 8);
        glowDisc(bx + 29, by + 15, 5, glow, 40, 0, 8);
        tri(L_ENEMY, bx - 16, by + 8,  255, 140, 30, bx + 16, by + 14, 150, 40, 10, bx + 10, by + 34, 200, 70, 20);
        tri(L_ENEMY, bx + 64, by + 8,  255, 140, 30, bx + 32, by + 14, 150, 40, 10, bx + 38, by + 34, 200, 70, 20);
        tri(L_ENEMY, bx + 24, by + 44, 255, 220, 120, bx,      by,      170, 50, 20, bx + 48, by,      170, 50, 20);
        break;
    }
    }
}

/* ---------- Laser ---------- */

static void drawLaser(int x, int y, int frame) {
    int fl = ((frame / 2) & 1) * 20;
    glowDisc(x + 2, y + 4, 7, 80 + fl, 200, 255, 8);
    rect(L_BULLET, x,     y - 2, 4, 18, 255, 255, 255);
    rect(L_BULLET, x + 1, y - 5, 2, 4,  255, 255, 255);
}

/* ---------- Ledakan ---------- */

static void drawExplosion(const Explosion *e) {
    int t     = EXPLOSION_LEN - e->timer;
    int grow  = e->big ? 3 : 2;
    int rad   = 4 + t * grow;
    int fade  = e->timer * 255 / EXPLOSION_LEN;
    int x = e->x, y = e->y;

    if (t > 2) {
        int rw = rad + 6;
        for (int i = 0; i < 16; i += 2) {
            int x1 = x + cosI(i)     * rw / 127;
            int y1 = y + sinI(i)     * rw / 127;
            int x2 = x + cosI(i + 2) * rw / 127;
            int y2 = y + sinI(i + 2) * rw / 127;
            int x3 = x + cosI(i + 1) * (rw + 4) / 127;
            int y3 = y + sinI(i + 1) * (rw + 4) / 127;
            tri(L_FX, x1, y1, fade / 3, fade / 3, fade / 2,
                       x2, y2, fade / 3, fade / 3, fade / 2,
                       x3, y3, 0, 0, 0);
        }
    }

    burst(L_FX, x, y, rad + 6, rad / 2, t / 2,
          255, 230, 120, fade, fade / 3, 0);

    disc(L_FX, x, y, rad, 255, 255, 200, fade, fade / 2, 0);
    if (rad > 8)
        glowDisc(x, y, rad / 2, 255, 255, 255, 8);
}

static void drawSpark(const Spark *s) {
    int fade = s->life * 255 / s->maxlife;
    int px = s->x >> 4, py = s->y >> 4;
    int len = 2 + s->size;
    int vx = s->vx >> 4, vy = s->vy >> 4;
    int r = s->r * fade / 255, g = s->g * fade / 255, b = s->b * fade / 255;

    tri(L_FX, px, py, r, g, b,
              px - vx * len, py - vy * len, 0, 0, 0,
              px + 1, py + 1, r / 2, g / 2, b / 2);
    rect(L_FX, px - 1, py - 1, 2 + s->size / 2, 2 + s->size / 2,
         r + 40, g + 40, b + 40);
}

static void spawnSparks(int x, int y, int n, int r, int g, int b, int big) {
    for (int i = 0, made = 0; i < MAX_SPARKS && made < n; i++) {
        if (sparks[i].alive) continue;
        sparks[i].x = x << 4;
        sparks[i].y = y << 4;
        sparks[i].vx = (rand() % 128) - 64;
        sparks[i].vy = (rand() % 128) - 64;
        sparks[i].maxlife = 16 + rand() % 12;
        sparks[i].life = sparks[i].maxlife;
        sparks[i].r = r; sparks[i].g = g; sparks[i].b = b;
        sparks[i].size = big ? 2 : 0;
        sparks[i].alive = 1;
        made++;
    }
}

static void spawnExplosion(int x, int y, int big) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!explosions[i].alive) {
            explosions[i].x = x;
            explosions[i].y = y;
            explosions[i].timer = EXPLOSION_LEN;
            explosions[i].big = big;
            explosions[i].alive = 1;
            break;
        }
    }
    spawnSparks(x, y, big ? 14 : 7, 255, 210, 90, big);
}

/* ---------- HUD ---------- */

static void drawHUD(int lives, int level) {
    for (int i = 0; i < START_LIVES; i++) {
        int lx = SCREEN_W - 26 - i * 18;
        if (i < lives) {
            tri(L_HUD_TOP, lx + 5, 8,  255, 255, 255,  lx, 21, 60, 120, 230,  lx + 10, 21, 60, 120, 230);
            rect(L_HUD_TOP, lx + 4, 20, 2, 3, 255, 150, 30);
        } else {
            tri(L_HUD_TOP, lx + 5, 8,  60, 60, 80,  lx, 21, 30, 30, 50,  lx + 10, 21, 30, 30, 50);
        }
    }

    for (int i = 0; i < 10; i++) {
        int cx = 10 + i * 9;
        if (i < level) glowDisc(cx, 26, 3, 0, 230, 140, 8);
        else           discLo(L_HUD_TOP, cx, 26, 2, 30, 40, 60, 20, 25, 40);
    }

    roundedPanel(L_HUD_BASE, 0, 0, SCREEN_W, HUD_H, 10, 20, 40, 90, 1);
    rect(L_HUD_BASE, 0, HUD_H, SCREEN_W, 1, 90, 170, 255);
    setBlendMode(L_HUD_BASE, 0);
}

/* ---------- Main menu ---------- */

static void drawMenu(int frame) {
    roundedPanel(L_HUD_BASE, 40, 52, 240, 44, 12, 255, 150, 40, 0);
    roundedPanel(L_HUD_TOP, 40, 52, 240, 44, 12, 255, 255, 255, 1);
    setBlendMode(L_HUD_TOP, 0);

    glowDisc(160, 74, 90, 255, 180, 60, 8);

    int bob = sinS(frame / 2) / 20;
    drawPlayer(SCREEN_W / 2 - 8, 140 + bob, frame);
}

/* ---------- Logika ---------- */

static int overlap(int ax, int ay, int aw, int ah,
                   int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx &&
           ay < by + bh && ay + ah > by;
}

static void enemySize(const Enemy *e, int *w, int *h) {
    switch (e->type) {
    case E_BOSS: *w = 48; *h = 44; break;
    case E_TANK: *w = 22; *h = 22; break;
    default:     *w = 16; *h = 18; break;
    }
}

static void resetGame(int *px, int *py, int *score, int *lives,
                      int *invuln, int *frame, int *cooldown, int *bossOn) {
    for (int i = 0; i < MAX_BULLETS; i++)    bullets[i].alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)    enemies[i].alive = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) explosions[i].alive = 0;
    for (int i = 0; i < MAX_SPARKS; i++)     sparks[i].alive = 0;
    *px       = SCREEN_W / 2 - 8;
    *py       = SCREEN_H - 34;
    *score    = 0;
    *lives    = START_LIVES;
    *invuln   = 0;
    *frame    = 0;
    *cooldown = 0;
    *bossOn   = 0;
}

static int pickEnemyType(int level) {
    int r = rand() % 100;
    if (level >= 5 && r < 15) return E_TANK;
    if (level >= 3 && r < 40) return E_ZIGZAG;
    return E_DRONE;
}

int main(void) {
    int px, py, score, lives, invuln, frame, cooldown, bossOn;
    int state = STATE_MENU;
    int nextBossScore = 30;

    initVideo();
    initStars();

    FntLoad(960, 0);
    FntOpen(8, TEXT_Y, SCREEN_W - 16, 200, 0, 512);

    InitPAD(padbuf[0], 34, padbuf[1], 34);
    StartPAD();
    ChangeClearPAD(0);

    resetGame(&px, &py, &score, &lives, &invuln, &frame, &cooldown, &bossOn);

    ClearOTagR(buffers[0].ot, OT_LEN);
    nextpri = buffers[0].buf;

    while (1) {
        PADTYPE *pad = (PADTYPE *)padbuf[0];
        uint16_t btn = 0xFFFF;
        if (pad->stat == 0) btn = pad->btn;

        int level = 1 + score / 10;
        if (level > 10) level = 10;

        for (int i = 0; i < NUM_STARS; i++) {
            stars[i].y += stars[i].speed;
            if (stars[i].y >= SCREEN_H) {
                stars[i].y = 0;
                stars[i].x = rand() % SCREEN_W;
            }
        }
        updateShootingStar(frame);

        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (!explosions[i].alive) continue;
            if (--explosions[i].timer <= 0) explosions[i].alive = 0;
        }
        for (int i = 0; i < MAX_SPARKS; i++) {
            if (!sparks[i].alive) continue;
            sparks[i].x += sparks[i].vx;
            sparks[i].y += sparks[i].vy;
            if (--sparks[i].life <= 0) sparks[i].alive = 0;
        }

        if (state == STATE_MENU) {
            if (!(btn & PAD_START) || !(btn & PAD_CROSS)) {
                resetGame(&px, &py, &score, &lives, &invuln, &frame, &cooldown, &bossOn);
                nextBossScore = 30;
                state = STATE_PLAY;
            }
        } else if (state == STATE_PLAY) {
            if (!(btn & PAD_LEFT)  && px > 0)             px -= 3;
            if (!(btn & PAD_RIGHT) && px < SCREEN_W - 16) px += 3;
            if (!(btn & PAD_UP)    && py > HUD_H + 16)    py -= 2;
            if (!(btn & PAD_DOWN)  && py < SCREEN_H - 24) py += 2;

            if (cooldown > 0) cooldown--;
            if (!(btn & PAD_CROSS) && cooldown == 0) {
                int shots = (level >= 6) ? 3 : (level >= 3 ? 2 : 1);
                int made = 0;
                for (int i = 0; i < MAX_BULLETS && made < shots; i++) {
                    if (bullets[i].alive) continue;
                    int off = (shots == 1) ? 6 : (shots == 2 ? (made ? 12 : 0) : made * 6);
                    bullets[i].x = px + off;
                    bullets[i].y = py - 6;
                    bullets[i].dx = (shots == 3) ? (made - 1) : 0;
                    bullets[i].alive = 1;
                    made++;
                }
                cooldown = (level >= 5) ? 5 : 6;
            }

            int spawnEvery = 42 - level * 3;
            if (spawnEvery < 12) spawnEvery = 12;
            if (!bossOn && frame % spawnEvery == 0) {
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (enemies[i].alive) continue;
                    enemies[i].type = pickEnemyType(level);
                    enemies[i].x = 10 + rand() % (SCREEN_W - 40);
                    enemies[i].y = HUD_H + 2;
                    enemies[i].hp = (enemies[i].type == E_TANK) ? 4 : 1;
                    enemies[i].t = rand() % 32;
                    enemies[i].alive = 1;
                    break;
                }
            }

            if (!bossOn && score >= nextBossScore) {
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (enemies[i].alive) continue;
                    enemies[i].type = E_BOSS;
                    enemies[i].x = SCREEN_W / 2 - 24;
                    enemies[i].y = HUD_H + 2;
                    enemies[i].hp = 30;
                    enemies[i].t = 0;
                    enemies[i].alive = 1;
                    bossOn = 1;
                    break;
                }
            }

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].alive) continue;
                bullets[i].y -= 7;
                bullets[i].x += bullets[i].dx;
                if (bullets[i].y < HUD_H) bullets[i].alive = 0;
            }

            if (invuln > 0) invuln--;

            int baseSpeed = 1 + level / 5;

            for (int i = 0; i < MAX_ENEMIES; i++) {
                Enemy *e = &enemies[i];
                if (!e->alive) continue;

                e->t++;
                int w, h;
                enemySize(e, &w, &h);

                if (e->type == E_BOSS) {
                    if (e->y < HUD_H + 20) e->y++;
                    e->x = SCREEN_W / 2 - 24 + sinS(e->t / 3) * 90 / 127;
                } else if (e->type == E_ZIGZAG) {
                    e->y += baseSpeed + 1;
                    e->x += sinS(e->t) / 40;
                    if (e->x < 0) e->x = 0;
                    if (e->x > SCREEN_W - 20) e->x = SCREEN_W - 20;
                } else if (e->type == E_TANK) {
                    e->y += 1;
                } else {
                    e->y += baseSpeed;
                }

                if (e->type != E_BOSS && e->y > SCREEN_H) e->alive = 0;

                for (int j = 0; j < MAX_BULLETS; j++) {
                    if (!bullets[j].alive || !e->alive) continue;
                    if (overlap(bullets[j].x, bullets[j].y, 4, 14, e->x, e->y, w, h)) {
                        bullets[j].alive = 0;
                        e->hp--;
                        spawnSparks(bullets[j].x + 2, bullets[j].y, 4, 120, 230, 255, 0);
                        if (e->hp <= 0) {
                            spawnExplosion(e->x + w / 2, e->y + h / 2,
                                           e->type == E_BOSS || e->type == E_TANK);
                            e->alive = 0;
                            if (e->type == E_BOSS) {
                                score += 10;
                                nextBossScore += 30;
                                bossOn = 0;
                                spawnExplosion(e->x + 10, e->y + 10, 1);
                                spawnExplosion(e->x + 38, e->y + 30, 1);
                            } else if (e->type == E_TANK) {
                                score += 3;
                            } else {
                                score += 1;
                            }
                        }
                    }
                }

                if (e->alive && invuln == 0 &&
                    overlap(px, py, 16, 16, e->x, e->y, w, h)) {
                    spawnExplosion(e->x + w / 2, e->y + h / 2, 0);
                    if (e->type != E_BOSS) e->alive = 0;
                    lives--;
                    invuln = 90;
                    if (lives <= 0) {
                        spawnExplosion(px + 8, py + 8, 1);
                        state = STATE_GAMEOVER;
                    }
                }
            }
        } else {
            if (!(btn & PAD_START)) {
                state = STATE_MENU;
            }
        }

        /* ---------- Gambar ---------- */

        drawBackground(frame);
        drawStars(frame);
        drawShootingStar();

        if (state == STATE_MENU) {
            drawMenu(frame);
        } else {
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (enemies[i].alive) drawEnemy(&enemies[i], frame);

            for (int i = 0; i < MAX_BULLETS; i++)
                if (bullets[i].alive) drawLaser(bullets[i].x, bullets[i].y, frame);

            if (state == STATE_PLAY && (invuln == 0 || (frame / 4) % 2 == 0))
                drawPlayer(px, py, frame);

            for (int i = 0; i < MAX_EXPLOSIONS; i++)
                if (explosions[i].alive) drawExplosion(&explosions[i]);

            for (int i = 0; i < MAX_SPARKS; i++)
                if (sparks[i].alive) drawSpark(&sparks[i]);

            drawHUD(lives, level);
        }

        if (state == STATE_MENU) {
            FntPrint(-1, "\n\n\n   SPACE SHOOTER\n\n\n\n\n\n\n\n\n\n\n\n\n\n   PRESS START");
        } else if (state == STATE_PLAY) {
            FntPrint(-1, "SCORE %d", score);
        } else {
            FntPrint(-1, "SCORE %d\n\n\n\n\n\n\n\n    GAME OVER\n\n    FINAL %d\n\n\n    PRESS START", score, score);
        }
        FntFlush(-1);

        /* HARUS terakhir: mengatur mode blend layer L_GLOW sebelum flip,
           supaya semua glowDisc() yang ditambahkan sepanjang frame ini
           diproses GPU dengan mode aditif. */
        setBlendMode(L_GLOW, 1);

        flip();
        frame++;
    }

    return 0;
}
