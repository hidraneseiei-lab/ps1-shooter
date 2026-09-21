/*
 * audio.c - driver SPU langsung (memory-mapped register) + sequencer musik.
 *
 * Kenapa register langsung? Alamat SPU PS1 itu tetap di hardware, jadi kode
 * ini tidak tergantung nama fungsi / versi header PSn00bSDK.
 *
 * Alokasi voice (24 voice tersedia):
 *   0..7   : SFX (round-robin)
 *   8      : lead
 *   9      : pad
 *   10     : bass
 *   11     : arpeggio (saw)
 *   12     : kick
 *   13     : snare
 *   14     : hat
 */
#include <stdint.h>
#include "audio.h"
#include "audio_data.h"

/* ---------- register SPU ---------- */
#define SPU_BASE      0x1F801C00u
#define REG16(a)      (*(volatile uint16_t *)(a))

#define SPU_VOICE(n)          (SPU_BASE + (n) * 0x10)
#define SPU_VOL_L(n)          REG16(SPU_VOICE(n) + 0x0)
#define SPU_VOL_R(n)          REG16(SPU_VOICE(n) + 0x2)
#define SPU_PITCH(n)          REG16(SPU_VOICE(n) + 0x4)
#define SPU_STARTADDR(n)      REG16(SPU_VOICE(n) + 0x6)
#define SPU_ADSR1(n)          REG16(SPU_VOICE(n) + 0x8)
#define SPU_ADSR2(n)          REG16(SPU_VOICE(n) + 0xA)

#define SPU_MAIN_VOL_L        REG16(0x1F801D80)
#define SPU_MAIN_VOL_R        REG16(0x1F801D82)
#define SPU_KEY_ON_LO         REG16(0x1F801D88)
#define SPU_KEY_ON_HI         REG16(0x1F801D8A)
#define SPU_KEY_OFF_LO        REG16(0x1F801D8C)
#define SPU_KEY_OFF_HI        REG16(0x1F801D8E)
#define SPU_RAM_ADDR          REG16(0x1F801DA6)
#define SPU_RAM_DATA          REG16(0x1F801DA8)
#define SPU_CTRL              REG16(0x1F801DAA)
#define SPU_XFER_CTRL         REG16(0x1F801DAC)
#define SPU_STAT              REG16(0x1F801DAE)
#define SPU_CD_VOL_L          REG16(0x1F801DB0)
#define SPU_CD_VOL_R          REG16(0x1F801DB2)

/* ---------- konstanta ---------- */
#define SPU_RAM_START   0x1010          /* alamat mulai upload, dalam byte (setelah area capture) */
#define NUM_SFX_VOICES  8
#define V_LEAD  8
#define V_PAD   9
#define V_BASS  10
#define V_ARP   11
#define V_KICK  12
#define V_SNARE 13
#define V_HAT   14

/* alamat sample di SPU RAM, dalam satuan 8 byte (yang dipakai register start-address) */
static uint16_t sampleAddr[AUDIO_NUM_SAMPLES];
static uint16_t sampleRate[AUDIO_NUM_SAMPLES];

static int sfxNext = 0;

/* pitch register: 0x1000 = 44100 Hz. Sample kami 22050 Hz -> 0x800 */
#define PITCH_FOR_RATE(hz)  ((uint32_t)(hz) * 0x1000u / 44100u)

static void spuWait(void) {
    /* tunggu transfer selesai (bit 10 pada SPUSTAT = busy) */
    int guard = 0x100000;
    while ((SPU_STAT & 0x400) && --guard) { }
}

static void spuUpload(uint32_t byteAddr, const uint8_t *data, int size) {
    /* urutan sesuai hardware: SPU tetap enable (bit15), transfer mode = stop dulu */
    SPU_CTRL = 0x8000;
    spuWait();
    SPU_XFER_CTRL = 0x0004;             /* auto-increment, wajib sebelum tulis data */
    SPU_RAM_ADDR  = (uint16_t)(byteAddr >> 3);
    SPU_CTRL = 0x8010;                  /* enable + transfer mode = manual write (bit 4) */
    spuWait();
    for (int i = 0; i < size; i += 2) {
        uint16_t w = (uint16_t)data[i] | ((uint16_t)(i + 1 < size ? data[i + 1] : 0) << 8);
        SPU_RAM_DATA = w;
    }
    SPU_CTRL = 0x8000;                  /* stop transfer, kembali normal */
    spuWait();
}

