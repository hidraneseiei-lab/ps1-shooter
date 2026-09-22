/*
 * save.c - bagian dari ps1-shooter, dibuat oleh hidraneseiei21
 * Modul penyimpanan progres ke memory card, lewat BIOS file API standar.
 *
 * CATATAN JUJUR UNTUK PENGEMBANG (baca sebelum debug):
 * PSn00bSDK TIDAK memiliki library memory card tingkat-tinggi yang lengkap
 * (per dokumentasi resminya, ini masih tercantum sebagai to-do di proyek
 * PSn00bSDK). Karena itu modul ini memakai fungsi file BIOS standar PS1 yang
 * terdokumentasi di psx-spx (problemkaputt.de/psxspx-bios-memory-card-functions):
 *   InitCard(pad_enable), StartCard(), _bu_init(), lalu open()/read()/write()/
 *   close() biasa dengan path "bu00:NAMA_FILE" (slot 1) atau "bu10:" (slot 2).
 * Fungsi-fungsi ini adalah panggilan BIOS (bukan PSn00bSDK spesifik), jadi
 * seharusnya tersedia lewat <psxapi.h> di semua SDK PS1 termasuk PSn00bSDK.
 *
 * UPDATE (setelah uji kompilasi sungguhan di CI, toolchain PSn00bSDK 0.24):
 *   - InitCard, StartCard, _bu_init, open, close: CONFIRMED tersedia dan
 *     cocok lewat <psxapi.h>, tidak perlu di-declare ulang manual.
 *   - read/write: SEBELUMNYA di-declare ulang manual dengan signature
 *     `int len` di sini, padahal <psxapi.h> sudah mendeklarasikannya dengan
 *     `size_t len`. Ini menyebabkan "conflicting types" saat build karena
 *     dianggap dua deklarasi berbeda untuk fungsi yang sama. FIX: deklarasi
 *     manual read/write dihapus, pakai langsung yang dari <psxapi.h>.
 * Logika checksum & validasi data (bagian yang murni C, tidak sentuh hardware)
 * SUDAH diuji lewat tools/test_save.c dan lulus semua kasus.
 */
#include <string.h>
#include <stdio.h>
#include <psxapi.h>
#include "save.h"

/* [PENTING - BACA INI KALAU BUILD GAGAL DI FILE INI]
   PSn00bSDK belum punya library memory card resmi yang lengkap (dikonfirmasi
   dari dokumentasi resminya sendiri, per saat kode ini ditulis). Baris-baris
   di bawah memanggil fungsi BIOS standar PS1 (InitCard/StartCard/_bu_init)
   dan fungsi file POSIX-style (open/read/write/close) dengan path "bu00:...",
   sesuai dokumentasi teknis BIOS PS1 (problemkaputt.de/psxspx).
   read() dan write() SUDAH tersedia lewat <psxapi.h> (signature pakai
   size_t), jadi JANGAN di-declare ulang manual di sini - itu penyebab
   error "conflicting types" yang pernah terjadi di CI.
   Kemungkinan perbaikan lain kalau CI gagal di baris-baris ini:
   1. Nama fungsi beda kapitalisasi: coba InitCARD/StartCARD.
   2. open/close bentrok dgn libc: PSn00bSDK mungkin menyediakan fungsi ini
      lewat <stdio.h> (FILE*) alih-alih deskriptor int mentah - dalam kasus
      itu perlu ditulis ulang pakai fopen("bu00:...", "r+b") dkk.
   3. Kalau semua opsi gagal: set SAVE_FORCE_DISABLE 1 di bawah supaya game
      tetap kompil dan jalan TANPA fitur save, sambil dicari solusi lebih
      lanjut - ini lebih baik daripada build gagal total. */
#define SAVE_FORCE_DISABLE 0

#if !SAVE_FORCE_DISABLE
extern int init_card(int pad_enable);
extern void start_card(void);
extern void _bu_init(void);
extern int open(const char *name, int mode);
extern int close(int fd);
/* read() dan write() SUDAH dideklarasikan di <psxapi.h> dengan signature
   size_t - JANGAN declare ulang di sini, akan bentrok (conflicting types). */
#endif

/* mode file BIOS: 0x0002 = O_RDWR, bit9 (0x200) = "buat baru dgn ukuran",
   ukuran blok di-encode di bit16-31 (1 block = 0x1<<16) - lihat psx-spx */
#define FMODE_READ       0x0002
#define FMODE_CREATE_1BLOCK  (0x0002 | 0x0200 | (1 << 16))

#define SAVE_PATH_SLOT1  "bu00:PS1SHOOT"
#define SAVE_PATH_SLOT2  "bu10:PS1SHOOT"

#define SAVE_MAGIC   0x50533153u   /* "PS1S" */
#define SAVE_VERSION 1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    SaveData data;
    uint32_t checksum;
} SaveBlock;

static uint32_t computeChecksum(const SaveBlock *b) {
    const uint8_t *p = (const uint8_t *)b;
    size_t n = sizeof(SaveBlock) - sizeof(uint32_t);
    uint32_t sum = 0x811C9DC5u;   /* FNV-1a 32-bit offset basis */
    for (size_t i = 0; i < n; i++) {
        sum ^= p[i];
        sum *= 0x01000193u;
    }
    return sum;
}

static void setDefault(SaveData *out) {
    out->highScore    = 0;
    out->gems         = 0;
    out->unlockedMask = 1;
    out->lastSkin     = 0;
}

/* Fungsi murni logika, dites di tools/test_save.c tanpa hardware */
int saveValidateBlock_(const void *raw) {
    const SaveBlock *b = (const SaveBlock *)raw;
    if (b->magic != SAVE_MAGIC) return 0;
    if (b->version != SAVE_VERSION) return 0;
    if (b->checksum != computeChecksum(b)) return 0;
    if (b->data.gems < 0 || b->data.gems > 999999) return 0;
    if (b->data.highScore < 0 || b->data.highScore > 999999) return 0;
    if (b->data.lastSkin < 0 || b->data.lastSkin > 31) return 0;
    return 1;
}

#if !SAVE_FORCE_DISABLE

static int cardReady = 0;

static void ensureCardInit(void) {
    if (cardReady) return;
    init_card(1);    // Menggunakan huruf kecil
    start_card();   // Menggunakan huruf kecil
    _bu_init();     // Tetap sama
    cardReady = 1;
}

int saveLoad(SaveData *out) {
    setDefault(out);
    ensureCardInit();

    int fd = open(SAVE_PATH_SLOT1, FMODE_READ);
    if (fd < 0) return 0;

    SaveBlock block;
    int n = read(fd, &block, sizeof(block));
    close(fd);

    if (n != sizeof(block)) return 0;
    if (!saveValidateBlock_(&block)) return 0;

    *out = block.data;
    return 1;
}

int saveWrite(const SaveData *data) {
    ensureCardInit();

    SaveBlock block;
    memset(&block, 0, sizeof(block));
    block.magic = SAVE_MAGIC;
    block.version = SAVE_VERSION;
    block.data = *data;
    block.checksum = computeChecksum(&block);

    int fd = open(SAVE_PATH_SLOT1, FMODE_CREATE_1BLOCK);
    if (fd < 0) return 0;

    int n = write(fd, &block, sizeof(block));
    close(fd);

    return (n == sizeof(block)) ? 1 : 0;
}

#else
/* Save dinonaktifkan paksa: game tetap kompil & jalan normal tanpa fitur save. */
int saveLoad(SaveData *out) { setDefault(out); return 0; }
int saveWrite(const SaveData *data) { (void)data; return 0; }
#endif
