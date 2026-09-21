# Fase 3 - audio & efek visual

## Audio (audio.c, audio_ids.h, audio_data.h, tools/gen_audio.py)
- 12 SFX + 3 lagu (menu / gameplay / boss), dibuat prosedural, 72 KB di SPU RAM.
- Musik otomatis ganti ke lagu boss saat boss / Mad Cruiser muncul.

## Efek visual (main.c)
- Screen shake: ledakan besar, pemain kena, boss fase 2 / sekarat.
- Flash putih (additive): pemain kena, boss. Ada cooldown -> maks ~2 kilat/detik (aman fotosensitif).
- Fade hitam antar layar (menu <-> main <-> gacha <-> skin <-> game over). Reset dunia dilakukan saat layar gelap penuh.
- Tema latar 5 warna (ganti tiap 2 level, crossfade ~0,85 dtk). Dibekukan saat game over.
- Awan parallax 2 lapis + bintang 3 bentuk (89% lebih murah dari sebelumnya).
- Layer baru L_OVERLAY (OT_LEN 9 -> 10) + cadangan 2 KB di buffer primitif khusus overlay.

## Risiko yang belum bisa dibuktikan tanpa toolchain PS1
1. `draw.ofs[0]/[1]` dipakai untuk shake. Kalau SDK menamai lain -> error kompilasi (perbaikan 1 baris).
2. Register SPU di audio.c belum diuji di hardware/emulator.
3. Blend mode subtractive (mode 2) untuk fade hitam belum dilihat di layar.
