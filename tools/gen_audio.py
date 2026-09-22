#!/usr/bin/env python3
"""
gen_audio.py - membuat semua audio game secara prosedural, meng-encode ke
PS1 SPU-ADPCM (VAG), lalu menulis audio_data.h.

Pemakaian:  python3 tools/gen_audio.py  ->  menghasilkan audio_data.h
Opsional:   python3 tools/gen_audio.py --wav   (juga tulis preview .wav ke tools/preview/)

Semua sample dibuat mono 22050 Hz supaya hemat SPU RAM (512 KB total).
"""
import sys, os, math, struct
import numpy as np

RATE = 22050
rng = np.random.default_rng(1234)

# ----------------------------------------------------------------------------
# Encoder SPU-ADPCM (VAG). Filter standar PS1.
# ----------------------------------------------------------------------------
FILTERS = [(0.0, 0.0), (60.0/64, 0.0), (115.0/64, -52.0/64),
           (98.0/64, -55.0/64), (122.0/64, -60.0/64)]

def encode_vag_block(samples, s1, s2, flag):
    """Encode 28 sample -> 16 byte. Coba semua filter & shift, pilih error terkecil."""
    best = None
    for f in range(5):
        c1, c2 = FILTERS[f]
        for shift in range(13):
            p1, p2 = s1, s2
            err = 0.0
            nibbles = []
            for x in samples:
                pred = p1 * c1 + p2 * c2
                diff = x - pred
                # nibble 4-bit signed, skala 2^-shift dari 12 bit atas
                scale = float(1 << shift)
                q = int(round(diff / scale * 1.0 / 16.0))  # diff dalam satuan 16-bit -> nibble
                if q > 7: q = 7
                if q < -8: q = -8
                rec = q * 16.0 * scale
                out = pred + rec
                if out > 32767: out = 32767
                if out < -32768: out = -32768
                err += (x - out) ** 2
                nibbles.append(q & 0xF)
                p2, p1 = p1, out
            if best is None or err < best[0]:
                best = (err, f, shift, nibbles, p1, p2)
    _, f, shift, nibbles, p1, p2 = best
    blk = bytearray(16)
    blk[0] = ((f & 0xF) << 4) | ((12 - shift) & 0xF)
    blk[1] = flag
    for i in range(14):
        lo = nibbles[2 * i]
        hi = nibbles[2 * i + 1]
        blk[2 + i] = lo | (hi << 4)
    return bytes(blk), p1, p2

def encode_vag(pcm16, loop=False):
    """pcm16: np.int16. Return bytes VAG (tanpa header). Blok pertama = flag 4 (loop start) bila loop."""
    x = pcm16.astype(np.float64)
    pad = (-len(x)) % 28
    if pad:
        x = np.concatenate([x, np.zeros(pad)])
    nblocks = len(x) // 28
    out = bytearray()
    s1 = s2 = 0.0
    # blok kosong awal (praktik umum, hindari klik)
    out += bytes(16)
    for b in range(nblocks):
        if loop:
            if b == 0: flag = 6      # loop start (2) + loop? -> pakai 6 = start+... lihat catatan
            elif b == nblocks - 1: flag = 3   # loop end + repeat
            else: flag = 2
        else:
            flag = 1 if b == nblocks - 1 else 0
        # penyederhanaan flag: 0 normal, 1 = end (tanpa repeat), 3 = end+repeat, 4 = loop start
        if loop:
            flag = 4 if b == 0 else (3 if b == nblocks - 1 else 0)
        blk, s1, s2 = encode_vag_block(x[b*28:(b+1)*28], s1, s2, flag)
        out += blk
    return bytes(out)

# ----------------------------------------------------------------------------
# Sintesis dasar
# ----------------------------------------------------------------------------
def t_arr(dur):
    return np.arange(int(RATE * dur)) / RATE

def square(freq, dur, duty=0.5):
    t = t_arr(dur)
    return np.where((t * freq) % 1.0 < duty, 1.0, -1.0)

def saw(freq, dur):
    t = t_arr(dur)
    return 2.0 * ((t * freq) % 1.0) - 1.0

