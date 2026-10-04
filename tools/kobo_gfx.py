#!/usr/bin/env python3
"""
kobo_gfx.py - convert Kobo Deluxe's PNG graphics into 32X-ready 256-colour data.

WHY each step:
  - Kobo draws every image at 2x (the "2.0f" in kobo.cpp's gfx table) and scales it down
    when the game starts. The 32X has no time for that, so we halve here, once, on the PC.
    Colour is averaged using alpha as weight, so edges don't pick up dark/transparent fringes.
  - The 32X 256-colour mode uses ONE palette of 256 entries in 15-bit colour (5 bits per
    channel). We quantise every image together to 254 colours in that 15-bit space.
    Index 0 = "transparent" (the sprite drawer skips it).  Index 255 is RESERVED and never
    used for a visible pixel: on the console/Fusion, palette indices 0 and 255 showed the
    Genesis layer instead of their colour (K7/K8 tests), whatever colour they held.
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
            u16 frame count, u16 reserved, u32 data offset (from file start),
            u32 check = sum of the first min(4096, bank size) data bytes (the SH2 verifies
                        its copy against this - catches a wrong CD/frame-buffer transfer)
  then    pixel data: per bank, frames one after another, w*h bytes each, row by row
"""
import argparse, os, struct, sys
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


PANEL_ROWS = [(3, 45), (52, 188), (191, 237)]   # K30: Kobo 1x rows of the top boxes, radar box,
                                                 # bottom boxes -> stacked = 42 + 136 + 46 = 224 rows

def make_panel(gfx):
    """K30 - the right-hand dashboard: screen2.png at 1x, columns 240..319 (80 px), the three
    row bands above stacked into 224 rows (Kobo's screen is 240 lines, ours 224).  Opaque."""
    img = np.asarray(Image.open(os.path.join(gfx, "screen2.png")).convert("RGBA"), dtype=np.float64) / 255.0
    img[..., 3] = 1.0
    small = halve(img)[:, 240:320]
    band = np.concatenate([small[a:b] for a, b in PANEL_ROWS], axis=0)
    assert band.shape[0] == 224, band.shape
    return dict(file="screen2.png", id="B_PANEL", w=80, h=224, cols=1, rows=1, img=band, kind="tile")

def make_logo(gfx):
    """K35 - Kobo's title logo at game size: the gold outline (logo-outline.png), the solid
    shape (logomask3.png, 4x) filled with a gold -> red gradient (Kobo fills it with an
    effect), and the "DELUXE" wordmark (deluxe.png)."""
    out = np.asarray(Image.open(os.path.join(gfx, "logo-outline.png")).convert("RGBA"), dtype=np.float64) / 255.0
    out = halve(out)                                             # 207 x 62
    m = np.asarray(Image.open(os.path.join(gfx, "logomask3.png")).convert("RGBA"), dtype=np.float64) / 255.0
    m = halve(halve(m))                                          # 207 x 61
    h, w = out.shape[:2]
    mask = np.zeros((h, w)); mask[:m.shape[0], :m.shape[1]] = m[:h, :w, 0]
    fill = np.zeros((h, w, 4))
    top = np.array([1.0, 0.85, 0.25]); bot = np.array([0.75, 0.10, 0.05])
    for y in range(h):
        t = y / max(1, h - 1)
        fill[y, :, :3] = top * (1 - t) + bot * t
    fill[..., 3] = (mask > 0.5) * 1.0
    dx = np.asarray(Image.open(os.path.join(gfx, "deluxe.png")).convert("RGBA"), dtype=np.float64) / 255.0
    dx = halve(dx)
    return [dict(file="logo-outline.png", id="B_LOGO_OUT", w=w, h=h, cols=1, rows=1, img=out, kind="sprite"),
            dict(file="logomask3.png", id="B_LOGO_FILL", w=w, h=h, cols=1, rows=1, img=fill, kind="sprite"),
            dict(file="deluxe.png", id="B_DELUXE", w=dx.shape[1], h=dx.shape[0], cols=1, rows=1, img=dx, kind="sprite")]

