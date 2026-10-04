#!/usr/bin/env python3
"""
fix_raw_height.py  -  make 320x200 IMAGE.RAW files out of 32X .RAW pictures of another height

Your loader shows 200 rows (320 x 200 x 2 bytes = 128000 bytes): that is all the 32X frame buffer
can hold in 32K colour mode (it fits 204 rows at most).  A 320x224 picture (143360 bytes) has 24 rows
too many, so its bottom is cut off.  This script squeezes such pictures to 200 rows (smooth resize,
not a crop), forces bit 15 (opaque) on every pixel, and writes them to a new folder.

    python3 fix_raw_height.py IMAGE.RAW IMAGE1.RAW IMAGE2.RAW            -> fixed/IMAGE.RAW ...
    python3 fix_raw_height.py *.RAW --outdir C:\\somewhere

Input format: 320 pixels wide, 16-bit words 0BBBBBGGGGGRRRRR (bit 15 is ignored), big-endian.
Needs:  pip install pillow
"""
import argparse, os, struct, sys
from PIL import Image

W, H_OUT = 320, 200


def load(path):
    d = open(path, "rb").read()
    n = len(d) // 2
    if n % W:
        sys.exit("%s: %d bytes is not a whole number of 320-pixel rows" % (path, len(d)))
    h = n // W
    words = struct.unpack(">%dH" % n, d[: n * 2])
    img = Image.new("RGB", (W, h))
    px = img.load()
    for i, v in enumerate(words):
        r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
        px[i % W, i // W] = ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))
    return img


def q5(v):
    return max(0, min(31, (v * 31 + 127) // 255))


def save(img, path):
    px = img.load()
    out = []
    for y in range(img.size[1]):
        for x in range(W):
            r, g, b = px[x, y]
            out.append(0x8000 | (q5(b) << 10) | (q5(g) << 5) | q5(r))
    open(path, "wb").write(struct.pack(">%dH" % len(out), *out))


def main():
    ap = argparse.ArgumentParser(description="Squeeze 32X .RAW pictures to 320x200")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--outdir", default="fixed")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    for f in a.files:
        img = load(f)
        h = img.size[1]
        if h != H_OUT:
            img = img.resize((W, H_OUT), Image.LANCZOS)
        dst = os.path.join(a.outdir, os.path.basename(f))
        save(img, dst)
        print("%s: %d rows -> %d rows  (%d bytes)  %s" % (f, h, H_OUT, W * H_OUT * 2, dst))


if __name__ == "__main__":
    main()
