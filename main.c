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
static uint16_t     prevBtn = 0xFFFF;

enum { L_HUD_TOP = 0, L_HUD_BASE = 1, L_GLOW = 2, L_FX = 3, L_PLAYER = 4,
       L_BULLET = 5, L_ENEMY = 6, L_PLANET = 7, L_BG = 8 };

typedef struct { int x, y, alive, hp, type, t, shield, timer, vx; } Enemy;
typedef struct { int x, y, alive, dx; } Bullet;
typedef struct { int x, y, alive, vy; } EBullet;
typedef struct { int x, y, alive, timer, big; } Explosion;
typedef struct { int x, y, vx, vy, life, maxlife, r, g, b, alive, size; } Spark;
typedef struct { int x, y, speed, layer, phase; } Star;
typedef struct { int x, y, alive, type; } Item;

#define MAX_BULLETS    16
#define MAX_EBULLETS   12
#define MAX_ENEMIES    12
#define MAX_EXPLOSIONS 8
#define MAX_SPARKS     40
#define MAX_ITEMS      8
#define NUM_STARS      50
#define START_LIVES    3
#define MAX_LIVES      5
#define EXPLOSION_LEN  20

#define HUD_H   30
#define TEXT_Y  12

#define MAX_STACK        3
#define SHIELD_DURATION  420   /* ~7 detik @60fps */
#define POWER_DURATION   300   /* ~5 detik */
#define SPEED_DURATION   360   /* ~6 detik */

enum { STATE_MENU, STATE_PLAY, STATE_GAMEOVER, STATE_GACHA, STATE_SKINSELECT };
enum { E_DRONE = 0, E_ZIGZAG = 1, E_TANK = 2, E_SPINNER = 3, E_SHOOTER = 4,
       E_SPLITTER = 5, E_SHIELDED = 6, E_SHARD = 7, E_BOSS = 8 };
enum { ITEM_HEAL = 0, ITEM_SHIELD = 1, ITEM_POWER = 2, ITEM_SPEED = 3 };

static Bullet    bullets[MAX_BULLETS];
static EBullet   ebullets[MAX_EBULLETS];
static Enemy     enemies[MAX_ENEMIES];
static Explosion explosions[MAX_EXPLOSIONS];
static Spark     sparks[MAX_SPARKS];
static Star      stars[NUM_STARS];
static Item      items[MAX_ITEMS];

static int shootX = -100, shootY = 0, shootT = 0;

/* Status buff aktif pemain */
static int shieldStack = 0, shieldTimer = 0;
static int powerStack  = 0, powerTimer  = 0;
static int speedStack  = 0, speedTimer  = 0;

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

static int pressed(uint16_t btn, uint16_t mask) {
    return !(btn & mask) && (prevBtn & mask);
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
static int currentSkin = 0;
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

/* ---------- Item: drop, update, gambar ---------- */

static void spawnItem(int x, int y) {
    if (rand() % 100 >= 20) return;   /* 20% peluang drop */
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (items[i].alive) continue;
        items[i].x = x; items[i].y = y;
        items[i].type = rand() % 4;
        items[i].alive = 1;
        return;
    }
}

static void forceSpawnItem(int x, int y) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (items[i].alive) continue;
        items[i].x = x; items[i].y = y;
        items[i].type = rand() % 4;
        items[i].alive = 1;
        return;
    }
}

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

/* Ambil efek item ke status pemain. lives dikirim lewat pointer
   karena heal langsung memodifikasi nyawa. */
static void applyItem(int type, int *lives) {
    switch (type) {
    case ITEM_HEAL:
        if (*lives < MAX_LIVES) (*lives)++;
        break;
    case ITEM_SHIELD:
        shieldStack = (shieldStack < MAX_STACK) ? shieldStack + 1 : MAX_STACK;
        shieldTimer = SHIELD_DURATION;
        break;
    case ITEM_POWER:
        powerStack = (powerStack < MAX_STACK) ? powerStack + 1 : MAX_STACK;
        powerTimer = POWER_DURATION;
        break;
    case ITEM_SPEED:
        speedStack = (speedStack < MAX_STACK) ? speedStack + 1 : MAX_STACK;
        speedTimer = SPEED_DURATION;
        break;
    }
}