static void keyOn(int voice)  {
    if (voice < 16) SPU_KEY_ON_LO  = (uint16_t)(1u << voice);
    else            SPU_KEY_ON_HI  = (uint16_t)(1u << (voice - 16));
}
static void keyOff(int voice) {
    if (voice < 16) SPU_KEY_OFF_LO = (uint16_t)(1u << voice);
    else            SPU_KEY_OFF_HI = (uint16_t)(1u << (voice - 16));
}

/* ADSR: attack cepat, sustain penuh, release sedang */
#define ADSR1_FAST   0x00FF
#define ADSR2_MUSIC  0x1FC0

static void voiceSetup(int v, int sampleId, uint32_t pitch, int volL, int volR) {
    SPU_VOL_L(v)      = (uint16_t)(volL << 1);   /* skala 0x3FFF max, volume "fixed" */
    SPU_VOL_R(v)      = (uint16_t)(volR << 1);
    SPU_PITCH(v)      = (uint16_t)pitch;
    SPU_STARTADDR(v)  = sampleAddr[sampleId];
    SPU_ADSR1(v)      = ADSR1_FAST;
    SPU_ADSR2(v)      = ADSR2_MUSIC;
}

void audioInit(void) {
    /* aktifkan SPU + unmute + volume utama */
    SPU_CTRL = 0x8000;
    SPU_MAIN_VOL_L = 0x3FFF;
    SPU_MAIN_VOL_R = 0x3FFF;
    SPU_CD_VOL_L = 0;
    SPU_CD_VOL_R = 0;

    /* matikan semua voice */
    SPU_KEY_OFF_LO = 0xFFFF;
    SPU_KEY_OFF_HI = 0x00FF;

    /* upload semua sample berurutan */
    uint32_t addr = SPU_RAM_START;
    for (int i = 0; i < AUDIO_NUM_SAMPLES; i++) {
        addr = (addr + 7u) & ~7u;
        spuUpload(addr, audioSamples[i].data, audioSamples[i].size);
        sampleAddr[i] = (uint16_t)(addr >> 3);
        sampleRate[i] = AUDIO_RATE;
        addr += (uint32_t)audioSamples[i].size;
    }

    SPU_CTRL = 0xC000;        /* SPU enable + unmute, mode normal */
}

void sfxPlay(int sampleId) {
    if (sampleId < 0 || sampleId >= AUDIO_NUM_SAMPLES) return;
    int v = sfxNext;
    sfxNext = (sfxNext + 1) % NUM_SFX_VOICES;
    keyOff(v);
    voiceSetup(v, sampleId, PITCH_FOR_RATE(sampleRate[sampleId]), 0x2400, 0x2400);
    keyOn(v);
}

/* ---------------------------------------------------------------------------
 * Sequencer musik
 * ------------------------------------------------------------------------- */

/* Nada dalam semitone relatif C4 (0 = C4). -99 = rest, -98 = tahan */
#define R  -99
#define H  -98

/* pitch untuk semitone tertentu relatif C4 pada sample 22050 Hz:
   pitch = base * 2^(n/12). Tabel 2^(n/12) x 1024 untuk n = 0..11 */
static const uint16_t semiTab[12] = {
    1024, 1085, 1149, 1218, 1290, 1367, 1449, 1535, 1627, 1723, 1826, 1935
};

static uint32_t notePitch(int semi) {
    int oct = 0;
    while (semi < 0)  { semi += 12; oct--; }
    while (semi >= 12){ semi -= 12; oct++; }
    uint32_t p = (uint32_t)semiTab[semi];
    uint32_t base = PITCH_FOR_RATE(AUDIO_RATE);            /* 0x800 */
    uint32_t v = base * p / 1024u;
    if (oct > 0) v <<= oct; else if (oct < 0) v >>= -oct;
    if (v > 0x3FFF) v = 0x3FFF;
    return v;
}

typedef struct {
    int bpm;            /* tempo */
    int stepsPerBeat;   /* resolusi (4 = 16th note) */
    int length;         /* jumlah step pola */
    const int8_t *lead;
    const int8_t *pad;
    const int8_t *bass;
    const int8_t *arp;
    const uint8_t *kick;    /* 1 = bunyi */
    const uint8_t *snare;
    const uint8_t *hat;
    int leadVol, padVol, bassVol, arpVol;
} Song;