def tri_w(freq, dur):
    t = t_arr(dur)
    return 2.0 * np.abs(2.0 * ((t * freq) % 1.0) - 1.0) - 1.0

def sine(freq, dur):
    return np.sin(2 * np.pi * freq * t_arr(dur))

def noise(dur):
    return rng.uniform(-1.0, 1.0, int(RATE * dur))

def env_exp(n, decay):
    t = np.arange(n) / RATE
    return np.exp(-t * decay)

def lowpass(x, a):
    y = np.zeros_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc += a * (v - acc)
        y[i] = acc
    return y

def norm(x, peak=0.9):
    m = np.max(np.abs(x))
    return x if m == 0 else x * (peak / m)

def to_pcm(x):
    return np.clip(x * 32767, -32768, 32767).astype(np.int16)

# ----------------------------------------------------------------------------
# SFX
# ----------------------------------------------------------------------------
def sfx_shoot():
    dur = 0.11
    t = t_arr(dur)
    f = 1400 - 900 * (t / dur)
    ph = np.cumsum(2 * np.pi * f / RATE)
    x = np.sign(np.sin(ph)) * 0.6 + 0.4 * np.sin(ph * 2)
    x *= env_exp(len(x), 22)
    return norm(x, 0.7)

def sfx_explosion_small():
    dur = 0.45
    n = noise(dur)
    n = lowpass(n, 0.35)
    x = n * env_exp(len(n), 7)
    t = t_arr(dur)
    thump = np.sin(2 * np.pi * (90 - 60 * t / dur) * t) * env_exp(len(t), 12)
    return norm(x * 0.8 + thump * 0.6, 0.9)

def sfx_explosion_big():
    dur = 0.9
    n = lowpass(noise(dur), 0.18)
    x = n * env_exp(len(n), 3.5)
    t = t_arr(dur)
    thump = np.sin(2 * np.pi * (70 - 45 * t / dur) * t) * env_exp(len(t), 5)
    crack = noise(0.05)
    x[:len(crack)] += crack * 0.9
    return norm(x * 0.8 + thump * 0.8, 0.95)

def sfx_item():
    notes = [880, 1108, 1318, 1760]
    parts = []
    for f in notes:
        w = square(f, 0.06, 0.25) * env_exp(int(RATE * 0.06), 10)
        parts.append(w)
    return norm(np.concatenate(parts), 0.7)

def sfx_hit():
    dur = 0.28
    t = t_arr(dur)
    f = 260 - 180 * (t / dur)
    ph = np.cumsum(2 * np.pi * f / RATE)
    x = np.sign(np.sin(ph)) * env_exp(len(t), 9)
    x += noise(dur) * env_exp(len(t), 25) * 0.6
    return norm(x, 0.85)

def sfx_hurt():
    dur = 0.5
    t = t_arr(dur)
    f = 500 - 380 * (t / dur)
    ph = np.cumsum(2 * np.pi * f / RATE)
    x = saw(1, dur) * 0  # placeholder agar shape sama
    x = (2.0 * ((ph / (2 * np.pi)) % 1.0) - 1.0) * env_exp(len(t), 4)
    x += noise(dur) * env_exp(len(t), 12) * 0.4
    return norm(x, 0.9)

def sfx_warning():
    parts = []
    for _ in range(3):
        parts.append(square(880, 0.09, 0.5) * 0.6)
        parts.append(np.zeros(int(RATE * 0.05)))
    return norm(np.concatenate(parts), 0.6)

def sfx_laser_fire():
    dur = 0.6
    t = t_arr(dur)
    f = 120 + 40 * np.sin(2 * np.pi * 12 * t)
    ph = np.cumsum(2 * np.pi * f / RATE)
    x = (2.0 * ((ph / (2 * np.pi)) % 1.0) - 1.0)
    x += 0.5 * np.sign(np.sin(ph * 3))
    x *= np.minimum(1.0, t * 40) * np.exp(-t * 2.2)
    return norm(x, 0.85)

