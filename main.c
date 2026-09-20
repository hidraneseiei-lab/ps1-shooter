#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>

#define OT_LEN     9
#define BUFFER_LEN 131072
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

#define MAX_PLAYERS 4
static uint8_t padbuf[2][34];
static uint16_t prevBtn[MAX_PLAYERS] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };

enum { L_HUD_TOP = 0, L_HUD_BASE = 1, L_GLOW = 2, L_FX = 3, L_PLAYER = 4,
       L_BULLET = 5, L_ENEMY = 6, L_PLANET = 7, L_BG = 8 };

typedef struct { int x, y, alive, hp, type, t, shield, timer, vx; } Enemy;
typedef struct { int x, y, alive, dx; } Bullet;
typedef struct { int x, y, alive, vy; } EBullet;
typedef struct { int x, y, alive, timer, big; } Explosion;
typedef struct { int x, y, vx, vy, life, maxlife, r, g, b, alive, size; } Spark;
typedef struct { int x, y, speed, layer, phase; } Star;
typedef struct { int x, y, alive, type; } Item;
typedef struct { int x, y, phase, timer, alive; } NovaBurst;

#define MAX_BULLETS    48
#define MAX_EBULLETS   16
#define MAX_ENEMIES    24
#define MAX_EXPLOSIONS 12
#define MAX_SPARKS     56
#define MAX_ITEMS      10
#define MAX_NOVABURST  4
#define NUM_STARS      50
#define START_LIVES    3
#define MAX_LIVES      5
#define EXPLOSION_LEN  20
#define NOVA_WARN      24
#define NOVA_ACTIVE    18
#define NOVA_LEN       220

#define HUD_H   30
#define TEXT_Y  12

#define MAX_STACK        3
#define SHIELD_DURATION  260
#define POWER_DURATION   200
#define SPEED_DURATION   220

#define ENEMY_CAP_BASE   6

enum { STATE_MENU, STATE_PLAY, STATE_GAMEOVER, STATE_GACHA, STATE_SKINSELECT };
enum { E_DRONE = 0, E_ZIGZAG, E_TANK, E_SPINNER, E_SHOOTER, E_SPLITTER, E_SHIELDED,
       E_STINGER, E_ORBITER, E_PHANTOM, E_JUGGERNAUT, E_NOVA, E_SHARD, E_BOSS };
enum { ITEM_HEAL = 0, ITEM_SHIELD = 1, ITEM_POWER = 2, ITEM_SPEED = 3 };

/* ---------- [MAD] Boss rahasia ---------- */
#define MAD_HP        90
#define MAX_MISSILES  14
#define MAX_BOMBS     3
#define MAX_MLASERS   3
#define MLASER_WARN   40
#define MLASER_FIRE   108   /* 1,8 detik @60fps */
#define MAD_ATTACKS   5

enum { MAD_IDLE = 0, MAD_LOCK, MAD_DASH, MAD_BOMB, MAD_LASER, MAD_SWARM, MAD_NOVA, MAD_DYING };
/* Indeks serangan: 0 rudal balistik, 1 bom, 2 laser, 3 hujan rudal, 4 nova */

typedef struct {
    int alive, x, y, hp, maxhp;
    int state, timer, lastAtk;
    int dir, dirT;
    int vx, vy;
    int tx, ty;
    int hit;
    int dyingT;
    int phase2;
} MadBoss;

typedef struct { int x, y, vx, vy, hp, alive; } Missile;
typedef struct { int x, y, timer, alive; } Bomb;
typedef struct { int x, y, angle, timer, alive; } MLaser;

static MadBoss  mad;
static Missile  missiles[MAX_MISSILES];
static Bomb     mbombs[MAX_BOMBS];
static MLaser   mlasers[MAX_MLASERS];
static int      madSpawned = 0;
static int      konami = 0;
static int      forceMad = 0;

/* ---------- Pemain ---------- */
typedef struct {
    int active;
    int alive;
    int x, y;
    int lives, invuln, cooldown;
    int score;
    int skin;
    int shieldStack, shieldTimer;
    int powerStack,  powerTimer;
    int speedStack,  speedTimer;
} Player;

static Player players[MAX_PLAYERS];

static Bullet    bullets[MAX_BULLETS];
static EBullet   ebullets[MAX_EBULLETS];
static Enemy     enemies[MAX_ENEMIES];
static Explosion explosions[MAX_EXPLOSIONS];
static Spark     sparks[MAX_SPARKS];
static Star      stars[NUM_STARS];
static Item      items[MAX_ITEMS];
static NovaBurst novabursts[MAX_NOVABURST];

static int shootX = -100, shootY = 0, shootT = 0;

static const int playerColor[MAX_PLAYERS][3] = {
    {  80, 160, 255 },
    { 255,  90,  90 },
    {  90, 230, 120 },
    { 255, 220,  80 }
};

static const int8_t sin16[16] = {
    0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117, -127, -117, -90, -49
};
static int sinI(int t) { return sin16[t & 15]; }
static int cosI(int t) { return sin16[(t + 4) & 15]; }

static const int8_t sinTab[32] = {
    0, 24, 48, 70, 89, 105, 116, 124, 127, 124, 116, 105, 89, 70, 48, 24,
    0, -24, -48, -70, -89, -105, -116, -124, -127, -124, -116, -105, -89, -70, -48, -24
};
static int sinS(int t) { return sinTab[t & 31]; }

static const int8_t sin64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12
};
static int sinO(int t) { return sin64[t & 63]; }
static int cosO(int t) { return sin64[(t + 16) & 63]; }

static int pressedP(int pl, uint16_t btn, uint16_t mask) {
    return !(btn & mask) && (prevBtn[pl] & mask);
}

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

static void rect(int layer, int x, int y, int w, int h, int r, int g, int b) {
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

static void setBlendMode(int layer, int mode) {
    DR_TPAGE *p = (DR_TPAGE *)nextpri;
    setDrawTPage(p, 0, 1, getTPage(0, mode, 0, 0));
    addPrim(&buffers[active].ot[layer], p);
    nextpri += sizeof(DR_TPAGE);
}

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

static void beamQuad(int layer, int cx, int cy, int angle, int len, int thick,
                     int r, int g, int b, int trans) {
    int dx = cosI(angle), dy = sinI(angle);
    int nx = -dy, ny = dx;
    int hx1 = cx + dx * len / 127, hy1 = cy + dy * len / 127;
    int hx2 = cx - dx * len / 127, hy2 = cy - dy * len / 127;
    int ox = nx * thick / 254, oy = ny * thick / 254;
    POLY_F4 *p = (POLY_F4 *)nextpri;
    setPolyF4(p);
    if (trans) setSemiTrans(p, 1);
    setXY4(p, hx1 + ox, hy1 + oy, hx1 - ox, hy1 - oy, hx2 + ox, hy2 + oy, hx2 - ox, hy2 - oy);
    setRGB0(p, clamp255(r), clamp255(g), clamp255(b));
    addPrim(&buffers[active].ot[layer], p);
    nextpri += sizeof(POLY_F4);
}

static int beamHit(int bx, int by, int angle, int len, int halfThick, int px, int py) {
    int dx = cosI(angle), dy = sinI(angle);
    int rx = px - bx, ry = py - by;
    int along = (rx * dx + ry * dy) / 127;
    if (along < -len || along > len) return 0;
    int cross = rx * dy - ry * dx;
    if (cross < 0) cross = -cross;
    return (cross / 127) < halfThick;
}

/* ---------- UI melengkung ---------- */

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

/* ---------- Planet ---------- */

static void planetSphere(int cx, int cy, int rad,
                         int litR, int litG, int litB,
                         int darkR, int darkG, int darkB) {
    for (int i = 0; i < 16; i++) {
        int x1 = cx + cosI(i)     * rad / 127;
        int y1 = cy + sinI(i)     * rad / 127;
        int x2 = cx + cosI(i + 1) * rad / 127;
        int y2 = cy + sinI(i + 1) * rad / 127;
        int litAmt = cosI(i) + 127;
        int r = darkR + (litR - darkR) * litAmt / 254;
        int g = darkG + (litG - darkG) * litAmt / 254;
        int b = darkB + (litB - darkB) * litAmt / 254;
        tri(L_PLANET, cx, cy, r, g, b,
                      x1, y1, r / 3, g / 3, b / 3,
                      x2, y2, r / 3, g / 3, b / 3);
    }
    glowDisc(cx - rad / 4, cy - rad / 4, rad + 6, litR, litG, litB, 8);
}

/* ---------- Starfield ---------- */

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
    if (shootT > 0) { shootX += 6; shootY += 4; shootT--; }
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

/* ---------- Skin system ---------- */

typedef struct {
    const char *name;
    int hullR, hullG, hullB;
    int wingR, wingG, wingB;
    int glowR, glowG, glowB;
    int rarity;
} SkinDef;

#define NUM_SKINS 6
static const SkinDef skinTable[NUM_SKINS] = {
    { "AZURE",   60,120,230,  20,60,200,   255,150,40,  0 },
    { "CRIMSON", 230,40,60,   180,20,40,   255,80,20,   0 },
    { "EMERALD", 40,220,140,  20,160,90,   120,255,180, 1 },
    { "VIOLET",  180,60,230,  120,20,180,  220,120,255, 1 },
    { "GOLD",    255,210,60,  220,160,20,  255,240,180, 2 },
    { "PRISM",   240,240,255, 180,200,255, 255,255,255, 3 },
};

static unsigned int unlockedMask = 1;
static int skinCursor  = 0;

/* ---------- Gacha system ---------- */

#define GACHA_COST 50
static int gems = 0;
static int gachaResultSkin = -1;
static int gachaResultDup  = 0;
static int gachaFlashT     = 0;

static void awardGems(int score) { gems += score / 2 + 5; }

static int gachaRollRarity(void) {
    int r = rand() % 100;
    if (r < 60) return 0;
    if (r < 85) return 1;
    if (r < 97) return 2;
    return 3;
}

static int gachaRoll(void) {
    int rarity = gachaRollRarity();
    int pool[NUM_SKINS], n = 0;
    for (int i = 0; i < NUM_SKINS; i++)
        if (skinTable[i].rarity == rarity) pool[n++] = i;
    if (n == 0) return 0;
    return pool[rand() % n];
}

static int doGachaPull(void) {
    if (gems < GACHA_COST) return 0;
    gems -= GACHA_COST;
    int idx = gachaRoll();
    gachaResultSkin = idx;
    if (unlockedMask & (1u << idx)) { gachaResultDup = 1; gems += 15; }
    else { gachaResultDup = 0; unlockedMask |= (1u << idx); }
    return 1;
}

/* ---------- Item ---------- */

static void putItem(int x, int y) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (items[i].alive) continue;
        items[i].x = x; items[i].y = y;
        items[i].type = rand() % 4;
        items[i].alive = 1;
        return;
    }
}