def rle_encode(frames):
    """K35 - run-length encoded sprite frames (index 0 = see-through).
    Bank data: u16 offset per frame (big-endian, from the bank start), then per frame, per row:
      runs of  [skip][count][data]  - skip = see-through pixels before the run;
               count bit 7 set  -> FILL: (count & 0x7F) pixels of the ONE colour byte that follows
               count bit 7 clear -> LITERAL: count pixel bytes follow
      a skip of 0xFF ends the row."""
    out = bytearray(2 * len(frames))
    for fi, f in enumerate(frames):
        out[2 * fi] = len(out) >> 8; out[2 * fi + 1] = len(out) & 0xFF
        for row in f:
            x, w = 0, len(row)
            while True:
                sk = 0
                while x < w and row[x] == 0 and sk < 254:
                    x += 1; sk += 1
                if x >= w or row[x] == 0:
                    if x >= w: break
                    out += bytes([sk, 0]); continue          # long gap: an empty run
                n = x
                while n < w and row[n] != 0: n += 1          # opaque span x..n
                out.append(sk)
                first = True
                while x < n:
                    if not first: out.append(0)
                    first = False
                    c = row[x]; r = 1
                    while x + r < n and row[x + r] == c and r < 127: r += 1
                    if r >= 3:
                        out += bytes([0x80 | r, c]); x += r
                    else:
                        e = x
                        while e < n and e - x < 127:
                            if e + 2 < n and row[e] == row[e + 1] == row[e + 2]: break
                            e += 1
                        if e == x: e = x + 1
                        out += bytes([e - x]) + bytes(row[x:e]); x = e
            out.append(0xFF)
    assert len(out) < 65536
    return bytes(out)

FONT_CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-:.!?/"   # K30: the characters the game uses