/* ---------- Ikon status buff (pojok kanan bawah) ---------- */

static void drawBuffIcon(int x, int y, int r, int g, int b,
                         int shape, int stack, int timer, int maxTimer) {
    disc(L_HUD_TOP, x, y, 8, r / 3, g / 3, b / 3, 10, 10, 15);
    glowDisc(x, y, 9, r, g, b, 8);

    switch (shape) {
    case 0: /* shield: cincin */
        disc(L_HUD_TOP, x, y, 4, r, g, b, r / 2, g / 2, b / 2);
        break;
    case 1: /* power: panah atas */
        tri(L_HUD_TOP, x, y - 4, 255, 255, 255, x - 4, y + 4, r, g, b, x + 4, y + 4, r, g, b);
        break;
    case 2: /* speed: panah kanan */
        tri(L_HUD_TOP, x - 4, y - 4, 255, 255, 255, x - 4, y + 4, r, g, b, x + 4, y, r, g, b);
        break;
    }

    for (int i = 0; i < stack; i++)
        rect(L_HUD_TOP, x - 6 + i * 5, y + 11, 3, 3, r, g, b);

    int w = 16 * timer / maxTimer;
    rect(L_HUD_BASE, x - 8, y + 15, 16, 2, 40, 40, 50);
    rect(L_HUD_BASE, x - 8, y + 15, w, 2, r, g, b);
}

static void drawActiveBuffs(void) {
    int x = SCREEN_W - 20, y = SCREEN_H - 26;
    if (shieldStack > 0) { drawBuffIcon(x, y, 80, 180, 255, 0, shieldStack, shieldTimer, SHIELD_DURATION); x -= 26; }
    if (powerStack  > 0) { drawBuffIcon(x, y, 255, 120, 40, 1, powerStack,  powerTimer,  POWER_DURATION);  x -= 26; }
    if (speedStack  > 0) { drawBuffIcon(x, y, 255, 240, 80, 2, speedStack,  speedTimer,  SPEED_DURATION);  x -= 26; }
}

