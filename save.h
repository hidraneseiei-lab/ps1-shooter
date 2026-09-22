#ifndef SAVE_H
#define SAVE_H
#include <stdint.h>

/* [MODUL SAVE] Menyimpan progres ke memory card PS1 (slot 1).
   Didesain aman-gagal: kalau memory card tidak terpasang, rusak, atau format
   berbeda (kartu lama / dari game lain di slot sama), game tetap berjalan
   normal dengan data default (skor 0, gems 0, hanya skin awal terbuka).
   TIDAK PERNAH menyebabkan crash atau hang walau memory card bermasalah. */

typedef struct {
    int32_t  highScore;
    int32_t  gems;
    uint32_t unlockedMask;   /* bitmask skin yang sudah terbuka */
    int32_t  lastSkin;       /* skin terakhir dipakai P1 */
} SaveData;

/* Panggil sekali saat boot. Mengisi *out dengan data tersimpan bila ada,
   atau nilai default (0, 0, mask=1, skin=0) bila tidak ada/gagal.
   Return: 1 bila berhasil memuat data tersimpan, 0 bila pakai default. */
int saveLoad(SaveData *out);

/* Simpan data ke memory card. Boleh dipanggil kapan saja (mis. tiap kali
   gems/highscore berubah, atau di titik aman seperti transisi state).
   Return: 1 bila berhasil, 0 bila gagal (memory card penuh/tidak ada/dll).
   Kegagalan TIDAK melempar error yang menghentikan game. */
int saveWrite(const SaveData *data);

#endif
