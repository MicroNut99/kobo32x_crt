#!/usr/bin/env python3
"""
kobo_sfx.py - build KOBOSFX.BIN, the sound-effect pack for the Sega CD PCM chip (RF5C164).

INPUT: Kobo's own sound effects, rendered from the .agw files by renderer/agwrender.c
       (16-bit mono 32 kHz WAVs named SOUND_<ID>__<name>.wav, see KOBO_SFX_WAV.zip).
WHY THESE STEPS:
  - The PCM chip has 64 KB of sample RAM and plays 8-bit SIGN-MAGNITUDE samples (bit 7 set =
    positive, low 7 bits = size).  Byte 0xFF is the chip's loop marker and must not appear
    in sound data.
  - Kobo's 39 effects are 1.3 MB at 16-bit 32 kHz, so we keep the ones the game uses, and
    give each a sample rate and a maximum length (long fade-outs trimmed, with a short
    fade so nothing clicks).  If the total is still too big, every rate is scaled down.
  - Every sample starts on a 256-byte page (the chip's start register is the page number)
    and ends with 16 bytes of silence + 0xFF; the loop address points at that silence, so a
    finished effect loops silence forever (the channel falls quiet by itself).

KOBOSFX.BIN (big-endian):
  0x000  'KSFX'  u16 count  u16 image_bytes
  0x008  count x 8-byte entries:  u8 start_page, u8 volume, u16 fd (step: 0x800 = 32552 Hz),
                                  u16 loop_address, u16 length
  0x100  PCM image (image_bytes, max 65536) - copied as-is into the chip's wave RAM.
"""
import os, sys, struct, wave
import numpy as np

# Our effect numbers (1..N) - MUST match sh2_main.c SFX_* and main.c.  (Kobo ID, rate Hz,
# max seconds, volume 0-255)
EFFECTS = [
    ("SFX_SHOT",    "SOUND_SHOT",         16000, 0.05, 0xA0),   # 1 player gun
    ("SFX_BEAM",    "SOUND_BEAM",         16000, 0.10, 0x90),   # 2 enemy / gun bullet
    ("SFX_METAL",   "SOUND_METALLIC1",    16000, 0.10, 0x90),   # 3 bolt hits a pipe
    ("SFX_NODE",    "SOUND_EXPLO_NODE1",   8000, 0.70, 0xE0),   # 4 node destroyed
    ("SFX_CORE",    "SOUND_EXPLO_NODE2",   8000, 1.00, 0xFF),   # 5 core destroyed
    ("SFX_ENEMY1",  "SOUND_EXPLO_ENEMY1",  8000, 0.60, 0xD0),   # 6 enemy explodes
    ("SFX_ENEMY2",  "SOUND_EXPLO_ENEMY2",  8000, 0.60, 0xD0),   # 7 enemy explodes (variant)
    ("SFX_RING",    "SOUND_EXPLO_RING1",   8000, 0.50, 0xC0),   # 8 ring destroyed
    ("SFX_ROCK",    "SOUND_EXPLO_ROCK",    8000, 0.60, 0xD0),   # 9 rock destroyed
    ("SFX_DETO",    "SOUND_BOMB_DETO",     8000, 0.60, 0xD0),   # 10 bomb bursts
    ("SFX_PLAYER",  "SOUND_EXPLO_PLAYER",  8000, 1.30, 0xFF),   # 11 player's ship explodes
    ("SFX_LAUNCH",  "SOUND_ENEMYM",        8000, 0.60, 0xC0),   # 12 mother ship launches
    ("SFX_GAMEOVER","SOUND_GAMEOVER",      8000, 1.50, 0xFF),   # 13 game over
    ("SFX_TICK",    "SOUND_TICK",         16000, 0.10, 0xC0),   # 14 menu move
    ("SFX_PLAY",    "SOUND_PLAY",          8000, 0.80, 0xE0),   # 15 menu: start game
]
PAGE = 256
TAIL = 16
RAM = 65536


def load(wavdir, kobo_id):
    f = [x for x in os.listdir(wavdir) if x.startswith(kobo_id + "__")]
    if not f:
        sys.exit("missing %s in %s" % (kobo_id, wavdir))
    w = wave.open(os.path.join(wavdir, f[0]), "rb")
    rate = w.getframerate()
    a = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
    return a, rate


def resample(a, src, dst):
    n = max(1, int(round(len(a) * dst / src)))
    x = np.linspace(0, len(a) - 1, n)
    # box-average first when shrinking a lot (simple anti-alias)
    k = max(1, int(src // dst))
    if k > 1:
        a = np.convolve(a, np.ones(k) / k, mode="same")
    return np.interp(x, np.arange(len(a)), a)


def to_sm8(a):
    m = np.clip(np.round(np.abs(a) * 127), 0, 126).astype(np.uint8)   # 127 would give 0xFF
    return np.where(a >= 0, 0x80 | m, m).astype(np.uint8)


def build(wavdir, scale):
    image = bytearray()
    table = []
    for name, kid, rate, secs, vol in EFFECTS:
        a, src = load(wavdir, kid)
        r = int(rate * scale)
        a = a[: int(secs * src)]
        fade = int(0.03 * src)                                    # 30 ms fade-out, no click
        if len(a) > fade:
            a[-fade:] *= np.linspace(1, 0, fade)
        peak = np.max(np.abs(a)) or 1.0
        s = to_sm8(resample(a / peak * 0.95, src, r))              # normalise, then 8-bit
        while len(image) % PAGE:
            image.append(0x80)
        start = len(image)
        image += bytes(s)
        loop = len(image)
        image += bytes([0x80] * TAIL) + b"\xff"
        fd = int(round(r * 2048 / 32552))
        table.append((start // PAGE, vol, fd, loop, len(s), name, r))
    return image, table


def main():
    wavdir = sys.argv[1] if len(sys.argv) > 1 else "wav"
    out = sys.argv[2] if len(sys.argv) > 2 else "KOBOSFX.BIN"
    scale = 1.0
    while True:
        image, table = build(wavdir, scale)
        if len(image) <= RAM:
            break
        scale *= 0.95
    hdr = bytearray(b"KSFX") + struct.pack(">HH", len(table), len(image))
    for st, vol, fd, loop, n, name, r in table:
        hdr += struct.pack(">BBHHH", st, vol, fd, loop, n)
    hdr += bytes(256 - len(hdr))
    open(out, "wb").write(bytes(hdr) + bytes(image))
    print("KOBOSFX.BIN: %d effects, PCM image %d bytes of 65536 (rate scale %.2f)" % (len(table), len(image), scale))
    for i, (st, vol, fd, loop, n, name, r) in enumerate(table, 1):
        print("  %2d %-13s page %3d  %5d Hz  %5d bytes  %.2f s" % (i, name, st, r, n, n / r))


if __name__ == "__main__":
    main()
