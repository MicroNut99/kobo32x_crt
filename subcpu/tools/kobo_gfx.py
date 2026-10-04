#!/usr/bin/env python3
"""
kobo_gfx.py - convert Kobo Deluxe's PNG graphics into 32X-ready 256-colour data.

WHY each step:
  - Kobo draws every image at 2x (the "2.0f" in kobo.cpp's gfx table) and scales it down
    when the game starts. The 32X has no time for that, so we halve here, once, on the PC.
    Colour is averaged using alpha as weight, so edges don't pick up dark/transparent fringes.
  - The 32X 256-colour mode uses ONE palette of 256 entries in 15-bit colour (5 bits per
    channel). We quantise every image together to 255 colours in that 15-bit space and keep
    index 0 for "transparent" (the sprite drawer skips it).
  - Images are cut into their frames (sizes from kobo.cpp) so the game can address frame N.

OUTPUT (in --out):
  KOBOGFX.BIN   the pack (layout below) - goes on the CD, the 68K copies it into the cart
  preview/*.png every bank as it will look on the 32X (3x size, frames on a grid)
  palette.png   the 256 colours

KOBOGFX.BIN layout (big-endian, as both 68K and SH2 read it):
  0x0000  'KGFX'            magic
  0x0004  u16 version=1, u16 number of banks
  0x0008  256 x u16         palette, 32X CRAM format: bits 0-4 R, 5-9 G, 10-14 B
  0x0208  bank table, 16 bytes per bank:
            u16 bank id (Kobo B_ number, see BANKS below), u8 frame w, u8 frame h,
            u16 frame count, u16 reserved, u32 data offset (from file start), u32 reserved
  then    pixel data: per bank, frames one after another, w*h bytes each, row by row
"""
import argparse, os, struct
import numpy as np
from PIL import Image

# (png, bank id, frame w, frame h at PNG scale, kind)   - sizes from kobo.cpp gfx table
BANKS = [
    ("tiles-green.png",  "B_TILES1",    32, 32, "tile"),
    ("tiles-metal.png",  "B_TILES2",    32, 32, "tile"),
    ("tiles-blood.png",  "B_TILES3",    32, 32, "tile"),
    ("tiles-double.png", "B_TILES4",    32, 32, "tile"),
    ("tiles-chrome.png", "B_TILES5",    32, 32, "tile"),
    ("player.png",       "B_PLAYER",    40, 40, "sprite"),
    ("bmr-green.png",    "B_BMR_GREEN", 40, 40, "sprite"),
    ("bmr-purple.png",   "B_BMR_PURPLE",40, 40, "sprite"),
    ("bmr-pink.png",     "B_BMR_PINK",  40, 40, "sprite"),
    ("fighter.png",      "B_FIGHTER",   40, 40, "sprite"),
    ("missile.png",      "B_MISSILE1",  40, 40, "sprite"),
    ("missile2.png",     "B_MISSILE2",  40, 40, "sprite"),
    ("missile3.png",     "B_MISSILE3",  40, 40, "sprite"),
    ("bolt.png",         "B_BOLT",      16, 16, "sprite"),
    ("boltexpl.png",     "B_BOLTEXPL",  32, 32, "sprite"),
    ("explo1e.png",      "B_EXPLO1",    48, 48, "sprite"),
    ("explo3e.png",      "B_EXPLO3",    64, 64, "sprite"),
    ("explo4e.png",      "B_EXPLO4",    64, 64, "sprite"),
    ("explo5e.png",      "B_EXPLO5",    64, 64, "sprite"),
    ("rock1c.png",       "B_ROCK1",     32, 32, "sprite"),
    ("rock2.png",        "B_ROCK2",     32, 32, "sprite"),
    ("shinyrock.png",    "B_ROCK3",     32, 32, "sprite"),
    ("rockexpl.png",     "B_ROCKEXPL",  64, 64, "sprite"),
    ("bullet5b.png",     "B_BULLETS",   16, 16, "sprite"),
    ("bulletexpl2.png",  "B_BULLETEXPL",32, 32, "sprite"),
    ("ring.png",         "B_RING",      32, 32, "sprite"),
    ("ringexpl2b.png",   "B_RINGEXPL",  40, 40, "sprite"),
    ("bomb.png",         "B_BOMB",      24, 24, "sprite"),
    ("bombdeto.png",     "B_BOMBDETO",  40, 40, "sprite"),
    ("bigship.png",      "B_BIGSHIP",   72, 72, "sprite"),
    ("flatstars1.png",   "B_OLDSTARS",  32, 32, "tile"),     # 30: XKobo-style star tiles for empty space
]

ALPHA_CUT = 0.5          # below this a pixel becomes transparent (index 0)


def halve(rgba):
    """2x2 box downscale, colour weighted by alpha (premultiplied average)."""
    a = rgba[..., 3:4]
    h, w = rgba.shape[0] // 2 * 2, rgba.shape[1] // 2 * 2
    rgba, a = rgba[:h, :w], a[:h, :w]
    pm = rgba[..., :3] * a
    def box(x):
        return (x[0::2, 0::2] + x[1::2, 0::2] + x[0::2, 1::2] + x[1::2, 1::2]) / 4.0
    pa, pc = box(a), box(pm)
    rgb = np.where(pa > 1e-6, pc / np.maximum(pa, 1e-6), 0.0)
    return np.concatenate([rgb, pa], axis=2)