/* Aura visual shield yang mengorbit pemain */
static void drawShieldAura(int px, int py, int frame) {
    if (shieldStack <= 0) return;
    int cx = px + 8, cy = py + 8;
    int rad = 14 + shieldStack * 6;
    glowDisc(cx, cy, rad, 80, 180, 255, 8);
    for (int i = 0; i < shieldStack; i++) {
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

/* ---------- Musuh ---------- */

static void drawDrone(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 8;
    int glow = 160 + ((frame / 3) & 1) * 90;
    glowDisc(cx, y + 4, 4, glow, glow / 2, 0, 8);
    tri(L_ENEMY, x - 3,  y,      200, 40, 60,   x + 6,  y + 6,  120, 10, 40,  x + 4,  y + 13, 160, 20, 60);
    tri(L_ENEMY, x + 19, y,      200, 40, 60,   x + 10, y + 6,  120, 10, 40,  x + 12, y + 13, 160, 20, 60);
    tri(L_ENEMY, cx,     y + 17, 255, 140, 140, cx - 7, y,      170, 25, 60,  cx + 7, y,      170, 25, 60);
}

static void drawZigzag(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 8;
    glowDisc(cx, y + 5, 4, 255, 255, 120, 8);
    tri(L_ENEMY, x - 4,  y + 4,  60, 255, 140,  x + 6,  y + 2,  10, 120, 60,  x + 6,  y + 12, 20, 160, 90);
    tri(L_ENEMY, x + 20, y + 4,  60, 255, 140,  x + 10, y + 2,  10, 120, 60,  x + 10, y + 12, 20, 160, 90);
    tri(L_ENEMY, cx,     y + 18, 200, 255, 220, cx - 6, y,      30, 190, 110, cx + 6, y,      30, 190, 110);
}

static void drawTank(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 8;
    int glow = 160 + ((frame / 3) & 1) * 90;
    glowDisc(cx - 3, y + 7, 3, glow, 60, glow, 8);
    glowDisc(cx + 4, y + 7, 3, glow, 60, glow, 8);
    tri(L_ENEMY, x - 6,  y + 2,  190, 100, 255, x + 8,  y + 8,  90, 40, 160, x + 4,  y + 18, 120, 60, 200);
    tri(L_ENEMY, x + 22, y + 2,  190, 100, 255, x + 8,  y + 8,  90, 40, 160, x + 12, y + 18, 120, 60, 200);
    tri(L_ENEMY, cx,     y + 22, 230, 180, 255, cx - 10, y,     110, 50, 190, cx + 10, y,     110, 50, 190);
}

static void drawSpinner(const Enemy *e, int frame) {
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
    if (e->timer <= 2) glowDisc(cx, cy + 17, 4, 255, 200, 80, 8);
}

static void drawSplitter(const Enemy *e, int frame) {
    int cx = e->x + 10, cy = e->y + 10;
    int pulse = 3 + sinS(frame * 2) / 30;
    tri(L_ENEMY, cx, cy - 10 - pulse, 255, 255, 140, cx - 10, cy, 200, 200, 40, cx, cy + 10 + pulse, 160, 160, 20);
    tri(L_ENEMY, cx, cy - 10 - pulse, 255, 255, 140, cx + 10, cy, 200, 200, 40, cx, cy + 10 + pulse, 160, 160, 20);
    glowDisc(cx, cy, 5, 255, 255, 150, 8);
}

static void drawShielded(const Enemy *e, int frame) {
    int cx = e->x + 10, cy = e->y + 10;
    tri(L_ENEMY, cx, cy + 9, 120, 200, 255, cx - 8, cy - 7, 40, 80, 170, cx + 8, cy - 7, 40, 80, 170);
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

static void drawEnemy(const Enemy *e, int frame) {
    switch (e->type) {
    case E_DRONE:    drawDrone(e, frame);    break;
    case E_ZIGZAG:   drawZigzag(e, frame);   break;
    case E_TANK:     drawTank(e, frame);     break;
    case E_SPINNER:  drawSpinner(e, frame);  break;
    case E_SHOOTER:  drawShooter(e, frame);  break;
    case E_SPLITTER: drawSplitter(e, frame); break;
    case E_SHIELDED: drawShielded(e, frame); break;
    case E_SHARD:    drawShard(e, frame);    break;
    case E_BOSS: {
        int bx = e->x, by = e->y;
        int glow = 160 + ((frame / 3) & 1) * 90;
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

/* ---------- Laser pemain & peluru musuh ---------- */

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

static void drawHUD(int lives, int level) {
    for (int i = 0; i < MAX_LIVES; i++) {
        int lx = SCREEN_W - 20 - i * 14;
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

static void drawMenu(int frame) {
    roundedPanel(L_HUD_BASE, 40, 52, 240, 44, 12, 255, 150, 40, 0);
    roundedPanel(L_HUD_TOP, 40, 52, 240, 44, 12, 255, 255, 255, 1);
    setBlendMode(L_HUD_TOP, 0);
    glowDisc(160, 74, 90, 255, 180, 60, 8);
    int bob = sinS(frame / 2) / 20;
    drawPlayer(SCREEN_W / 2 - 8, 140 + bob, frame, currentSkin);
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

/* ---------- Logika ---------- */

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void enemySize(const Enemy *e, int *w, int *h) {
    switch (e->type) {
    case E_BOSS:     *w = 48; *h = 44; break;
    case E_TANK:     *w = 22; *h = 22; break;
    case E_SHOOTER:  *w = 22; *h = 22; break;
    case E_SHIELDED: *w = 20; *h = 20; break;
    case E_SPLITTER: *w = 20; *h = 20; break;
    case E_SPINNER:  *w = 22; *h = 22; break;
    case E_SHARD:    *w = 10; *h = 10; break;
    default:         *w = 16; *h = 18; break;
    }
}

static void resetGame(int *px, int *py, int *score, int *lives,
                      int *invuln, int *frame, int *cooldown, int *bossOn) {
    for (int i = 0; i < MAX_BULLETS; i++)    bullets[i].alive = 0;
    for (int i = 0; i < MAX_EBULLETS; i++)   ebullets[i].alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)    enemies[i].alive = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) explosions[i].alive = 0;
    for (int i = 0; i < MAX_SPARKS; i++)     sparks[i].alive = 0;
    for (int i = 0; i < MAX_ITEMS; i++)      items[i].alive = 0;
    shieldStack = shieldTimer = 0;
    powerStack  = powerTimer  = 0;
    speedStack  = speedTimer  = 0;
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
    int pool[8], n = 0;
    pool[n++] = E_DRONE;
    if (level >= 3) pool[n++] = E_ZIGZAG;
    if (level >= 4) pool[n++] = E_SPINNER;
    if (level >= 5) pool[n++] = E_TANK;
    if (level >= 6) pool[n++] = E_SHOOTER;
    if (level >= 7) pool[n++] = E_SPLITTER;
    if (level >= 8) pool[n++] = E_SHIELDED;
    return pool[rand() % n];
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

        if (state == STATE_MENU) {
            if (pressed(btn, PAD_START) || pressed(btn, PAD_CROSS)) {
                resetGame(&px, &py, &score, &lives, &invuln, &frame, &cooldown, &bossOn);
                nextBossScore = 30;
                state = STATE_PLAY;
            } else if (pressed(btn, PAD_SELECT)) {
                state = STATE_GACHA;
            } else if (pressed(btn, PAD_SQUARE)) {
                state = STATE_SKINSELECT;
            }
        } else if (state == STATE_PLAY) {
            if (!(btn & PAD_LEFT)  && px > 0)             px -= 3;
            if (!(btn & PAD_RIGHT) && px < SCREEN_W - 16) px += 3;
            if (!(btn & PAD_UP)    && py > HUD_H + 16)    py -= 2;
            if (!(btn & PAD_DOWN)  && py < SCREEN_H - 24) py += 2;

            /* Buff timer berjalan mundur, stack direset kalau habis */
            if (shieldTimer > 0) { if (--shieldTimer <= 0) shieldStack = 0; }
            if (powerTimer  > 0) { if (--powerTimer  <= 0) powerStack  = 0; }
            if (speedTimer  > 0) { if (--speedTimer  <= 0) speedStack  = 0; }

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
                int cd = (level >= 5) ? 5 : 6;
                cd -= speedStack * 2;      /* buff speed: cooldown makin pendek tiap stack */
                if (cd < 1) cd = 1;
                cooldown = cd;
            }

            int spawnEvery = 42 - level * 3;
            if (spawnEvery < 12) spawnEvery = 12;
            if (!bossOn && frame % spawnEvery == 0) {
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (enemies[i].alive) continue;
                    Enemy *e = &enemies[i];
                    e->type = pickEnemyType(level);
                    e->x = 10 + rand() % (SCREEN_W - 40);
                    e->y = HUD_H + 2;
                    e->t = rand() % 32;
                    e->vx = 0;
                    switch (e->type) {
                        case E_TANK:     e->hp = 4; e->shield = 0; e->timer = 0;  break;
                        case E_SHOOTER:  e->hp = 2; e->shield = 0; e->timer = 40; break;
                        case E_SPLITTER: e->hp = 2; e->shield = 0; e->timer = 0;  break;
                        case E_SHIELDED: e->hp = 1; e->shield = 2; e->timer = 0;  break;
                        default:         e->hp = 1; e->shield = 0; e->timer = 0;  break;
                    }
                    e->alive = 1;
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
                    enemies[i].shield = 0;
                    enemies[i].timer = 0;
                    enemies[i].vx = 0;
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

            for (int i = 0; i < MAX_EBULLETS; i++) {
                if (!ebullets[i].alive) continue;
                ebullets[i].y += ebullets[i].vy;
                if (ebullets[i].y > SCREEN_H) ebullets[i].alive = 0;
            }

            /* Item jatuh & hilang kalau keluar layar */
            for (int i = 0; i < MAX_ITEMS; i++) {
                if (!items[i].alive) continue;
                items[i].y += 2;
                if (items[i].y > SCREEN_H) items[i].alive = 0;
            }

            if (invuln > 0) invuln--;

            int baseSpeed = 1 + level / 5;
            int dmg = 1 + powerStack;   /* buff power: damage tembakan naik tiap stack */

            for (int i = 0; i < MAX_ENEMIES; i++) {
                Enemy *e = &enemies[i];
                if (!e->alive) continue;
                e->t++;
                int w, h;
                enemySize(e, &w, &h);

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
                default:
                    e->y += baseSpeed;
                    break;
                }

                if (e->type != E_BOSS && e->y > SCREEN_H) e->alive = 0;

                for (int j = 0; j < MAX_BULLETS; j++) {
                    if (!bullets[j].alive || !e->alive) continue;
                    if (overlap(bullets[j].x, bullets[j].y, 4, 14, e->x, e->y, w, h)) {
                        bullets[j].alive = 0;
                        if (e->shield > 0) {
                            e->shield--;
                            spawnSparks(bullets[j].x + 2, bullets[j].y, 4, 100, 200, 255, 0);
                            continue;
                        }
                        e->hp -= dmg;
                        spawnSparks(bullets[j].x + 2, bullets[j].y, 4, 120, 230, 255, 0);
                        if (e->hp <= 0) {
                            spawnExplosion(e->x + w / 2, e->y + h / 2,
                                           e->type == E_BOSS || e->type == E_TANK);
                            if (e->type == E_SPLITTER) spawnShards(e->x + w / 2, e->y + h / 2);
                            e->alive = 0;
                            if (e->type == E_BOSS) {
                                score += 10;
                                nextBossScore += 30;
                                bossOn = 0;
                                spawnExplosion(e->x + 10, e->y + 10, 1);
                                spawnExplosion(e->x + 38, e->y + 30, 1);
                                forceSpawnItem(e->x + 10, e->y + 10);
                                forceSpawnItem(e->x + 38, e->y + 30);
                            } else if (e->type == E_TANK || e->type == E_SHIELDED) {
                                score += 3;
                                spawnItem(e->x + w / 2, e->y + h / 2);
                            } else if (e->type == E_SPLITTER || e->type == E_SHOOTER || e->type == E_SPINNER) {
                                score += 2;
                                spawnItem(e->x + w / 2, e->y + h / 2);
                            } else if (e->type == E_SHARD) {
                                score += 1;   /* shard tidak drop item, terlalu sering muncul */
                            } else {
                                score += 1;
                                spawnItem(e->x + w / 2, e->y + h / 2);
                            }
                        }
                    }
                }

                /* Shield: musuh non-boss yang mendekat langsung hancur */
                if (shieldStack > 0 && e->alive && e->type != E_BOSS) {
                    int srad = 14 + shieldStack * 6;
                    int ex = e->x + w / 2, ey = e->y + h / 2;
                    int dx = ex - (px + 8), dy = ey - (py + 8);
                    if (dx * dx + dy * dy <= srad * srad) {
                        spawnExplosion(ex, ey, 0);
                        spawnSparks(ex, ey, 5, 120, 220, 255, 0);
                        e->alive = 0;
                        score += 1;
                        continue;
                    }
                }

                if (e->alive && invuln == 0 && shieldStack == 0 &&
                    overlap(px, py, 16, 16, e->x, e->y, w, h)) {
                    spawnExplosion(e->x + w / 2, e->y + h / 2, 0);
                    if (e->type != E_BOSS) e->alive = 0;
                    lives--;
                    invuln = 90;
                    if (lives <= 0) {
                        spawnExplosion(px + 8, py + 8, 1);
                        awardGems(score);
                        state = STATE_GAMEOVER;
                    }
                }
            }

            for (int i = 0; i < MAX_EBULLETS; i++) {
                if (!ebullets[i].alive) continue;
                if (shieldStack > 0 &&
                    overlap(px, py, 16, 16, ebullets[i].x, ebullets[i].y, 3, 9)) {
                    ebullets[i].alive = 0;
                    spawnSparks(ebullets[i].x, ebullets[i].y, 3, 100, 200, 255, 0);
                    continue;
                }
                if (invuln == 0 && shieldStack == 0 &&
                    overlap(px, py, 16, 16, ebullets[i].x, ebullets[i].y, 3, 9)) {
                    ebullets[i].alive = 0;
                    spawnExplosion(px + 8, py + 8, 0);
                    lives--;
                    invuln = 90;
                    if (lives <= 0) {
                        spawnExplosion(px + 8, py + 8, 1);
                        awardGems(score);
                        state = STATE_GAMEOVER;
                    }
                }
            }

            /* Pemain ambil item */
            for (int i = 0; i < MAX_ITEMS; i++) {
                if (!items[i].alive) continue;
                if (overlap(px, py, 16, 16, items[i].x, items[i].y, 10, 10)) {
                    applyItem(items[i].type, &lives);
                    spawnSparks(items[i].x + 5, items[i].y + 5, 5, 200, 255, 200, 0);
                    items[i].alive = 0;
                }
            }
        } else if (state == STATE_GACHA) {
            if (gachaFlashT > 0) gachaFlashT--;
            if (pressed(btn, PAD_CROSS) && gachaFlashT == 0) { if (doGachaPull()) gachaFlashT = 20; }
            if (pressed(btn, PAD_CIRCLE)) state = STATE_MENU;
        } else if (state == STATE_SKINSELECT) {
            if (pressed(btn, PAD_LEFT))  skinCursor = (skinCursor + NUM_SKINS - 1) % NUM_SKINS;
            if (pressed(btn, PAD_RIGHT)) skinCursor = (skinCursor + 1) % NUM_SKINS;
            if (pressed(btn, PAD_CROSS) && (unlockedMask & (1u << skinCursor))) currentSkin = skinCursor;
            if (pressed(btn, PAD_CIRCLE)) state = STATE_MENU;
        } else {
            if (pressed(btn, PAD_START)) state = STATE_MENU;
        }

        /* ---------- Gambar ---------- */

        drawBackground(frame);
        drawStars(frame);
        drawShootingStar();

        if (state == STATE_MENU) {
            drawMenu(frame);
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

            for (int i = 0; i < MAX_ITEMS; i++)
                if (items[i].alive) drawItem(&items[i], frame);

            if (state == STATE_PLAY) {
                drawShieldAura(px, py, frame);
                if (invuln == 0 || (frame / 4) % 2 == 0)
                    drawPlayer(px, py, frame, currentSkin);
            }

            for (int i = 0; i < MAX_EXPLOSIONS; i++)
                if (explosions[i].alive) drawExplosion(&explosions[i]);

            for (int i = 0; i < MAX_SPARKS; i++)
                if (sparks[i].alive) drawSpark(&sparks[i]);

            drawHUD(lives, level);
            if (state == STATE_PLAY) drawActiveBuffs();
        }

        if (state == STATE_MENU) {
            FntPrint(-1, "\n\n\n   SPACE SHOOTER\n\n\n\n\n\n\n\n\n\n\n\n\n\n   PRESS START\n   SELECT=GACHA  SQUARE=SKIN\n   GEMS %d", gems);
        } else if (state == STATE_PLAY) {
            FntPrint(-1, "SCORE %d   GEMS %d", score, gems);
        } else if (state == STATE_GACHA) {
            if (gachaFlashT > 0) {
                FntPrint(-1, "\n\n\n\n\n\n\n\n\n\n\n\n      %s%s",
                         skinTable[gachaResultSkin].name,
                         gachaResultDup ? " (DUPLICATE +15 GEMS)" : " UNLOCKED!");
            } else {
                FntPrint(-1, "GEMS %d\n\n\n\n\n\n\n\n\n\n\n    X = PULL (%d GEMS)\n    O = KEMBALI", gems, GACHA_COST);
            }
        } else if (state == STATE_SKINSELECT) {
            FntPrint(-1, "PILIH SKIN\n\n\n\n\n\n\n\n\n\n\n\n    %s%s\n    O = KEMBALI",
                     skinTable[skinCursor].name,
                     (unlockedMask & (1u << skinCursor)) ? "" : " (TERKUNCI)");
        } else {
            FntPrint(-1, "SCORE %d\n\n\n\n\n\n\n\n    GAME OVER\n\n    FINAL %d\n\n\n    PRESS START", score, score);
        }
        FntFlush(-1);

        setBlendMode(L_GLOW, 1);
        flip();
        prevBtn = btn;
        frame++;
    }

    return 0;
}
