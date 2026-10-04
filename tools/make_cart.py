#!/usr/bin/env python3
"""make_cart.py - build the Kobo Deluxe 32X CARTRIDGE ROM.

  python3 tools/make_cart.py <sh2 build (CD-boot binary)> <cart_md.bin> <ROMS dir> <out .32x>

ROM layout:
  0x000000  standard 32X boot block (tools/mars_boot.bin: 68000 vectors, Genesis header, 32X
            vector jump list at 0x200, 32X header at 0x3C0, Sega's ICD start-up at 0x3F0) -
            patched: title, ROM end, no SRAM, the 32X header's SH2 values, checksum
  0x000800  cart_md.bin  - the 68000 program (linked at 0x880800)
  0x008000  Kobo's SH2 program (the CD build minus its 56-byte CD boot table)
  0x200000  KOBOGFX.BIN  - where Kobo's cart mode reads it (file 16 x 128 KB)
  0x300000  IMAGE.RAW    - the splash (KOBO_ROMCART: the SH2 copies it from here)
  0x320000  KOBOSFX.BIN  - sound pack, for the Sega CD step
The SH2 values come from Kobo's own CD boot table (same fields as the cartridge header):
  CD table +0x1C length, +0x20 master entry, +0x24 slave entry, +0x28 master VBR, +0x2C slave VBR
"""
import sys, os, struct
sh2_path, md_path, roms, out = sys.argv[1:5]
HERE = os.path.dirname(os.path.abspath(__file__))
boot = bytearray(open(os.path.join(HERE, "mars_boot.bin"), "rb").read())
assert len(boot) == 0x800 and boot[0x100:0x108] == b"SEGA 32X", "bad tools/mars_boot.bin"
sh2 = open(sh2_path, "rb").read()
md = open(md_path, "rb").read()

# ---- Kobo's CD boot table -> cartridge header values
tab = sh2[:0x38]
size, ment, sent, mvbr, svbr = struct.unpack(">5I", tab[0x1C:0x30])
assert tab[0x18:0x1C] == b"\x06\x00\x00\x00", "SH2 build: unexpected destination in the CD table"
assert 0 < size <= len(sh2) - 0x38 and (ment >> 24) == 6 and (sent >> 24) == 6, "SH2 build: bad CD table"
SH2_OFF = 0x8000
if 0x800 + len(md) > SH2_OFF: sys.exit("*** 68000 program too big (%d bytes)" % len(md))
if SH2_OFF + size > 0x200000: sys.exit("*** SH2 program too big")

rom = bytearray(b"\xFF" * 0x400000)
rom[0:0x800] = boot
def put_str(off, text, n):
    rom[off:off + n] = text.encode("ascii")[:n].ljust(n, b" ")
put_str(0x120, "KOBO DELUXE 32X", 48)                   # domestic title
put_str(0x150, "KOBO DELUXE 32X", 48)                   # overseas title
rom[0x1A0:0x1A8] = struct.pack(">II", 0, 0x3FFFFF)      # ROM start / end
rom[0x1B0:0x1BC] = b" " * 12                            # no cartridge SRAM (saves: Sega CD, C2)
put_str(0x3C0, "KOBO DELUXE 32X", 16)                   # 32X header: module name
rom[0x3D0:0x3F0] = struct.pack(">8I", 0, SH2_OFF, 0, size, ment, sent, mvbr, svbr)

rom[0x800:0x800 + len(md)] = md
rom[SH2_OFF:SH2_OFF + size] = sh2[0x38:0x38 + size]
for name, off, limit in (("KOBOGFX.BIN", 0x200000, 0x100000), ("IMAGE.RAW", 0x300000, 0x20000),
                         ("KOBOSFX.BIN", 0x320000, 0x40000)):
    data = open(os.path.join(roms, name), "rb").read()
    if len(data) > limit: sys.exit("*** %s too big (%d bytes)" % (name, len(data)))
    rom[off:off + len(data)] = data
    print("  %-12s at 0x%06X, %7d bytes" % (name, off, len(data)))

ck = 0                                                   # Genesis checksum (words from 0x200)
for i in range(0x200, len(rom), 2): ck = (ck + (rom[i] << 8 | rom[i + 1])) & 0xFFFF
rom[0x18E:0x190] = struct.pack(">H", ck)
open(out, "wb").write(rom)
print("  68000 program at 0x000800, %d bytes" % len(md))
print("  SH2 program   at 0x%06X, %d bytes, master 0x%08X slave 0x%08X VBR 0x%08X/0x%08X" % (SH2_OFF, size, ment, sent, mvbr, svbr))
print("written %s (4 MB, checksum 0x%04X)" % (out, ck))