def to555(rgb):
    """float 0..1 -> nearest 15-bit colour, returned as 0..255 values the 32X can show."""
    q = np.clip(np.round(rgb * 31.0), 0, 31)
    return (q * 255.0 / 31.0).round().astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gfx", required=True, help="Kobo data/gfx folder")
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default=None,
                    help="comma list of bank ids to write, e.g. B_TILES1,B_OLDSTARS  (Fusion pack: its backup RAM "
                         "cart holds at most 128 KB).  The palette is still built from ALL banks, so colours match "
                         "the full pack.  Banks are renumbered 0,1,... in the order given.")
    ap.add_argument("--name", default="KOBOGFX.BIN", help="output file name")
    a = ap.parse_args()
    os.makedirs(os.path.join(a.out, "preview"), exist_ok=True)

    banks = []
    for fn, bid, fw, fh, kind in BANKS:
        img = np.asarray(Image.open(os.path.join(a.gfx, fn)).convert("RGBA"), dtype=np.float64) / 255.0
        if kind == "tile":
            img[..., 3] = 1.0                      # tiles are opaque backgrounds (KOBO_CLAMP)
        small = halve(img)
        gw, gh = fw // 2, fh // 2
        cols, rows = small.shape[1] // gw, small.shape[0] // gh
        banks.append(dict(file=fn, id=bid, w=gw, h=gh, cols=cols, rows=rows, img=small, kind=kind))

    # ---- one shared palette: quantise all opaque pixels in 15-bit space to 255 colours
    pix = []
    for b in banks:
        m = b["img"][..., 3] >= ALPHA_CUT
        pix.append(to555(b["img"][..., :3][m]))
    allpix = np.concatenate(pix)
    strip = Image.fromarray(allpix.reshape(1, -1, 3), "RGB")
    pal_img = strip.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array(pal_img.getpalette()[:255 * 3], dtype=np.uint8).reshape(-1, 3)
    pal = to555(pal / 255.0)                       # palette entries themselves exactly 15-bit
    palette = np.vstack([[0, 0, 0], pal])          # index 0 = transparent (shown black)

    # palette-lookup image for PIL remapping (entries 1..255 only)
    lut = Image.new("P", (1, 1))
    lut.putpalette(list(pal.flatten()) + [0] * (3 * (256 - 255)))

    # ---- map every bank to indices
    for b in banks:
        rgb = to555(b["img"][..., :3])
        q = Image.fromarray(rgb, "RGB").quantize(palette=lut, dither=Image.Dither.NONE)
        idx = np.asarray(q, dtype=np.uint8).astype(np.int32) + 1      # 0..254 -> 1..255
        idx[b["img"][..., 3] < ALPHA_CUT] = 0
        b["idx"] = idx.astype(np.uint8)

    # ---- write the pack (optionally only some banks)
    all_banks = banks
    if a.only:
        want = a.only.split(",")
        byid = {b["id"]: b for b in banks}
        banks = [byid[w] for w in want]
    hdr = bytearray(b"KGFX") + struct.pack(">HH", 1, len(banks))
    for r, g, bl in palette:
        hdr += struct.pack(">H", (int(r) >> 3) | ((int(g) >> 3) << 5) | ((int(bl) >> 3) << 10))
    table_off = len(hdr)
    data_off = table_off + 16 * len(banks)
    table, data = bytearray(), bytearray()
    for n, b in enumerate(banks):
        frames = []
        for fy in range(b["rows"]):
            for fx in range(b["cols"]):
                frames.append(b["idx"][fy * b["h"]:(fy + 1) * b["h"], fx * b["w"]:(fx + 1) * b["w"]])
        b["nframes"] = len(frames)
        table += struct.pack(">HBBHHII", n, b["w"], b["h"], len(frames), 0, data_off + len(data), 0)
        for f in frames:
            data += f.tobytes()
        while len(data) % 4:
            data += b"\0"
    blob = bytes(hdr + table + data)
    open(os.path.join(a.out, a.name), "wb").write(blob)

    # ---- previews: exactly what the 32X will show (3x, grey checker = transparent)
    rgbpal = palette.astype(np.uint8)
    for b in all_banks:
        idx = b["idx"]
        im = rgbpal[idx]
        chk = ((np.indices(idx.shape).sum(0) // 2) % 2)[..., None] * 40 + 50
        im = np.where(idx[..., None] == 0, chk, im).astype(np.uint8)
        big = Image.fromarray(im, "RGB").resize((idx.shape[1] * 3, idx.shape[0] * 3), Image.NEAREST)
        big.save(os.path.join(a.out, "preview", "%s.png" % b["id"]))
    sw = np.repeat(np.repeat(rgbpal.reshape(16, 16, 3), 16, 0), 16, 1)
    Image.fromarray(sw, "RGB").save(os.path.join(a.out, "palette.png"))

    print("banks: %d   pack: %d bytes (%.1f KB)" % (len(banks), len(blob), len(blob) / 1024))
    for n, b in enumerate(banks):
        print("  %2d %-13s %-17s %2dx%-2d  %3d frames" % (n, b["id"], b["file"], b["w"], b["h"], b["nframes"]))


if __name__ == "__main__":
    main()
