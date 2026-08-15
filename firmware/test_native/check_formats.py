#!/usr/bin/env python3
"""Génère des WAV de tous les formats et vérifie que le loader les convertit
correctement en int16 (compare out.wav à la conversion attendue calculée ici)."""
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave

BASE = os.path.join(os.path.dirname(__file__), "test_wavs")
os.makedirs(BASE, exist_ok=True)
SR = 44100
AMP = 0.5


def sine(n, amp=AMP, freq=440.0):
    return [amp * math.sin(2 * math.pi * freq * i / SR) for i in range(n)]


def write_wave(path, fmt_code, bits, ch, n, pack_sample):
    """En-tête WAV manuel (fmt code + bits quelconques) + data."""
    bytes_per = bits // 8
    sig = sine(n)
    data = b"".join(pack_sample(x) for x in sig for _ in range(ch))
    fmt = struct.pack("<HHIIHH", fmt_code, ch, SR, SR * ch * bytes_per, ch * bytes_per, bits)
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(data))
    with open(path, "wb") as f:
        f.write(hdr + data)


def u8(x):
    return struct.pack("<B", int(128 + 127 * x))


def s16(x):
    return struct.pack("<h", int(round(32767 * x)))


def s24(x):
    v = int(round(8388607 * x)) & 0xFFFFFF
    return struct.pack("<BBB", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF)


def s32(x):
    return struct.pack("<i", int(round(2147483647 * x)))


def f32(x):
    return struct.pack("<f", x)


N = SR  # 1 seconde
write_wave(os.path.join(BASE, "t8.wav"), 1, 8, 1, N, u8)
write_wave(os.path.join(BASE, "t16.wav"), 1, 16, 2, N, s16)
write_wave(os.path.join(BASE, "t24.wav"), 1, 24, 2, N, s24)
write_wave(os.path.join(BASE, "t32.wav"), 1, 32, 1, N, s32)
write_wave(os.path.join(BASE, "tf32.wav"), 3, 32, 2, N, f32)


def expected_int16(path):
    """Rejoue la conversion du loader en pur Python pour comparaison."""
    raw = open(path, "rb").read()
    fmt_code, ch, sr, _, _, bits = struct.unpack_from("<HHIIHH", raw, 20)
    data = raw[44:]
    bps = bits // 8
    out = bytearray()
    for i in range(0, len(data) - bps + 1, bps):
        b = data[i:i + bps]
        if fmt_code == 3:
            f = struct.unpack("<f", b)[0]
            f = max(-1.0, min(1.0, f))
            out += struct.pack("<h", int(round(f * 32767)))
        elif bits == 8:
            out += struct.pack("<h", (b[0] - 128) << 8)
        elif bits == 16:
            out += b
        elif bits == 24:
            v = b[0] | b[1] << 8 | b[2] << 16
            if v & 0x800000:
                v -= 0x1000000
            out += struct.pack("<h", v >> 8)
        elif bits == 32:
            v = struct.unpack("<i", b)[0]
            out += struct.pack("<h", v >> 16)
    return bytes(out)


TEST = "/tmp/amen_test"
ok = True
for name in ["t8.wav", "t16.wav", "t24.wav", "t32.wav", "tf32.wav"]:
    src = os.path.join(BASE, name)
    with tempfile.TemporaryDirectory() as td:
        r = subprocess.run([TEST, src], capture_output=True, text=True, cwd=td)
        if r.returncode != 0:
            print(f"{name}: ECHEC loader ({r.stdout.strip()})")
            ok = False
            continue
        outwav = os.path.join(td, "out.wav")
        got = open(outwav, "rb").read()[44:]
        want = expected_int16(src)
        if got == want:
            print(f"{name}: OK — conversion int16 identique à l'attendu "
                  f"({len(got)//2} échantillons)")
        else:
            print(f"{name}: CONVERSION FAUSSE ({len(got)} vs {len(want)} octets)")
            ok = False
sys.exit(0 if ok else 1)
