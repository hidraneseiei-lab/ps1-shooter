#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>

#define OT_LEN     8
#define BUFFER_LEN 16384
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

typedef struct { int x, y, alive; } Obj;

typedef struct {
    int x, y, alive;
    int timer;
} Explosion;

typedef struct {
    int x, y;
    int speed;      /* 1 = jauh/lambat, 3 = dekat/cepat */
} Star;

#define MAX_BULLETS    12
#define MAX_ENEMIES    10
#define MAX_EXPLOSIONS 8
#define NUM_STARS      40
#define START_LIVES    3
#define EXPLOSION_LEN  14

enum { STATE_PLAY, STATE_GAMEOVER };

static Obj       bullets[MAX_BULLETS];
static Obj       enemies[MAX_ENEMIES];
static Explosion explosions[MAX_EXPLOSIONS];
static Star      stars[NUM_STARS];

static void initVideo(void) {
    ResetGraph(0);

    SetDefDispEnv(&buffers[0].disp, 0, 0,        SCREEN_W, SCREEN_H);
    SetDefDrawEnv(&buffers[0].draw, 0, SCREEN_H, SCREEN_W, SCREEN_H);
    SetDefDispEnv(&buffers[1].disp, 0, SCREEN_H, SCREEN_W, SCREEN_H);
    SetDefDrawEnv(&buffers[1].draw, 0, 0,        SCREEN_W, SCREEN_H);

    for (int i = 0; i < 2; i++) {
        setRGB0(&buffers[i].draw, 0, 0, 20);
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

/* layer: 0 = digambar paling belakang, OT_LEN-1 = paling depan
   (ClearOTagR + DrawOTag dari OT_LEN-1 menggambar index tinggi dulu) */
static void drawRect(int layer, int x, int y, int w, int h,
                     int r, int g, int b) {
    TILE *t = (TILE *)nextpri;
    setTile(t);
    setXY0(t, x, y);
    setWH(t, w, h);
    setRGB0(t, r, g, b);
    addPrim(&buffers[active].ot[layer], t);
    nextpri += sizeof(TILE);
}

static int overlap(int ax, int ay, int aw, int ah,
                   int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx &&
           ay < by + bh && ay + ah > by;
}

static void spawnExplosion(int x, int y) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!explosions[i].alive) {
            explosions[i].x = x;
            explosions[i].y = y;
            explosions[i].timer = EXPLOSION_LEN;
            explosions[i].alive = 1;
            return;
        }
    }
}

static void initStars(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].x = rand() % SCREEN_W;
        stars[i].y = rand() % SCREEN_H;
        stars[i].speed = 1 + (i % 3);   /* 3 lapis: 1, 2, 3 */
    }
}

static void resetGame(int *px, int *py, int *score, int *lives,
                      int *invuln, int *frame, int *cooldown) {
    for (int i = 0; i < MAX_BULLETS; i++)    bullets[i].alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)    enemies[i].alive = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) explosions[i].alive = 0;
    *px       = SCREEN_W / 2;
    *py       = SCREEN_H - 30;
    *score    = 0;
    *lives    = START_LIVES;
    *invuln   = 0;
    *frame    = 0;
    *cooldown = 0;
}

