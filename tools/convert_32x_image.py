#!/usr/bin/env python3
"""
convert_32x_image.py  -  make an IMAGE.RAW for the 32X CD loader          (V2)

Output format (what the loader expects):
  * 320 x 200 pixels (default), 2 bytes per pixel  ->  128000 bytes
  * each pixel = 16-bit word  1BBBBBGGGGGRRRRR
        bit 15      = 1 (opaque / 32X priority bit - the loader also forces it)
        bits 14-10  = blue   (5 bit)
        bits  9-5   = green  (5 bit)
        bits  4-0   = red    (5 bit)
  * big-endian (high byte first), rows top to bottom, no header

The 32X frame buffer holds 65,280 pixel words after its line table, so the
picture can be at most 320 x 204.  The current SH2 program draws 200 lines,
so keep --height 200 unless you also change IMG_H in sh2_main.c.

Usage
  python convert_32x_image.py  picture.png                     -> IMAGE.RAW
  python convert_32x_image.py  picture.jpg  out.raw  --mode fit --dither fs
  python convert_32x_image.py  --decode IMAGE.RAW  check.png   -> preview of a .raw

  --mode   stretch  scale to exactly 320x200 (default; ignores aspect ratio)
           fit      keep aspect ratio, black bars top/bottom or left/right
           fill     keep aspect ratio, crop the overflow (centre)
  --dither none | fs (Floyd-Steinberg) | bayer (ordered)     (default none)
Needs:  pip install pillow
"""
import argparse, struct, sys
from PIL import Image

W = 320
BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def q5(v):                       # 0..255 -> 0..31, rounded
    return max(0, min(31, (v * 31 + 127) // 255))


def prepare(img, height, mode):
    img = img.convert("RGB")
    if mode == "stretch":
        return img.resize((W, height), Image.LANCZOS)
    sw, sh = img.size
    if mode == "fit":
        s = min(W / sw, height / sh)
    else:                        # fill
        s = max(W / sw, height / sh)
    nw, nh = max(1, round(sw * s)), max(1, round(sh * s))
    r = img.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGB", (W, height), (0, 0, 0))
    canvas.paste(r, ((W - nw) // 2, (height - nh) // 2))
    return canvas


def encode(img, dither):
    w, h = img.size
    px = img.load()
    words = []
    if dither == "fs":
        buf = [[list(map(float, px[x, y])) for x in range(w)] for y in range(h)]
        for y in range(h):
            for x in range(w):
                old = buf[y][x]
                q = [q5(int(round(max(0, min(255, c))))) for c in old]
                new = [(c * 255) / 31.0 for c in q]
                err = [old[i] - new[i] for i in range(3)]
                for dx, dy, f in ((1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h:
                        for i in range(3):
                            buf[ny][nx][i] += err[i] * f
                words.append(0x8000 | (q[2] << 10) | (q[1] << 5) | q[0])
    else:
        for y in range(h):
            for x in range(w):
                r, g, b = px[x, y]
                if dither == "bayer":
                    t = (BAYER4[y & 3][x & 3] - 7.5) * (255 / 31.0) / 16.0
                    r, g, b = (int(max(0, min(255, c + t))) for c in (r, g, b))
                words.append(0x8000 | (q5(b) << 10) | (q5(g) << 5) | q5(r))
    return struct.pack(">%dH" % len(words), *words)


def decode(path, out_png):
    d = open(path, "rb").read()
    n = len(d) // 2
    h = n // W
    if n % W:
        print("warning: size is not a multiple of 320 pixels per row")
    words = struct.unpack(">%dH" % n, d[: n * 2])
    img = Image.new("RGB", (W, h))
    px = img.load()
    for i, v in enumerate(words[: W * h]):
        r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
        px[i % W, i // W] = ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))
    img.save(out_png)
    print("decoded %d bytes -> %dx%d  %s" % (len(d), W, h, out_png))


def main():
    ap = argparse.ArgumentParser(description="Convert an image to a 32X IMAGE.RAW")
    ap.add_argument("input")
    ap.add_argument("output", nargs="?", default=None)
    ap.add_argument("--height", type=int, default=200)
    ap.add_argument("--mode", choices=("stretch", "fit", "fill"), default="stretch")
    ap.add_argument("--dither", choices=("none", "fs", "bayer"), default="none")
    ap.add_argument("--decode", action="store_true", help="input is a .raw, output is a .png preview")
    a = ap.parse_args()

    if a.decode:
        decode(a.input, a.output or "preview.png")
        return

    if not 1 <= a.height <= 204:
        sys.exit("height must be 1..204 (the frame buffer holds at most 320x204 pixels)")
    if a.height != 200:
        print("note: the current SH2 program draws 200 lines (IMG_H in sh2_main.c)")
    out = a.output or "IMAGE.RAW"
    img = prepare(Image.open(a.input), a.height, a.mode)
    data = encode(img, a.dither)
    assert len(data) == W * a.height * 2
    open(out, "wb").write(data)
    print("wrote %s: %d bytes (%dx%d, 5:5:5 big-endian, bit 15 set)" % (out, len(data), W, a.height))


if __name__ == "__main__":
    main()