/* ===== LAGU 0: MENU (synthwave santai, 90 bpm, 32 step = 2 bar) ===== */
static const int8_t m_lead[32] = {
    R,R,R,R, 12,R,H,R, 10,R,H,R, 7,R,R,R,   R,R,R,R, 9,R,H,R, 7,R,H,R, 4,R,R,R
};
static const int8_t m_pad[32] = {
    -5,H,H,H, H,H,H,H, H,H,H,H, H,H,H,H,   -8,H,H,H, H,H,H,H, H,H,H,H, H,H,H,H
};
static const int8_t m_bass[32] = {
   -24,R,R,R, -24,R,-24,R, -17,R,R,R, -24,R,R,R,   -20,R,R,R, -20,R,-20,R, -13,R,R,R, -20,R,R,R
};
static const int8_t m_arp[32] = {
    0,4,7,12, 0,4,7,12, -5,-1,2,7, -5,-1,2,7,   -3,0,4,9, -3,0,4,9, -8,-4,-1,4, -8,-4,-1,4
};
static const uint8_t m_kick[32]  = {1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0,  1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0};
static const uint8_t m_snare[32] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0,  0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0};
static const uint8_t m_hat[32]   = {0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0,  0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,1};

/* ===== LAGU 1: GAMEPLAY (chiptune energik, 150 bpm, 64 step = 4 bar) ===== */
static const int8_t g_lead[64] = {
    12,R,12,R, 15,R,12,R, 10,R,12,R, 7,R,R,R,   12,R,12,R, 15,R,17,R, 19,R,17,R, 15,R,R,R,
    14,R,14,R, 17,R,14,R, 12,R,14,R, 10,R,R,R,   14,R,14,R, 17,R,19,R, 21,R,19,R, 17,R,15,12
};
static const int8_t g_pad[64] = {
    R,R,R,R, R,R,R,R, R,R,R,R, R,R,R,R,   R,R,R,R, R,R,R,R, R,R,R,R, R,R,R,R,
    R,R,R,R, R,R,R,R, R,R,R,R, R,R,R,R,   R,R,R,R, R,R,R,R, R,R,R,R, R,R,R,R
};
static const int8_t g_bass[64] = {
   -12,R,-12,-12, R,-12,-12,R, -8,R,-8,-8, R,-8,-8,R,   -5,R,-5,-5, R,-5,-5,R, -7,R,-7,-7, R,-7,-7,R,
   -10,R,-10,-10, R,-10,-10,R, -5,R,-5,-5, R,-5,-5,R,   -3,R,-3,-3, R,-3,-3,R, -5,R,-5,-5, R,-5,-5,R
};
static const int8_t g_arp[64] = {
    0,3,7,12, 15,12,7,3, -4,0,3,8, 12,8,3,0,   -7,-3,0,5, 9,5,0,-3, -5,-1,2,7, 10,7,2,-1,
    -2,2,5,10, 14,10,5,2, -7,-3,0,5, 9,5,0,-3,  -5,-1,2,7, 10,7,2,-1, -7,-3,0,5, 9,5,0,-3
};
static const uint8_t g_kick[64]  = {1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,1,
                                    1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,1,0};
static const uint8_t g_snare[64] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0,
                                    0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,1,1};
static const uint8_t g_hat[64]   = {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,1,
                                    1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,0, 1,0,1,0, 1,1,1,1};

/* ===== LAGU 2: BOSS (gelap & agresif, 170 bpm, 32 step = 2 bar) ===== */
static const int8_t b_lead[32] = {
    0,R,0,R, 1,R,0,R, -2,R,0,R, 3,R,R,R,   0,R,0,R, 1,R,3,R, 6,R,3,R, 1,R,R,R
};
static const int8_t b_pad[32] = {
    R,R,R,R, R,R,R,R, R,R,R,R, R,R,R,R,   R,R,R,R, R,R,R,R, R,R,R,R, R,R,R,R
};
static const int8_t b_bass[32] = {
   -24,-24,R,-24, -24,R,-24,-24, -23,-23,R,-23, -23,R,-23,-23,   -21,-21,R,-21, -21,R,-21,-21, -24,-24,R,-24, -24,R,-24,-24
};
static const int8_t b_arp[32] = {
    0,1,7,1, 0,1,7,1, -2,1,5,1, -2,1,5,1,   0,3,6,3, 0,3,6,3, -1,2,6,2, -1,2,6,2
};
static const uint8_t b_kick[32]  = {1,0,0,1, 0,0,1,0, 1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,1,0, 1,0,1,0, 1,0,1,1};
static const uint8_t b_snare[32] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1,  0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,1,1};
static const uint8_t b_hat[32]   = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,  1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};

