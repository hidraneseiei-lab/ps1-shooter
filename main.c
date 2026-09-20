#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>

#define OT_LEN     8
#define BUFFER_LEN 8192
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

#define MAX_BULLETS 8
#define MAX_ENEMIES 6

static Obj bullets[MAX_BULLETS];
static Obj enemies[MAX_ENEMIES];

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

static void drawRect(int x, int y, int w, int h, int r, int g, int b) {
    TILE *t = (TILE *)nextpri;
    setTile(t);
    setXY0(t, x, y);
    setWH(t, w, h);
    setRGB0(t, r, g, b);
    addPrim(&buffers[active].ot[0], t);
    nextpri += sizeof(TILE);
}

int main(void) {
    int px = SCREEN_W / 2;
    int py = SCREEN_H - 30;
    int frame = 0;
    int score = 0;

    initVideo();

    InitPAD(padbuf[0], 34, padbuf[1], 34);
    StartPAD();
    ChangeClearPAD(0);

    ClearOTagR(buffers[0].ot, OT_LEN);
    nextpri = buffers[0].buf;

    while (1) {
        /* --- Input --- */
        PADTYPE *pad = (PADTYPE *)padbuf[0];
        if (pad->stat == 0) {
            uint16_t btn = pad->btn;

            if (!(btn & PAD_LEFT)  && px > 0)             px -= 3;
            if (!(btn & PAD_RIGHT) && px < SCREEN_W - 16) px += 3;

            if (!(btn & PAD_CROSS) && (frame % 8 == 0)) {
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!bullets[i].alive) {
                        bullets[i].x = px + 6;
                        bullets[i].y = py;
                        bullets[i].alive = 1;
                        break;
                    }
                }
            }
        }

        /* --- Spawn musuh --- */
        if (frame % 30 == 0) {
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
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].alive) continue;

            enemies[i].y += 1;
            if (enemies[i].y > SCREEN_H) enemies[i].alive = 0;

            for (int j = 0; j < MAX_BULLETS; j++) {
                if (bullets[j].alive &&
                    bullets[j].x > enemies[i].x - 4 &&
                    bullets[j].x < enemies[i].x + 16 &&
                    bullets[j].y > enemies[i].y - 4 &&
                    bullets[j].y < enemies[i].y + 16) {
                    enemies[i].alive = 0;
                    bullets[j].alive = 0;
                    score++;
                }
            }
        }

        /* --- Gambar --- */
        drawRect(px, py, 16, 16, 0, 255, 0);

        for (int i = 0; i < MAX_BULLETS; i++)
            if (bullets[i].alive)
                drawRect(bullets[i].x, bullets[i].y, 4, 8, 255, 255, 0);

        for (int i = 0; i < MAX_ENEMIES; i++)
            if (enemies[i].alive)
                drawRect(enemies[i].x, enemies[i].y, 16, 16, 255, 0, 0);

        flip();
        frame++;
    }

    return 0;
}