# FONT (final): every character the game uses is drawn at game size in one style, so letters
# and numbers match: 8 rows tall, 2-pixel strokes, Kobo's ice colours (white -> light blue ->
# mid blue) and a 1-pixel dark-blue drop shadow (+1,+1) that softens the edges like Kobo's own
# font.  (Before: icefont shrunk from 2x by averaging = blurry; then hand-drawn digits only,
# 6 x 9 - too big and too hard next to the letters.)
# Kobo CD32X game-size font: every character 8 rows tall, 2-pixel strokes, + 1-pixel shadow
KFONT = {
'0':[".###.","##.##","##.##","##.##","##.##","##.##","##.##",".###."],
'1':[".##.","###.",".##.",".##.",".##.",".##.",".##.","####"],
'2':[".###.","##.##","...##","..##.",".##..","##...","##...","#####"],
'3':[".###.","##.##","...##","..##.","...##","...##","##.##",".###."],
'4':["...##","..###",".####","##.##","#####","...##","...##","...##"],
'5':["#####","##...","##...","####.","...##","...##","##.##",".###."],
'6':[".###.","##...","##...","####.","##.##","##.##","##.##",".###."],
'7':["#####","...##","...##","..##.","..##.",".##..",".##..",".##.."],
'8':[".###.","##.##","##.##",".###.","##.##","##.##","##.##",".###."],
'9':[".###.","##.##","##.##","##.##",".####","...##","...##",".###."],
'A':[".###.","##.##","##.##","##.##","#####","##.##","##.##","##.##"],
'B':["####.","##.##","##.##","####.","##.##","##.##","##.##","####."],
'C':[".###.","##.##","##...","##...","##...","##...","##.##",".###."],
'D':["####.","##.##","##.##","##.##","##.##","##.##","##.##","####."],
'E':["#####","##...","##...","####.","##...","##...","##...","#####"],
'F':["#####","##...","##...","####.","##...","##...","##...","##..."],
'G':[".###.","##.##","##...","##...","##.##","##.##","##.##",".####"],
'H':["##.##","##.##","##.##","#####","##.##","##.##","##.##","##.##"],
'I':["####",".##.",".##.",".##.",".##.",".##.",".##.","####"],
'J':["..###","...##","...##","...##","...##","##.##","##.##",".###."],
'K':["##.##","##.##","####.","###..","####.","##.##","##.##","##.##"],
'L':["##...","##...","##...","##...","##...","##...","##...","#####"],
'M':["#...#","##.##","#####","#####","##.##","##.##","##.##","##.##"],
'N':["##..#","###.#","#####","##.##","##.##","##.##","##.##","##.##"],
'O':[".###.","##.##","##.##","##.##","##.##","##.##","##.##",".###."],
'P':["####.","##.##","##.##","####.","##...","##...","##...","##..."],
'Q':[".###.","##.##","##.##","##.##","##.##","##.##","##.#.",".##.#"],
'R':["####.","##.##","##.##","####.","####.","##.##","##.##","##.##"],
'S':[".###.","##.##","##...",".###.","...##","...##","##.##",".###."],
'T':["######","..##..","..##..","..##..","..##..","..##..","..##..","..##.."],
'U':["##.##","##.##","##.##","##.##","##.##","##.##","##.##",".###."],
'V':["##.##","##.##","##.##","##.##","##.##","##.##",".###.","..#.."],
'W':["##.##","##.##","##.##","##.##","#####","#####","##.##","#...#"],
'X':["##.##","##.##",".###.","..#..",".###.","##.##","##.##","##.##"],
'Y':["##.##","##.##","##.##",".###.","..#..","..#..","..#..","..#.."],
'Z':["#####","...##","..##.","..#..",".##..","##...","##...","#####"],
'-':["....","....","....","####","....","....","....","...."],
':':["..","##","##","..","..","##","##",".."],
'.':["..","..","..","..","..","..","##","##"],
'!':["##","##","##","##","##","..","##","##"],
'?':[".###.","##.##","...##","..##.",".##..",".....",".##..",".##.."],
'/':["...##","...##","..##.","..##.",".##..",".##..","##...","##..."],
}
KROW = [(0.94,0.96,1.0)]*2 + [(0.74,0.86,1.0)]*3 + [(0.52,0.66,0.94)]*3     # ice gradient
KSHADOW = (0.10,0.14,0.36)                                                   # soft drop shadow
def kglyph(ch):
    """returns (9 x 7 RGBA float array, advance width)"""
    g = KFONT[ch]; w = len(g[0]); out = np.zeros((9, 7, 4))
    for r, row in enumerate(g):            # shadow first (+1,+1)
        for c, v in enumerate(row):
            if v == '#' and r + 1 < 9 and c + 1 < 7:
                out[r + 1, c + 1, :3] = KSHADOW; out[r + 1, c + 1, 3] = 1
    for r, row in enumerate(g):
        for c, v in enumerate(row):
            if v == '#':
                out[r, c, :3] = KROW[r]; out[r, c, 3] = 1
    return out, w + 1

def sharp_halve(g):
    h, w = g.shape[0] // 2, g.shape[1] // 2
    out = np.zeros((h, w, 4))
    for y in range(h):
        for x in range(w):
            blk = g[2 * y:2 * y + 2, 2 * x:2 * x + 2].reshape(4, 4)
            m = blk[:, 3] > 0.5
            if m.sum() >= 2:
                c = blk[m]
                out[y, x, :3] = c[np.argmax(c[:, :3].sum(1)), :3]
                out[y, x, 3] = 1.0
    return out