static void spawnItem(int x, int y)      { if (rand() % 100 < 8) putItem(x, y); }
static void forceSpawnItem(int x, int y) { putItem(x, y); }

static void drawItem(const Item *it, int frame) {
    int x = it->x, y = it->y;
    int cx = x + 5, cy = y + 5;
    int pulse = 7 + sinS(frame * 2) / 40;

    switch (it->type) {
    case ITEM_HEAL:
        glowDisc(cx, cy, pulse, 60, 255, 120, 8);
        rect(L_FX, x + 3, y,     4, 10, 255, 255, 255);
        rect(L_FX, x,     y + 3, 10, 4, 255, 255, 255);
        break;
    case ITEM_SHIELD:
        glowDisc(cx, cy, pulse, 80, 180, 255, 8);
        disc(L_FX, cx, cy, 5, 160, 220, 255, 20, 70, 140);
        break;
    case ITEM_POWER:
        glowDisc(cx, cy, pulse, 255, 120, 40, 8);
        tri(L_FX, cx, y, 255, 220, 140, x + 1, y + 10, 255, 120, 40, x + 9, y + 10, 255, 120, 40);
        break;
    case ITEM_SPEED:
        glowDisc(cx, cy, pulse, 255, 240, 80, 8);
        tri(L_FX, x + 2, y, 255, 255, 180, x + 2, y + 10, 255, 230, 60, x + 9, y + 5, 255, 230, 60);
        break;
    }
}

static void applyItem(Player *pl, int type) {
    switch (type) {
    case ITEM_HEAL:
        if (pl->lives < MAX_LIVES) pl->lives++;
        break;
    case ITEM_SHIELD:
        pl->shieldStack = (pl->shieldStack < MAX_STACK) ? pl->shieldStack + 1 : MAX_STACK;
        pl->shieldTimer = SHIELD_DURATION;
        break;
    case ITEM_POWER:
        pl->powerStack = (pl->powerStack < MAX_STACK) ? pl->powerStack + 1 : MAX_STACK;
        pl->powerTimer = POWER_DURATION;
        break;
    case ITEM_SPEED:
        pl->speedStack = (pl->speedStack < MAX_STACK) ? pl->speedStack + 1 : MAX_STACK;
        pl->speedTimer = SPEED_DURATION;
        break;
    }
}

/* ---------- Ikon buff ---------- */

static void drawBuffIcon(int x, int y, int r, int g, int b,
                         int shape, int stack, int timer, int maxTimer) {
    disc(L_HUD_TOP, x, y, 6, r / 3, g / 3, b / 3, 10, 10, 15);
    glowDisc(x, y, 7, r, g, b, 6);

    switch (shape) {
    case 0: disc(L_HUD_TOP, x, y, 3, r, g, b, r / 2, g / 2, b / 2); break;
    case 1: tri(L_HUD_TOP, x, y - 3, 255, 255, 255, x - 3, y + 3, r, g, b, x + 3, y + 3, r, g, b); break;
    case 2: tri(L_HUD_TOP, x - 3, y - 3, 255, 255, 255, x - 3, y + 3, r, g, b, x + 3, y, r, g, b); break;
    }
    for (int i = 0; i < stack; i++)
        rect(L_HUD_TOP, x - 5 + i * 4, y + 8, 3, 2, r, g, b);
    int w = 12 * timer / maxTimer;
    rect(L_HUD_BASE, x - 6, y + 11, 12, 2, 40, 40, 50);
    rect(L_HUD_BASE, x - 6, y + 11, w, 2, r, g, b);
}

static void drawActiveBuffs(void) {
    for (int p = 0; p < MAX_PLAYERS; p++) {
        const Player *pl = &players[p];
        if (!pl->active || !pl->alive) continue;
        int x = 14 + p * 78;
        int y = SCREEN_H - 22;
        if (pl->shieldStack > 0) { drawBuffIcon(x, y, 80, 180, 255, 0, pl->shieldStack, pl->shieldTimer, SHIELD_DURATION); x += 20; }
        if (pl->powerStack  > 0) { drawBuffIcon(x, y, 255, 120, 40, 1, pl->powerStack,  pl->powerTimer,  POWER_DURATION);  x += 20; }
        if (pl->speedStack  > 0) { drawBuffIcon(x, y, 255, 240, 80, 2, pl->speedStack,  pl->speedTimer,  SPEED_DURATION);  x += 20; }
    }
}

static void drawShieldAura(const Player *pl, int frame) {
    if (pl->shieldStack <= 0) return;
    int cx = pl->x + 8, cy = pl->y + 8;
    int rad = 12 + pl->shieldStack * 4;
    glowDisc(cx, cy, rad, 80, 180, 255, 8);
    for (int i = 0; i < pl->shieldStack; i++) {
        int a = (frame * 2 + i * (16 / MAX_STACK)) & 15;
        int ox = cx + cosI(a) * rad / 127;
        int oy = cy + sinI(a) * rad / 127;
        glowDisc(ox, oy, 4, 150, 220, 255, 8);
    }
}

/* ---------- Pesawat pemain ---------- */

static void drawPlayer(int x, int y, int frame, int skin) {
    const SkinDef *s = &skinTable[skin];
    int cx = x + 8;
    int fl = 4 + (frame / 2) % 4;

    glowDisc(cx - 4, y + 18, 4 + fl / 2, s->glowR, s->glowG, s->glowB, 8);
    glowDisc(cx + 4, y + 18, 4 + fl / 2, s->glowR, s->glowG, s->glowB, 8);
    tri(L_PLAYER, cx - 5, y + 16, 255, 255, 220,  cx - 2, y + 16, 255, 255, 220,
                  cx - 4, y + 16 + fl, s->glowR, s->glowG / 3, 0);
    tri(L_PLAYER, cx + 2, y + 16, 255, 255, 220,  cx + 5, y + 16, 255, 255, 220,
                  cx + 4, y + 16 + fl, s->glowR, s->glowG / 3, 0);

    tri(L_PLAYER, cx - 3, y + 4,  clamp255(s->wingR+50), clamp255(s->wingG+50), 255,
                  x - 8,  y + 18, s->wingR, s->wingG, s->wingB,
                  cx - 3, y + 15, clamp255(s->wingR+20), clamp255(s->wingG+20), clamp255(s->wingB+20));
    tri(L_PLAYER, cx + 3, y + 4,  clamp255(s->wingR+50), clamp255(s->wingG+50), 255,
                  x + 24, y + 18, s->wingR, s->wingG, s->wingB,
                  cx + 3, y + 15, clamp255(s->wingR+20), clamp255(s->wingG+20), clamp255(s->wingB+20));
    rect(L_PLAYER, x - 8,  y + 16, 3, 3, 255, 240, 80);
    rect(L_PLAYER, x + 21, y + 16, 3, 3, 255, 240, 80);

    tri(L_PLAYER, cx, y - 4, 255, 255, 255,
                  cx - 6, y + 16, s->hullR, s->hullG, s->hullB,
                  cx + 6, y + 16, s->hullR, s->hullG, s->hullB);
    rect(L_PLAYER, cx - 1, y + 6, 3, 8, 255, 150, 30);

    tri(L_PLAYER, cx, y + 1, 220, 255, 255,
                  cx - 3, y + 9, 40, 160, 230,
                  cx + 3, y + 9, 40, 160, 230);
}

static void drawPlayerTag(int p, int x, int y) {
    const int *c = playerColor[p];
    tri(L_HUD_TOP, x + 8, y - 12, 255, 255, 255,
                   x + 3, y - 20, c[0], c[1], c[2],
                   x + 13, y - 20, c[0], c[1], c[2]);
}

/* ---------- Musuh ---------- */

static void drawDrone(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 8;
    int glow = 160 + ((frame / 3) & 1) * 90;
    glowDisc(cx, y + 4, 4, glow, glow / 2, 0, 8);
    tri(L_ENEMY, x - 3,  y,      200, 40, 60,   x + 6,  y + 6,  120, 10, 40,  x + 4,  y + 13, 160, 20, 60);
    tri(L_ENEMY, x + 19, y,      200, 40, 60,   x + 10, y + 6,  120, 10, 40,  x + 12, y + 13, 160, 20, 60);
    tri(L_ENEMY, cx,     y + 17, 255, 140, 140, cx - 7, y,      170, 25, 60,  cx + 7, y,      170, 25, 60);
    rect(L_ENEMY, x - 2, y + 2, 2, 2, 255, 200, 200);
    rect(L_ENEMY, x + 18, y + 2, 2, 2, 255, 200, 200);
}

static void drawZigzag(const Enemy *e, int frame) {
    (void)frame;
    int x = e->x, y = e->y, cx = x + 8;
    glowDisc(cx, y + 5, 4, 255, 255, 120, 8);
    tri(L_ENEMY, x - 4,  y + 4,  60, 255, 140,  x + 6,  y + 2,  10, 120, 60,  x + 6,  y + 12, 20, 160, 90);
    tri(L_ENEMY, x + 20, y + 4,  60, 255, 140,  x + 10, y + 2,  10, 120, 60,  x + 10, y + 12, 20, 160, 90);
    tri(L_ENEMY, cx,     y + 18, 200, 255, 220, cx - 6, y,      30, 190, 110, cx + 6, y,      30, 190, 110);
    discLo(L_ENEMY, cx, y + 6, 2, 255, 255, 255, 200, 255, 200);
}

static void drawTank(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 8;
    int glow = 160 + ((frame / 3) & 1) * 90;
    glowDisc(cx - 3, y + 7, 3, glow, 60, glow, 8);
    glowDisc(cx + 4, y + 7, 3, glow, 60, glow, 8);
    tri(L_ENEMY, x - 6,  y + 2,  190, 100, 255, x + 8,  y + 8,  90, 40, 160, x + 4,  y + 18, 120, 60, 200);
    tri(L_ENEMY, x + 22, y + 2,  190, 100, 255, x + 8,  y + 8,  90, 40, 160, x + 12, y + 18, 120, 60, 200);
    tri(L_ENEMY, cx,     y + 22, 230, 180, 255, cx - 10, y,     110, 50, 190, cx + 10, y,     110, 50, 190);
    rect(L_ENEMY, cx - 1, y + 4, 2, 4, 255, 240, 255);
}

