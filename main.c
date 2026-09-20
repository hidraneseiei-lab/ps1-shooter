#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>

#define OT_LEN     8
#define BUFFER_LEN 65536
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

/* layer: 0 = paling belakang, 7 = paling depan */
enum { L_BG = 0, L_STAR = 1, L_PLANET = 2, L_ENEMY = 3, L_BULLET = 4,
       L_PLAYER = 5, L_FX = 6, L_HUD = 7 };

typedef struct { int x, y, alive, hp, type, t; } Enemy;
typedef struct { int x, y, alive, dx; } Bullet;
typedef struct { int x, y, alive, timer, big; } Explosion;
typedef struct { int x, y, vx, vy, life, r, g, b, alive; } Spark;
typedef struct { int x, y, speed; } Star;

#define MAX_BULLETS    16
#define MAX_ENEMIES    10
#define MAX_EXPLOSIONS 8
#define MAX_SPARKS     48
#define NUM_STARS      50
#define START_LIVES    3
#define EXPLOSION_LEN  18

enum { STATE_PLAY, STATE_GAMEOVER };
enum { E_DRONE = 0, E_ZIGZAG = 1, E_TANK = 2, E_BOSS = 3 };

static Bullet    bullets[MAX_BULLETS];
static Enemy     enemies[MAX_ENEMIES];
static Explosion explosions[MAX_EXPLOSIONS];
static Spark     sparks[MAX_SPARKS];
static Star      stars[NUM_STARS];

/* Tabel sinus kecil (32 langkah, skala 0..127) untuk gerak zig-zag & pulsa */
static const int8_t sinTab[32] = {
    0, 24, 48, 70, 89, 105, 116, 124, 127, 124, 116, 105, 89, 70, 48, 24,
    0, -24, -48, -70, -89, -105, -116, -124, -127, -124, -116, -105, -89, -70, -48, -24
};
static int sinI(int t) { return sinTab[t & 31]; }

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

/* ---------- Primitif gambar ---------- */

static void rect(int layer, int x, int y, int w, int h,
                 int r, int g, int b) {
    TILE *t = (TILE *)nextpri;
    setTile(t);
    setXY0(t, x, y);
    setWH(t, w, h);
    setRGB0(t, r, g, b);
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
    setRGB0(p, r0, g0, b0);
    setRGB1(p, r1, g1, b1);
    setRGB2(p, r2, g2, b2);
    addPrim(&buffers[active].ot[layer], p);
    nextpri += sizeof(POLY_G3);
}

/* Kotak gradasi vertikal */
static void rectGradV(int layer, int x, int y, int w, int h,
                      int r0, int g0, int b0, int r1, int g1, int b1) {
    POLY_G4 *p = (POLY_G4 *)nextpri;
    setPolyG4(p);
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    setRGB0(p, r0, g0, b0);
    setRGB1(p, r0, g0, b0);
    setRGB2(p, r1, g1, b1);
    setRGB3(p, r1, g1, b1);
    addPrim(&buffers[active].ot[layer], p);
    nextpri += sizeof(POLY_G4);
}

/* Belah ketupat gradasi (dua segitiga) - untuk planet/cincin/glow */
static void diamond(int layer, int cx, int cy, int rx, int ry,
                    int r, int g, int b, int r2, int g2, int b2) {
    tri(layer, cx, cy - ry, r, g, b,  cx - rx, cy, r2, g2, b2,  cx + rx, cy, r2, g2, b2);
    tri(layer, cx, cy + ry, r, g, b,  cx - rx, cy, r2, g2, b2,  cx + rx, cy, r2, g2, b2);
}

/* ---------- Latar: gradasi galaksi, nebula, planet, bintang ---------- */

