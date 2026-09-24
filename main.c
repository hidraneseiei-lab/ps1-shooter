/* ps1-shooter - dibuat oleh hidraneseiei21 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>
#include "audio.h"
#include "save.h"
#include "render.h"

#define MAX_PLAYERS 4
static uint8_t padbuf[2][34];
static uint16_t prevBtn[MAX_PLAYERS] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };

/* [FIX] hitbox pemain dulu magic number "16,16" ditulis berulang di banyak tempat */
#define PLAYER_W 16
#define PLAYER_H 16

/* [FIX] Enemy: tambah vy, tx, ty, buff untuk kebutuhan 6 musuh baru
   (vy/tx/ty dipakai Charger buat simpan arah & target dash,
    buff dipakai Healer buat kasih "extra hit" ke musuh lain) */
typedef struct { int x, y, alive, hp, type, t, shield, timer, vx, vy, tx, ty, buff; } Enemy;
typedef struct { int x, y, alive, dx; } Bullet;
/* [FIX] EBullet: tambah vx supaya peluru musuh bisa mengarah (aim), bukan cuma lurus ke bawah */
typedef struct { int x, y, alive, vx, vy; } EBullet;
typedef struct { int x, y, alive, timer, big; } Explosion;
typedef struct { int x, y, vx, vy, life, maxlife, r, g, b, alive, size; } Spark;
typedef struct { int x, y, speed, layer, phase; } Star;
typedef struct { int x, y, alive, type; } Item;
typedef struct { int x, y, phase, timer, alive; } NovaBurst;
/* [BARU] hazard ranjau, dipakai musuh Minelayer */
typedef struct { int x, y, timer, alive; } Mine;
/* [BARU v2] gelombang kejut melingkar milik E_VOIDCORE: radius membesar terus
   dari pusat, beda dari NovaBurst (beam salib lurus) - ini cincin sungguhan,
   tabrakan dicek berbasis jarak-dari-pusat, bukan beamHit. */
typedef struct { int x, y, radius, alive; } VoidPulse;

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

/* [BARU] konstanta 6 musuh baru */
#define MAX_MINES       8
#define MINE_LIFE       150
#define MINE_ARM_AT     130
#define ABSORBER_CHARGE 5

/* [BARU v2] konstanta 5 musuh level-puncak */
#define MAX_VOIDPULSE     3
#define SNIPER_WARN       50    /* frame laser Sniper berkedip sebelum tembak */
#define SNIPER_COOLDOWN   90
#define SWARMER_SPAWN_GAP 70    /* frame antar drone yang dimuntahkan Swarmer */
#define VOIDCORE_PULSE_GAP 130  /* frame antar gelombang VoidCore */
#define VOIDPULSE_GROWTH  2     /* piksel radius bertambah tiap frame */
#define VOIDPULSE_MAX_R   90
#define VOIDPULSE_RING_W  10    /* ketebalan cincin yang bisa melukai */
#define TWIN_ENRAGE_SPEED 2     /* pengali kecepatan tembak saat pasangan mati */

#define HUD_H   30
#define TEXT_Y  12

#define MAX_STACK        3
#define SHIELD_DURATION  260
#define POWER_DURATION   200
#define SPEED_DURATION   220

#define ENEMY_CAP_BASE   6

enum { STATE_MENU, STATE_PLAY, STATE_GAMEOVER, STATE_GACHA, STATE_SKINSELECT };
enum { E_DRONE = 0, E_ZIGZAG, E_TANK, E_SPINNER, E_SHOOTER, E_SPLITTER, E_SHIELDED,
       E_STINGER, E_ORBITER, E_PHANTOM, E_JUGGERNAUT, E_NOVA, E_SHARD, E_BOSS,
       /* [BARU] 6 musuh baru, ditaruh di akhir biar nilai enum lama tidak berubah */
       E_TURRET, E_MINELAYER, E_CHARGER, E_MIRROR, E_HEALER, E_ABSORBER,
       /* [BARU v2] 5 musuh level-puncak (10+), lebih sulit secara MEKANIK
          (bukan cuma HP lebih tebal): pola serangan baru yang belum ada. */
       E_SNIPER, E_SWARMER, E_REFLECTOR, E_VOIDCORE, E_TWIN };
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
/* [BARU v2] beam milik E_SNIPER, terpisah dari MLaser (yang eksklusif boss,
   slotnya cuma 3 dan dipakai penuh sekaligus untuk pola salib). Struktur mirip
   tapi array sendiri supaya banyak Sniper aktif tidak berebut slot dgn boss. */
typedef struct { int x, y, angle, timer, alive; } SniperBeam;

static MadBoss  mad;
static Missile  missiles[MAX_MISSILES];
static Bomb     mbombs[MAX_BOMBS];
static MLaser   mlasers[MAX_MLASERS];
#define MAX_SNIPERBEAM 4
static SniperBeam sniperBeams[MAX_SNIPERBEAM];   /* [BARU v2] */
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
static Mine      mines[MAX_MINES];   /* [BARU] */
static VoidPulse voidpulses[MAX_VOIDPULSE];   /* [BARU v2] */

static int shootX = -100, shootY = 0, shootT = 0;

/* [POLISH] jendela teks berposisi tetap (ID dari FntOpen). Tidak lagi bergantung hitungan \n. */
static int fntTop = -1, fntMid = -1, fntBot = -1, fntScore = -1;

static const int playerColor[MAX_PLAYERS][3] = {
    {  80, 160, 255 },
    { 255,  90,  90 },
    {  90, 230, 120 },
    { 255, 220,  80 }
};

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
        baseOfs[i][0] = buffers[i].draw.ofs[0];
        baseOfs[i][1] = buffers[i].draw.ofs[1];
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

/* =====================================================================
   [FASE 3] Efek layar: screen shake, flash, fade transisi
   Semua state ada di sini. Dipanggil dari main():
     fxUpdate()      1x per frame (sebelum menggambar)
     fxApplyShake()  1x per frame tepat sebelum flip() -> geser DRAWENV.ofs
     fxDraw()        1x per frame setelah semua gambar -> overlay flash/fade
   ===================================================================== */
static int shakeMag  = 0;     /* amplitudo shake saat ini (piksel) */
static int flashT    = 0;     /* sisa frame flash putih */
static int flashMax  = 1;
static int fadeDir   = 0;     /* -1 = fade in (terang), +1 = fade out (gelap), 0 = diam */
static int fadeLevel = 255;   /* 0 = terang penuh, 255 = hitam penuh */
static int fadePending = -1;  /* state tujuan setelah fade out selesai (-1 = tidak ada) */
static int fxTick    = 0;     /* pencacah internal untuk peluruhan shake */

#define SHAKE_MAX   6
#define FADE_SPEED  24

static void fxShake(int mag) {
    if (mag > shakeMag) shakeMag = mag > SHAKE_MAX ? SHAKE_MAX : mag;
}
#define FLASH_COOLDOWN 28      /* jeda minimal (frame) antar flash => maks ~2 kilat/detik (aman fotosensitif) */
static int flashCool = 0;
static void fxFlash(int frames) {
    if (flashCool > 0 && frames < 10) return;   /* flash kecil diabaikan bila baru saja flash */
    flashT = frames; flashMax = frames > 0 ? frames : 1;
    flashCool = FLASH_COOLDOWN + frames;
}

static void fxFadeTo(int newState) { fadePending = newState; fadeDir = 1; }
static void fxFadeInNow(void)      { fadeLevel = 255; fadeDir = -1; }

/* return: 1 bila fade out selesai & state harus berganti, dengan *outState terisi */
static int fxUpdate(int *outState) {
    int switched = 0;
    /* shake meluruh 1 piksel tiap 2 frame agar terasa "getar lalu reda", bukan langsung hilang */
    if (shakeMag > 0 && (fxTick++ & 1)) shakeMag--;
    if (flashT > 0) flashT--;
    if (flashCool > 0) flashCool--;

    if (fadeDir > 0) {
        fadeLevel += FADE_SPEED;
        if (fadeLevel >= 255) {
            fadeLevel = 255;
            if (fadePending >= 0) { *outState = fadePending; switched = 1; }
            fadePending = -1;
            fadeDir = -1;              /* langsung lanjut fade in di state baru */
        }
    } else if (fadeDir < 0) {
        fadeLevel -= FADE_SPEED;
        if (fadeLevel <= 0) { fadeLevel = 0; fadeDir = 0; }
    }
    return switched;
}

static int shakeOffsetX(int frame) { return shakeMag ? ((frame & 1) ? shakeMag : -shakeMag) : 0; }
static void fxApplyShake(int frame);   /* didefinisikan setelah baseOfs & buffers tersedia */
static int shakeOffsetY(int frame) { return shakeMag ? (((frame >> 1) & 1) ? shakeMag / 2 + 1 : -(shakeMag / 2 + 1)) : 0; }

/* ---------- Primitif dasar ---------- */

/* [FIX] semua alokasi primitif disentralisasi lewat primAlloc() supaya ada
   bound-check terhadap BUFFER_LEN. Sebelumnya nextpri ditambah tanpa cek sama
   sekali -> berisiko overflow diam-diam & merusak struct RenderBuffer lain. */
/* [FASE 3] 2 KB terakhir buffer dicadangkan khusus untuk overlay (fade/flash) supaya
   transisi layar tidak pernah gagal tergambar, walau adegan sedang sangat padat. */
static void fxApplyShake(int frame) {
    buffers[active].draw.ofs[0] = baseOfs[active][0] + shakeOffsetX(frame);
    buffers[active].draw.ofs[1] = baseOfs[active][1] + shakeOffsetY(frame);
}

/* [FASE 3] overlay flash putih (additive) + fade hitam. Panggil PALING AKHIR sebelum flip(). */
static void fxDraw(void) {
    primOverlayMode = 1;
    /* Karena ofs bergeser saat shake, gambar overlay sedikit lebih besar dari layar. */
    const int m = SHAKE_MAX + 2;
    if (flashT > 0) {
        int a = flashT * 200 / flashMax;
        flatRect(L_OVERLAY, -m, -m, SCREEN_W + 2 * m, SCREEN_H + 2 * m, a, a, a, 1);
        setBlendMode(L_OVERLAY, 1);      /* additive: menambah terang, tidak menutupi */
    }
    if (fadeLevel > 0) {
        int a = fadeLevel;
        /* fade hitam pakai subtractive?  -> lebih sederhana: kotak hitam opaque dengan
           skala warna lewat mode 2 (kurangi). Kurangi (a,a,a) dari piksel = gelap merata. */
        flatRect(L_OVERLAY, -m, -m, SCREEN_W + 2 * m, SCREEN_H + 2 * m, a, a, a, 1);
        setBlendMode(L_OVERLAY, 2);      /* subtractive: mengurangi terang = memudar ke hitam */
    }
    primOverlayMode = 0;
}

/* ---------- Planet ---------- */

/* [POLISH] Planet: sisi gelap tetap berwarna (tidak hitam), tepi memberi cincin atmosfer,
   dan ada pita permukaan supaya tampak seperti planet gas, bukan segi-8 hitam polos. */
/* [DETAIL] Planet 64-segmen (dulu 16) untuk siluet lebih bulat/halus, dengan
   3 lapis: inti bershading gradien halus, pita awan mengikuti kurva lengkung
   permukaan (bukan garis lurus), dan atmosfer 3-lapis (dalam->luar) untuk
   kesan kedalaman, bukan satu glow datar. */
