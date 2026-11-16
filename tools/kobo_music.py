#!/usr/bin/env python3
"""
kobo_music.py - build KOBOMUS.BIN, the music the cartridge plays by itself (no Sega CD).

  python3 tools/kobo_music.py [Track02.wav Track03.wav Track04.wav] [--out subcpu/ROMs/KOBOMUS.BIN]
                              [--rate 16276] [--volume 128]

INPUT: the same WAVs the music disc is made from (subcpu/Track02.wav = title / menu / level,
       Track03.wav = credits, Track04.wav = between levels).
WHY THESE STEPS:
  - The 32X plays sound through its PWM, fed one sample at a time by the slave SH2 (sh2_main.c
    secondary()).  That loop already mixes the sound effects at 16276 Hz, mono; the music is one more
    voice in the same loop, read straight from the ROM, so it is stored at that rate: one byte per
    output sample, no rate conversion on the console.
  - 8-bit signed samples: at 16276 Hz the three tracks (115 s) take 1.87 MB, and the 4 MB ROM has
    1.875 MB free below the graphics (make_cart.py).  16-bit would not fit.
  - Each track is made mono, low-pass filtered and resampled (scipy if it is there, numpy otherwise),
    brought to full 8-bit scale by itself (the WAVs peak at 30-45 % - that would waste a bit and a
    half), dithered, and gets 8 ms fades at both ends so a repeat never clicks.
  - The directory's volume says how loud the mixer plays it (128 = as stored, peak 126 of the PWM's
    +-706).  "auto" (the default) plays each track as loud as its WAV is against full scale - what
    the CD gives - but never above a peak of 450, so that the effects (peaks 60-120 each) still fit
    on top.

KOBOMUS.BIN (big-endian):
  0x00  'KMUS'  u16 count  u16 rate (Hz)
  0x08  count x 12-byte entries:  u32 offset (from the start of this file)  u32 length (bytes)
                                  u16 volume (128 = as stored)  u16 repeat (1 = plays again from the start)
  0x100 the samples, 8-bit signed, track after track (each starts on a 4-byte boundary)
Track numbers in the game: ROM music 1, 2, 3 = CD tracks 2, 3, 4.
"""
import os, struct, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def read_wav(path):
    w = wave.open(path)
    assert w.getsampwidth() == 2, "%s: 16-bit WAV expected" % path
    d = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    d = d.reshape(-1, w.getnchannels()).mean(axis=1)
    return d, w.getframerate()


def resample(x, src, dst):
    """Band-limited resampling: scipy's polyphase filter if it is installed, else a windowed sinc."""
    try:
        from fractions import Fraction
        from scipy.signal import resample_poly
        f = Fraction(dst, src).limit_denominator(20000)
        return resample_poly(x, f.numerator, f.denominator, window=("kaiser", 9.0))
    except ImportError:
        pass
    ratio = dst / src
    cut = 0.5 * min(1.0, ratio) * 0.94                   # of the source rate
    half = 48
    n = int(len(x) * ratio)
    out = np.empty(n)
    k = np.arange(-half, half + 1)
    win = np.kaiser(2 * half + 1, 9.0)
    pad = np.concatenate([np.zeros(half + 1), x, np.zeros(half + 2)])
    for start in range(0, n, 65536):
        t = np.arange(start, min(n, start + 65536)) / ratio
        i = np.floor(t).astype(int)
        frac = t - i
        taps = 2 * cut * np.sinc(2 * cut * (k[None, :] - frac[:, None])) * win[None, :]
        idx = i[:, None] + k[None, :] + half + 1
        out[start:start + len(t)] = (pad[idx] * taps).sum(axis=1)
    return out


PWM_HALF = 706          # the mixer's range around its middle (sh2_main.c PWM_CYC 1414)
PEAK_MAX = 450          # a track's loudest sample in PWM steps ("auto" volume)


def convert(path, rate, seed):
    x, src = read_wav(path)
    y = resample(x, src, rate)
    peak = np.abs(y).max()
    auto = min(PEAK_MAX, peak / 32768.0 * PWM_HALF) / 126.0 * 128.0
    y = y * (126.0 / peak) if peak > 0 else y
    fade = min(len(y) // 2, int(rate * 0.008))
    ramp = np.linspace(0.0, 1.0, fade, endpoint=False)
    y[:fade] *= ramp
    y[len(y) - fade:] *= ramp[::-1]
    rng = np.random.default_rng(seed)                    # the same bytes on every run
    y = y + (rng.random(len(y)) - rng.random(len(y))) * 0.5   # triangular dither, +-0.5 LSB
    return np.clip(np.round(y), -127, 127).astype(np.int8).tobytes(), len(x) / src, int(round(auto))


def main():
    args = sys.argv[1:]
    out = os.path.join(ROOT, "subcpu", "ROMs", "KOBOMUS.BIN")
    rate, volume, wavs = 16276, None, []
    i = 0
    while i < len(args):
        if args[i] == "--out": out = args[i + 1]; i += 2
        elif args[i] == "--rate": rate = int(args[i + 1]); i += 2
        elif args[i] == "--volume": volume = None if args[i + 1] == "auto" else int(args[i + 1]); i += 2
        else: wavs.append(args[i]); i += 1
    if not wavs:
        wavs = [os.path.join(ROOT, "subcpu", "Track%02d.wav" % n) for n in (2, 3, 4)]
    head = bytearray(b"KMUS" + struct.pack(">HH", len(wavs), rate)).ljust(0x100, b"\0")
    body = bytearray()
    for n, path in enumerate(wavs):
        data, seconds, auto = convert(path, rate, 0x4B4F + n)
        vol = auto if volume is None else volume
        while len(body) & 3: body.append(0)
        head[8 + n * 12:20 + n * 12] = struct.pack(">IIHH", 0x100 + len(body), len(data), vol, 1)
        print("  music %d: %-14s %5.1f s -> %7d bytes at 0x%06X, volume %d (peak %d of %d)"
              % (n + 1, os.path.basename(path), seconds, len(data), 0x100 + len(body), vol, 126 * vol // 128, PWM_HALF))
        body += data
    open(out, "wb").write(head + body)
    print("  %s: %d bytes (%d Hz, 8-bit mono)" % (out, len(head) + len(body), rate))


if __name__ == "__main__":
    main()