static void drawBackground(int frame) {
    /* Gradasi langit: ungu tua di atas -> biru gelap di bawah */
    rectGradV(L_BG, 0, 0, SCREEN_W, SCREEN_H / 2, 22, 6, 46, 6, 8, 40);
    rectGradV(L_BG, 0, SCREEN_H / 2, SCREEN_W, SCREEN_H / 2, 6, 8, 40, 2, 4, 20);

    /* Nebula: dua awan besar berdenyut pelan */
    int pulse = 20 + sinI(frame / 6) / 12;
    diamond(L_BG, 80, 70, 90, 60, pulse + 30, 8, pulse + 50, 4, 4, 30);
    diamond(L_BG, 250, 150, 80, 55, 8, pulse + 40, pulse + 40, 4, 4, 30);

    /* Planet bergerak sangat pelan ke bawah (loop) */
    int py = (frame / 5) % (SCREEN_H + 120) - 60;
    int px = 250;
    /* Badan planet: gradasi terang di kiri-atas, gelap di kanan-bawah */
    diamond(L_PLANET, px, py, 34, 34, 255, 170, 90, 120, 40, 20);
    /* Cincin planet */
    tri(L_PLANET, px - 52, py + 4, 200, 160, 220,  px + 52, py - 4, 200, 160, 220,  px, py + 12, 90, 60, 130);

    /* Planet kecil biru di kiri */
    int py2 = (frame / 8 + 100) % (SCREEN_H + 80) - 40;
    diamond(L_PLANET, 40, py2, 16, 16, 120, 220, 255, 20, 70, 150);
}

/* ---------- Pesawat pemain: jet arcade ---------- */

static void drawPlayer(int x, int y, int frame) {
    int cx = x + 8;

    /* Api mesin ganda: warna berubah dari putih ke oranye ke merah */
    int fl = 6 + (frame / 2) % 5;
    tri(L_PLAYER, cx - 5, y + 16, 255, 255, 200,
                  cx - 2, y + 16, 255, 255, 200,
                  cx - 4, y + 16 + fl, 255, 60, 0);
    tri(L_PLAYER, cx + 2, y + 16, 255, 255, 200,
                  cx + 5, y + 16, 255, 255, 200,
                  cx + 4, y + 16 + fl, 255, 60, 0);

    /* Sayap lebar dengan ujung menyala */
    tri(L_PLAYER, cx - 3, y + 4,  90, 200, 255,
                  x - 8,  y + 18, 20, 60, 200,
                  cx - 3, y + 15, 30, 100, 230);
    tri(L_PLAYER, cx + 3, y + 4,  90, 200, 255,
                  x + 24, y + 18, 20, 60, 200,
                  cx + 3, y + 15, 30, 100, 230);
    rect(L_PLAYER, x - 8,  y + 16, 3, 3, 255, 240, 80);
    rect(L_PLAYER, x + 21, y + 16, 3, 3, 255, 240, 80);

    /* Badan tengah (putih -> biru) */
    tri(L_PLAYER, cx,     y - 4,  255, 255, 255,
                  cx - 6, y + 16, 60, 120, 230,
                  cx + 6, y + 16, 60, 120, 230);

    /* Garis aksen oranye di badan */
    rect(L_PLAYER, cx - 1, y + 6, 3, 8, 255, 150, 30);

    /* Kokpit kaca */
    tri(L_PLAYER, cx,     y + 1, 220, 255, 255,
                  cx - 3, y + 9, 40, 160, 230,
                  cx + 3, y + 9, 40, 160, 230);
}

/* ---------- Musuh: 4 jenis ---------- */

