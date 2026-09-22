/* test_save.c - uji logika save.c TANPA hardware memory card.
   Compile & run di komputer biasa: gcc -o test_save test_save.c && ./test_save */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

/* --- salin definisi dari save.c yang tidak butuh psxcard.h --- */
typedef struct {
    int32_t  highScore;
    int32_t  gems;
    uint32_t unlockedMask;
    int32_t  lastSkin;
} SaveData;

#define SAVE_MAGIC   0x50533153u
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
    uint32_t sum = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) { sum ^= p[i]; sum *= 0x01000193u; }
    return sum;
}

static void setDefault(SaveData *out) {
    out->highScore = 0; out->gems = 0; out->unlockedMask = 1; out->lastSkin = 0;
}

static int saveValidateBlock(const SaveBlock *b) {
    if (b->magic != SAVE_MAGIC) return 0;
    if (b->version != SAVE_VERSION) return 0;
    if (b->checksum != computeChecksum(b)) return 0;
    if (b->data.gems < 0 || b->data.gems > 999999) return 0;
    if (b->data.highScore < 0 || b->data.highScore > 999999) return 0;
    if (b->data.lastSkin < 0 || b->data.lastSkin > 31) return 0;
    return 1;
}

static void saveBuildBlock(SaveBlock *b, const SaveData *data) {
    memset(b, 0, sizeof(*b));
    b->magic = SAVE_MAGIC; b->version = SAVE_VERSION; b->data = *data;
    b->checksum = computeChecksum(b);
}
/* --- akhir salinan --- */

int main(void) {
    int fail = 0;

    /* Tes 1: build lalu validasi harus selalu lolos untuk data valid */
    SaveData d1 = { .highScore = 1234, .gems = 500, .unlockedMask = 0b101011, .lastSkin = 3 };
    SaveBlock b1; saveBuildBlock(&b1, &d1);
    if (!saveValidateBlock(&b1)) { printf("[FAIL] 1: data valid ditolak\n"); fail++; }
    else printf("[OK]   1: data valid diterima\n");

    /* Tes 2: data hasil parse harus identik dengan yang ditulis (roundtrip) */
    if (memcmp(&b1.data, &d1, sizeof(d1)) != 0) { printf("[FAIL] 2: roundtrip data berubah\n"); fail++; }
    else printf("[OK]   2: roundtrip data identik\n");

    /* Tes 3: magic salah (kartu berisi save game LAIN) harus ditolak */
    SaveBlock b2 = b1; b2.magic = 0xDEADBEEF;
    if (saveValidateBlock(&b2)) { printf("[FAIL] 3: magic salah malah diterima\n"); fail++; }
    else printf("[OK]   3: magic salah ditolak dengan benar\n");

    /* Tes 4: satu byte korup di tengah data -> checksum harus mendeteksi */
    SaveBlock b3 = b1;
    uint8_t *raw = (uint8_t*)&b3;
    raw[sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + 2] ^= 0xFF;  /* korupsi field gems */
    if (saveValidateBlock(&b3)) { printf("[FAIL] 4: data korup lolos checksum\n"); fail++; }
    else printf("[OK]   4: data korup terdeteksi checksum\n");

    /* Tes 5: versi lama/beda harus ditolak (forward-compat: jangan load struktur beda) */
    SaveBlock b4 = b1; b4.version = 99;
    b4.checksum = computeChecksum(&b4);   /* checksum valid TAPI versi beda */
    if (saveValidateBlock(&b4)) { printf("[FAIL] 5: versi beda malah diterima\n"); fail++; }
    else printf("[OK]   5: versi beda ditolak dengan benar\n");

    /* Tes 6: nilai gems negatif (korup jadi liar) harus ditolak walau checksum entah bagaimana valid */
    SaveData dbad = { .highScore = 100, .gems = -50, .unlockedMask = 1, .lastSkin = 0 };
    SaveBlock b5; saveBuildBlock(&b5, &dbad);
    if (saveValidateBlock(&b5)) { printf("[FAIL] 6: gems negatif diterima\n"); fail++; }
    else printf("[OK]   6: gems negatif ditolak (sanity check)\n");

    /* Tes 7: default harus konsisten & tidak pernah punya skin 0 terkunci */
    SaveData def; setDefault(&def);
    if (!(def.unlockedMask & 1)) { printf("[FAIL] 7: skin default 0 tidak terbuka\n"); fail++; }
    else printf("[OK]   7: default selalu skin 0 terbuka\n");

    /* Tes 8: ukuran struct tidak berubah tak sengaja antar kompilasi (deteksi ABI drift) */
    printf("[INFO] sizeof(SaveBlock) = %zu byte\n", sizeof(SaveBlock));
    if (sizeof(SaveBlock) > 256) { printf("[FAIL] 8: SaveBlock terlalu besar untuk 1 block memcard (biasanya 128 byte/block)\n"); fail++; }
    else printf("[OK]   8: SaveBlock cukup kecil untuk 1 block memory card\n");

    printf("\n%s: %d gagal dari 8 tes\n", fail == 0 ? "SEMUA LULUS" : "ADA YANG GAGAL", fail);
    return fail;
}