static void planetSphere(int cx, int cy, int rad,
                         int litR, int litG, int litB,
                         int darkR, int darkG, int darkB) {
    int fR = litR * 32 / 100, fG = litG * 32 / 100, fB = litB * 32 / 100;
    if (darkR < fR) darkR = fR;
    if (darkG < fG) darkG = fG;
    if (darkB < fB) darkB = fB;

    /* inti: 64 segmen, shading dari arah cahaya kiri-atas, dengan sedikit
       terminator lebih tajam (pow-ish) supaya batas siang/malam lebih jelas
       ketimbang gradien linear datar */
    for (int i = 0; i < 64; i++) {
        int x1 = cx + cosO(i)     * rad / 127;
        int y1 = cy + sinO(i)     * rad / 127;
        int x2 = cx + cosO(i + 1) * rad / 127;
        int y2 = cy + sinO(i + 1) * rad / 127;
        int litAmt = (-cosO(i) + 127);
        litAmt = (litAmt * 3 + (-sinO(i) + 127)) / 4;   /* bobot lebih ke horizontal = terminator lebih tajam */
        if (litAmt < 0) litAmt = 0;
        if (litAmt > 254) litAmt = 254;
        int r = darkR + (litR - darkR) * litAmt / 254;
        int g = darkG + (litG - darkG) * litAmt / 254;
        int b = darkB + (litB - darkB) * litAmt / 254;
        int er = r * 55 / 100, eg = g * 55 / 100, eb = b * 55 / 100;   /* tepi sedikit lebih gelap dari tengah */
        tri(L_PLANET, cx, cy, r, g, b,
                      x1, y1, er, eg, eb,
                      x2, y2, er, eg, eb);
    }

    /* [FIX] Pita awan: irisan elips HORIZONTAL pada beberapa ketinggian,
       bukan busur sudut 0..360 (itu penyebab bug "garis silang seperti
       bintang" pada render sebelumnya). Untuk tiap ketinggian y relatif
       pusat, lebar pita di titik itu = rad*cos(asin(y/rad)) -> pakai tabel
       sinus terbalik sederhana lewat sinO/cosO berpasangan. */
    for (int band = 0; band < 3; band++) {
        int hy = (-rad / 2) + band * (rad / 2);              /* ketinggian pita relatif pusat: atas, tengah, bawah */
        if (hy <= -rad || hy >= rad) continue;
        /* cari sudut ang di mana sinO(ang)*rad/127 == hy, dengan mencari nilai
           terdekat di tabel 64-segmen (cukup presisi untuk ukuran planet kita) */
        int bestAng = 0, bestDiff = 99999;
        for (int a = 0; a < 32; a++) {                        /* 0..31 = setengah atas tabel (y dari -rad..+rad) */
            int yy = sinO(a) * rad / 127;
            int diff = yy - hy; if (diff < 0) diff = -diff;
            if (diff < bestDiff) { bestDiff = diff; bestAng = a; }
        }
        int halfW = cosO(bestAng) * rad / 127;                /* setengah lebar pita di ketinggian ini */
        if (halfW < 0) halfW = -halfW;
        if (halfW < 3) continue;
        int bandLit  = litR * (50 + band * 12) / 100;
        int bandLitG = litG * (50 + band * 12) / 100;
        int bandLitB = litB * (50 + band * 12) / 100;
        int thick = (band == 1) ? 2 : 1;                       /* pita tengah sedikit lebih tebal */
        rect(L_PLANET, cx - halfW, cy + hy, halfW * 2, thick, bandLit, bandLitG, bandLitB);
    }

    /* atmosfer 3-lapis: dalam (rapat,terang) -> tengah -> luar (lebar,redup).
       Ini yang memberi kesan "bercahaya dari dalam" alih-alih 1 lingkaran blur. */
    /* [FIX] Gradien radial (Gouraud triangle-fan) pada radius besar secara
       inheren menghasilkan pola "starburst" 8-arah (Mach banding di tepi
       segitiga - fenomena optik nyata, bukan bug; efek serupa juga muncul di
       banyak game PS1 asli). Sudah dicoba: menambah segmen (64 vs 16) TIDAK
       menghilangkannya, menumpuk banyak lapis MEMPERPARAH. Solusi yang
       terbukti efektif dari eksperimen: pakai HANYA 1 lapis tipis, radius
       dekat dengan planet (bukan jauh melebar), dan redupkan drastis supaya
       starburst tetap ada tapi halus/tidak dominan - berkesan seperti
       corona/atmosfer redup, bukan lens-flare mencolok. */
    glowDiscHD(cx, cy, rad + 6, litR * 3 / 10, litG * 3 / 10, litB * 3 / 10);
    /* sorot matahari: highlight kecil & terang di sisi yang menghadap cahaya.
       Radius dikecilkan signifikan (dulu rad/2+4 ~ terlalu besar utk planet
       gede, jadi starburst kedua yg tumpang tindih dgn atmosfer). */
    glowDiscHD(cx - rad / 3, cy - rad / 3, rad / 4, litR * 6 / 10, litG * 6 / 10, litB * 6 / 10);
    glowDiscHD(cx - rad / 3, cy - rad / 3, rad / 8, 255, 255, 240);   /* highlight inti kecil, hampir putih */
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

/* [FASE 3] Bintang 3 lapis parallax dengan bentuk berbeda (dan lebih murah dari discLo 8 segitiga):
     lapis 0 (jauh)  : titik 1x1 redup           -> 1 TILE      (12 byte)
     lapis 1 (tengah): tanda '+' kecil berkelip   -> 2 TILE      (24 byte)
     lapis 2 (dekat) : bintang besar + ekor gerak -> 1 G3 + TILE (40 byte)
   'speedBoost' menambah panjang ekor (dipakai saat pemain bergerak/boss agar terasa cepat). */
static void drawStars(int frame, int speedBoost) {
    for (int i = 0; i < NUM_STARS; i++) {
        int x = stars[i].x, y = stars[i].y;
        int tw = 140 + sinS(frame + stars[i].phase) / 2;
        switch (stars[i].layer) {
        case 0:
            rect(L_PLANET, x, y, 1, 1, tw / 2, tw / 2, tw / 2 + 20);
            break;
        case 1: {
            int v = tw * 3 / 4;
            rect(L_PLANET, x - 1, y, 3, 1, v, v, v);
            rect(L_PLANET, x, y - 1, 1, 3, v, v, v);
            break;
        }
        default: {
            int tail = 4 + speedBoost * 2;
            tri(L_PLANET, x, y, tw, tw, 255,
                          x - 1, y - tail, 0, 0, 0,
                          x + 1, y - tail, 0, 0, 0);
            rect(L_PLANET, x - 1, y - 1, 2, 2, 255, 255, 255);
            break;
        }
        }
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

/* [FASE 3] Tema latar per stage. Tiap 2 level ganti tema; warna diinterpolasi halus. */
typedef struct {
    int neb1R, neb1G, neb1B;      /* warna nebula grup A (kiri atas) */
    int neb2R, neb2G, neb2B;      /* warna nebula grup B (kanan bawah) */
    int topR, topG, topB;         /* gradien langit atas */
    int botR, botG, botB;         /* gradien langit bawah */
    int planR, planG, planB;      /* warna planet besar */
    int cloudR, cloudG, cloudB;   /* warna awan/debu parallax */
} Theme;

#define NUM_THEMES 5
static const Theme themes[NUM_THEMES] = {
    /* 1 UNGU-BIRU (asli)  */ {  40,10,60,   8,45,55,    20,6,44,   2,4,20,    255,210,150,  90,70,130 },
    /* 2 HIJAU TOKSIK      */ {  10,60,30,  30,55,10,    6,30,18,   2,12,8,    190,255,140,  60,120,70 },
    /* 3 MERAH MEMBARA     */ {  70,15,10,  60,30,5,     40,8,8,    16,2,4,    255,160,110,  140,60,50 },
    /* 4 BIRU ES           */ {  10,40,80,  20,70,90,    6,20,52,   2,8,24,    170,230,255,  70,110,150 },
    /* 5 EMAS BADAI        */ {  70,50,10,  80,20,60,    44,26,6,   18,6,12,   255,230,140,  150,110,60 },
};

/* themeFor memperlakukan Theme sebagai array int. Pastikan tak ada padding/field non-int. */
_Static_assert(sizeof(Theme) == 18 * sizeof(int), "Theme harus 18 int murni (tanpa padding)");

/* interpolasi linear satu warna; t dalam 0..256 */
static int lerpI(int a, int b, int t) { return a + (b - a) * t / 256; }

/* [FASE 3] Tema TEGAS per pasang level (1-2, 3-4, 5-6, 7-8, 9-10).
   Peralihan halus dilakukan lewat crossfade sebenarnya: 'themeMix' (0..256) menahan
   tema lama lalu meluncur ke tema baru selama ~1 detik setiap kali level naik. */
static int themePrev = 0;      /* indeks tema sebelum berganti */
static int themeCur  = 0;      /* indeks tema tujuan */
static int themeMix  = 256;    /* 0 = tema lama penuh, 256 = tema baru penuh */

static void themeSetLevel(int level, int instant) {
    int idx = (level - 1) / 2;
    if (idx >= NUM_THEMES) idx = NUM_THEMES - 1;
    if (idx < 0) idx = 0;
    if (idx == themeCur) return;
    themePrev = themeCur;
    themeCur  = idx;
    themeMix  = instant ? 256 : 0;
}

static void themeUpdate(void) {
    if (themeMix < 256) { themeMix += 5; if (themeMix > 256) themeMix = 256; }   /* ~51 frame = 0,85 dtk */
}

static void themeFor(int frame, Theme *out) {
    const int *A = (const int *)&themes[themePrev];
    const int *B = (const int *)&themes[themeCur];
    int *O = (int *)out;
    int breathe = sinS(frame / 12) / 24;             /* denyut halus supaya latar tetap hidup */
    for (int i = 0; i < (int)(sizeof(Theme) / sizeof(int)); i++) {
        int v = lerpI(A[i], B[i], themeMix) + (i < 6 ? breathe : 0);   /* denyut hanya di warna nebula */
        O[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
    }
}

static void drawBackground(int frame) {
    Theme th;
    themeFor(frame, &th);

    /* --- planet besar & planet kecil (parallax lambat) --- */
    int py = (frame / 5) % (SCREEN_H + 140) - 70;
    int px = 250;
    tri(L_PLANET, px - 48, py + 3, 210, 180, 230,  px + 48, py - 3, 210, 180, 230,  px, py + 10, 100, 70, 140);
    tri(L_PLANET, px - 48, py + 3, 210, 180, 230,  px + 48, py - 3, 210, 180, 230,  px, py - 8, 150, 120, 190);
    planetSphere(px, py, 30, th.planR, th.planG, th.planB, th.planR / 6, th.planG / 8, th.planB / 4);
    disc(L_PLANET, px - 8, py - 8, 12, 255, 240, 200, th.planR, th.planG * 3 / 4, th.planB / 2);

    int py2 = (frame / 8 + 100) % (SCREEN_H + 80) - 40;
    planetSphere(40, py2, 14, 220, 245, 255, 20, 50, 120);

    /* --- nebula (tema) --- */
    int pulse = 14 + sinS(frame / 6) / 14;
    disc(L_BG, 70, 80, 70,  th.neb1R + pulse, th.neb1G, th.neb1B + pulse,  th.topR, th.topG, th.topB);
    disc(L_BG, 110, 60, 50, th.neb1R + pulse + 10, th.neb1G + 10, th.neb1B + pulse + 10,  th.topR, th.topG, th.topB);
    disc(L_BG, 260, 160, 70, th.neb2R, th.neb2G + pulse, th.neb2B + pulse,  th.botR, th.botG, th.botB);
    disc(L_BG, 230, 180, 50, th.neb2R + 4, th.neb2G + pulse + 10, th.neb2B + pulse + 5,  th.botR, th.botG, th.botB);

    /* --- [POLISH] debu kosmik: garis-garis kecil jatuh bertingkat kecepatan (efek melaju).
       Sebelumnya berupa cakram gelap besar yang tampak seperti "lubang hitam". Sekarang
       hanya TILE tipis berwarna tema, jadi tidak pernah menutupi musuh/peluru. --- */
    for (int i = 0; i < 10; i++) {
        int sp = 2 + (i % 3) * 2;                          /* 2,4,6 px/frame */
        int cy = (frame * sp + i * 37) % (SCREEN_H + 24) - 12;
        int cx = (i * 61 + 13) % SCREEN_W;
        int len = 4 + sp * 2;
        int a  = 50 + sp * 14;
        rect(L_PLANET, cx, cy, 1, len, th.cloudR * a / 255, th.cloudG * a / 255, th.cloudB * a / 255);
    }

    rectGradV(L_BG, 0, 0, SCREEN_W, SCREEN_H / 2,  th.topR, th.topG, th.topB,  th.botR * 3, th.botG * 3, th.botB * 2);
    rectGradV(L_BG, 0, SCREEN_H / 2, SCREEN_W, SCREEN_H / 2,  th.botR * 3, th.botG * 3, th.botB * 2,  th.botR, th.botG, th.botB);
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
static int highScore = 0;   /* dimuat dari memory card saat boot, ditampilkan di menu */
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

/* [SAVE] Kumpulkan state saat ini & tulis ke memory card. Dipanggil hanya di
   titik-titik aman (bukan tiap frame) karena I/O memory card lambat & tidak
   perlu presisi real-time. Gagal diam-diam bila kartu tidak ada (lihat save.c). */
static void persistProgress(void) {
    SaveData sd;
    sd.highScore    = highScore;
    sd.gems         = gems;
    sd.unlockedMask = unlockedMask;
    sd.lastSkin     = skinCursor;
    saveWrite(&sd);
}

static int doGachaPull(void) {
    if (gems < GACHA_COST) return 0;
    gems -= GACHA_COST;
    int idx = gachaRoll();
    gachaResultSkin = idx;
    if (unlockedMask & (1u << idx)) { gachaResultDup = 1; gems += 15; }
    else { gachaResultDup = 0; unlockedMask |= (1u << idx); }
    persistProgress();
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

/* [POLISH] Desain pesawat baru "STARFURY".
   Kotak hitbox tetap 16x16 (x..x+16, y..y+16) supaya gameplay tidak berubah;
   sayap dan api boleh menjorok keluar secara visual.
     - hidung runcing panjang dengan pantulan cahaya
     - badan dua-tone (terang di tengah, gelap di tepi) memberi kesan volume
     - sayap sapu ke belakang, ujungnya lancip, ada strip warna skin
     - dua sirip kecil di belakang
     - kokpit kaca bercahaya biru-putih
     - dua mesin dengan api tiga lapis yang berdenyut
*/
static void drawPlayer(int x, int y, int frame, int skin) {
    const SkinDef *s = &skinTable[skin];
    int cx = x + 8;
    int flick = (frame / 2) % 3;             /* 0..2 : denyut api */
    int fl = 5 + flick * 2;                  /* panjang api */

    /* --- api mesin (di belakang badan) --- */
    for (int side = -1; side <= 1; side += 2) {
        int ex = cx + side * 4;
        glowDisc(ex, y + 20, 5 + flick, s->glowR, s->glowG, s->glowB, 8);
        tri(L_PLAYER, ex - 2, y + 17, 255, 255, 255,
                      ex + 2, y + 17, 255, 255, 255,
                      ex,     y + 17 + fl + 3, 255, 200, 60);
        tri(L_PLAYER, ex - 3, y + 17, s->glowR, s->glowG, s->glowB,
                      ex + 3, y + 17, s->glowR, s->glowG, s->glowB,
                      ex,     y + 17 + fl + 6, s->glowR / 2, 20, 0);
    }

    /* --- sirip ekor kecil --- */
    tri(L_PLAYER, cx - 5, y + 10, s->wingR, s->wingG, s->wingB,
                  cx - 7, y + 20, s->hullR / 2, s->hullG / 2, s->hullB / 2,
                  cx - 3, y + 16, s->wingR / 2, s->wingG / 2, s->wingB / 2);
    tri(L_PLAYER, cx + 5, y + 10, s->wingR, s->wingG, s->wingB,
                  cx + 7, y + 20, s->hullR / 2, s->hullG / 2, s->hullB / 2,
                  cx + 3, y + 16, s->wingR / 2, s->wingG / 2, s->wingB / 2);

    /* --- sayap sapu --- */
    for (int side = -1; side <= 1; side += 2) {
        int rx = cx + side * 4;            /* pangkal sayap di badan */
        int tx = cx + side * 17;           /* ujung sayap (menjorok keluar) */
        /* panel atas sayap: terang di pangkal, gelap di ujung */
        tri(L_PLAYER, rx, y + 3,
                      clamp255(s->wingR + 70), clamp255(s->wingG + 70), clamp255(s->wingB + 70),
                      tx, y + 19, s->wingR / 2, s->wingG / 2, s->wingB / 2,
                      rx, y + 15, s->wingR, s->wingG, s->wingB);
        /* strip warna aksen di tepi depan sayap */
        tri(L_PLAYER, rx, y + 3,  255, 255, 255,
                      tx, y + 19, s->glowR, s->glowG, s->glowB,
                      rx + side * 2, y + 9, s->glowR, s->glowG, s->glowB);
        /* lampu ujung sayap */
        rect(L_PLAYER, tx - 1, y + 17, 3, 3, 255, 250, 200);
    }

    /* --- badan utama: dua sisi berbeda kecerahan -> kesan volume --- */
    tri(L_PLAYER, cx, y - 8, 255, 255, 255,               /* hidung */
                  cx - 6, y + 17,
                  s->hullR * 6 / 10, s->hullG * 6 / 10, s->hullB * 6 / 10,
                  cx,     y + 17, s->hullR, s->hullG, s->hullB);
    tri(L_PLAYER, cx, y - 8, 255, 255, 255,
                  cx,     y + 17, s->hullR, s->hullG, s->hullB,
                  cx + 6, y + 17,
                  clamp255(s->hullR + 40), clamp255(s->hullG + 40), clamp255(s->hullB + 40));
    /* panel jahitan badan (di bawah kokpit, tidak menimpanya) */
    rect(L_PLAYER, cx - 1, y + 11, 2, 5, 30, 30, 50);
    rect(L_PLAYER, cx - 4, y + 14, 8, 1, 20, 20, 40);

    /* --- kokpit kaca (digambar TERAKHIR di atas badan, lebih besar & jelas) --- */
    tri(L_PLAYER, cx, y - 5, 240, 255, 255,
                  cx - 3, y + 7, 40, 150, 230,
                  cx + 3, y + 7, 20, 90, 200);
    tri(L_PLAYER, cx, y - 5, 255, 255, 255,
                  cx - 1, y + 1, 210, 245, 255,
                  cx + 1, y + 1, 210, 245, 255);        /* pantulan */

    /* --- moncong meriam kecil di hidung --- */
    rect(L_PLAYER, cx - 1, y - 9, 2, 3, 255, 255, 255);
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

/* [BARU] 6 musuh baru */

static void drawTurret(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 12, cy = y + 12;
    int locking = (e->vx != 0 && e->timer < 15);
    rect(L_ENEMY, x, y + 10, 24, 14, 90, 90, 100);
    tri(L_ENEMY, x, y + 24, 60, 60, 70, x + 24, y + 24, 60, 60, 70, cx, y + 10, 100, 100, 115);
    glowDisc(cx, cy - 2, locking ? 8 : 5, locking ? 255 : 120, locking ? 60 : 160, 60, 8);
    disc(L_ENEMY, cx, cy - 2, 5, 220, 80, 60, 80, 20, 20);
    int rot = (frame / 4) & 15;
    int lx = cx + cosI(rot) * 4 / 127, ly = (cy - 2) + sinI(rot) * 4 / 127;
    rect(L_ENEMY, lx - 1, ly - 1, 2, 2, 255, 255, 255);
}

static void drawMinelayer(const Enemy *e, int frame) {
    (void)frame;
    int x = e->x, y = e->y, cx = x + 9;
    int open = (e->timer < 12);
    glowDisc(cx, y + 6, 5, 120, 255, 140, 8);
    tri(L_ENEMY, x - 2, y + 4, 60, 160, 90, x + 20, y + 4, 60, 160, 90, cx, y + 16, 40, 110, 60);
    rect(L_ENEMY, cx - 3, y + 10, 6, open ? 6 : 3, open ? 255 : 90, open ? 220 : 140, 60);
    discLo(L_ENEMY, cx, y + 2, 3, 200, 255, 200, 60, 160, 90);
}

static void drawCharger(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 8;
    if (e->shield == 1) {
        if (frame & 1) glowDisc(cx, y + 9, 10, 255, 60, 60, 8);
    } else if (e->shield == 2) {
        glowDisc(cx, y + 9, 6, 255, 180, 80, 8);
    }
    tri(L_ENEMY, cx, y - 4, 255, 200, 180, cx - 8, y + 14, 200, 60, 40, cx + 8, y + 14, 200, 60, 40);
    tri(L_ENEMY, cx, y + 18, 255, 140, 100, cx - 5, y + 10, 160, 40, 30, cx + 5, y + 10, 160, 40, 30);
    discLo(L_ENEMY, cx, y + 6, 2, 255, 255, 255, 255, 200, 180);
}

static void drawMirror(const Enemy *e, int frame) {
    int cx = e->x + 8, cy = e->y + 8;
    int pulse = sinS(frame * 3) / 30;
    int a = e->shield ? 160 : 255; /* clone sedikit lebih redup */
    tri(L_ENEMY, cx, cy - 8 - pulse, a, a, 255, cx - 7, cy, a/2, a/2, 200, cx, cy + 8 + pulse, a/3, a/3, 160);
    tri(L_ENEMY, cx, cy - 8 - pulse, a, a, 255, cx + 7, cy, a/2, a/2, 200, cx, cy + 8 + pulse, a/3, a/3, 160);
    glowDisc(cx, cy, 5, 160, 180, 255, 8);
    discLo(L_ENEMY, cx, cy, 2, 255, 255, 255, 200, 220, 255);
}

static void drawHealer(const Enemy *e, int frame) {
    int cx = e->x + 9, cy = e->y + 9;
    int pulseSoon = e->timer < 15;
    glowDisc(cx, cy, pulseSoon ? 10 : 6, 100, 255, 160, 8);
    disc(L_ENEMY, cx, cy, 7, 180, 255, 200, 40, 140, 90);
    rect(L_ENEMY, cx - 1, cy - 4, 2, 8, 255, 255, 255);
    rect(L_ENEMY, cx - 4, cy - 1, 8, 2, 255, 255, 255);
    if (pulseSoon && (frame & 2)) glowDisc(cx, cy, 30, 120, 255, 160, 8);
}

static void drawAbsorber(const Enemy *e, int frame) {
    int cx = e->x + 11, cy = e->y + 11;
    int glow = 100 + e->timer * 30;
    if (glow > 255) glow = 255;
    glowDisc(cx, cy, 10, glow, 80, 255, 8);
    disc(L_ENEMY, cx, cy, 8, 160, 100, 255, 40, 20, 90);
    int rot = (frame / 2) & 15;
    for (int i = 0; i < 16; i += 4) {
        int a = (i + rot) & 15;
        int x1 = cx + cosI(a) * 12 / 127, y1 = cy + sinI(a) * 12 / 127;
        rect(L_ENEMY, x1 - 1, y1 - 1, 2, 2, 220, 180, 255);
    }
    discLo(L_ENEMY, cx, cy, 3, 255, 255, 255, 200, 160, 255);
}

/* [BARU v2] E_SNIPER: diam, mengunci lama, lalu tembak beam lurus presisi.
   Mata merah membesar & berkedip cepat saat warning (e->timer < SNIPER_WARN). */
static void drawSniper(const Enemy *e, int frame) {
    int x = e->x, y = e->y, cx = x + 12, cy = y + 12;
    int locking = (e->timer > 0 && e->timer < SNIPER_WARN);
    rect(L_ENEMY, x + 2, y + 14, 20, 10, 50, 50, 60);
    tri(L_ENEMY, x, y + 24, 30, 30, 40, x + 24, y + 24, 30, 30, 40, cx, y + 10, 70, 70, 85);
    int blink = locking ? ((frame & 1) ? 255 : 60) : 180;
    int eyeR = locking ? 7 + (frame % 4) : 4;
    glowDisc(cx, cy - 2, eyeR + 4, blink, 20, 20, 8);
    disc(L_ENEMY, cx, cy - 2, eyeR, blink, 20, 20, blink / 3, 5, 5);
    rect(L_ENEMY, x - 2, y + 18, 3, 6, 60, 60, 70);
    rect(L_ENEMY, x + 23, y + 18, 3, 6, 60, 60, 70);
}

/* [BARU v2] E_SWARMER: sarang organik diam yang terus memuntahkan drone kecil
   cepat selama hidup - tekanan datang dari JUMLAH, bukan tembakan individual. */
static void drawSwarmer(const Enemy *e, int frame) {
    int cx = e->x + 13, cy = e->y + 13;
    int pulse = sinS(frame * 2) / 20;
    glowDisc(cx, cy, 14 + pulse, 140, 255, 90, 8);
    for (int i = 0; i < 16; i += 2) {
        int x1 = cx + cosI(i)     * (12 + pulse) / 127, y1 = cy + sinI(i)     * (12 + pulse) / 127;
        int x2 = cx + cosI(i + 2) * (12 + pulse) / 127, y2 = cy + sinI(i + 2) * (12 + pulse) / 127;
        tri(L_ENEMY, cx, cy, 40, 90, 30, x1, y1, 100, 200, 70, x2, y2, 100, 200, 70);
    }
    int holeGlow = (e->timer < 10) ? 255 : 120;
    disc(L_ENEMY, cx, cy, 6, holeGlow, 255, holeGlow / 2, 20, 60, 15);
}

/* [BARU v2] E_REFLECTOR: kristal berputar dengan perisai HANYA di satu sisi
   (sisi terang = terlindungi, sisi gelap = rentan). Pemain harus memutari
   posisi tembak, bukan sekadar spam peluru dari depan. */
static void drawReflector(const Enemy *e, int frame) {
    int cx = e->x + 10, cy = e->y + 10;
    int rot = (e->t * 2) & 15;   /* arah perisai berputar pelan seiring waktu hidup */
    for (int i = 0; i < 16; i += 2) {
        /* [FIX] shielded HARUS dicek dari i (posisi relatif SEBELUM rotasi),
           bukan dari a (posisi absolut SESUDAH rotasi) - versi lama salah,
           bikin sisi terang keliatan statis walau rot berubah (dibuktikan
           lewat simulasi terpisah: rot=0 dan rot=4 menghasilkan sisi terang
           yang identik, padahal harusnya ikut berputar). */
        int shielded = (i < 8);  /* setengah lingkaran TETAP relatif rotasi = terlindungi */
        int a = (i + rot) & 15;
        int x1 = cx + cosI(a)     * 11 / 127, y1 = cy + sinI(a)     * 11 / 127;
        int x2 = cx + cosI(a + 2) * 11 / 127, y2 = cy + sinI(a + 2) * 11 / 127;
        if (shielded)
            tri(L_ENEMY, cx, cy, 200, 220, 255, x1, y1, 120, 180, 255, x2, y2, 120, 180, 255);
        else
            tri(L_ENEMY, cx, cy, 60, 30, 70, x1, y1, 30, 10, 40, x2, y2, 30, 10, 40);
    }
    discLo(L_ENEMY, cx, cy, 3, 255, 255, 255, 200, 220, 255);
    /* penanda kecil di sisi rentan supaya pemain bisa baca pola */
    int markA = (rot + 12) & 15;
    int mx = cx + cosI(markA) * 9 / 127, my = cy + sinI(markA) * 9 / 127;
    rect(L_ENEMY, mx - 1, my - 1, 2, 2, 255, 80, 80);
}

/* [BARU v2] E_VOIDCORE: inti diam yang melepas gelombang kejut MELINGKAR
   (bukan beam lurus seperti Nova) secara berkala, radius terus membesar. */
static void drawVoidcore(const Enemy *e, int frame) {
    int cx = e->x + 12, cy = e->y + 12;
    int chargeT = VOIDCORE_PULSE_GAP - e->timer;
    int pulse = (chargeT > VOIDCORE_PULSE_GAP - 20) ? (chargeT - (VOIDCORE_PULSE_GAP - 20)) : 0;
    glowDisc(cx, cy, 10 + pulse, 150, 60, 220, 8);
    disc(L_ENEMY, cx, cy, 9, 40, 10, 60, 90, 20, 140);
    /* partikel tersedot masuk ke inti - kesan "menyerap energi" sebelum meledak */
    int rot = (frame * 3) & 63;
    for (int i = 0; i < 4; i++) {
        int a = (rot + i * 16) & 63;
        int dist = 16 - ((frame + i * 8) % 16);
        int px = cx + cosO(a) * dist / 127, py = cy + sinO(a) * dist / 127;
        rect(L_ENEMY, px - 1, py - 1, 2, 2, 200, 140, 255);
    }
    discLo(L_ENEMY, cx, cy, 3, 255, 255, 255, 180, 100, 255);
}

/* [BARU v2] E_TWIN: musuh berpasangan terhubung tali energi. e->buff menyimpan
   indeks pasangan di array enemies[]. Kalau pasangan mati, sisanya "enrage"
   (ditandai warna lebih merah & berkedip cepat, kecepatan ditangani di update). */
static void drawTwin(const Enemy *e, int frame) {
    int cx = e->x + 9, cy = e->y + 9;
    int enraged = (e->vx == 1);   /* vx dipakai sbg flag enrage (1 = pasangan sudah mati) */
    int r = enraged ? 255 : 160, g = enraged ? 60 : 140, b = enraged ? 60 : 255;
    int flick = enraged ? ((frame & 2) ? 255 : 150) : 200;
    glowDisc(cx, cy, 8, r, g, b, 8);
    tri(L_ENEMY, cx, cy - 8, flick, flick, 255, cx - 7, cy + 6, r / 2, g / 2, b / 2, cx + 7, cy + 6, r / 2, g / 2, b / 2);
    discLo(L_ENEMY, cx, cy, 3, 255, 255, 255, r, g, b);
}

/* [BARU v2] gambar tali energi antar Twin (dipanggil terpisah dari loop utama
   karena butuh akses ke DUA enemy sekaligus - lihat pemanggilan di render). */
static void drawTwinLink(int x1, int y1, int x2, int y2, int frame) {
    int mx = (x1 + y1) & 1;   /* variasi kecil biar tidak statis-kaku */
    int flick = (frame & 4) ? 180 : 100;
    int steps = 6;
    for (int i = 0; i < steps; i++) {
        int ax = x1 + (x2 - x1) * i / steps, ay = y1 + (y2 - y1) * i / steps;
        int bx = x1 + (x2 - x1) * (i + 1) / steps, by = y1 + (y2 - y1) * (i + 1) / steps;
        int wob = (mx + i + frame / 3) % 2 ? 1 : -1;
        rect(L_FX, (ax + bx) / 2, (ay + by) / 2 + wob, 2, 1, flick, flick / 3, flick / 2);
    }
}

/* [BARU v2] gambar cincin gelombang kejut VoidPulse: MELINGKAR, bukan salib
   lurus seperti Nova - beda mekanik & visual dari sistem hazard yang sudah ada. */
static void drawVoidPulse(const VoidPulse *v) {
    int fadeOut = v->radius > VOIDPULSE_MAX_R - 20 ? (VOIDPULSE_MAX_R - v->radius) * 255 / 20 : 255;
    if (fadeOut < 0) fadeOut = 0;
    for (int i = 0; i < 32; i++) {
        int a1 = i * 2, a2 = i * 2 + 2;
        int x1 = v->x + cosO(a1) * v->radius / 127, y1 = v->y + sinO(a1) * v->radius / 127;
        int x2 = v->x + cosO(a2) * v->radius / 127, y2 = v->y + sinO(a2) * v->radius / 127;
        rect(L_FX, (x1 + x2) / 2 - 1, (y1 + y2) / 2 - 1, 2, 2, 180 * fadeOut / 255, 60 * fadeOut / 255, 220 * fadeOut / 255);
    }
}

static void drawMine(const Mine *m, int frame) {
    int armed = m->timer < MINE_ARM_AT;
    int blinkRate = armed ? 4 : 10;
    int on = (frame % blinkRate) < 2;
    int v = on ? 255 : 80;
    disc(L_ENEMY, m->x, m->y, 5, v, v / 4, v / 4, 60, 10, 10);
    glowDisc(m->x, m->y, armed ? 8 : 5, v, 40, 30, 8);
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
    case E_TURRET:     drawTurret(e, frame);     break;
    case E_MINELAYER:  drawMinelayer(e, frame);  break;
    case E_CHARGER:    drawCharger(e, frame);    break;
    case E_MIRROR:     drawMirror(e, frame);     break;
    case E_HEALER:     drawHealer(e, frame);     break;
    case E_ABSORBER:   drawAbsorber(e, frame);   break;
    case E_SNIPER:     drawSniper(e, frame);     break;
    case E_SWARMER:    drawSwarmer(e, frame);    break;
    case E_REFLECTOR:  drawReflector(e, frame);  break;
    case E_VOIDCORE:   drawVoidcore(e, frame);   break;
    case E_TWIN:       drawTwin(e, frame);       break;
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

/* [FIX] spawnEBullet sekarang menerima vx,vy supaya peluru bisa mengarah (aim),
   bukan cuma jatuh lurus ke bawah. Semua pemanggilan lama harus diupdate. */
static void spawnEBullet(int x, int y, int vx, int vy) {
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (ebullets[i].alive) continue;
        ebullets[i].x = x;
        ebullets[i].y = y;
        ebullets[i].vx = vx;
        ebullets[i].vy = vy;
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
        sfxPlay(SND_WARN);
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
    sfxPlay(big ? SND_EXPLODE_B : SND_EXPLODE_S);
    if (big) fxShake(3);
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
        enemies[k].vy = 0; enemies[k].tx = 0; enemies[k].ty = 0; enemies[k].buff = 0;
        enemies[k].alive = 1;
        made++;
    }
}

/* [BARU v2] E_SWARMER memuntahkan 1 E_DRONE cepat tiap SWARMER_SPAWN_GAP frame
   selama masih hidup - drone yang dipakai adalah tipe yang sudah ada (tidak
   menambah tipe musuh baru lagi), supaya tekanannya murni dari JUMLAH. */
static void spawnSwarmDrone(int x, int y) {
    for (int k = 0; k < MAX_ENEMIES; k++) {
        if (enemies[k].alive) continue;
        enemies[k].type = E_DRONE;
        enemies[k].x = x; enemies[k].y = y;
        enemies[k].hp = 1; enemies[k].t = rand() % 16;
        enemies[k].shield = 0; enemies[k].timer = 0;
        enemies[k].vx = 0; enemies[k].vy = 0; enemies[k].tx = 0; enemies[k].ty = 0; enemies[k].buff = 0;
        enemies[k].alive = 1;
        return;
    }
}

/* [BARU v2] E_TWIN selalu muncul berpasangan (BUKAN lewat pickEnemyType acak
   biasa - lihat komentar di pickEnemyType). e->buff tiap unit menyimpan
   indeks pasangannya di array enemies[], dicek tiap frame di loop update
   untuk menentukan status enrage. Gagal diam-diam kalau slot kurang dari 2. */
static void spawnTwinPair(int level) {
    int slot[2] = { -1, -1 }, found = 0;
    for (int k = 0; k < MAX_ENEMIES && found < 2; k++)
        if (!enemies[k].alive) slot[found++] = k;
    if (found < 2) return;   /* tidak cukup slot, lewati kali ini */

    int baseX = 40 + rand() % (SCREEN_W - 120);
    for (int i = 0; i < 2; i++) {
        Enemy *e = &enemies[slot[i]];
        e->type = E_TWIN;
        e->x = baseX + i * 60; e->y = HUD_H + 2;
        e->hp = 2 + (level >= 10 ? 1 : 0);
        e->t = rand() % 16; e->shield = 0; e->timer = 0;
        e->vx = 0; e->vy = 0; e->tx = 0; e->ty = 0;
        e->buff = slot[1 - i];   /* index pasangan (silang: 0->1, 1->0) */
        e->alive = 1;
    }
}

/* [BARU] fungsi bantu 6 musuh baru */

static void spawnMine(int x, int y) {
    for (int i = 0; i < MAX_MINES; i++) {
        if (mines[i].alive) continue;
        mines[i].x = x; mines[i].y = y;
        mines[i].timer = MINE_LIFE;
        mines[i].alive = 1;
        return;
    }
}

/* [BARU v2] gelombang kejut melingkar milik E_VOIDCORE */
static void spawnVoidPulse(int x, int y) {
    for (int i = 0; i < MAX_VOIDPULSE; i++) {
        if (voidpulses[i].alive) continue;
        voidpulses[i].x = x; voidpulses[i].y = y;
        voidpulses[i].radius = 0;
        voidpulses[i].alive = 1;
        return;
    }
}

static void spawnMirrorClone(int x, int y) {
    for (int k = 0; k < MAX_ENEMIES; k++) {
        if (enemies[k].alive) continue;
        enemies[k].type = E_MIRROR;
        enemies[k].x = x + ((rand() & 1) ? 10 : -10);
        enemies[k].y = y;
        enemies[k].hp = 1;
        enemies[k].t = 0;
        enemies[k].shield = 1; /* [PENTING] sudah "dipakai" jatah split, tidak boleh clone lagi */
        enemies[k].timer = 0;
        enemies[k].vx = (rand() & 1) ? 2 : -2;
        enemies[k].vy = 0; enemies[k].tx = 0; enemies[k].ty = 0; enemies[k].buff = 0;
        enemies[k].alive = 1;
        return;
    }
}

static int nearestPlayer(int x, int y); /* forward decl, definisi lengkap di bawah */

static void absorberRetaliate(int ex, int ey) {
    int t = nearestPlayer(ex, ey);
    int baseX = (t >= 0) ? players[t].x + 8 : ex;
    int baseY = (t >= 0) ? players[t].y + 8 : ey + 60;
    for (int k = -2; k <= 2; k++) {
        int avx, avy;
        aimVector(ex, ey, baseX + k * 16, baseY, 3, &avx, &avy);
        spawnEBullet(ex, ey, avx, avy);
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

/* [POLISH] Panel UI: bayangan lembut + isi gelap semi-transparan + garis tepi neon.
   Tidak lagi menumpuk dua panel bertransparansi (penyebab abu-abu keruh sebelumnya). */
static void uiPanel(int x, int y, int w, int h, int r, int g, int b) {
    /* isi: gelap, semi-transparan (mode 0 = 50% belakang + 50% depan) */
    roundedPanel(L_HUD_BASE, x, y, w, h, 10, r / 4, g / 4, b / 4, 1);
    setBlendMode(L_HUD_BASE, 0);
    /* garis tepi neon (opaque) di 4 sisi */
    rect(L_HUD_TOP, x + 10,     y,         w - 20, 1, r, g, b);
    rect(L_HUD_TOP, x + 10,     y + h - 1, w - 20, 1, r, g, b);
    rect(L_HUD_TOP, x,          y + 10,    1, h - 20, r, g, b);
    rect(L_HUD_TOP, x + w - 1,  y + 10,    1, h - 20, r, g, b);
}

static void drawMenu(int frame, int nPlayers) {
    int bob = sinS(frame / 2) / 20;

    /* [FIX] panel judul: dulu glowDisc raksasa (radius 70) menimpa seluruh panel
       jadi kelihatan kotak jingga solid kosong. Sekarang pakai uiPanel yang sama
       dengan panel lain (konsisten) + glow KECIL di belakang teks saja. */
    uiPanel(24, 6, 272, 34, 255, 170, 60);
    glowDisc(45, 20, 12, 255, 170, 60, 8);     /* kilau kecil di pojok kiri panel, bukan menutupi semua */

    /* garis kecepatan dekoratif kiri-kanan, di bawah panel judul (tidak menimpanya) */
    for (int i = 0; i < 3; i++) {
        int len = 14 + ((frame / 2 + i * 7) % 18);
        int yy = 46 + i * 8;
        rect(L_HUD_TOP, 24 - len, yy, len, 1, 80, 160, 255);
        rect(L_HUD_TOP, 296,      yy, len, 1, 80, 160, 255);
    }

    int n = nPlayers < 1 ? 1 : nPlayers;
    for (int p = 0; p < n; p++) {
        int x = SCREEN_W / 2 - 8 + (p * 44 - (n - 1) * 22);
        drawPlayer(x, 128 + bob, frame, players[p].skin);
    }

    /* skor tertinggi tersimpan, ditampilkan di bawah pesawat, di atas panel petunjuk */
    uiPanel(70, 158, 180, 24, 255, 220, 100);

    /* panel petunjuk di bawah */
    uiPanel(20, 190, 280, 42, 80, 160, 255);
}

static void drawGachaScreen(int frame) {
    uiPanel(30, 40, 260, 160, 200, 150, 255);
    if (gachaFlashT > 0) {
        glowDisc(160, 106, 30 + (20 - gachaFlashT), skinTable[gachaResultSkin].glowR,
                 skinTable[gachaResultSkin].glowG, skinTable[gachaResultSkin].glowB, 8);
        drawPlayer(152, 92, frame, gachaResultSkin);
    } else {
        /* kapsul gacha berdenyut */
        int pl = 26 + sinS(frame) / 16;
        glowDisc(160, 106, pl, 200, 150, 255, 8);
        disc(L_HUD_TOP, 160, 106, 14, 255, 240, 255, 150, 90, 220);
        rect(L_HUD_TOP, 146, 105, 28, 2, 255, 255, 255);
    }
}

static void drawSkinSelectScreen(int frame) {
    uiPanel(8, 36, 304, 178, 120, 200, 255);
    for (int i = 0; i < NUM_SKINS; i++) {
        int col = i % 3, row = i / 3;
        int x = 55 + col * 105, y = 70 + row * 62;
        int sel = (i == skinCursor);
        /* kotak slot */
        rect(L_HUD_BASE, x - 32, y - 10, 64, 52, sel ? 60 : 20, sel ? 80 : 26, sel ? 130 : 46);
        if (sel) {
            rect(L_HUD_TOP, x - 32, y - 10, 64, 1, 255, 255, 255);
            rect(L_HUD_TOP, x - 32, y + 41, 64, 1, 255, 255, 255);
            rect(L_HUD_TOP, x - 32, y - 10, 1, 52, 255, 255, 255);
            rect(L_HUD_TOP, x + 31, y - 10, 1, 52, 255, 255, 255);
        }
        /* pesawat digambar di L_PLAYER (bukan di bawah panel) berkat panel sekarang ada di HUD_BASE */
        if (unlockedMask & (1u << i)) drawPlayer(x - 8, y + 4, frame, i);
        else {
            disc(L_HUD_TOP, x, y + 14, 10, 50, 50, 64, 24, 24, 34);
            rect(L_HUD_TOP, x - 3, y + 12, 6, 6, 120, 120, 140);   /* gembok */
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
    case E_TURRET:     *w = 24; *h = 26; break;
    case E_MINELAYER:  *w = 20; *h = 18; break;
    case E_CHARGER:    *w = 16; *h = 18; break;
    case E_MIRROR:     *w = 16; *h = 16; break;
    case E_HEALER:     *w = 18; *h = 18; break;
    case E_ABSORBER:   *w = 22; *h = 22; break;
    case E_SNIPER:     *w = 24; *h = 24; break;
    case E_SWARMER:    *w = 26; *h = 26; break;
    case E_REFLECTOR:  *w = 20; *h = 20; break;
    case E_VOIDCORE:   *w = 24; *h = 24; break;
    case E_TWIN:       *w = 18; *h = 18; break;
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

/* [BARU v2] cari sudut diskrit (0..15, skala sinI/cosI) yang paling dekat
   mengarah dari (fromX,fromY) ke (toX,toY). Dipakai E_SNIPER supaya beamQuad/
   beamHit (yang cuma menerima sudut 16-segmen) bisa "mengunci" ke arah pemain
   alih-alih cuma 4-8 arah tetap seperti MLaser boss. */
static int angleToTarget(int fromX, int fromY, int toX, int toY) {
    int dx = toX - fromX, dy = toY - fromY;
    int bestAng = 0, bestDot = -0x7fffffff;
    for (int a = 0; a < 16; a++) {
        int dot = dx * cosI(a) + dy * sinI(a);   /* proyeksi: makin besar makin searah */
        if (dot > bestDot) { bestDot = dot; bestAng = a; }
    }
    return bestAng;
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
    for (int i = 0; i < MAX_MINES; i++)      mines[i].alive = 0;   /* [BARU] */
    for (int i = 0; i < MAX_SNIPERBEAM; i++) sniperBeams[i].alive = 0;   /* [BARU v2] */
    for (int i = 0; i < MAX_VOIDPULSE; i++)  voidpulses[i].alive = 0;    /* [BARU v2] */
    madReset();
}

/* [FIX] pool[] dulu ukuran 16, dengan 6 tipe musuh baru ditambahkan ke pool
   level>=9, total entri bisa tembus ~18 -> overflow risk. Dinaikkan ke 32. */
static int pickEnemyType(int level) {
    int pool[32], n = 0;
    pool[n++] = E_DRONE;
    if (level >= 3) pool[n++] = E_ZIGZAG;
    if (level >= 4) { pool[n++] = E_SPINNER;  pool[n++] = E_STINGER;    }
    if (level >= 5) { pool[n++] = E_TANK;     pool[n++] = E_ORBITER;    pool[n++] = E_TURRET; }
    if (level >= 6) { pool[n++] = E_SHOOTER;  pool[n++] = E_PHANTOM;    pool[n++] = E_MINELAYER; }
    if (level >= 7) { pool[n++] = E_SPLITTER; pool[n++] = E_JUGGERNAUT; pool[n++] = E_CHARGER; }
    if (level >= 8) { pool[n++] = E_SHIELDED; pool[n++] = E_NOVA;       pool[n++] = E_MIRROR; }
    if (level >= 9) { pool[n++] = E_HEALER;   pool[n++] = E_ABSORBER;  }
    /* [BARU v2] level puncak: 5 musuh baru yang lebih sulit secara mekanik.
       Sengaja HANYA muncul di level 10 (bukan dicampur lebih awal) supaya
       terasa sebagai tantangan akhir yang jelas, bukan tercampur biasa. */
    if (level >= 10) { pool[n++] = E_SNIPER; pool[n++] = E_SWARMER; pool[n++] = E_REFLECTOR;
                        pool[n++] = E_VOIDCORE; }
    /* E_TWIN sengaja TIDAK masuk pool acak biasa - selalu di-spawn berpasangan
       lewat fungsi khusus (lihat spawnTwinPair), bukan lewat pickEnemyType. */
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
    sfxPlay(SND_HURT);
    fxShake(5);
    fxFlash(4);
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
        sfxPlay(SND_LASER);
        return;
    }
}

/* [BARU v2] beam milik E_SNIPER. Lebih pendek durasinya dari MLaser boss
   (SNIPER lebih sering nembak tapi tiap tembakan lebih singkat & presisi). */
#define SNIPERBEAM_FIRE 45
static void spawnSniperBeam(int x, int y, int angle) {
    for (int i = 0; i < MAX_SNIPERBEAM; i++) {
        if (sniperBeams[i].alive) continue;
        sniperBeams[i].x = x; sniperBeams[i].y = y;
        sniperBeams[i].angle = angle;
        sniperBeams[i].timer = SNIPERBEAM_FIRE;
        sniperBeams[i].alive = 1;
        sfxPlay(SND_LASER);
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
            enemies[k].vy = 0; enemies[k].tx = 0; enemies[k].ty = 0; enemies[k].buff = 0;
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
    srand(VSync(-1));   /* [FIX] seed RNG - sebelumnya tidak pernah di-seed sama sekali */
    initStars();

    FntLoad(960, 0);
    fntTop = FntOpen(8,   8, SCREEN_W - 16,  24, 0, 96);
    fntMid = FntOpen(24, 96, SCREEN_W - 48, 100, 0, 320);
    fntBot = FntOpen(28, 192, SCREEN_W - 56,  40, 0, 240);
    fntScore = FntOpen(76, 163, SCREEN_W - 152, 16, 0, 64);

    InitPAD(padbuf[0], 34, padbuf[1], 34);
    StartPAD();
    ChangeClearPAD(0);
    audioInit();
    musicPlay(SONG_MENU);
    fxFadeInNow();

    /* [SAVE] Muat progres dari memory card. Aman-gagal: kalau tidak ada
       kartu atau data korup, semua variabel dibiarkan di nilai default
       (0, hanya skin 0 terbuka) dan game tetap jalan normal. */
    {
        SaveData sd;
        saveLoad(&sd);
        highScore    = sd.highScore;
        gems         = sd.gems;
        unlockedMask = sd.unlockedMask;
        if (sd.lastSkin >= 0 && sd.lastSkin < NUM_SKINS && (unlockedMask & (1u << sd.lastSkin)))
            skinCursor = sd.lastSkin;
    }

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

        {
            int newState = state;
            if (fxUpdate(&newState)) {
                /* layar gelap penuh: aman melakukan reset & ganti state tanpa terlihat */
                if (newState == STATE_PLAY) {
                    clearWorld();
                    nextBossScore = 30;
                    bossOn = 0;
                    madSpawned = 0;
                    frame = 0;
                    musicPlay(SONG_PLAY);
                } else if (newState == STATE_MENU) {
                    for (int q = 0; q < MAX_PLAYERS; q++) { players[q].active = 0; players[q].alive = 0; }
                    musicPlay(SONG_MENU);
                }
                state = newState;
            }
        }
        int fading = (fadeDir != 0);   /* selama fade: abaikan input pindah-state */

        int level = 1 + totalScore() / 10;
        if (level > 10) level = 10;
        /* Tema hanya mengikuti level saat STATE_PLAY. Di GAMEOVER tema DIBEKUKAN (tidak berubah).
           Di menu/gacha/skin tema selalu tema 1. Kembali ke tema 1 terjadi instan di balik layar
           gelap saat fade (lihat blok fxUpdate) sehingga tidak terlihat. */
        if (state == STATE_PLAY)                 themeSetLevel(level, frame < 2);
        else if (state != STATE_GAMEOVER)        themeSetLevel(1, 1);
        themeUpdate();

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
                /* [FIX] dead code sisa debug dihapus (dulu ada `newly` yang selalu 0) */
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

            if (!fading && countActive() > 0 && connected[0] && pressedP(0, btn[0], PAD_CROSS) && konami == 0) {
                sfxPlay(SND_MENU_SEL);
                fxFadeTo(STATE_PLAY);
            }
            if (!fading && connected[0] && pressedP(0, btn[0], PAD_SELECT)) fxFadeTo(STATE_GACHA);
            if (!fading && connected[0] && pressedP(0, btn[0], PAD_SQUARE)) fxFadeTo(STATE_SKINSELECT);
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
                if (!(b & PAD_LEFT)  && pl->x > 0)                     pl->x -= 3;
                if (!(b & PAD_RIGHT) && pl->x < SCREEN_W - PLAYER_W)   pl->x += 3;
                if (!(b & PAD_UP)    && pl->y > HUD_H + 16)            pl->y -= 2;
                if (!(b & PAD_DOWN)  && pl->y < SCREEN_H - 24)         pl->y += 2;

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
                    sfxPlay(SND_SHOOT);
                }
            }

            int madBusy = mad.alive;

            int spawnEvery = 42 - level * 3 - (nPlayers - 1) * 3;
            if (spawnEvery < 10) spawnEvery = 10;

            /* [BARU v2] E_TWIN dipicu terpisah dari pool acak biasa - selalu
               berpasangan, hanya di level 10, interval lebih jarang dari
               spawn musuh biasa (Twin butuh 2 slot & butuh dibunuh berurutan). */
            if (!bossOn && !madBusy && level >= 10 && frame % (spawnEvery * 3) == 0 &&
                countEnemies() < enemyCap - 2)
                spawnTwinPair(level);

            if (!bossOn && !madBusy && frame % spawnEvery == 0 && countEnemies() < enemyCap) {
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (enemies[i].alive) continue;
                    Enemy *e = &enemies[i];
                    e->type = pickEnemyType(level);
                    e->x = 24 + rand() % (SCREEN_W - 56);
                    e->y = HUD_H + 2;
                    e->t = rand() % 32;
                    e->vx = 0;
                    e->vy = 0; e->tx = 0; e->ty = 0; e->buff = 0;   /* [FIX] reset field baru, cegah leftover dari slot lama */
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
                        /* [BARU] 6 musuh baru */
                        case E_TURRET:     e->hp = 5;  e->shield = 0; e->timer = 0;   e->vx = 0; break;
                        case E_MINELAYER:  e->hp = 3;  e->shield = 0; e->timer = 60;  e->vx = (rand() & 1) ? 2 : -2; break;
                        case E_CHARGER:    e->hp = 2;  e->shield = 0; e->timer = 0;   break;
                        case E_MIRROR:     e->hp = 3;  e->shield = 0; e->timer = 0;   e->vx = (rand() & 1) ? 1 : -1; break;
                        case E_HEALER:     e->hp = 4;  e->shield = 0; e->timer = 100; e->vx = (rand() & 1) ? 1 : -1; break;
                        case E_ABSORBER:   e->hp = 10; e->shield = 0; e->timer = 0;   e->vx = 0; break;
                        /* [BARU v2] 5 musuh level-puncak. HP sengaja rendah-sedang
                           (bukan tank) karena kesulitan datang dari POLA SERANGAN,
                           bukan dari lama ditembak - filosofi beda dari Juggernaut/Absorber. */
                        case E_SNIPER:     e->hp = 2; e->shield = 0; e->timer = SNIPER_COOLDOWN; e->vx = 0; break;
                        case E_SWARMER:    e->hp = 6; e->shield = 0; e->timer = SWARMER_SPAWN_GAP; break;
                        case E_REFLECTOR:  e->hp = 2; e->shield = 0; e->timer = 0; break;
                        case E_VOIDCORE:   e->hp = 5; e->shield = 0; e->timer = VOIDCORE_PULSE_GAP; break;
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
            {
                int wantSong = (bossOn || mad.alive) ? SONG_BOSS : SONG_PLAY;
                if (musicCurrent() != wantSong) {
                    musicPlay(wantSong);
                    if (wantSong == SONG_BOSS) sfxPlay(SND_BOSS);
                }
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
                    enemies[i].vy = 0; enemies[i].tx = 0; enemies[i].ty = 0; enemies[i].buff = 0;
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
            /* [FIX] EBullet sekarang bergerak dengan vx,vy (bisa diagonal/aim) */
            for (int i = 0; i < MAX_EBULLETS; i++) {
                if (!ebullets[i].alive) continue;
                ebullets[i].x += ebullets[i].vx;
                ebullets[i].y += ebullets[i].vy;
                if (ebullets[i].y > SCREEN_H || ebullets[i].y < -20 ||
                    ebullets[i].x < -20 || ebullets[i].x > SCREEN_W + 20) ebullets[i].alive = 0;
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
                int ty = (tgt >= 0) ? players[tgt].y : HUD_H + 100;   /* [BARU] target Y, dipakai Turret & Charger */

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
                        else { spawnEBullet(e->x + 9, e->y + 18, 0, 3); e->timer = 60; }
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

                /* [BARU] logika gerak 6 musuh baru */
                case E_TURRET:
                    if (!e->vx) {
                        e->y += 1;
                        if (e->y >= HUD_H + 40) { e->y = HUD_H + 40; e->vx = 1; e->timer = 40; }
                    } else if (tgt >= 0) {
                        if (e->timer > 0) e->timer--;
                        else {
                            int avx, avy;
                            aimVector(e->x + 11, e->y + 10, tx + 8, ty + 8, 3, &avx, &avy);
                            spawnEBullet(e->x + 9, e->y + 18, avx, avy);
                            e->timer = 75 - level * 4;
                            if (e->timer < 25) e->timer = 25;
                        }
                    }
                    break;

                case E_MINELAYER:
                    e->y += 1;
                    e->x += e->vx;
                    if (e->x < 4) { e->x = 4; e->vx = -e->vx; }
                    if (e->x > SCREEN_W - 24) { e->x = SCREEN_W - 24; e->vx = -e->vx; }
                    if (--e->timer <= 0) { spawnMine(e->x + 9, e->y + 18); e->timer = 70; }
                    break;

                case E_CHARGER:
                    if (e->shield == 0) {
                        e->y += baseSpeed + 1;
                        if (e->y > HUD_H + 40) { e->shield = 1; e->timer = 35; e->tx = tx; e->ty = ty; }
                    } else if (e->shield == 1) {
                        if (--e->timer <= 0) {
                            int avx, avy;
                            aimVector(e->x + 8, e->y + 9, e->tx, e->ty, 6, &avx, &avy);
                            e->vx = avx; e->vy = avy;
                            e->shield = 2; e->timer = 40;
                        }
                    } else {
                        e->x += e->vx; e->y += e->vy;
                        if (--e->timer <= 0 || e->y > SCREEN_H || e->x < -20 || e->x > SCREEN_W + 20)
                            e->alive = 0;
                    }
                    break;

                case E_MIRROR:
                    e->y += baseSpeed;
                    e->x += e->vx;
                    if (e->x < 0 || e->x > SCREEN_W - 16) e->vx = -e->vx;
                    break;

                case E_HEALER:
                    e->y += 1;
                    e->x += e->vx;
                    if (e->x < 0 || e->x > SCREEN_W - 18) e->vx = -e->vx;
                    if (--e->timer <= 0) {
                        for (int k = 0; k < MAX_ENEMIES; k++) {
                            if (!enemies[k].alive || &enemies[k] == e) continue;
                            if (enemies[k].type == E_BOSS || enemies[k].type == E_HEALER || enemies[k].type == E_SHARD) continue;
                            int dxh = enemies[k].x - e->x, dyh = enemies[k].y - e->y;
                            if (dxh * dxh + dyh * dyh <= 70 * 70 && enemies[k].buff < 3) enemies[k].buff++;
                        }
                        spawnSparks(e->x + 9, e->y + 9, 10, 120, 255, 160, 1);
                        e->timer = 130;
                    }
                    break;

                case E_ABSORBER:
                    if (e->y < HUD_H + 30) e->y += 1;
                    break;

                /* [BARU v2] E_SNIPER: masuk pelan, lalu diam & mengunci sudut
                   presisi ke pemain sebelum tembak beam singkat. */
                case E_SNIPER:
                    if (e->y < HUD_H + 36) { e->y += 1; break; }
                    if (e->timer > SNIPER_WARN) {
                        e->timer--;
                    } else if (e->timer > 0) {
                        e->t = angleToTarget(e->x + 12, e->y + 12, tx + 8, ty + 8);   /* kunci terus tiap frame warning */
                        e->timer--;
                        if (e->timer == 0) {
                            spawnSniperBeam(e->x + 12, e->y + 12, e->t);
                            e->timer = SNIPER_COOLDOWN;
                        }
                    }
                    break;

                /* [BARU v2] E_SWARMER: diam total, terus memuntahkan E_DRONE
                   selama masih hidup - berhenti otomatis kalau enemies penuh
                   (spawnSwarmDrone diam-diam gagal kalau tidak ada slot). */
                case E_SWARMER:
                    if (e->y < HUD_H + 24) { e->y += 1; break; }
                    if (--e->timer <= 0) {
                        spawnSwarmDrone(e->x + 3, e->y + 26);
                        e->timer = SWARMER_SPAWN_GAP;
                    }
                    break;

                /* [BARU v2] E_REFLECTOR: melayang turun sangat pelan, rotasi
                   perisai otomatis lewat e->t (sudah di-increment di awal loop
                   untuk SEMUA musuh, lihat "e->t++;" di atas). */
                case E_REFLECTOR:
                    if (e->y < SCREEN_H - 60) e->y += 1;
                    e->x += sinS(e->t) / 48;
                    break;

                /* [BARU v2] E_VOIDCORE: diam total, lepas VoidPulse berkala. */
                case E_VOIDCORE:
                    if (e->y < HUD_H + 40) { e->y += 1; break; }
                    if (--e->timer <= 0) {
                        spawnVoidPulse(e->x + 12, e->y + 12);
                        e->timer = VOIDCORE_PULSE_GAP;
                    }
                    break;

                /* [BARU v2] E_TWIN: bergerak turun pelan berpasangan. Kalau
                   pasangan (indeks di e->buff) sudah mati, unit ini "enrage":
                   vx dipakai sbg flag (1 = enrage) dan gerak jadi 2x lebih
                   cepat + oscillasi horizontal lebih liar. */
                case E_TWIN: {
                    int partnerAlive = (e->buff >= 0 && e->buff < MAX_ENEMIES &&
                                         enemies[e->buff].alive && enemies[e->buff].type == E_TWIN);
                    if (!partnerAlive) e->vx = 1;   /* enrage permanen setelah pasangan mati */
                    int spd = e->vx ? TWIN_ENRAGE_SPEED : 1;
                    e->y += spd;
                    e->x += (sinS(e->t * (e->vx ? 3 : 1)) / (e->vx ? 24 : 40));
                    break;
                }

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

                    /* [BARU] lapisan buff dari Healer, berlaku universal, dicek sebelum shield asli.
                       [FIX KRITIS v2] E_TWIN memakai field `buff` untuk INDEKS PASANGAN (0..23),
                       bukan counter perisai - tanpa pengecualian ini, Twin yang pasangannya
                       kebetulan berindeks > 0 akan salah dianggap "terlindungi Healer" dan
                       menyerap peluru tanpa rusak sama sekali. */
                    if (e->type != E_TWIN && e->buff > 0) {
                        e->buff--;
                        spawnSparks(bullets[j].x + 2, bullets[j].y, 4, 140, 255, 180, 0);
                        continue;
                    }

                    /* [BARU v2] E_REFLECTOR: peluru dari sisi TERLINDUNGI (setengah
                       lingkaran searah rotasi e->t, sama seperti logika drawReflector)
                       dipantulkan/diserap tanpa damage. Sisi rentan (belakang) tetap
                       bisa ditembak normal - memaksa pemain memposisikan tembakan. */
                    if (e->type == E_REFLECTOR) {
                        int cx = e->x + 10, cy = e->y + 10;
                        int dx = bullets[j].x - cx, dy = bullets[j].y - cy;
                        int rot = (e->t * 2) & 15;
                        /* sudut datang peluru relatif pusat -> cari sudut diskrit terdekat,
                           lalu cek apakah masuk rentang shielded (sama seperti drawReflector) */
                        int bulletAng = angleToTarget(cx, cy, cx + dx, cy + dy);
                        int rel = (bulletAng - rot) & 15;
                        if (rel < 8) {   /* sisi terlindungi: tolak peluru */
                            spawnSparks(bullets[j].x + 2, bullets[j].y, 3, 150, 200, 255, 0);
                            continue;
                        }
                    }

                    /* [FIX] Charger & Mirror numpang field `shield` untuk hal LAIN (state/flag),
                       bukan shield sungguhan - wajib dikecualikan di sini, kalau tidak mereka akan
                       salah dianggap "menyerap peluru tanpa rusak" oleh cek generic di bawah ini. */
                    if (e->type != E_PHANTOM && e->type != E_CHARGER && e->type != E_MIRROR && e->shield > 0) {
                        e->shield--;
                        sfxPlay(SND_HIT);
                        spawnSparks(bullets[j].x + 2, bullets[j].y, 4, 100, 200, 255, 0);
                        continue;
                    }

                    int maxPow = 0;
                    for (int p = 0; p < MAX_PLAYERS; p++)
                        if (players[p].active && players[p].alive && players[p].powerStack > maxPow)
                            maxPow = players[p].powerStack;
                    e->hp -= dmgBase + maxPow;

                    /* [BARU] Mirror pecah SEKALI saat kena hit pertama, bukan saat mati */
                    if (e->type == E_MIRROR && e->shield == 0) {
                        e->shield = 1;
                        spawnMirrorClone(e->x, e->y);
                    }

                    spawnSparks(bullets[j].x + 2, bullets[j].y, 4, 120, 230, 255, 0);

                    /* [BARU] Absorber charge & retaliate */
                    if (e->type == E_ABSORBER && e->hp > 0) {
                        e->timer++;
                        if (e->timer >= ABSORBER_CHARGE) {
                            e->timer = 0;
                            absorberRetaliate(e->x + 11, e->y + 11);
                        }
                    }

                    if (e->hp <= 0) {
                        spawnExplosion(e->x + w / 2, e->y + h / 2,
                                       e->type == E_BOSS || e->type == E_TANK || e->type == E_JUGGERNAUT);
                        if (e->type == E_SPLITTER) spawnShards(e->x + w / 2, e->y + h / 2);
                        if (e->type == E_NOVA) spawnNovaBurst(e->x + w / 2, e->y + h / 2);
                        e->alive = 0;

                        int pts = 1;
                        if (e->type == E_BOSS) pts = 10;
                        else if (e->type == E_SNIPER || e->type == E_VOIDCORE) pts = 5;   /* [BARU v2] */
                        else if (e->type == E_SWARMER || e->type == E_REFLECTOR || e->type == E_TWIN) pts = 4;   /* [BARU v2] */
                        else if (e->type == E_ABSORBER || e->type == E_JUGGERNAUT) pts = 4;
                        else if (e->type == E_TANK || e->type == E_SHIELDED || e->type == E_NOVA || e->type == E_HEALER) pts = 3;
                        else if (e->type == E_TURRET || e->type == E_MINELAYER || e->type == E_CHARGER) pts = 2;
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
                        overlap(pl->x, pl->y, PLAYER_W, PLAYER_H, e->x, e->y, w, h)) {
                        spawnExplosion(e->x + w / 2, e->y + h / 2, 0);
                        if (e->type != E_BOSS) e->alive = 0;
                        hurtPlayer(p);
                    }
                }
            }

            /* ---------- [MAD] logika boss rahasia ---------- */
            if (mad.alive) {
                if (mad.hit > 0) mad.hit--;
                if (!mad.phase2 && mad.hp * 2 <= mad.maxhp && mad.state != MAD_DYING) { mad.phase2 = 1; fxShake(SHAKE_MAX); fxFlash(8); sfxPlay(SND_BOSS); }

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
                        fxShake(SHAKE_MAX); fxFlash(14);
                        for (int i = 0; i < MAX_MISSILES; i++) missiles[i].alive = 0;
                        for (int i = 0; i < MAX_BOMBS; i++)    mbombs[i].alive = 0;
                        for (int i = 0; i < MAX_MLASERS; i++)  mlasers[i].alive = 0;
                    }
                }

                if (mad.state != MAD_DYING) {
                    for (int p = 0; p < MAX_PLAYERS; p++) {
                        Player *pl = &players[p];
                        if (!pl->active || !pl->alive || pl->invuln > 0 || pl->shieldStack > 0) continue;
                        if (overlap(pl->x, pl->y, PLAYER_W, PLAYER_H, mad.x - 6, mad.y, 62, 42)) hurtPlayer(p);
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
                    if (!overlap(pl->x, pl->y, PLAYER_W, PLAYER_H, missiles[i].x - 3, missiles[i].y - 3, 8, 10)) continue;
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

            /* [BARU] mine dari musuh Minelayer: bisa ditembak, bisa trigger jarak dekat, atau timeout */
            for (int i = 0; i < MAX_MINES; i++) {
                if (!mines[i].alive) continue;
                int triggered = 0;

                for (int j = 0; j < MAX_BULLETS; j++) {
                    if (!bullets[j].alive) continue;
                    if (!overlap(bullets[j].x, bullets[j].y, 4, 14, mines[i].x - 5, mines[i].y - 5, 10, 10)) continue;
                    bullets[j].alive = 0;
                    triggered = 1;
                    break;
                }

                if (!triggered && mines[i].timer < MINE_ARM_AT) {
                    for (int p = 0; p < MAX_PLAYERS; p++) {
                        Player *pl = &players[p];
                        if (!pl->active || !pl->alive) continue;
                        int dx = pl->x + 8 - mines[i].x, dy = pl->y + 8 - mines[i].y;
                        if (dx * dx + dy * dy <= 22 * 22) { triggered = 1; break; }
                    }
                }

                if (--mines[i].timer <= 0) triggered = 1;

                if (triggered) {
                    spawnExplosion(mines[i].x, mines[i].y, 0);
                    for (int p = 0; p < MAX_PLAYERS; p++) {
                        Player *pl = &players[p];
                        if (!pl->active || !pl->alive || pl->invuln > 0 || pl->shieldStack > 0) continue;
                        int dx = pl->x + 8 - mines[i].x, dy = pl->y + 8 - mines[i].y;
                        if (dx * dx + dy * dy <= 30 * 30) hurtPlayer(p);
                    }
                    mines[i].alive = 0;
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

            /* [BARU v2] beam E_SNIPER: aktif penuh sejak ditembak (peringatan
               sudah terjadi lewat visual mata berkedip sebelum ini dipanggil).
               Lebih tipis dari MLaser boss (halfThick 4 vs 7) - presisi tinggi
               tapi bisa dihindari dengan gerak kecil di saat tepat. */
            for (int i = 0; i < MAX_SNIPERBEAM; i++) {
                if (!sniperBeams[i].alive) continue;
                if (--sniperBeams[i].timer <= 0) { sniperBeams[i].alive = 0; continue; }
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    Player *pl = &players[p];
                    if (!pl->active || !pl->alive || pl->invuln > 0 || pl->shieldStack > 0) continue;
                    if (beamHit(sniperBeams[i].x, sniperBeams[i].y, sniperBeams[i].angle, 260, 4, pl->x + 8, pl->y + 8))
                        hurtPlayer(p);
                }
            }

            /* [BARU v2] VoidPulse: cincin membesar terus, melukai HANYA saat
               pemain berada tepat di lingkar cincin (jarak dlm VOIDPULSE_RING_W
               dari radius saat ini) - beda dari area solid, jadi bisa dihindari
               dengan masuk KE DALAM cincin sebelum kena, bukan cuma lari keluar. */
            for (int i = 0; i < MAX_VOIDPULSE; i++) {
                if (!voidpulses[i].alive) continue;
                voidpulses[i].radius += VOIDPULSE_GROWTH;
                if (voidpulses[i].radius > VOIDPULSE_MAX_R) { voidpulses[i].alive = 0; continue; }
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    Player *pl = &players[p];
                    if (!pl->active || !pl->alive || pl->invuln > 0 || pl->shieldStack > 0) continue;
                    int dx = pl->x + 8 - voidpulses[i].x, dy = pl->y + 8 - voidpulses[i].y;
                    int dist2 = dx * dx + dy * dy;
                    int rIn = voidpulses[i].radius - VOIDPULSE_RING_W, rOut = voidpulses[i].radius + VOIDPULSE_RING_W;
                    if (rIn < 0) rIn = 0;
                    if (dist2 >= rIn * rIn && dist2 <= rOut * rOut) hurtPlayer(p);
                }
            }

            for (int i = 0; i < MAX_EBULLETS; i++) {
                if (!ebullets[i].alive) continue;
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    Player *pl = &players[p];
                    if (!pl->active || !pl->alive) continue;
                    if (!overlap(pl->x, pl->y, PLAYER_W, PLAYER_H, ebullets[i].x, ebullets[i].y, 3, 9)) continue;
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
                    if (overlap(pl->x, pl->y, PLAYER_W, PLAYER_H, items[i].x, items[i].y, 10, 10)) {
                        applyItem(pl, items[i].type);
                        sfxPlay(SND_ITEM);
                        spawnSparks(items[i].x + 5, items[i].y + 5, 5, 200, 255, 200, 0);
                        items[i].alive = 0;
                        break;
                    }
                }
            }

            if (countAlive() == 0) {
                awardGems(totalScore());
                if (totalScore() > highScore) highScore = totalScore();
                persistProgress();
                fxFlash(10);
                fxShake(SHAKE_MAX);
                state = STATE_GAMEOVER;
            }
        } else if (state == STATE_GACHA) {
            if (gachaFlashT > 0) gachaFlashT--;
            if (connected[0] && pressedP(0, btn[0], PAD_CROSS) && gachaFlashT == 0) {
                if (doGachaPull()) { gachaFlashT = 20; sfxPlay(SND_POWERUP); }
            }
            if (!fading && connected[0] && pressedP(0, btn[0], PAD_CIRCLE)) fxFadeTo(STATE_MENU);
        } else if (state == STATE_SKINSELECT) {
            if (connected[0]) {
                if (pressedP(0, btn[0], PAD_LEFT))  { skinCursor = (skinCursor + NUM_SKINS - 1) % NUM_SKINS; sfxPlay(SND_MENU_MOVE); }
                if (pressedP(0, btn[0], PAD_RIGHT)) skinCursor = (skinCursor + 1) % NUM_SKINS;
                if (pressedP(0, btn[0], PAD_CROSS) && (unlockedMask & (1u << skinCursor))) {
                    players[0].skin = skinCursor;
                    persistProgress();
                }
                if (!fading && pressedP(0, btn[0], PAD_CIRCLE)) fxFadeTo(STATE_MENU);
            }
        } else {
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (!fading && connected[p] && pressedP(p, btn[p], PAD_START)) {
                    fxFadeTo(STATE_MENU);
                    break;
                }
            }
        }

        /* ---------- Gambar ---------- */

        drawBackground(frame);
        drawStars(frame, (state == STATE_PLAY && (mad.alive || bossOn)) ? 2 : 0);
        drawShootingStar();

        if (state == STATE_MENU) {
            drawMenu(frame, countActive());
        } else if (state == STATE_GACHA) {
            drawGachaScreen(frame);
        } else if (state == STATE_SKINSELECT) {
            drawSkinSelectScreen(frame);
        } else {
            /* [BARU v2] tali energi antar pasangan E_TWIN, digambar SEBELUM
               drawEnemy supaya tali tampak di belakang pesawat, bukan menimpa.
               Cek i < e->buff supaya tiap pasangan cuma digambar sekali. */
            for (int i = 0; i < MAX_ENEMIES; i++) {
                const Enemy *e = &enemies[i];
                if (!e->alive || e->type != E_TWIN) continue;
                if (e->buff <= i || e->buff >= MAX_ENEMIES) continue;
                const Enemy *partner = &enemies[e->buff];
                if (!partner->alive || partner->type != E_TWIN) continue;
                drawTwinLink(e->x + 9, e->y + 9, partner->x + 9, partner->y + 9, frame);
            }

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

            /* [BARU] gambar mine milik Minelayer */
            for (int i = 0; i < MAX_MINES; i++)
                if (mines[i].alive) drawMine(&mines[i], frame);

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

            /* [BARU v2] gambar beam E_SNIPER: tipis & tajam, kedip di akhir durasi
               sebagai tanda mau hilang (bantu pemain baca timing). */
            for (int i = 0; i < MAX_SNIPERBEAM; i++) {
                if (!sniperBeams[i].alive) continue;
                int fading = sniperBeams[i].timer < 10;
                if (!fading || (frame & 1)) {
                    beamQuad(L_FX, sniperBeams[i].x, sniperBeams[i].y, sniperBeams[i].angle, 260, 6, 255, 60, 60, 0);
                    beamQuad(L_FX, sniperBeams[i].x, sniperBeams[i].y, sniperBeams[i].angle, 260, 2, 255, 220, 220, 0);
                }
            }

            /* [BARU v2] gambar cincin VoidPulse */
            for (int i = 0; i < MAX_VOIDPULSE; i++)
                if (voidpulses[i].alive) drawVoidPulse(&voidpulses[i]);

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

        /* ---------- Teks (jendela berposisi tetap) ---------- */
        if (state == STATE_MENU) {
            /* [FIX] Semua baris disusun dulu ke SATU buffer lalu SATU kali FntPrint.
               Sebelumnya banyak FntPrint terpisah ke jendela sama -> posisi kursor
               font tidak terjamin urut, hasilnya baris saling tabrakan/tertimpa. */
            int n = countActive();
            char topbuf[32];
            snprintf(topbuf, sizeof(topbuf), "SPACE SHOOTER");
            FntPrint(fntTop, "%s", topbuf);

            char botbuf[128];
            int len = 0;
            if (n == 0) len += snprintf(botbuf + len, sizeof(botbuf) - len, "1P PRESS START TO JOIN\n");
            else        len += snprintf(botbuf + len, sizeof(botbuf) - len, "%d PLAYER%s  X=MULAI\n", n, n > 1 ? "S" : "");
            for (int p = 1; p < MAX_PLAYERS; p++)
                if (connected[p] && !players[p].active && len < (int)sizeof(botbuf) - 20)
                    len += snprintf(botbuf + len, sizeof(botbuf) - len, "%dP:START ", p + 1);
            len += snprintf(botbuf + len, sizeof(botbuf) - len, "\nGACHA=SEL SKIN=SQR  GEMS %d", gems);
            FntPrint(fntBot, "%s", botbuf);

            if (highScore > 0) FntPrint(fntScore, "HIGH SCORE %d", highScore);
            else                FntPrint(fntScore, "");
            FntPrint(fntMid, "");
        } else if (state == STATE_PLAY) {
            FntPrint(fntTop, "TOTAL %d   GEMS %d", totalScore(), gems);
            FntPrint(fntScore, "");
            FntPrint(fntMid, "");
        } else if (state == STATE_GACHA) {
            FntPrint(fntTop, "GACHA   GEMS %d", gems);
            if (gachaFlashT > 0)
                FntPrint(fntMid, "\n\n\n\n\n%s%s", skinTable[gachaResultSkin].name,
                         gachaResultDup ? " (DUP +15)" : " UNLOCKED!");
            else
                FntPrint(fntMid, "");
            FntPrint(fntBot, "X=PULL (%d)  O=KEMBALI", GACHA_COST);
            FntPrint(fntScore, "");
        } else if (state == STATE_SKINSELECT) {
            FntPrint(fntTop, "PILIH SKIN (P1)");
            FntPrint(fntBot, "%s%s\nX=PILIH O=KEMBALI", skinTable[skinCursor].name,
                     (unlockedMask & (1u << skinCursor)) ? "" : " (TERKUNCI)");
            FntPrint(fntScore, "");
            FntPrint(fntMid, "");
        } else {
            FntPrint(fntTop, "TOTAL %d", totalScore());
            FntPrint(fntMid, "      GAME OVER\n\n      FINAL SCORE %d\n\n      GEMS +%d%s", totalScore(), totalScore() / 2 + 5,
                     totalScore() > highScore ? "\n\n      HIGH SCORE BARU!" : "");
            FntPrint(fntBot, "    PRESS START");
            FntPrint(fntScore, "");
        }
        FntFlush(fntTop);
        FntFlush(fntMid);
        FntFlush(fntBot);
        FntFlush(fntScore);

        audioUpdate();
        setBlendMode(L_GLOW, 1);
        fxDraw();
        fxApplyShake(frame);
        flip();
        for (int p = 0; p < MAX_PLAYERS; p++) prevBtn[p] = btn[p];
        frame++;
    }

    return 0;
}