static void drawEnemy(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 8;
    int glow = 160 + ((frame / 3) & 1) * 90;

    switch (e->type) {
    case E_DRONE:   /* drone merah kecil */
        tri(L_ENEMY, x - 3,  y,      200, 40, 60,   x + 6,  y + 6,  120, 10, 40,  x + 4,  y + 13, 160, 20, 60);
        tri(L_ENEMY, x + 19, y,      200, 40, 60,   x + 10, y + 6,  120, 10, 40,  x + 12, y + 13, 160, 20, 60);
        tri(L_ENEMY, cx,     y + 17, 255, 140, 140, cx - 7, y,      170, 25, 60,  cx + 7, y,      170, 25, 60);
        rect(L_ENEMY, cx - 1, y + 3, 3, 3, glow, glow / 2, 0);
        break;

    case E_ZIGZAG:  /* pesawat hijau-neon yang bergerak zig-zag */
        tri(L_ENEMY, x - 4,  y + 4,  60, 255, 140,  x + 6,  y + 2,  10, 120, 60,  x + 6,  y + 12, 20, 160, 90);
        tri(L_ENEMY, x + 20, y + 4,  60, 255, 140,  x + 10, y + 2,  10, 120, 60,  x + 10, y + 12, 20, 160, 90);
        tri(L_ENEMY, cx,     y + 18, 200, 255, 220, cx - 6, y,      30, 190, 110, cx + 6, y,      30, 190, 110);
        rect(L_ENEMY, cx - 2, y + 4, 5, 3, glow, 255, 60);
        break;

    case E_TANK:    /* kapal berat, butuh banyak tembakan (ungu) */
        tri(L_ENEMY, x - 6,  y + 2,  190, 100, 255, x + 8,  y + 8,  90, 40, 160, x + 4,  y + 18, 120, 60, 200);
        tri(L_ENEMY, x + 22, y + 2,  190, 100, 255, x + 8,  y + 8,  90, 40, 160, x + 12, y + 18, 120, 60, 200);
        tri(L_ENEMY, cx,     y + 22, 230, 180, 255, cx - 10, y,     110, 50, 190, cx + 10, y,     110, 50, 190);
        rect(L_ENEMY, cx - 4, y + 5, 3, 4, glow, 60, glow);
        rect(L_ENEMY, cx + 2, y + 5, 3, 4, glow, 60, glow);
        break;

    case E_BOSS: {  /* boss besar dengan bar nyawa */
        int bx = x, by = y;
        /* Sayap besar */
        tri(L_ENEMY, bx - 16, by + 8,  255, 140, 30, bx + 16, by + 14, 150, 40, 10, bx + 10, by + 34, 200, 70, 20);
        tri(L_ENEMY, bx + 64, by + 8,  255, 140, 30, bx + 32, by + 14, 150, 40, 10, bx + 38, by + 34, 200, 70, 20);
        /* Badan */
        tri(L_ENEMY, bx + 24, by + 44, 255, 220, 120, bx,      by,      170, 50, 20, bx + 48, by,      170, 50, 20);
        /* Mata besar berkedip */
        rect(L_ENEMY, bx + 16, by + 12, 6, 6, glow, 40, 0);
        rect(L_ENEMY, bx + 26, by + 12, 6, 6, glow, 40, 0);
        rect(L_ENEMY, bx + 21, by + 24, 6, 4, 255, 255, 120);
        /* Bar nyawa boss (maks hp 30) */
        rect(L_FX, bx - 6, by - 8, 60, 4, 50, 10, 10);
        int w = e->hp * 60 / 30;
        if (w < 0) w = 0;
        rect(L_FX, bx - 6, by - 8, w, 4, 255, 60, 60);
        break;
    }
    }
}

/* ---------- Laser: lapisan pendar cyan dengan ekor ---------- */

static void drawLaser(int x, int y, int frame) {
    int fl = ((frame / 2) & 1) * 40;
    /* Pendar luar lebar */
    rect(L_BULLET, x - 3, y - 2, 10, 18, 0, 40 + fl / 2, 90);
    /* Pendar tengah */
    rect(L_BULLET, x - 1, y - 1, 6, 16, 0, 140 + fl, 230);
    /* Inti putih */
    rect(L_BULLET, x,     y,     4, 14, 200, 255, 255);
    /* Ujung tajam terang */
    rect(L_BULLET, x + 1, y - 4, 2, 4, 255, 255, 255);
    /* Ekor memudar */
    rect(L_BULLET, x + 1, y + 14, 2, 4, 0, 160, 230);
    rect(L_BULLET, x + 1, y + 18, 2, 4, 0, 90, 150);
}

/* ---------- Ledakan + percikan ---------- */

static void drawExplosion(const Explosion *e) {
    int t    = EXPLOSION_LEN - e->timer;
    int base = e->big ? 6 : 4;
    int size = base + t * (e->big ? 3 : 2);
    int fade = e->timer * 255 / EXPLOSION_LEN;
    int x = e->x, y = e->y;

    /* Gelombang kejut (cincin tipis di luar) */
    diamond(L_FX, x, y, size + 6, size + 6, fade / 2, fade / 4, fade / 2, 0, 0, 0);
    /* Cincin oranye */
    rect(L_FX, x - size / 2, y - size / 2, size, size, fade, fade / 3, 0);
    int s2 = size * 2 / 3;
    rect(L_FX, x - s2 / 2, y - s2 / 2, s2, s2, fade, fade * 2 / 3, 0);
    int s3 = size / 3;
    rect(L_FX, x - s3 / 2, y - s3 / 2, s3, s3, fade, fade, fade);
}