def sfx_powerup():
    dur = 0.35
    t = t_arr(dur)
    f = 300 + 900 * (t / dur) ** 1.5
    ph = np.cumsum(2 * np.pi * f / RATE)
    x = np.sin(ph) + 0.4 * np.sin(ph * 2)
    x *= np.minimum(1.0, t * 60) * np.exp(-t * 3)
    return norm(x, 0.75)

def sfx_menu_move():
    x = square(660, 0.05, 0.5) * env_exp(int(RATE * 0.05), 30)
    return norm(x, 0.5)

def sfx_menu_select():
    a = square(660, 0.06, 0.25)
    b = square(990, 0.10, 0.25)
    x = np.concatenate([a * env_exp(len(a), 14), b * env_exp(len(b), 14)])
    return norm(x, 0.65)

def sfx_boss_roar():
    dur = 1.0
    t = t_arr(dur)
    f = 55 + 25 * np.sin(2 * np.pi * 6 * t)
    ph = np.cumsum(2 * np.pi * f / RATE)
    x = (2.0 * ((ph / (2 * np.pi)) % 1.0) - 1.0) * 0.7 + np.sign(np.sin(ph * 2)) * 0.3
    x = lowpass(x, 0.25)
    x *= np.minimum(1.0, t * 12) * np.exp(-t * 1.6)
    return norm(x, 0.9)

# ----------------------------------------------------------------------------
# Instrumen musik (sample looping pendek, pitch diubah via SPU pitch register)
# Semua dibuat pada nada dasar C4 = 261.63 Hz supaya rumus pitch mudah.
# ----------------------------------------------------------------------------
BASE_FREQ = 261.63   # C4

def _loop_cycles(wave_fn, cycles):
    """Bangun sample loop dengan jumlah siklus bulat supaya loop mulus."""
    dur = cycles / BASE_FREQ
    return wave_fn(BASE_FREQ, dur)

def _soften(x, cutoff=0.55):
    """Low-pass ringan: melembutkan tepi tajam gelombang kotak/gergaji supaya tidak
    terdengar seperti buzzer 'net-net-net' saat dimainkan staccato cepat.
    cutoff mendekati 1.0 = nyaris tidak difilter; makin kecil = makin lembut/redup."""
    return lowpass(x, cutoff)

def inst_lead():      # square duty 28%, dilembutkan cukup kuat (~600Hz cutoff) - tetap jadi lead
    #                       yang jelas tapi tepinya landai, tidak lagi berbunyi buzzer tajam
    return norm(_soften(_loop_cycles(lambda f, d: square(f, d, 0.28), 16), 0.16), 0.8)

def inst_pad():       # square duty 50% + sedikit saw - pad synthwave, PALING lembut (latar, bukan fokus)
    a = _loop_cycles(lambda f, d: square(f, d, 0.5), 16)
    b = _loop_cycles(saw, 16)
    return norm(_soften(a * 0.5 + b * 0.5, 0.10), 0.75)

def inst_bass():      # segitiga tebal - bass
    return norm(_loop_cycles(tri_w, 16), 0.85)

def inst_saw():       # saw untuk arpeggio, dilembutkan sedang (tetap ada "gigi" tapi tidak menusuk)
    return norm(_soften(_loop_cycles(saw, 16), 0.22), 0.8)

def drum_kick():
    dur = 0.22
    t = t_arr(dur)
    f = 150 * np.exp(-t * 22) + 42
    ph = np.cumsum(2 * np.pi * f / RATE)
    x = np.sin(ph) * np.exp(-t * 14)
    return norm(x, 0.95)

def drum_snare():
    dur = 0.2
    n = noise(dur) * env_exp(int(RATE * dur), 20)
    t = t_arr(dur)
    body = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 28)
    return norm(n * 0.9 + body * 0.5, 0.85)

def drum_hat():
    dur = 0.06
    n = noise(dur)
    hp = n - lowpass(n, 0.3)   # high-pass kasar
    return norm(hp * env_exp(len(hp), 60), 0.6)