def make_font(gfx):
    """K30 - icefont2.png (SFont: pink markers on row 0 separate the glyphs, which start at '!').
    Kept: FONT_CHARS only, each in a 7 x 9 cell (1x), left-aligned; widths in a 1x1-frame bank."""
    src = np.asarray(Image.open(os.path.join(gfx, "icefont2.png")).convert("RGBA"), dtype=np.float64) / 255.0
    row = src[0]
    pink = (row[:, 0] > 0.99) & (row[:, 1] < 0.01) & (row[:, 2] > 0.99)
    spans, st = [], None
    for x in range(len(pink)):
        if not pink[x] and st is None: st = x
        if pink[x] and st is not None: spans.append((st, x - 1)); st = None
    body = src[1:].copy()
    cells, widths = [], []
    for ch in FONT_CHARS:
        if ch == " ":
            cells.append(np.zeros((9, 7, 4)))
            widths.append(3)
            continue
        g, adv = kglyph(ch)                          # game-size glyph + shadow, 9 x 7
        cells.append(g)
        widths.append(adv)
    img = np.concatenate(cells, axis=1)              # 9 x (7 * n)
    fontb = dict(file="icefont2.png", id="B_FONT", w=7, h=9, cols=len(FONT_CHARS), rows=1, img=img, kind="sprite")
    wimg = np.zeros((1, len(FONT_CHARS), 4)); wimg[..., 3] = 1.0
    fontw = dict(file="(widths)", id="B_FONTW", w=1, h=1, cols=len(FONT_CHARS), rows=1, img=wimg, kind="raw",
                 raw=bytes(widths))
    return fontb, fontw

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

    # ---- K30 special banks: the right-hand PANEL and the menu FONT (see make_panel/make_font)
    banks.append(make_panel(a.gfx))
    fb, fw = make_font(a.gfx)
    banks.append(fb); banks.append(fw)
    banks += make_logo(a.gfx)                      # K35: banks 34 LOGO_OUT, 35 LOGO_FILL, 36 DELUXE

    # ---- one shared palette: quantise all opaque pixels in 15-bit space to 255 colours
    pix = []
    for b in banks:
        if b["kind"] == "raw": continue
        m = b["img"][..., 3] >= ALPHA_CUT
        pix.append(to555(b["img"][..., :3][m]))
    allpix = np.concatenate(pix)
    strip = Image.fromarray(allpix.reshape(1, -1, 3), "RGB")
    NCOL = 254                                     # visible colours -> indices 1..254
    pal_img = strip.quantize(colors=NCOL, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array(pal_img.getpalette()[:NCOL * 3], dtype=np.uint8).reshape(-1, 3)
    pal = to555(pal / 255.0)                       # palette entries themselves exactly 15-bit
    palette = np.vstack([[0, 0, 0], pal, [0, 0, 0]])   # 0 = transparent, 255 = reserved (unused)

    # palette-lookup image for PIL remapping (entries 1..254 only)
    lut = Image.new("P", (1, 1))
    lut.putpalette(list(pal.flatten()) + [0] * (3 * (256 - NCOL)))

    # ---- map every bank to indices
    for b in banks:
        if b["kind"] == "raw":
            b["idx"] = np.frombuffer(b["raw"], dtype=np.uint8).reshape(1, -1)
            continue
        rgb = to555(b["img"][..., :3])
        q = Image.fromarray(rgb, "RGB").quantize(palette=lut, dither=Image.Dither.NONE)
        idx = np.asarray(q, dtype=np.uint8).astype(np.int32) + 1      # 0..253 -> 1..254
        idx[b["img"][..., 3] < ALPHA_CUT] = 0
        b["idx"] = idx.astype(np.uint8)

    # ---- K35: run-length encoded copies (banks 37..46) of sprites the SH2 keeps in little
    #      memory: the explosion sets and the logo.  Data = rle_encode() layout.
    byid = {b["id"]: b for b in banks}
    for src, rid in (("B_BOLTEXPL", "R_BOLTEXPL"), ("B_EXPLO3", "R_EXPLO3"), ("B_EXPLO4", "R_EXPLO4"),
                     ("B_EXPLO5", "R_EXPLO5"), ("B_ROCKEXPL", "R_ROCKEXPL"), ("B_BULLETEXPL", "R_BULLETEXPL"),
                     ("B_RINGEXPL", "R_RINGEXPL"), ("B_LOGO_FILL", "R_LOGO_FILL"), ("B_LOGO_OUT", "R_LOGO_OUT"),
                     ("B_DELUXE", "R_DELUXE")):
        b = byid[src]
        fr = [b["idx"][fy * b["h"]:(fy + 1) * b["h"], fx * b["w"]:(fx + 1) * b["w"]]
              for fy in range(b["rows"]) for fx in range(b["cols"])]
        data = rle_encode(fr)
        banks.append(dict(file=src, id=rid, w=b["w"], h=b["h"], cols=len(fr), rows=1, kind="rle",
                          idx=np.frombuffer(data, dtype=np.uint8).reshape(1, -1), rlelen=len(data)))

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
    # HW3: banks the game never reads are written EMPTY (table entry kept, so every bank number
    # stays the same, but 0 frames / no pixel data).  The explosions and the logo are drawn from
    # their RLE copies (banks 37-46) since K35, so their raw pictures are dead weight.  The star
    # bank is cut to the 32 frames the game uses (STAR_FRAMES).  WHY: on the real cart the pack
    # must fit in 9 chunks of 32 KB = 294,912 bytes (chunk 9 at cart 0x648000 failed to verify
    # with the 312,924-byte pack - screenshot STATE:4BE2).
    UNUSED = {"B_BOLTEXPL", "B_EXPLO3", "B_EXPLO4", "B_EXPLO5", "B_ROCKEXPL", "B_BULLETEXPL",
              "B_RINGEXPL", "B_LOGO_OUT", "B_LOGO_FILL", "B_DELUXE"}
    FRAME_LIMIT = {"B_OLDSTARS": 32}
    for n, b in enumerate(banks):
        if b["id"] in UNUSED:
            table += struct.pack(">HBBHHII", n, b["w"], b["h"], 0, 0, data_off + len(data), 0)
            b["nframes"] = 0
            continue
        if b.get("kind") == "rle":
            bank_bytes = b["idx"].tobytes()
            b["nframes"] = b["cols"]
            check = sum(bank_bytes[:4096]) & 0xFFFFFFFF
            table += struct.pack(">HBBHHII", n, b["w"], b["h"], b["cols"], len(bank_bytes), data_off + len(data), check)
            data += bank_bytes
            while len(data) % 4:
                data += b"\0"
            continue
        frames = []
        for fy in range(b["rows"]):
            for fx in range(b["cols"]):
                frames.append(b["idx"][fy * b["h"]:(fy + 1) * b["h"], fx * b["w"]:(fx + 1) * b["w"]])
        if b["id"] in FRAME_LIMIT:
            frames = frames[:FRAME_LIMIT[b["id"]]]
        b["nframes"] = len(frames)
        bank_bytes = b"".join(f.tobytes() for f in frames)
        check = sum(bank_bytes[:4096]) & 0xFFFFFFFF
        table += struct.pack(">HBBHHII", n, b["w"], b["h"], len(frames), 0, data_off + len(data), check)
        data += bank_bytes
        while len(data) % 4:
            data += b"\0"
    blob = bytes(hdr + table + data)
    CART_LIMIT = 9 * 32768            # HW3: what the cart copy path takes (see UNUSED above)
    if len(blob) > CART_LIMIT and not a.only:
        sys.exit("*** pack is %d bytes - more than the cart limit of %d (9 x 32 KB)" % (len(blob), CART_LIMIT))
    open(os.path.join(a.out, a.name), "wb").write(blob)

    # ---- previews: exactly what the 32X will show (3x, grey checker = transparent)
    rgbpal = palette.astype(np.uint8)
    for b in all_banks:
        if b["kind"] in ("raw", "rle"): continue
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