static void spawnSparks(int x, int y, int n, int r, int g, int b) {
    for (int i = 0, made = 0; i < MAX_SPARKS && made < n; i++) {
        if (sparks[i].alive) continue;
        sparks[i].x = x << 4;                 /* posisi 12.4 fixed-point */
        sparks[i].y = y << 4;
        sparks[i].vx = (rand() % 96) - 48;
        sparks[i].vy = (rand() % 96) - 48;
        sparks[i].life = 14 + rand() % 12;
        sparks[i].r = r; sparks[i].g = g; sparks[i].b = b;
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
    spawnSparks(x, y, big ? 20 : 10, 255, 200, 60);
}

/* ---------- HUD (versi sederhana) ---------- */

static void drawHUD(int lives, int level) {
    rectGradV(L_HUD, 0, 0, SCREEN_W, 24, 10, 20, 50, 2, 2, 14);
    rect(L_HUD, 0, 24, SCREEN_W, 1, 60, 140, 255);

    for (int i = 0; i < START_LIVES; i++) {
        int lx = SCREEN_W - 20 - i * 18;
        if (i < lives) {
            tri(L_HUD, lx + 5, 5,  255, 255, 255,
                       lx,     17, 60, 120, 230,
                       lx + 10, 17, 60, 120, 230);
        } else {
            rect(L_HUD, lx + 3, 12, 4, 4, 40, 40, 60);
        }
    }

    for (int i = 0; i < 10; i++) {
        if (i < level)
            rect(L_HUD, 8 + i * 9, 20, 7, 3, 0, 220, 130);
        else
            rect(L_HUD, 8 + i * 9, 20, 7, 3, 30, 40, 60);
    }
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

static void initStars(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].x = rand() % SCREEN_W;
        stars[i].y = rand() % SCREEN_H;
        stars[i].speed = 1 + (i % 3);
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
    int state = STATE_PLAY;
    int nextBossScore = 30;

    initVideo();
    initStars();

    FntLoad(960, 0);
    FntOpen(8, 6, SCREEN_W - 16, 20, 0, 128);

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

        /* Bintang */
        for (int i = 0; i < NUM_STARS; i++) {
            stars[i].y += stars[i].speed;
            if (stars[i].y >= SCREEN_H) {
                stars[i].y = 0;
                stars[i].x = rand() % SCREEN_W;
            }
        }

        /* Ledakan & percikan selalu berjalan */
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

        if (state == STATE_PLAY) {
            /* Gerak 4 arah */
            if (!(btn & PAD_LEFT)  && px > 0)             px -= 3;
            if (!(btn & PAD_RIGHT) && px < SCREEN_W - 16) px += 3;
            if (!(btn & PAD_UP)    && py > 40)            py -= 2;
            if (!(btn & PAD_DOWN)  && py < SCREEN_H - 24) py += 2;

            /* Tembak: makin tinggi level, makin cepat, dan ada tembakan ganda */
            if (cooldown > 0) cooldown--;
            if (!(btn & PAD_CROSS) && cooldown == 0) {
                int shots = (level >= 6) ? 3 : (level >= 3 ? 2 : 1);
                int made = 0;
                for (int i = 0; i < MAX_BULLETS && made < shots; i++) {
                    if (bullets[i].alive) continue;
                    int off = (shots == 1) ? 6 : (shots == 2 ? (made ? 12 : 0) : made * 6);
                    bullets[i].x = px + off - (shots >= 2 ? 0 : 0);
                    bullets[i].y = py - 6;
                    bullets[i].dx = (shots == 3) ? (made - 1) : 0;
                    bullets[i].alive = 1;
                    made++;
                }
                cooldown = (level >= 5) ? 5 : 6;
            }

            /* Spawn musuh biasa */
            int spawnEvery = 42 - level * 3;
            if (spawnEvery < 12) spawnEvery = 12;
            if (!bossOn && frame % spawnEvery == 0) {
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (enemies[i].alive) continue;
                    enemies[i].type = pickEnemyType(level);
                    enemies[i].x = 10 + rand() % (SCREEN_W - 40);
                    enemies[i].y = 26;
                    enemies[i].hp = (enemies[i].type == E_TANK) ? 4 : 1;
                    enemies[i].t = rand() % 32;
                    enemies[i].alive = 1;
                    break;
                }
            }

            /* Boss tiap kelipatan skor */
            if (!bossOn && score >= nextBossScore) {
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (enemies[i].alive) continue;
                    enemies[i].type = E_BOSS;
                    enemies[i].x = SCREEN_W / 2 - 24;
                    enemies[i].y = 26;
                    enemies[i].hp = 30;
                    enemies[i].t = 0;
                    enemies[i].alive = 1;
                    bossOn = 1;
                    break;
                }
            }

            /* Peluru */
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].alive) continue;
                bullets[i].y -= 7;
                bullets[i].x += bullets[i].dx;
                if (bullets[i].y < 26) bullets[i].alive = 0;
            }

            if (invuln > 0) invuln--;

            int baseSpeed = 1 + level / 5;

            for (int i = 0; i < MAX_ENEMIES; i++) {
                Enemy *e = &enemies[i];
                if (!e->alive) continue;

                e->t++;
                int w, h;
                enemySize(e, &w, &h);

                /* Gerakan tiap jenis */
                if (e->type == E_BOSS) {
                    if (e->y < 44) e->y++;                     /* turun pelan lalu berhenti */
                    e->x = SCREEN_W / 2 - 24 + sinI(e->t / 3) * 90 / 127;
                } else if (e->type == E_ZIGZAG) {
                    e->y += baseSpeed + 1;
                    e->x += sinI(e->t) / 40;
                    if (e->x < 0) e->x = 0;
                    if (e->x > SCREEN_W - 20) e->x = SCREEN_W - 20;
                } else if (e->type == E_TANK) {
                    e->y += 1;
                } else {
                    e->y += baseSpeed;
                }

                if (e->type != E_BOSS && e->y > SCREEN_H) e->alive = 0;

                /* Kena peluru */
                for (int j = 0; j < MAX_BULLETS; j++) {
                    if (!bullets[j].alive || !e->alive) continue;
                    if (overlap(bullets[j].x, bullets[j].y, 4, 14, e->x, e->y, w, h)) {
                        bullets[j].alive = 0;
                        e->hp--;
                        spawnSparks(bullets[j].x + 2, bullets[j].y, 4, 120, 230, 255);
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

                /* Nabrak kapal pemain */
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
                resetGame(&px, &py, &score, &lives, &invuln, &frame, &cooldown, &bossOn);
                nextBossScore = 30;
                state = STATE_PLAY;
            }
        }

        /* ---------- Gambar ---------- */

        drawBackground(frame);

        for (int i = 0; i < NUM_STARS; i++) {
            int c = 90 + stars[i].speed * 55;
            rect(L_STAR, stars[i].x, stars[i].y,
                 stars[i].speed, stars[i].speed, c, c, c);
        }

        for (int i = 0; i < MAX_ENEMIES; i++)
            if (enemies[i].alive) drawEnemy(&enemies[i], frame);

        for (int i = 0; i < MAX_BULLETS; i++)
            if (bullets[i].alive) drawLaser(bullets[i].x, bullets[i].y, frame);

        if (state == STATE_PLAY && (invuln == 0 || (frame / 4) % 2 == 0))
            drawPlayer(px, py, frame);

        for (int i = 0; i < MAX_EXPLOSIONS; i++)
            if (explosions[i].alive) drawExplosion(&explosions[i]);

        for (int i = 0; i < MAX_SPARKS; i++) {
            if (!sparks[i].alive) continue;
            int fade = sparks[i].life * 255 / 26;
            if (fade > 255) fade = 255;
            rect(L_FX, sparks[i].x >> 4, sparks[i].y >> 4, 2, 2,
                 sparks[i].r * fade / 255, sparks[i].g * fade / 255,
                 sparks[i].b * fade / 255);
        }

        drawHUD(lives, level);

        if (state == STATE_PLAY) {
            FntPrint(-1, "SCORE %d", score);
        } else {
            FntPrint(-1, "GAME OVER  SCORE %d\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n     PRESS START", score);
        }
        FntFlush(-1);

        flip();
        frame++;
    }

    return 0;
}