static void drawSpinner(const Enemy *e, int frame) {
    (void)frame;
    int cx = e->x + 11, cy = e->y + 11;
    int rot = (e->t / 2) & 15;
    glowDisc(cx, cy, 5, 220, 100, 255, 8);
    for (int i = 0; i < 4; i++) {
        int a  = (rot + i * 4) & 15;
        int a2 = (a + 2) & 15;
        int xo = cx + cosI(a)  * 13 / 127;
        int yo = cy + sinI(a)  * 13 / 127;
        int xb = cx + cosI(a2) * 5  / 127;
        int yb = cy + sinI(a2) * 5  / 127;
        tri(L_ENEMY, cx, cy, 180, 40, 210, xo, yo, 255, 130, 255, xb, yb, 130, 20, 160);
    }
    discLo(L_ENEMY, cx, cy, 2, 255, 255, 255, 200, 100, 240);
}

static void drawShooter(const Enemy *e, int frame) {
    int cx = e->x + 11, cy = e->y + 10;
    int glow = 150 + ((frame / 4) & 1) * 90;
    for (int i = 0; i < 16; i += 3) {
        int x1 = cx + cosI(i)     * 11 / 127;
        int y1 = cy + sinI(i)     * 11 / 127;
        int x2 = cx + cosI(i + 3) * 11 / 127;
        int y2 = cy + sinI(i + 3) * 11 / 127;
        tri(L_ENEMY, cx, cy, 255, 170, 40, x1, y1, 150, 60, 10, x2, y2, 150, 60, 10);
    }
    rect(L_ENEMY, cx - 2, cy + 8, 4, 9, glow, 90, 20);
    rect(L_ENEMY, cx - 7, cy - 2, 2, 5, 200, 120, 30);
    rect(L_ENEMY, cx + 5, cy - 2, 2, 5, 200, 120, 30);
    if (e->timer <= 2) glowDisc(cx, cy + 17, 4, 255, 200, 80, 8);
}

static void drawSplitter(const Enemy *e, int frame) {
    int cx = e->x + 10, cy = e->y + 10;
    int pulse = 3 + sinS(frame * 2) / 30;
    tri(L_ENEMY, cx, cy - 10 - pulse, 255, 255, 140, cx - 10, cy, 200, 200, 40, cx, cy + 10 + pulse, 160, 160, 20);
    tri(L_ENEMY, cx, cy - 10 - pulse, 255, 255, 140, cx + 10, cy, 200, 200, 40, cx, cy + 10 + pulse, 160, 160, 20);
    glowDisc(cx, cy, 5, 255, 255, 150, 8);
    discLo(L_ENEMY, cx, cy, 2, 255, 255, 255, 255, 255, 180);
}

static void drawShielded(const Enemy *e, int frame) {
    int cx = e->x + 10, cy = e->y + 10;
    tri(L_ENEMY, cx, cy + 9, 120, 200, 255, cx - 8, cy - 7, 40, 80, 170, cx + 8, cy - 7, 40, 80, 170);
    rect(L_ENEMY, cx - 1, cy - 3, 2, 4, 220, 250, 255);
    if (e->shield > 0) {
        int rot = (frame / 3) & 15;
        for (int i = 0; i < 16; i += 4) {
            int a  = (i + rot) & 15;
            int a2 = (a + 2) & 15;
            int x1 = cx + cosI(a)  * 13 / 127;
            int y1 = cy + sinI(a)  * 13 / 127;
            int x2 = cx + cosI(a2) * 13 / 127;
            int y2 = cy + sinI(a2) * 13 / 127;
            tri(L_ENEMY, cx, cy, 0, 0, 0, x1, y1, 100, 210, 255, x2, y2, 100, 210, 255);
        }
    }
}

static void drawShard(const Enemy *e, int frame) {
    (void)frame;
    int cx = e->x + 5, cy = e->y + 5;
    tri(L_ENEMY, cx, cy - 5, 255, 200, 120, cx - 5, cy + 5, 200, 60, 30, cx + 5, cy + 5, 200, 60, 30);
}

static void drawStinger(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 8;
    int glow = 180 + ((frame / 2) & 1) * 60;
    glowDisc(cx, y + 2, 4, glow, 40, 60, 8);
    tri(L_ENEMY, cx, y - 2, 255, 220, 220, cx - 9, y + 10, 200, 20, 40, cx - 2, y + 14, 160, 10, 30);
    tri(L_ENEMY, cx, y - 2, 255, 220, 220, cx + 9, y + 10, 200, 20, 40, cx + 2, y + 14, 160, 10, 30);
    tri(L_ENEMY, cx, y + 16, 255, 255, 255, cx - 3, y + 8, 255, 120, 120, cx + 3, y + 8, 255, 120, 120);
    if (e->vx) glowDisc(cx, y - 6, 3, 255, 140, 100, 8);
}

static void drawOrbiter(const Enemy *e, int frame) {
    int cx = e->x + 10, cy = e->y + 10;
    glowDisc(cx, cy - 2, 6, 150, 220, 255, 8);
    tri(L_ENEMY, cx - 14, cy, 60, 180, 220, cx + 14, cy, 60, 180, 220, cx, cy - 6, 200, 240, 255);
    tri(L_ENEMY, cx - 14, cy, 40, 120, 160, cx + 14, cy, 40, 120, 160, cx, cy + 6, 20, 60, 100);
    disc(L_ENEMY, cx, cy - 4, 5, 220, 250, 255, 120, 200, 240);
    int base = (frame / 3) & 63;
    for (int i = 0; i < 3; i++) {
        int a = (base + i * 21) & 63;
        int ox = cx + cosO(a) * 13 / 127;
        int oy = cy + sinO(a) * 5  / 127;
        discLo(L_ENEMY, ox, oy, 2, 255, 255, 180, 200, 200, 100);
    }
}

static void drawPhantom(const Enemy *e, int frame) {
    if (e->shield == 0 && (frame & 2)) return;
    int cx = e->x + 8, cy = e->y + 8;
    int a = e->shield ? 255 : 130;
    glowDisc(cx, cy, 8, 160, 120, 255, 8);
    tri(L_ENEMY, cx, cy - 9, a, a, 255, cx - 8, cy, a / 2, a / 3, a, cx, cy + 9, a / 2, a / 3, a);
    tri(L_ENEMY, cx, cy - 9, a, a, 255, cx + 8, cy, a / 2, a / 3, a, cx, cy + 9, a / 2, a / 3, a);
    discLo(L_ENEMY, cx, cy, 3, 255, 255, 255, 160, 120, 255);
}

static void drawJuggernaut(const Enemy *e, int frame) {
    int cx = e->x + 14, cy = e->y + 14;
    int glow = 150 + ((frame / 4) & 1) * 80;
    for (int i = 0; i < 16; i += 2) {
        int x1 = cx + cosI(i)     * 15 / 127, y1 = cy + sinI(i)     * 15 / 127;
        int x2 = cx + cosI(i + 2) * 15 / 127, y2 = cy + sinI(i + 2) * 15 / 127;
        tri(L_ENEMY, cx, cy, 120, 80, 140, x1, y1, 70, 40, 90, x2, y2, 70, 40, 90);
    }
    glowDisc(cx, cy, 6, glow, 60, 120, 8);
    rect(L_ENEMY, cx - 2, cy - 2, 4, 4, 255, 220, 255);
    tri(L_ENEMY, e->x, e->y + 10, 200, 160, 220, e->x + 8, e->y, 140, 90, 170, e->x + 8, e->y + 20, 140, 90, 170);
    tri(L_ENEMY, e->x + 28, e->y + 10, 200, 160, 220, e->x + 20, e->y, 140, 90, 170, e->x + 20, e->y + 20, 140, 90, 170);
}

static void drawNova(const Enemy *e, int frame) {
    int cx = e->x + 8, cy = e->y + 8;
    int pulse = 6 + sinS(frame * 3) / 24;
    int danger = 200 + sinS(frame * 4) / 2;
    glowDisc(cx, cy, pulse + 6, danger, 60, 200, 8);
    disc(L_ENEMY, cx, cy, pulse, 255, 180, 255, 120, 20, 140);
    int rot = (frame / 2) & 15;
    for (int i = 0; i < 8; i += 2) {
        int a = (rot + i) & 15;
        int ox = cx + cosI(a) * (pulse + 5) / 127, oy = cy + sinI(a) * (pulse + 5) / 127;
        discLo(L_ENEMY, ox, oy, 2, 255, 220, 255, 160, 60, 180);
    }
}