static const Song songs[3] = {
    /* MENU */ { 90,  4, 32, m_lead, m_pad, m_bass, m_arp, m_kick, m_snare, m_hat, 0x1400, 0x0C00, 0x2000, 0x0F00 },
    /* PLAY */ { 150, 4, 64, g_lead, g_pad, g_bass, g_arp, g_kick, g_snare, g_hat, 0x1A00, 0x0000, 0x2200, 0x1100 },
    /* BOSS */ { 170, 4, 32, b_lead, b_pad, b_bass, b_arp, b_kick, b_snare, b_hat, 0x1E00, 0x0000, 0x2400, 0x1600 },
};

static int curSong  = SONG_NONE;
static int seqStep  = 0;
static int seqTick  = 0;         /* hitungan frame di dalam step */
static int seqTickLen = 6;       /* frame per step (dihitung dari bpm) */
static int seqAcc   = 0;         /* akumulator pecahan */

/* frame (60 Hz) per step = 3600 / (bpm * stepsPerBeat) ; pakai akumulator agar tempo akurat */
static void seqComputeTiming(const Song *s) {
    seqTickLen = 3600 / (s->bpm * s->stepsPerBeat);
    seqAcc = 0;
}

static void playNote(int voice, int sampleId, int semi, int vol) {
    if (semi == R) return;
    if (semi == H) return;                 /* tahan: biarkan bunyi berlanjut */
    keyOff(voice);
    voiceSetup(voice, sampleId, notePitch(semi), vol, vol);
    keyOn(voice);
}

static void playDrum(int voice, int sampleId, int vol) {
    keyOff(voice);
    voiceSetup(voice, sampleId, PITCH_FOR_RATE(AUDIO_RATE), vol, vol);
    keyOn(voice);
}

static void seqStepPlay(const Song *s) {
    int i = seqStep;
    playNote(V_LEAD, INS_LEAD, s->lead[i], s->leadVol);
    if (s->padVol)  playNote(V_PAD,  INS_PAD,  s->pad[i],  s->padVol);
    playNote(V_BASS, INS_BASS, s->bass[i], s->bassVol);
    playNote(V_ARP,  INS_SAW,  s->arp[i],  s->arpVol);
    if (s->kick[i])  playDrum(V_KICK,  DRM_KICK,  0x2600);
    if (s->snare[i]) playDrum(V_SNARE, DRM_SNARE, 0x2000);
    if (s->hat[i])   playDrum(V_HAT,   DRM_HAT,   0x0E00);
}

void musicStop(void) {
    for (int v = V_LEAD; v <= V_HAT; v++) keyOff(v);
    curSong = SONG_NONE;
}

int musicCurrent(void) { return curSong; }

void musicPlay(int song) {
    if (song == curSong) return;
    musicStop();
    if (song < 0 || song > 2) return;
    curSong = song;
    seqStep = 0;
    seqTick = 0;
    seqComputeTiming(&songs[song]);
    seqStepPlay(&songs[song]);
}

void audioUpdate(void) {
    if (curSong < 0) return;
    const Song *s = &songs[curSong];
    if (++seqTick >= seqTickLen) {
        seqTick = 0;
        /* koreksi drift: sisa pecahan dari pembagian bulat */
        seqAcc += 3600 - seqTickLen * (s->bpm * s->stepsPerBeat);
        if (seqAcc >= s->bpm * s->stepsPerBeat) {
            seqAcc -= s->bpm * s->stepsPerBeat;
            seqTick = -1;                      /* tambah 1 frame di step ini */
        }
        seqStep = (seqStep + 1) % s->length;
        seqStepPlay(s);
    }
}