int main(void) {
    int px, py, score, lives, invuln, frame, cooldown;
    int state = STATE_PLAY;

    initVideo();
    initStars();

    FntLoad(960, 0);
    FntOpen(8, 8, SCREEN_W - 16, 32, 0, 128);

    InitPAD(padbuf[0], 34, padbuf[1], 34);
    StartPAD();
    ChangeClearPAD(0);

    resetGame(&px, &py, &score, &lives, &invuln, &frame, &cooldown);

    ClearOTagR(buffers[0].ot, OT_LEN);
    nextpri = buffers[0].buf;

    while (1) {
        PADTYPE *pad = (PADTYPE *)padbuf[0];
        uint16_t btn = 0xFFFF;
        if (pad->stat == 0) btn = pad->btn;

        /* Level naik tiap 10 skor (level 1..10) */
        int level = 1 + score / 10;
        if (level > 10) level = 10;

        /* Bintang selalu bergerak, bahkan saat game over */
        for (int i = 0; i < NUM_STARS; i++) {
            stars[i].y += stars[i].speed;
            if (stars[i].y >= SCREEN_H) {
                stars[i].y = 0;
                stars[i].x = rand() % SCREEN_W;
            }
        }

        /* Ledakan berjalan terus */
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (!explosions[i].alive) continue;
            if (--explosions[i].timer <= 0) explosions[i].alive = 0;
        }

        if (state == STATE_PLAY) {
            /* --- Input gerak --- */
            if (!(btn & PAD_LEFT)  && px > 0)             px -= 3;
            if (!(btn & PAD_RIGHT) && px < SCREEN_W - 16) px += 3;

            /* --- Menembak dengan cooldown --- */
            if (cooldown > 0) cooldown--;

            if (!(btn & PAD_CROSS) && cooldown == 0) {
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!bullets[i].alive) {
                        bullets[i].x = px + 6;
                        bullets[i].y = py;
                        bullets[i].alive = 1;
                        cooldown = 6;
                        break;
                    }
                }
            }

            /* --- Spawn musuh: makin sering di level tinggi --- */
            int spawnEvery = 40 - level * 3;   /* level1: 37 ... level10: 10 */
            if (spawnEvery < 10) spawnEvery = 10;
            if (frame % spawnEvery == 0) {
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (!enemies[i].alive) {
                        enemies[i].x = rand() % (SCREEN_W - 16);
                        enemies[i].y = -16;
                        enemies[i].alive = 1;
                        break;
                    }
                }
            }

            /* --- Update peluru --- */
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].alive) continue;
                bullets[i].y -= 5;
                if (bullets[i].y < -8) bullets[i].alive = 0;
            }

            /* --- Update musuh + tabrakan --- */
            if (invuln > 0) invuln--;

            int enemySpeed = 1 + level / 4;    /* level 1-3: 1, 4-7: 2, 8-10: 3 */

            for (int i = 0; i < MAX_ENEMIES; i++) {
                if (!enemies[i].alive) continue;

                enemies[i].y += enemySpeed;
                if (enemies[i].y > SCREEN_H) enemies[i].alive = 0;

                for (int j = 0; j < MAX_BULLETS; j++) {
                    if (bullets[j].alive && enemies[i].alive &&
                        overlap(bullets[j].x, bullets[j].y, 4, 8,
                                enemies[i].x, enemies[i].y, 16, 16)) {
                        spawnExplosion(enemies[i].x + 8, enemies[i].y + 8);
                        enemies[i].alive = 0;
                        bullets[j].alive = 0;
                        score++;
                    }
                }

                if (enemies[i].alive && invuln == 0 &&
                    overlap(px, py, 16, 16,
                            enemies[i].x, enemies[i].y, 16, 16)) {
                    spawnExplosion(enemies[i].x + 8, enemies[i].y + 8);
                    enemies[i].alive = 0;
                    lives--;
                    invuln = 90;
                    if (lives <= 0) {
                        spawnExplosion(px + 8, py + 8);
                        state = STATE_GAMEOVER;
                    }
                }
            }
        } else {
            /* GAME OVER: START untuk main lagi */
            if (!(btn & PAD_START)) {
                resetGame(&px, &py, &score, &lives, &invuln, &frame, &cooldown);
                state = STATE_PLAY;
            }
        }

        /* --- Gambar (layer 0 belakang ... 7 depan) --- */

        /* Bintang: lapis lambat lebih redup */
        for (int i = 0; i < NUM_STARS; i++) {
            int c = 60 + stars[i].speed * 60;   /* 120, 180, 240 */
            drawRect(0, stars[i].x, stars[i].y,
                     stars[i].speed, stars[i].speed, c, c, c);
        }

        /* Musuh */
        for (int i = 0; i < MAX_ENEMIES; i++)
            if (enemies[i].alive)
                drawRect(2, enemies[i].x, enemies[i].y, 16, 16, 255, 0, 0);

        /* Peluru */
        for (int i = 0; i < MAX_BULLETS; i++)
            if (bullets[i].alive)
                drawRect(3, bullets[i].x, bullets[i].y, 4, 8, 255, 255, 0);

        /* Kapal, berkedip saat kebal */
        if (state == STATE_PLAY && (invuln == 0 || (frame / 4) % 2 == 0))
            drawRect(4, px, py, 16, 16, 0, 255, 0);

        /* Ledakan: kotak membesar lalu memudar (kuning -> oranye -> merah) */
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (!explosions[i].alive) continue;
            int t    = EXPLOSION_LEN - explosions[i].timer;   /* 0..LEN */
            int size = 4 + t * 2;
            int g    = 255 - t * 18;
            if (g < 0) g = 0;
            int c    = explosions[i].timer * 18;
            if (c > 255) c = 255;
            drawRect(5, explosions[i].x - size / 2, explosions[i].y - size / 2,
                     size, size, c, g * c / 255, 0);
        }

        /* Teks */
        if (state == STATE_PLAY) {
            FntPrint(-1, "SCORE %d  LIVES %d  LV %d", score, lives, level);
        } else {
            FntPrint(-1, "GAME OVER\nSCORE %d\nPRESS START", score);
        }
        FntFlush(-1);

        flip();
        frame++;
    }

    return 0;
}