static void drawEnemy(const Enemy *e, int frame) {
    switch (e->type) {
    case E_DRONE:      drawDrone(e, frame);      break;
    case E_ZIGZAG:     drawZigzag(e, frame);     break;
    case E_TANK:       drawTank(e, frame);       break;
    case E_SPINNER:    drawSpinner(e, frame);    break;
    case E_SHOOTER:    drawShooter(e, frame);    break;
    case E_SPLITTER:   drawSplitter(e, frame);   break;
    case E_SHIELDED:   drawShielded(e, frame);   break;
    case E_STINGER:    drawStinger(e, frame);    break;
    case E_ORBITER:    drawOrbiter(e, frame);    break;
    case E_PHANTOM:    drawPhantom(e, frame);    break;
    case E_JUGGERNAUT: drawJuggernaut(e, frame); break;
    case E_NOVA:       drawNova(e, frame);       break;
    case E_SHARD:      drawShard(e, frame);      break;
    case E_BOSS: {
        int bx = e->x, by = e->y;
        int glow = 160 + ((frame / 3) & 1) * 90;
        rect(L_FX, bx - 6, by - 8, 60, 4, 50, 10, 10);
        int w = e->hp * 60 / 30;
        if (w < 0) w = 0;
        if (w > 60) w = 60;
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

/* ---------- Laser pemain, peluru musuh, burst Nova ---------- */

static void drawLaser(int x, int y, int frame) {
    int fl = ((frame / 2) & 1) * 20;
    glowDisc(x + 2, y + 4, 7, 80 + fl, 200, 255, 8);
    rect(L_BULLET, x,     y - 2, 4, 18, 255, 255, 255);
    rect(L_BULLET, x + 1, y - 5, 2, 4,  255, 255, 255);
}

static void drawEBullet(const EBullet *b) {
    glowDisc(b->x + 2, b->y + 4, 5, 255, 60, 40, 8);
    rect(L_BULLET, b->x, b->y, 3, 9, 255, 180, 120);
}

static void spawnEBullet(int x, int y) {
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (ebullets[i].alive) continue;
        ebullets[i].x = x;
        ebullets[i].y = y;
        ebullets[i].vy = 3;
        ebullets[i].alive = 1;
        break;
    }
}

static void spawnNovaBurst(int x, int y) {
    for (int i = 0; i < MAX_NOVABURST; i++) {
        if (novabursts[i].alive) continue;
        novabursts[i].x = x; novabursts[i].y = y;
        novabursts[i].phase = 0; novabursts[i].timer = NOVA_WARN;
        novabursts[i].alive = 1;
        return;
    }
}

static void drawNovaBurst(const NovaBurst *n, int frame) {
    if (n->phase == 0) {
        if ((frame & 3) < 2) {
            beamQuad(L_FX, n->x, n->y, 0, NOVA_LEN, 2, 255, 60, 60, 0);
            beamQuad(L_FX, n->x, n->y, 4, NOVA_LEN, 2, 255, 60, 60, 0);
            beamQuad(L_FX, n->x, n->y, 2, NOVA_LEN, 2, 255, 60, 60, 0);
            beamQuad(L_FX, n->x, n->y, 6, NOVA_LEN, 2, 255, 60, 60, 0);
        }
        glowDisc(n->x, n->y, 10, 255, 80, 80, 8);
    } else {
        beamQuad(L_FX, n->x, n->y, 0, NOVA_LEN, 12, 255, 255, 255, 0);
        beamQuad(L_FX, n->x, n->y, 4, NOVA_LEN, 12, 255, 255, 255, 0);
        beamQuad(L_FX, n->x, n->y, 2, NOVA_LEN, 12, 255, 255, 255, 0);
        beamQuad(L_FX, n->x, n->y, 6, NOVA_LEN, 12, 255, 255, 255, 0);
        glowDisc(n->x, n->y, 20, 150, 200, 255, 8);
    }
}

/* ---------- Ledakan & spark ---------- */

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
    burst(L_FX, x, y, rad + 6, rad / 2, t / 2, 255, 230, 120, fade, fade / 3, 0);
    disc(L_FX, x, y, rad, 255, 255, 200, fade, fade / 2, 0);
    if (rad > 8) glowDisc(x, y, rad / 2, 255, 255, 255, 8);
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
    rect(L_FX, px - 1, py - 1, 2 + s->size / 2, 2 + s->size / 2, r + 40, g + 40, b + 40);
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

static void spawnShards(int x, int y) {
    int made = 0;
    for (int k = 0; k < MAX_ENEMIES && made < 2; k++) {
        if (enemies[k].alive) continue;
        enemies[k].type = E_SHARD;
        enemies[k].x = x; enemies[k].y = y;
        enemies[k].hp = 1; enemies[k].t = 0;
        enemies[k].shield = 0; enemies[k].timer = 0;
        enemies[k].vx = made == 0 ? -3 : 3;
        enemies[k].alive = 1;
        made++;
    }
}

/* ---------- HUD ---------- */

static void drawHUD(int level) {
    for (int i = 0; i < 10; i++) {
        int cx = 10 + i * 9;
        if (i < level) glowDisc(cx, 26, 3, 0, 230, 140, 6);
        else           discLo(L_HUD_TOP, cx, 26, 2, 30, 40, 60, 20, 25, 40);
    }

    for (int p = 0; p < MAX_PLAYERS; p++) {
        const Player *pl = &players[p];
        if (!pl->active) continue;
        const int *c = playerColor[p];
        int bx = SCREEN_W - 8 - (MAX_PLAYERS - p) * 42;
        rect(L_HUD_TOP, bx, 6, 6, 6, c[0], c[1], c[2]);
        for (int i = 0; i < MAX_LIVES; i++) {
            if (i < pl->lives)
                rect(L_HUD_TOP, bx + i * 7, 15, 5, 6, c[0], c[1], c[2]);
            else
                rect(L_HUD_TOP, bx + i * 7, 15, 5, 6, 40, 40, 55);
        }
    }

    roundedPanel(L_HUD_BASE, 0, 0, SCREEN_W, HUD_H, 10, 20, 40, 90, 1);
    rect(L_HUD_BASE, 0, HUD_H, SCREEN_W, 1, 90, 170, 255);
    setBlendMode(L_HUD_BASE, 0);
}

static void drawMenu(int frame, int nPlayers) {
    roundedPanel(L_HUD_BASE, 40, 52, 240, 44, 12, 255, 150, 40, 0);
    roundedPanel(L_HUD_TOP, 40, 52, 240, 44, 12, 255, 255, 255, 1);
    setBlendMode(L_HUD_TOP, 0);
    glowDisc(160, 74, 90, 255, 180, 60, 8);
    int bob = sinS(frame / 2) / 20;
    int n = nPlayers < 1 ? 1 : nPlayers;
    for (int p = 0; p < n; p++) {
        int x = SCREEN_W / 2 - 8 + (p * 44 - (n - 1) * 22);
        drawPlayer(x, 140 + bob, frame, players[p].skin);
    }
}

static void drawGachaScreen(int frame) {
    roundedPanel(L_HUD_BASE, 30, 40, 260, 160, 14, 20, 15, 45, 0);
    roundedPanel(L_HUD_TOP,  30, 40, 260, 160, 14, 255, 255, 255, 1);
    setBlendMode(L_HUD_TOP, 0);
    if (gachaFlashT > 0) {
        glowDisc(160, 110, 30 + (20 - gachaFlashT), skinTable[gachaResultSkin].glowR,
                 skinTable[gachaResultSkin].glowG, skinTable[gachaResultSkin].glowB, 8);
        drawPlayer(152, 95, frame, gachaResultSkin);
    } else {
        glowDisc(160, 110, 26 + sinS(frame) / 16, 200, 180, 255, 8);
    }
}

static void drawSkinSelectScreen(int frame) {
    roundedPanel(L_HUD_BASE, 10, 40, 300, 170, 14, 15, 20, 45, 0);
    roundedPanel(L_HUD_TOP,  10, 40, 300, 170, 14, 255, 255, 255, 1);
    setBlendMode(L_HUD_TOP, 0);
    for (int i = 0; i < NUM_SKINS; i++) {
        int col = i % 3, row = i / 3;
        int x = 65 + col * 95, y = 75 + row * 75;
        if (unlockedMask & (1u << i)) drawPlayer(x - 8, y, frame, i);
        else                          disc(L_PLAYER, x, y + 8, 10, 40, 40, 50, 20, 20, 25);
        if (i == skinCursor) {
            rect(L_HUD_TOP, x - 22, y - 8,  44, 2, 255, 255, 255);
            rect(L_HUD_TOP, x - 22, y + 32, 44, 2, 255, 255, 255);
        }
    }
}

/* ---------- Logika umum ---------- */

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void enemySize(const Enemy *e, int *w, int *h) {
    switch (e->type) {
    case E_BOSS:       *w = 48; *h = 44; break;
    case E_TANK:       *w = 22; *h = 22; break;
    case E_SHOOTER:    *w = 22; *h = 22; break;
    case E_SHIELDED:   *w = 20; *h = 20; break;
    case E_SPLITTER:   *w = 20; *h = 20; break;
    case E_SPINNER:    *w = 22; *h = 22; break;
    case E_SHARD:      *w = 10; *h = 10; break;
    case E_JUGGERNAUT: *w = 28; *h = 28; break;
    case E_ORBITER:    *w = 20; *h = 20; break;
    case E_STINGER:    *w = 16; *h = 18; break;
    case E_PHANTOM:    *w = 16; *h = 16; break;
    case E_NOVA:       *w = 16; *h = 16; break;
    default:           *w = 16; *h = 18; break;
    }
}

static int countActive(void) {
    int n = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) if (players[p].active) n++;
    return n;
}

static int countAlive(void) {
    int n = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (players[p].active && players[p].alive) n++;
    return n;
}

static int totalScore(void) {
    int s = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) if (players[p].active) s += players[p].score;
    return s;
}

static int nearestPlayer(int x, int y) {
    int best = -1, bestD = 0x7fffffff;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!players[p].active || !players[p].alive) continue;
        int dx = players[p].x - x, dy = players[p].y - y;
        int d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = p; }
    }
    return best;
}

static void placePlayer(int p) {
    Player *pl = &players[p];
    pl->x = SCREEN_W / 2 - 8 + (p - 1) * 40 - 20;
    if (pl->x < 4) pl->x = 4;
    if (pl->x > SCREEN_W - 20) pl->x = SCREEN_W - 20;
    pl->y = SCREEN_H - 34;
}

static void joinPlayer(int p) {
    Player *pl = &players[p];
    pl->active = 1;
    pl->alive  = 1;
    pl->lives  = START_LIVES;
    pl->invuln = 90;
    pl->cooldown = 0;
    pl->score  = 0;
    pl->shieldStack = pl->shieldTimer = 0;
    pl->powerStack  = pl->powerTimer  = 0;
    pl->speedStack  = pl->speedTimer  = 0;
    placePlayer(p);
}

static void madReset(void) {
    mad.alive = 0;
    for (int i = 0; i < MAX_MISSILES; i++) missiles[i].alive = 0;
    for (int i = 0; i < MAX_BOMBS; i++)    mbombs[i].alive = 0;
    for (int i = 0; i < MAX_MLASERS; i++)  mlasers[i].alive = 0;
}

static void clearWorld(void) {
    for (int i = 0; i < MAX_BULLETS; i++)    bullets[i].alive = 0;
    for (int i = 0; i < MAX_EBULLETS; i++)   ebullets[i].alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)    enemies[i].alive = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) explosions[i].alive = 0;
    for (int i = 0; i < MAX_SPARKS; i++)     sparks[i].alive = 0;
    for (int i = 0; i < MAX_ITEMS; i++)      items[i].alive = 0;
    for (int i = 0; i < MAX_NOVABURST; i++)  novabursts[i].alive = 0;
    madReset();
}

static int pickEnemyType(int level) {
    int pool[16], n = 0;
    pool[n++] = E_DRONE;
    if (level >= 3) pool[n++] = E_ZIGZAG;
    if (level >= 4) { pool[n++] = E_SPINNER;  pool[n++] = E_STINGER;    }
    if (level >= 5) { pool[n++] = E_TANK;     pool[n++] = E_ORBITER;    }
    if (level >= 6) { pool[n++] = E_SHOOTER;  pool[n++] = E_PHANTOM;    }
    if (level >= 7) { pool[n++] = E_SPLITTER; pool[n++] = E_JUGGERNAUT; }
    if (level >= 8) { pool[n++] = E_SHIELDED; pool[n++] = E_NOVA;       }
    return pool[rand() % n];
}

static int countEnemies(void) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (enemies[i].alive && enemies[i].type != E_SHARD) n++;
    return n;
}

static void hurtPlayer(int p) {
    Player *pl = &players[p];
    spawnExplosion(pl->x + 8, pl->y + 8, 0);
    pl->lives--;
    pl->invuln = 90;
    if (pl->lives <= 0) {
        pl->alive = 0;
        spawnExplosion(pl->x + 8, pl->y + 8, 1);
    }
}