# daftar sample: (nama, array, loop?)
SAMPLES = [
    ("SND_SHOOT",       sfx_shoot(),           False),
    ("SND_EXPLODE_S",   sfx_explosion_small(), False),
    ("SND_EXPLODE_B",   sfx_explosion_big(),   False),
    ("SND_ITEM",        sfx_item(),            False),
    ("SND_HIT",         sfx_hit(),             False),
    ("SND_HURT",        sfx_hurt(),            False),
    ("SND_WARN",        sfx_warning(),         False),
    ("SND_LASER",       sfx_laser_fire(),      False),
    ("SND_POWERUP",     sfx_powerup(),         False),
    ("SND_MENU_MOVE",   sfx_menu_move(),       False),
    ("SND_MENU_SEL",    sfx_menu_select(),     False),
    ("SND_BOSS",        sfx_boss_roar(),       False),
    ("INS_LEAD",        inst_lead(),           True),
    ("INS_PAD",         inst_pad(),            True),
    ("INS_BASS",        inst_bass(),           True),
    ("INS_SAW",         inst_saw(),            True),
    ("DRM_KICK",        drum_kick(),           False),
    ("DRM_SNARE",       drum_snare(),          False),
    ("DRM_HAT",         drum_hat(),            False),
]

def write_wav(path, pcm):
    import wave
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(pcm.tobytes())

def main():
    want_wav = "--wav" in sys.argv
    out_dir = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(out_dir)
    if want_wav:
        os.makedirs(os.path.join(out_dir, "preview"), exist_ok=True)

    blobs = []
    total = 0
    for name, arr, loop in SAMPLES:
        pcm = to_pcm(arr)
        if want_wav:
            write_wav(os.path.join(out_dir, "preview", name + ".wav"), pcm)
        vag = encode_vag(pcm, loop)
        blobs.append((name, vag, loop))
        total += len(vag)
        print("%-14s %6d byte  %s" % (name, len(vag), "LOOP" if loop else ""))
    print("TOTAL %d byte (%.1f KB) dari 500 KB SPU RAM efektif" % (total, total / 1024.0))
    if total > 480 * 1024:
        print("PERINGATAN: melebihi SPU RAM!", file=sys.stderr)

    # audio_ids.h: hanya konstanta ID (aman di-include di main.c)
    ip = os.path.join(root, "audio_ids.h")
    with open(ip, "w") as f:
        f.write("/* DIHASILKAN OTOMATIS oleh tools/gen_audio.py - hanya ID, tanpa data */\n")
        f.write("#ifndef AUDIO_IDS_H\n#define AUDIO_IDS_H\n")
        f.write("#define AUDIO_RATE %d\n#define AUDIO_BASE_FREQ_X100 %d\n" % (RATE, int(BASE_FREQ * 100)))
        for i, (name, vag, loop) in enumerate(blobs):
            f.write("#define %s %d\n" % (name, i))
        f.write("#define AUDIO_NUM_SAMPLES %d\n#endif\n" % len(blobs))
    print("Ditulis:", ip)

    # audio_data.h: array data sample (hanya di-include oleh audio.c)
    hp = os.path.join(root, "audio_data.h")
    with open(hp, "w") as f:
        f.write("/* DIHASILKAN OTOMATIS oleh tools/gen_audio.py - jangan diedit manual */\n")
        f.write("#ifndef AUDIO_DATA_H\n#define AUDIO_DATA_H\n#include <stdint.h>\n#include \"audio_ids.h\"\n\n")
        for name, vag, loop in blobs:
            f.write("static const uint8_t %s_DATA[%d] = {\n" % (name, len(vag)))
            for i in range(0, len(vag), 16):
                f.write("  " + ",".join("0x%02X" % b for b in vag[i:i+16]) + ",\n")
            f.write("};\n\n")
        f.write("typedef struct { const uint8_t *data; int size; int loop; } AudioSample;\n")
        f.write("static const AudioSample audioSamples[AUDIO_NUM_SAMPLES] = {\n")
        for name, vag, loop in blobs:
            f.write("  { %s_DATA, %d, %d },\n" % (name, len(vag), 1 if loop else 0))
        f.write("};\n\n#endif\n")
    print("Ditulis:", hp)

if __name__ == "__main__":
    main()
