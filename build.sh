#!/bin/bash
# Kobo Deluxe 32X - CARTRIDGE ROM build (kobo32x_crt): Kobo's SH2 program + the 68000 cartridge
# program (cart/cart_md.s) + graphics, splash and sound pack -> KOBO32X_CART.32x (4 MB).
# Run it on the EverDrive (or in Fusion as a normal 32X cartridge).  Uses the normal toolchain.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TC=/opt/toolchains/sega/m68k-elf/bin/m68k-elf-
grep -q '^#define KOBO_ROMCART 1' "$HERE/sh2/sh2_main.c" \
  || { echo "*** sh2/sh2_main.c is not the cartridge version (KOBO_ROMCART 1)"; exit 1; }
echo "==> SH2 (Kobo)"
cd "$HERE/sh2" && make clean && make
echo "==> 68000 (cart/cart_md.s, linked at 0x880800)"
cd "$HERE/cart"
# C2: D32XR's Sega CD code (proven): scd.c (InitCD, CD audio), kos.s (BIOS unpacker), cd/cd.bin
D32="$HERE/../d32xr-v33/src-md"
[ -f "$D32/cd/cd.bin" ] || { echo "*** $D32/cd/cd.bin missing - build d32xr-v33 once (make) first"; exit 1; }
cp "$D32/cd/cd.bin" cd.bin
${TC}as -m68000 --register-prefix-optional cart_md.s -o cart_md.o
${TC}as -m68000 --register-prefix-optional "$D32/kos.s" -o kos.o
${TC}gcc -m68000 -Os -c -fomit-frame-pointer -fno-builtin-printf "$D32/scd.c" -o scd.o
${TC}gcc -T cart.ld -nostdlib -Wl,-Map=cart.map cart_md.o scd.o kos.o -lc -lgcc -o cart_md.elf
${TC}objcopy -O binary -j .text -j .data cart_md.elf cart_md.bin
echo "==> ROM"
python3 "$HERE/tools/make_cart.py" "$HERE/sh2/D32XR.32x" "$HERE/cart/cart_md.bin" "$HERE/subcpu/ROMS" "$HERE/KOBO32X_CART.32x"
echo "==> done: $HERE/KOBO32X_CART.32x - test in Fusion as a 32X cartridge, then on the EverDrive"