static int readPad(int p, uint16_t *btn) {
    if (p >= 2) { *btn = 0xFFFF; return 0; }
    PADTYPE *pad = (PADTYPE *)padbuf[p];
    if (pad->stat != 0) { *btn = 0xFFFF; return 0; }
    *btn = pad->btn;
    return 1;
}

/* ---------- [MAD] Mad Scientist Cruiser (desain orisinal) ---------- */

static void drawMadCruiser(int x, int y, int frame, int hit) {
    int cx = x + 24, cy = y + 20;
    int body = hit ? 255 : 150;
    int pulse = 4 + sinS(frame * 2) / 40;

    glowDisc(x - 6,  y + 30, 6 + pulse, 80, 255, 120, 8);
    glowDisc(x + 54, y + 30, 6 + pulse, 80, 255, 120, 8);
    rect(L_ENEMY, x - 10, y + 26, 8, 6, 90, 100, 90);
    rect(L_ENEMY, x + 50, y + 26, 8, 6, 90, 100, 90);

    tri(L_ENEMY, x - 12, y + 8, body, body, 170,  x + 6, y + 18, 70, 80, 90,  x + 4, y + 34, 100, 110, 120);
    tri(L_ENEMY, x + 58, y + 14, body, body, 170, x + 42, y + 20, 70, 80, 90, x + 44, y + 32, 100, 110, 120);

    for (int i = 0; i < 16; i += 2) {
        int x1 = cx + cosI(i)     * 26 / 127, y1 = cy + sinI(i)     * 15 / 127;
        int x2 = cx + cosI(i + 2) * 26 / 127, y2 = cy + sinI(i + 2) * 15 / 127;
        int lit = 110 + (cosI(i) + 127) / 4;
        tri(L_ENEMY, cx, cy, lit, lit + 10, lit + 20, x1, y1, 60, 70, 85, x2, y2, 60, 70, 85);
    }
    rect(L_ENEMY, x + 8, y + 22, 32, 2, 40, 50, 60);
    int blink = (frame / 6) % 3;
    rect(L_ENEMY, x + 12, y + 26, 4, 3, blink == 0 ? 255 : 90, 60, 60);
    rect(L_ENEMY, x + 22, y + 26, 4, 3, blink == 1 ? 255 : 90, 220, 60);
    rect(L_ENEMY, x + 32, y + 26, 4, 3, 60, 120, blink == 2 ? 255 : 90);

    glowDisc(cx, y + 8, 14, 60, 255, 140, 8);
    disc(L_ENEMY, cx, y + 10, 11, 170, 255, 190, 30, 160, 80);
    disc(L_ENEMY, cx - 4, y + 6, 4, 240, 255, 245, 160, 230, 190);

    rect(L_ENEMY, cx + 8, y - 6, 2, 10, 200, 200, 210);
    glowDisc(cx + 9, y - 8, 4, 255, 80, 200, 8);
}

static void madSpawn(int nPlayers) {
    mad.alive = 1;
    mad.x = SCREEN_W / 2 - 24; mad.y = HUD_H + 8;
    mad.maxhp = MAD_HP + 30 * (nPlayers - 1);
    mad.hp = mad.maxhp;
    mad.state = MAD_IDLE; mad.timer = 90; mad.lastAtk = -1;
    mad.dir = 0; mad.dirT = 0; mad.vx = 1; mad.vy = 0;
    mad.hit = 0; mad.dyingT = 0; mad.phase2 = 0;
}

static void spawnMissile(int x, int y, int vx, int vy) {
    for (int i = 0; i < MAX_MISSILES; i++) {
        if (missiles[i].alive) continue;
        missiles[i].x = x; missiles[i].y = y;
        missiles[i].vx = vx; missiles[i].vy = vy;
        missiles[i].hp = 2; missiles[i].alive = 1;
        return;
    }
}

static void spawnBomb(int x, int y) {
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (mbombs[i].alive) continue;
        mbombs[i].x = x; mbombs[i].y = y;
        mbombs[i].timer = 100; mbombs[i].alive = 1;
        return;
    }
}

static void spawnMLaser(int x, int y, int angle) {
    for (int i = 0; i < MAX_MLASERS; i++) {
        if (mlasers[i].alive) continue;
        mlasers[i].x = x; mlasers[i].y = y;
        mlasers[i].angle = angle;
        mlasers[i].timer = MLASER_WARN + MLASER_FIRE;
        mlasers[i].alive = 1;
        return;
    }
}

/* Panggil Nova dari sisi kiri dan kanan boss */
static void madSummonNova(void) {
    int sides[2] = { mad.x - 14, mad.x + 46 };
    for (int s = 0; s < 2; s++) {
        for (int k = 0; k < MAX_ENEMIES; k++) {
            if (enemies[k].alive) continue;
            enemies[k].type = E_NOVA;
            enemies[k].x = sides[s]; enemies[k].y = mad.y + 24;
            enemies[k].hp = 3; enemies[k].t = 0;
            enemies[k].shield = 0; enemies[k].timer = 0; enemies[k].vx = 0;
            enemies[k].alive = 1;
            break;
        }
    }
}

static void madMove(void) {
    if (--mad.dirT <= 0) {
        mad.dir = rand() % 3;
        mad.dirT = 60 + rand() % 60;
        int sp = mad.phase2 ? 3 : 2;
        int sx = (rand() & 1) ? sp : -sp, sy = (rand() & 1) ? sp : -sp;
        if (mad.dir == 0)      { mad.vx = sx; mad.vy = 0; }
        else if (mad.dir == 1) { mad.vx = 0;  mad.vy = sy; }
        else                   { mad.vx = sx; mad.vy = sy; }
    }
    mad.x += mad.vx; mad.y += mad.vy;
    if (mad.x < 12)               { mad.x = 12;               mad.vx = -mad.vx; }
    if (mad.x > SCREEN_W - 62)    { mad.x = SCREEN_W - 62;    mad.vx = -mad.vx; }
    if (mad.y < HUD_H + 4)        { mad.y = HUD_H + 4;        mad.vy = -mad.vy; }
    if (mad.y > HUD_H + 70)       { mad.y = HUD_H + 70;       mad.vy = -mad.vy; }
}

/* Pilih serangan acak; tidak boleh sama dengan sebelumnya. Di fase 2,
   serangan berat (laser=2, nova=4) lebih sering dipilih. */
static int madPickAttack(void) {
    int a;
    int tries = 0;
    do {
        a = rand() % MAD_ATTACKS;
        if (mad.phase2 && (a == 0 || a == 1 || a == 3) && (rand() % 3 == 0))
            a = (rand() & 1) ? 2 : 4;
        tries++;
    } while (a == mad.lastAtk && tries < 8);
    return a;
}

static void madBeginAttack(void) {
    int t = nearestPlayer(mad.x + 24, mad.y + 20);
    if (t >= 0) { mad.tx = players[t].x + 8; mad.ty = players[t].y + 8; }
    int a = madPickAttack();
    mad.lastAtk = a;
    switch (a) {
    case 0: mad.state = MAD_LOCK;  mad.timer = 50; break;   /* rudal balistik */
    case 1: mad.state = MAD_BOMB;  mad.timer = 30; break;
    case 2: mad.state = MAD_LASER; mad.timer = 20; break;
    case 3: mad.state = MAD_SWARM; mad.timer = 90; break;
    case 4: mad.state = MAD_NOVA;  mad.timer = 30; break;
    }
}

int main(void) {
    int frame = 0;
    int state = STATE_MENU;
    int nextBossScore = 30;
    int bossOn = 0;

    initVideo();
    initStars();

    FntLoad(960, 0);
    FntOpen(8, TEXT_Y, SCREEN_W - 16, 200, 0, 512);

    InitPAD(padbuf[0], 34, padbuf[1], 34);
    StartPAD();
    ChangeClearPAD(0);

    for (int p = 0; p < MAX_PLAYERS; p++) {
        players[p].active = 0;
        players[p].alive = 0;
        players[p].skin = 0;
    }
    madReset();

    ClearOTagR(buffers[0].ot, OT_LEN);
    nextpri = buffers[0].buf;

    while (1) {
        uint16_t btn[MAX_PLAYERS];
        int connected[MAX_PLAYERS];
        for (int p = 0; p < MAX_PLAYERS; p++)
            connected[p] = readPad(p, &btn[p]);

        int level = 1 + totalScore() / 10;
        if (level > 10) level = 10;

        for (int i = 0; i < NUM_STARS; i++) {
            stars[i].y += stars[i].speed;
            if (stars[i].y >= SCREEN_H) { stars[i].y = 0; stars[i].x = rand() % SCREEN_W; }
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
        for (int i = 0; i < MAX_NOVABURST; i++) {
            if (!novabursts[i].alive) continue;
            if (--novabursts[i].timer <= 0) {
                if (novabursts[i].phase == 0) { novabursts[i].phase = 1; novabursts[i].timer = NOVA_ACTIVE; }
                else novabursts[i].alive = 0;
            }
        }

        if (state == STATE_MENU) {
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (!connected[p]) continue;
                if (pressedP(p, btn[p], PAD_START)) {
                    if (!players[p].active) joinPlayer(p);
                }
            }

            /* [MAD] kode rahasia: Atas Atas Bawah Bawah Kiri Kanan Kiri Kanan O X */
            if (connected[0]) {
                static const uint16_t code[10] = { PAD_UP, PAD_UP, PAD_DOWN, PAD_DOWN,
                    PAD_LEFT, PAD_RIGHT, PAD_LEFT, PAD_RIGHT, PAD_CIRCLE, PAD_CROSS };
                uint16_t newly = (uint16_t)(~btn[0] & prevBtn[0] ? 0 : 0);
                (void)newly;
                uint16_t down = (uint16_t)(~btn[0]);           /* bit 1 = ditekan */
                uint16_t wasUp = prevBtn[0];                   /* bit 1 = tidak ditekan sebelumnya */
                uint16_t fresh = down & wasUp;                 /* baru ditekan frame ini */
                if (fresh) {
                    if (fresh & code[konami]) {
                        if (++konami >= 10) { forceMad = 1; konami = 0; }
                    } else {
                        konami = 0;
                    }
                }
            }

            if (countActive() > 0 && connected[0] && pressedP(0, btn[0], PAD_CROSS) && konami == 0) {
                clearWorld();
                nextBossScore = 30;
                bossOn = 0;
                madSpawned = 0;
                frame = 0;
                state = STATE_PLAY;
            }
            if (connected[0] && pressedP(0, btn[0], PAD_SELECT)) state = STATE_GACHA;
            if (connected[0] && pressedP(0, btn[0], PAD_SQUARE)) state = STATE_SKINSELECT;
        } else if (state == STATE_PLAY) {
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (!connected[p] || players[p].active) continue;
                if (pressedP(p, btn[p], PAD_START)) joinPlayer(p);
            }

            int nPlayers = countActive();
            int enemyCap = ENEMY_CAP_BASE + 2 * (nPlayers - 1);
            if (enemyCap > MAX_ENEMIES - 6) enemyCap = MAX_ENEMIES - 6;

            for (int p = 0; p < MAX_PLAYERS; p++) {
                Player *pl = &players[p];
                if (!pl->active) continue;

                if (pl->shieldTimer > 0) { if (--pl->shieldTimer <= 0) pl->shieldStack = 0; }
                if (pl->powerTimer  > 0) { if (--pl->powerTimer  <= 0) pl->powerStack  = 0; }
                if (pl->speedTimer  > 0) { if (--pl->speedTimer  <= 0) pl->speedStack  = 0; }
                if (pl->invuln > 0) pl->invuln--;

                if (!pl->alive || !connected[p]) continue;

                uint16_t b = btn[p];
                if (!(b & PAD_LEFT)  && pl->x > 0)             pl->x -= 3;
                if (!(b & PAD_RIGHT) && pl->x < SCREEN_W - 16) pl->x += 3;
                if (!(b & PAD_UP)    && pl->y > HUD_H + 16)    pl->y -= 2;
                if (!(b & PAD_DOWN)  && pl->y < SCREEN_H - 24) pl->y += 2;

                if (pl->cooldown > 0) pl->cooldown--;
                if (!(b & PAD_CROSS) && pl->cooldown == 0) {
                    int shots = (level >= 6) ? 3 : (level >= 3 ? 2 : 1);
                    int made = 0;
                    for (int i = 0; i < MAX_BULLETS && made < shots; i++) {
                        if (bullets[i].alive) continue;
                        int off = (shots == 1) ? 6 : (shots == 2 ? (made ? 12 : 0) : made * 6);
                        bullets[i].x = pl->x + off;
                        bullets[i].y = pl->y - 6;
                        bullets[i].dx = (shots == 3) ? (made - 1) : 0;
                        bullets[i].alive = 1;
                        made++;
                    }
                    int cd = (level >= 5) ? 5 : 6;
                    cd -= pl->speedStack * 2;
                    if (cd < 1) cd = 1;
                    pl->cooldown = cd;
                }
            }

            int madBusy = mad.alive;

            int spawnEvery = 42 - level * 3 - (nPlayers - 1) * 3;
            if (spawnEvery < 10) spawnEvery = 10;
            if (!bossOn && !madBusy && frame % spawnEvery == 0 && countEnemies() < enemyCap) {
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (enemies[i].alive) continue;
                    Enemy *e = &enemies[i];
                    e->type = pickEnemyType(level);
                    e->x = 24 + rand() % (SCREEN_W - 56);
                    e->y = HUD_H + 2;
                    e->t = rand() % 32;
                    e->vx = 0;
                    switch (e->type) {
                        case E_TANK:       e->hp = 4; e->shield = 0; e->timer = 0;  break;
                        case E_SHOOTER:    e->hp = 2; e->shield = 0; e->timer = 40; break;
                        case E_SPLITTER:   e->hp = 2; e->shield = 0; e->timer = 0;  break;
                        case E_SHIELDED:   e->hp = 1; e->shield = 2; e->timer = 0;  break;
                        case E_ORBITER:    e->hp = 2; e->shield = 0; e->timer = e->y; e->vx = e->x; break;
                        case E_PHANTOM:    e->hp = 2; e->shield = 1; e->timer = 50; break;
                        case E_JUGGERNAUT: e->hp = 8; e->shield = 0; e->timer = 0;  break;
                        case E_NOVA:       e->hp = 2; e->shield = 0; e->timer = 0;  break;
                        case E_STINGER:    e->hp = 1; e->shield = 0; e->timer = 0; e->vx = 0; break;
                        default:           e->hp = 1; e->shield = 0; e->timer = 0;  break;
                    }
                    if (e->type != E_SHARD && nPlayers > 1) e->hp += (nPlayers - 1) / 2;
                    e->alive = 1;
                    break;
                }
            }

            {
                int bossAlive = 0;
                for (int i = 0; i < MAX_ENEMIES; i++)
                    if (enemies[i].alive && enemies[i].type == E_BOSS) bossAlive = 1;
                bossOn = bossAlive;
            }
            if (!bossOn && !mad.alive && totalScore() >= nextBossScore) {
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (enemies[i].alive) continue;
                    enemies[i].type = E_BOSS;
                    enemies[i].x = SCREEN_W / 2 - 24;
                    enemies[i].y = HUD_H + 2;
                    enemies[i].hp = 30 + 10 * (nPlayers - 1);
                    enemies[i].t = 0;
                    enemies[i].shield = 0;
                    enemies[i].timer = 0;
                    enemies[i].vx = 0;
                    enemies[i].alive = 1;
                    bossOn = 1;
                    break;
                }
            }

            /* [MAD] pemicu boss rahasia: kode rahasia atau skor total 100 */
            if (!mad.alive && !bossOn && (forceMad || (totalScore() >= 100 && !madSpawned))) {
                madSpawn(nPlayers);
                madSpawned = 1;
                forceMad = 0;
            }

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].alive) continue;
                bullets[i].y -= 7;
                bullets[i].x += bullets[i].dx;
                if (bullets[i].y < HUD_H) bullets[i].alive = 0;
            }
            for (int i = 0; i < MAX_EBULLETS; i++) {
                if (!ebullets[i].alive) continue;
                ebullets[i].y += ebullets[i].vy;
                if (ebullets[i].y > SCREEN_H) ebullets[i].alive = 0;
            }
            for (int i = 0; i < MAX_ITEMS; i++) {
                if (!items[i].alive) continue;
                items[i].y += 2;
                if (items[i].y > SCREEN_H) items[i].alive = 0;
            }

            int baseSpeed = 1 + level / 5;

            for (int i = 0; i < MAX_ENEMIES; i++) {
                Enemy *e = &enemies[i];
                if (!e->alive) continue;
                e->t++;
                int w, h;
                enemySize(e, &w, &h);

                int tgt = nearestPlayer(e->x, e->y);
                int tx = (tgt >= 0) ? players[tgt].x : SCREEN_W / 2;

                switch (e->type) {
                case E_BOSS:
                    if (e->y < HUD_H + 20) e->y++;
                    e->x = SCREEN_W / 2 - 24 + sinS(e->t / 3) * 90 / 127;
                    break;
                case E_ZIGZAG:
                    e->y += baseSpeed + 1;
                    e->x += sinS(e->t) / 40;
                    if (e->x < 0) e->x = 0;
                    if (e->x > SCREEN_W - 20) e->x = SCREEN_W - 20;
                    break;
                case E_TANK:
                    e->y += 1;
                    break;
                case E_SPINNER:
                    e->y += baseSpeed + 1;
                    e->x += sinS(e->t * 2) / 24;
                    if (e->x < 0) e->x = 0;
                    if (e->x > SCREEN_W - 22) e->x = SCREEN_W - 22;
                    break;
                case E_SHOOTER:
                    if (e->y < HUD_H + 50) e->y += 1;
                    else {
                        if (e->timer > 0) e->timer--;
                        else { spawnEBullet(e->x + 9, e->y + 18); e->timer = 60; }
                    }
                    break;
                case E_SPLITTER:
                    e->y += baseSpeed;
                    break;
                case E_SHIELDED:
                    e->y += 1;
                    break;
                case E_SHARD:
                    e->x += e->vx;
                    e->y += baseSpeed + 2;
                    break;
                case E_STINGER:
                    if (e->vx == 0) {
                        e->y += 2;
                        if (e->x < tx) e->x += 3; else if (e->x > tx) e->x -= 3;
                        if (e->y > HUD_H + 50) e->vx = 1;
                    } else {
                        e->y += 7;
                    }
                    break;
                case E_ORBITER: {
                    int cxOrb = e->vx;
                    if (cxOrb < 30) cxOrb = 30;
                    if (cxOrb > SCREEN_W - 30) cxOrb = SCREEN_W - 30;
                    int cyOrb = e->timer + e->t / 4;
                    int ang = (e->t / 3) & 63;
                    e->x = cxOrb + cosO(ang) * 20 / 127;
                    e->y = cyOrb + sinO(ang) * 12 / 127;
                    break;
                }
                case E_PHANTOM:
                    e->y += baseSpeed + 1;
                    e->x += sinS(e->t) / 30;
                    if (e->x < 0) e->x = 0;
                    if (e->x > SCREEN_W - 16) e->x = SCREEN_W - 16;
                    if (--e->timer <= 0) { e->shield ^= 1; e->timer = e->shield ? 50 : 30; }
                    break;
                case E_JUGGERNAUT:
                    e->y += 1;
                    break;
                case E_NOVA:
                    e->y += baseSpeed;
                    break;
                default:
                    e->y += baseSpeed;
                    break;
                }

                if (e->type != E_BOSS && e->y > SCREEN_H) e->alive = 0;

                int dmgBase = 1;
                for (int j = 0; j < MAX_BULLETS; j++) {
                    if (!bullets[j].alive || !e->alive) continue;
                    if (!overlap(bullets[j].x, bullets[j].y, 4, 14, e->x, e->y, w, h)) continue;
                    bullets[j].alive = 0;

                    if (e->type == E_PHANTOM && e->shield == 0) {
                        spawnSparks(bullets[j].x + 2, bullets[j].y, 3, 160, 120, 255, 0);
                        continue;
                    }
                    if (e->type != E_PHANTOM && e->shield > 0) {
                        e->shield--;
                        spawnSparks(bullets[j].x + 2, bullets[j].y, 4, 100, 200, 255, 0);
                        continue;
                    }

                    int maxPow = 0;
                    for (int p = 0; p < MAX_PLAYERS; p++)
                        if (players[p].active && players[p].alive && players[p].powerStack > maxPow)
                            maxPow = players[p].powerStack;
                    e->hp -= dmgBase + maxPow;

                    spawnSparks(bullets[j].x + 2, bullets[j].y, 4, 120, 230, 255, 0);
                    if (e->hp <= 0) {
                        spawnExplosion(e->x + w / 2, e->y + h / 2,
                                       e->type == E_BOSS || e->type == E_TANK || e->type == E_JUGGERNAUT);
                        if (e->type == E_SPLITTER) spawnShards(e->x + w / 2, e->y + h / 2);
                        if (e->type == E_NOVA) spawnNovaBurst(e->x + w / 2, e->y + h / 2);
                        e->alive = 0;

                        int pts = 1;
                        if (e->type == E_BOSS) pts = 10;
                        else if (e->type == E_JUGGERNAUT) pts = 4;
                        else if (e->type == E_TANK || e->type == E_SHIELDED || e->type == E_NOVA) pts = 3;
                        else if (e->type == E_SPLITTER || e->type == E_SHOOTER || e->type == E_SPINNER ||
                                 e->type == E_STINGER || e->type == E_ORBITER || e->type == E_PHANTOM) pts = 2;

                        int who = nearestPlayer(e->x, e->y);
                        if (who >= 0) players[who].score += pts;

                        if (e->type == E_BOSS) {
                            nextBossScore += 30;
                            bossOn = 0;
                            spawnExplosion(e->x + 10, e->y + 10, 1);
                            spawnExplosion(e->x + 38, e->y + 30, 1);
                            forceSpawnItem(e->x + 10, e->y + 10);
                            forceSpawnItem(e->x + 38, e->y + 30);
                        } else if (e->type != E_SHARD) {
                            spawnItem(e->x + w / 2, e->y + h / 2);
                        }
                    }
                }

                if (!e->alive) continue;

                for (int p = 0; p < MAX_PLAYERS; p++) {
                    Player *pl = &players[p];
                    if (!pl->active || !pl->alive || !e->alive) continue;

                    if (pl->shieldStack > 0 && e->type != E_BOSS) {
                        int srad = 12 + pl->shieldStack * 4;
                        int ex = e->x + w / 2, ey = e->y + h / 2;
                        int dx = ex - (pl->x + 8), dy = ey - (pl->y + 8);
                        if (dx * dx + dy * dy <= srad * srad) {
                            spawnExplosion(ex, ey, 0);
                            spawnSparks(ex, ey, 5, 120, 220, 255, 0);
                            if (e->type == E_NOVA) spawnNovaBurst(ex, ey);
                            e->alive = 0;
                            pl->score += 1;
                            break;
                        }
                    }

                    int phantomImmune = (e->type == E_PHANTOM && e->shield == 0);
                    if (!phantomImmune && pl->invuln == 0 && pl->shieldStack == 0 &&
                        overlap(pl->x, pl->y, 16, 16, e->x, e->y, w, h)) {
                        spawnExplosion(e->x + w / 2, e->y + h / 2, 0);
                        if (e->type != E_BOSS) e->alive = 0;
                        hurtPlayer(p);
                    }
                }
            }

            /* ---------- [MAD] logika boss rahasia ---------- */
            if (mad.alive) {
                if (mad.hit > 0) mad.hit--;
                if (!mad.phase2 && mad.hp * 2 <= mad.maxhp && mad.state != MAD_DYING) mad.phase2 = 1;

                if (mad.state == MAD_DYING) {
                    mad.dyingT++;
                    if (mad.dyingT % 5 == 0)
                        spawnExplosion(mad.x + rand() % 52, mad.y + rand() % 40, 1);
                    if (mad.dyingT == 1) {
                        for (int k = 0; k < 6; k++)
                            spawnSparks(mad.x + 24, mad.y + 20, 8, 255, 200, 90, 1);
                    }
                    if (mad.dyingT > 80) {
                        spawnExplosion(mad.x + 24, mad.y + 20, 1);
                        for (int k = 0; k < 4; k++) forceSpawnItem(mad.x + k * 12, mad.y + 10);
                        for (int p = 0; p < MAX_PLAYERS; p++)
                            if (players[p].active) players[p].score += 25;
                        mad.alive = 0;
                    }
                } else {
                    madMove();
                    int pauseTime = mad.phase2 ? 45 : 70;
                    if (mad.state == MAD_IDLE) {
                        if (--mad.timer <= 0) madBeginAttack();
                    } else if (mad.state == MAD_LOCK) {
                        if (--mad.timer <= 0) { mad.state = MAD_DASH; mad.timer = 22; }
                    } else if (mad.state == MAD_DASH) {
                        int dx = mad.tx - (mad.x + 24), dy = mad.ty - (mad.y + 20);
                        mad.x += (dx > 6 ? 6 : (dx < -6 ? -6 : 0));
                        mad.y += (dy > 6 ? 6 : (dy < -6 ? -6 : 0));
                        if (--mad.timer <= 0) { mad.state = MAD_IDLE; mad.timer = pauseTime; }
                    } else if (mad.state == MAD_BOMB) {
                        if (--mad.timer <= 0) {
                            spawnBomb(mad.x + 24, mad.y + 30);
                            if (mad.phase2) spawnBomb(mad.x + 10, mad.y + 30);
                            mad.state = MAD_IDLE; mad.timer = pauseTime;
                        }
                    } else if (mad.state == MAD_LASER) {
                        if (--mad.timer <= 0) {
                            spawnMLaser(mad.x + 24, mad.y + 24, 0);   /* horizontal */
                            spawnMLaser(mad.x + 24, mad.y + 24, 4);   /* vertikal */
                            spawnMLaser(mad.x + 24, mad.y + 24, 2);   /* diagonal */
                            mad.state = MAD_IDLE; mad.timer = MLASER_WARN + MLASER_FIRE + 20;
                        }
                    } else if (mad.state == MAD_SWARM) {
                        int gap = mad.phase2 ? 7 : 10;
                        if (mad.timer % gap == 0)
                            spawnMissile(mad.x + 10 + rand() % 30, mad.y + 10, (rand() % 5) - 2, 2);
                        if (--mad.timer <= 0) { mad.state = MAD_IDLE; mad.timer = pauseTime + 10; }
                    } else if (mad.state == MAD_NOVA) {
                        if (--mad.timer <= 0) {
                            madSummonNova();
                            mad.state = MAD_IDLE; mad.timer = pauseTime + 30;
                        }
                    }
                }

                for (int j = 0; j < MAX_BULLETS; j++) {
                    if (!bullets[j].alive || mad.state == MAD_DYING) continue;
                    if (!overlap(bullets[j].x, bullets[j].y, 4, 14, mad.x - 6, mad.y, 62, 42)) continue;
                    bullets[j].alive = 0;
                    int maxPow = 0;
                    for (int p = 0; p < MAX_PLAYERS; p++)
                        if (players[p].active && players[p].alive && players[p].powerStack > maxPow)
                            maxPow = players[p].powerStack;
                    mad.hp -= 1 + maxPow;
                    mad.hit = 3;
                    spawnSparks(bullets[j].x + 2, bullets[j].y, 3, 120, 255, 160, 0);
                    if (mad.hp <= 0 && mad.state != MAD_DYING) {
                        mad.state = MAD_DYING; mad.dyingT = 0;
                        for (int i = 0; i < MAX_MISSILES; i++) missiles[i].alive = 0;
                        for (int i = 0; i < MAX_BOMBS; i++)    mbombs[i].alive = 0;
                        for (int i = 0; i < MAX_MLASERS; i++)  mlasers[i].alive = 0;
                    }
                }

                if (mad.state != MAD_DYING) {
                    for (int p = 0; p < MAX_PLAYERS; p++) {
                        Player *pl = &players[p];
                        if (!pl->active || !pl->alive || pl->invuln > 0 || pl->shieldStack > 0) continue;
                        if (overlap(pl->x, pl->y, 16, 16, mad.x - 6, mad.y, 62, 42)) hurtPlayer(p);
                    }
                }
            }

            /* rudal hujan: punya HP, bisa ditembak atau dihindari */
            for (int i = 0; i < MAX_MISSILES; i++) {
                if (!missiles[i].alive) continue;
                missiles[i].x += missiles[i].vx;
                missiles[i].y += missiles[i].vy + 1;
                if (missiles[i].y > SCREEN_H || missiles[i].x < -8 || missiles[i].x > SCREEN_W) {
                    missiles[i].alive = 0; continue;
                }
                for (int j = 0; j < MAX_BULLETS; j++) {
                    if (!bullets[j].alive) continue;
                    if (!overlap(bullets[j].x, bullets[j].y, 4, 14, missiles[i].x - 3, missiles[i].y - 3, 8, 10)) continue;
                    bullets[j].alive = 0;
                    missiles[i].hp--;
                    spawnSparks(missiles[i].x, missiles[i].y, 3, 255, 180, 80, 0);
                    if (missiles[i].hp <= 0) { spawnExplosion(missiles[i].x, missiles[i].y, 0); missiles[i].alive = 0; break; }
                }
                if (!missiles[i].alive) continue;
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    Player *pl = &players[p];
                    if (!pl->active || !pl->alive) continue;
                    if (!overlap(pl->x, pl->y, 16, 16, missiles[i].x - 3, missiles[i].y - 3, 8, 10)) continue;
                    missiles[i].alive = 0;
                    spawnExplosion(missiles[i].x, missiles[i].y, 0);
                    if (pl->shieldStack == 0 && pl->invuln == 0) hurtPlayer(p);
                    break;
                }
            }

            /* bom: dilempar ke pemain, berkedip makin cepat, lalu meledak dengan area damage */
            for (int i = 0; i < MAX_BOMBS; i++) {
                if (!mbombs[i].alive) continue;
                int t = nearestPlayer(mbombs[i].x, mbombs[i].y);
                if (t >= 0 && mbombs[i].timer > 40) {
                    int dx = players[t].x + 8 - mbombs[i].x, dy = players[t].y + 8 - mbombs[i].y;
                    mbombs[i].x += (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
                    mbombs[i].y += (dy > 0) ? 1 : (dy < 0 ? -1 : 0);
                }
                if (--mbombs[i].timer <= 0) {
                    spawnExplosion(mbombs[i].x, mbombs[i].y, 1);
                    for (int p = 0; p < MAX_PLAYERS; p++) {
                        Player *pl = &players[p];
                        if (!pl->active || !pl->alive || pl->invuln > 0 || pl->shieldStack > 0) continue;
                        int dx = pl->x + 8 - mbombs[i].x, dy = pl->y + 8 - mbombs[i].y;
                        if (dx * dx + dy * dy <= 40 * 40) hurtPlayer(p);
                    }
                    mbombs[i].alive = 0;
                }
            }

            /* laser boss: peringatan lalu menembak 1,8 detik, lalu hilang */
            for (int i = 0; i < MAX_MLASERS; i++) {
                if (!mlasers[i].alive) continue;
                if (--mlasers[i].timer <= 0) { mlasers[i].alive = 0; continue; }
                if (mlasers[i].timer > MLASER_FIRE) continue;
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    Player *pl = &players[p];
                    if (!pl->active || !pl->alive || pl->invuln > 0 || pl->shieldStack > 0) continue;
                    if (beamHit(mlasers[i].x, mlasers[i].y, mlasers[i].angle, 260, 7, pl->x + 8, pl->y + 8))
                        hurtPlayer(p);
                }
            }

            for (int i = 0; i < MAX_EBULLETS; i++) {
                if (!ebullets[i].alive) continue;
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    Player *pl = &players[p];
                    if (!pl->active || !pl->alive) continue;
                    if (!overlap(pl->x, pl->y, 16, 16, ebullets[i].x, ebullets[i].y, 3, 9)) continue;
                    ebullets[i].alive = 0;
                    if (pl->shieldStack > 0) {
                        spawnSparks(ebullets[i].x, ebullets[i].y, 3, 100, 200, 255, 0);
                    } else if (pl->invuln == 0) {
                        hurtPlayer(p);
                    }
                    break;
                }
            }

            for (int i = 0; i < MAX_NOVABURST; i++) {
                if (!novabursts[i].alive || novabursts[i].phase != 1) continue;
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    Player *pl = &players[p];
                    if (!pl->active || !pl->alive) continue;
                    if (pl->shieldStack > 0 || pl->invuln > 0) continue;
                    int cx = pl->x + 8, cy = pl->y + 8;
                    int hit = beamHit(novabursts[i].x, novabursts[i].y, 0, NOVA_LEN, 8, cx, cy) ||
                              beamHit(novabursts[i].x, novabursts[i].y, 4, NOVA_LEN, 8, cx, cy) ||
                              beamHit(novabursts[i].x, novabursts[i].y, 2, NOVA_LEN, 8, cx, cy) ||
                              beamHit(novabursts[i].x, novabursts[i].y, 6, NOVA_LEN, 8, cx, cy);
                    if (hit) hurtPlayer(p);
                }
            }

            for (int i = 0; i < MAX_ITEMS; i++) {
                if (!items[i].alive) continue;
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    Player *pl = &players[p];
                    if (!pl->active || !pl->alive) continue;
                    if (overlap(pl->x, pl->y, 16, 16, items[i].x, items[i].y, 10, 10)) {
                        applyItem(pl, items[i].type);
                        spawnSparks(items[i].x + 5, items[i].y + 5, 5, 200, 255, 200, 0);
                        items[i].alive = 0;
                        break;
                    }
                }
            }

            if (countAlive() == 0) {
                awardGems(totalScore());
                state = STATE_GAMEOVER;
            }
        } else if (state == STATE_GACHA) {
            if (gachaFlashT > 0) gachaFlashT--;
            if (connected[0] && pressedP(0, btn[0], PAD_CROSS) && gachaFlashT == 0) {
                if (doGachaPull()) gachaFlashT = 20;
            }
            if (connected[0] && pressedP(0, btn[0], PAD_CIRCLE)) state = STATE_MENU;
        } else if (state == STATE_SKINSELECT) {
            if (connected[0]) {
                if (pressedP(0, btn[0], PAD_LEFT))  skinCursor = (skinCursor + NUM_SKINS - 1) % NUM_SKINS;
                if (pressedP(0, btn[0], PAD_RIGHT)) skinCursor = (skinCursor + 1) % NUM_SKINS;
                if (pressedP(0, btn[0], PAD_CROSS) && (unlockedMask & (1u << skinCursor)))
                    players[0].skin = skinCursor;
                if (pressedP(0, btn[0], PAD_CIRCLE)) state = STATE_MENU;
            }
        } else {
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (connected[p] && pressedP(p, btn[p], PAD_START)) {
                    for (int q = 0; q < MAX_PLAYERS; q++) {
                        players[q].active = 0;
                        players[q].alive = 0;
                    }
                    state = STATE_MENU;
                    break;
                }
            }
        }

        /* ---------- Gambar ---------- */

        drawBackground(frame);
        drawStars(frame);
        drawShootingStar();

        if (state == STATE_MENU) {
            drawMenu(frame, countActive());
        } else if (state == STATE_GACHA) {
            drawGachaScreen(frame);
        } else if (state == STATE_SKINSELECT) {
            drawSkinSelectScreen(frame);
        } else {
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (enemies[i].alive) drawEnemy(&enemies[i], frame);

            for (int i = 0; i < MAX_BULLETS; i++)
                if (bullets[i].alive) drawLaser(bullets[i].x, bullets[i].y, frame);

            for (int i = 0; i < MAX_EBULLETS; i++)
                if (ebullets[i].alive) drawEBullet(&ebullets[i]);

            for (int i = 0; i < MAX_NOVABURST; i++)
                if (novabursts[i].alive) drawNovaBurst(&novabursts[i], frame);

            for (int i = 0; i < MAX_ITEMS; i++)
                if (items[i].alive) drawItem(&items[i], frame);

            /* [MAD] gambar boss rahasia dan serangannya */
            if (mad.alive) {
                if (mad.state != MAD_DYING || (frame & 2))
                    drawMadCruiser(mad.x, mad.y, frame, mad.hit);
                if (mad.state != MAD_DYING) {
                    int w = mad.hp * 60 / mad.maxhp;
                    if (w < 0) w = 0;
                    if (w > 60) w = 60;
                    rect(L_FX, mad.x - 6, mad.y - 12, 60, 4, 50, 10, 10);
                    rect(L_FX, mad.x - 6, mad.y - 12, w, 4, mad.phase2 ? 255 : 80, mad.phase2 ? 120 : 255, 140);
                }
                if (mad.state == MAD_LOCK && (frame & 2)) {
                    beamQuad(L_FX, (mad.x + 24 + mad.tx) / 2, (mad.y + 20 + mad.ty) / 2, 4, 120, 2, 255, 60, 60, 0);
                    disc(L_FX, mad.tx, mad.ty, 10, 255, 60, 60, 120, 0, 0);
                    discLo(L_FX, mad.tx, mad.ty, 5, 255, 220, 220, 255, 80, 80);
                }
            }
            for (int i = 0; i < MAX_MISSILES; i++) {
                if (!missiles[i].alive) continue;
                glowDisc(missiles[i].x, missiles[i].y, 5, 255, 140, 60, 8);
                tri(L_BULLET, missiles[i].x, missiles[i].y + 6, 255, 220, 160,
                              missiles[i].x - 3, missiles[i].y - 4, 200, 60, 40,
                              missiles[i].x + 3, missiles[i].y - 4, 200, 60, 40);
            }
            for (int i = 0; i < MAX_BOMBS; i++) {
                if (!mbombs[i].alive) continue;
                int rate = 2 + (100 - mbombs[i].timer) / 12;   /* makin cepat kedipnya */
                int on = (frame % (rate + 1)) < 2;
                int v = on ? 255 : 90;
                disc(L_BULLET, mbombs[i].x, mbombs[i].y, 7, v, v / 3, 40, 80, 10, 10);
                if (mbombs[i].timer < 40)
                    discLo(L_FX, mbombs[i].x, mbombs[i].y, 40, 255, 60, 30, 90, 0, 0);
            }
            for (int i = 0; i < MAX_MLASERS; i++) {
                if (!mlasers[i].alive) continue;
                if (mlasers[i].timer > MLASER_FIRE) {
                    if (frame & 2) beamQuad(L_FX, mlasers[i].x, mlasers[i].y, mlasers[i].angle, 260, 2, 255, 80, 80, 0);
                } else {
                    beamQuad(L_FX, mlasers[i].x, mlasers[i].y, mlasers[i].angle, 260, 12, 120, 255, 200, 0);
                    beamQuad(L_FX, mlasers[i].x, mlasers[i].y, mlasers[i].angle, 260, 5,  255, 255, 255, 0);
                }
            }

            if (state == STATE_PLAY) {
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    const Player *pl = &players[p];
                    if (!pl->active || !pl->alive) continue;
                    drawShieldAura(pl, frame);
                    if (pl->invuln == 0 || (frame / 4) % 2 == 0)
                        drawPlayer(pl->x, pl->y, frame, pl->skin);
                    drawPlayerTag(p, pl->x, pl->y);
                }
            }

            for (int i = 0; i < MAX_EXPLOSIONS; i++)
                if (explosions[i].alive) drawExplosion(&explosions[i]);

            for (int i = 0; i < MAX_SPARKS; i++)
                if (sparks[i].alive) drawSpark(&sparks[i]);

            drawHUD(level);
            if (state == STATE_PLAY) drawActiveBuffs();
        }

        /* ---------- Teks ---------- */
        if (state == STATE_MENU) {
            int n = countActive();
            char line[3][40];
            int lineUsed = 0;
            for (int p = 1; p < MAX_PLAYERS && lineUsed < 3; p++) {
                if (connected[p] && !players[p].active) {
                    snprintf(line[lineUsed], sizeof(line[0]), "   %dP PRESS START TO JOIN", p + 1);
                    lineUsed++;
                }
            }
            FntPrint(-1, "\n\n\n   SPACE SHOOTER\n\n\n\n\n\n\n\n\n\n\n\n\n");
            if (n == 0) FntPrint(-1, "   1P PRESS START TO JOIN\n");
            else        FntPrint(-1, "   %d PLAYER%s  X = MULAI\n", n, n > 1 ? "S" : "");
            for (int i = 0; i < lineUsed; i++) FntPrint(-1, "%s\n", line[i]);
            FntPrint(-1, "   SELECT=GACHA  SQUARE=SKIN   GEMS %d", gems);
        } else if (state == STATE_PLAY) {
            FntPrint(-1, "TOTAL %d  GEMS %d", totalScore(), gems);
        } else if (state == STATE_GACHA) {
            if (gachaFlashT > 0) {
                FntPrint(-1, "\n\n\n\n\n\n\n\n\n\n\n\n      %s%s",
                         skinTable[gachaResultSkin].name,
                         gachaResultDup ? " (DUPLICATE +15 GEMS)" : " UNLOCKED!");
            } else {
                FntPrint(-1, "GEMS %d\n\n\n\n\n\n\n\n\n\n\n    X = PULL (%d GEMS)\n    O = KEMBALI", gems, GACHA_COST);
            }
        } else if (state == STATE_SKINSELECT) {
            FntPrint(-1, "PILIH SKIN (P1)\n\n\n\n\n\n\n\n\n\n\n\n    %s%s\n    O = KEMBALI",
                     skinTable[skinCursor].name,
                     (unlockedMask & (1u << skinCursor)) ? "" : " (TERKUNCI)");
        } else {
            FntPrint(-1, "TOTAL %d\n\n\n\n\n\n\n    GAME OVER\n\n    FINAL %d\n\n\n    PRESS START", totalScore(), totalScore());
        }
        FntFlush(-1);

        setBlendMode(L_GLOW, 1);
        flip();
        for (int p = 0; p < MAX_PLAYERS; p++) prevBtn[p] = btn[p];
        frame++;
    }

    return 0;
}
