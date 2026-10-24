# Kobo 32X (Emulator Fusion Edition)

## This is a specialized build of the Kobo Deluxe port for the Sega 32X. 
A standard Sega CD / 32XCD releases, 
This version is engineered specifically to execute entirely from the real hardware or a standard Everdrive ROM cartridge.

## Building

### What you need
- **Chilly Willy's Sega MD/CD/32X devkit** (sh-elf + m68k-elf GCC 12.1), from the 32XDK releases page:
  https://github.com/viciious/32XDK/releases. The default location is `/opt/toolchains/sega`. The original setup
  keeps this project inside the toolchain folder, as `sega-toolchain-12.1/sega/kobo32x_crt`.
- **python3.** The music disc (below) also needs numpy on Python 3.13+.

### One command
```bash
cd kobo32x_crt
bash build.sh                                # toolchain in /opt/toolchains/sega
GENDEV=/path/to/sega bash build.sh           # toolchain somewhere else
```
The output is **`KOBO32X_CART.32x`** (4 MB). Run it in Fusion as a 32X cartridge, or on the EverDrive.
The compiler prints some warnings (unused functions, `pri_start`); they are expected.
`build.sh` takes **no arguments**. Its only setting is the `GENDEV` environment variable (the toolchain
folder). It works from a fresh download. It uses the copy of D32XR's Sega CD files kept in `d32xr/`, or
`../d32xr-v33/src-md/` when that exists (the original dev-folder layout), and reads the game files from
`subcpu/ROMs/`. `build_fresh.sh` still works: it just runs `build.sh`.

### Step by step (what `build.sh` runs)
```bash
TC=/opt/toolchains/sega/m68k-elf/bin/m68k-elf-
cd sh2 && make clean && make                       # Kobo's SH2 program -> sh2/D32XR.32x   (make GENDEV=... works too)
cd ../cart                                         # the 68000 cartridge program, linked at 0x880800
cp ../d32xr/cd/cd.bin cd.bin
${TC}as -m68000 --register-prefix-optional cart_md.s -o cart_md.o
${TC}as -m68000 --register-prefix-optional ../d32xr/kos.s -o kos.o
${TC}gcc -m68000 -Os -c -fomit-frame-pointer -fno-builtin-printf ../d32xr/scd.c -o scd.o
${TC}gcc -T cart.ld -nostdlib -Wl,-Map=cart.map cart_md.o scd.o kos.o -lc -lgcc -o cart_md.elf
${TC}objcopy -O binary -j .text -j .data cart_md.elf cart_md.bin
cd ..
python3 tools/make_cart.py sh2/D32XR.32x cart/cart_md.bin subcpu/ROMs KOBO32X_CART.32x
```
`make_cart.py <SH2 build> <68000 program> <game files folder> <output .32x>` lays out the ROM:
| Offset | What |
|---|---|
| `0x000000` | 32X boot block (`tools/mars_boot.bin`) |
| `0x000800` | the 68000 program |
| `0x008000` | the SH2 program |
| `0x200000` | `KOBOGFX.BIN` (graphics) |
| `0x300000` | `IMAGE.RAW` (splash) |
| `0x320000` | `KOBOSFX.BIN` (sound pack, for the Sega CD) |

It also fills in the header and checksum.

### The music disc (Sega CD, optional)
With a Sega CD attached, the cartridge plays CD music from **`KOBO32X_CART.cue` / `.bin`**: a data track
and Tracks 2-4. That disc is in the repository already. To make it again:
```bash
python3 tools/make_mixed_cd2.py subcpu/CDROMPlayer.iso subcpu/Track02.wav subcpu/Track03.wav subcpu/Track04.wav --out KOBO32X_CART
```
Options: `--out NAME` (default `<iso name>_mixed`); `--mode2` (data track as MODE2/2352 instead of
MODE1/2352); `--pregap N` (silent sectors before each audio track; default 150 = Red Book's 2 s; 0 = no gap).

The WAVs are 48 kHz and a CD needs 44.1 kHz. The script converts them with Python's `audioop` on Python
3.12 and older, or with numpy on 3.13+ (`pip install numpy`); both give the same bytes.

### Compile-time switches
There are no command-line switches for the game itself. These are `#define`s in `sh2/sh2_main.c`: edit the
value, then rebuild.
| Switch | Now | What it does |
|---|---|---|
| `KOBO_ROMCART` | 1 | **must be 1**: Kobo as a normal 32X cartridge, with its files in the ROM. The build scripts stop if it isn't |
| `KOBO_NOCART` | 0 | cart mode: the graphics are read from the cartridge |
| `KOBO_FIXED_STEP` | 0 | 0 = one game step per drawn frame. 1 = fixed step of `KOBO_STEP_MS`, the same speed at any frame rate |
| `KOBO_STEP_MS` | 30 | the fixed step, in ms (30 = Kobo Classic) |
| `KOBO_START_PAUSE` | 0 | pause is MODE (6-button pad); 1 = START pauses too |
| `TRACK02_SECONDS` | 60 | seconds of idle menu before the demo starts |
| `KOBO_SPLASH` | 1 | 1 = `IMAGE.RAW` splash at start-up; 0 = none |
| `KOBO_HW_PCM` | 1 | 1 = sound effects (sound pack into the Sega CD PCM chip, when there is one); 0 = off |
| `KOBO_HW_BRAM` | 1 | 1 = high scores / options in backup RAM (Sega CD); 0 = this session only |
| `KOBO_HW_DIAG` | 0 | 1 = hardware readout bottom-left (heartbeat, commands, pad requests, FPS) |
| `KOBO_HW_QUIET` | 0 | test only: 1 = no pad requests once the game runs (no controller) |
| `KOBO_DIAG_STRIP` | 0 | 0 = Kobo's right-hand panel; 1 = the diagnostic strip instead |
| `KOBO_DIAG_GRID` | 0 | 1 = palette grid in place of the radar |
| `KOBO_DEBUG_KEYS` | 0 | 1 = X (6-button pad) skips to the next level. 0 for a release |
| `KOBO_VERSION` | 37 | the version shown on screen |

### Asset tools (not part of the build; their outputs are already in `subcpu/ROMs/`)
```bash
python3 tools/kobo_gfx.py --gfx <Kobo data/gfx folder> --out <folder> [--only B_TILES1,B_OLDSTARS] [--name KOBOGFX.BIN]
python3 tools/kobo_sfx.py [wav folder (default wav)] [output (default KOBOSFX.BIN)]
python3 tools/convert_32x_image.py picture.png [IMAGE.RAW] [--height 200] [--mode stretch|fit|fill] [--dither none|fs|bayer]
python3 tools/convert_32x_image.py --decode IMAGE.RAW check.png       # preview a .raw
```
These need `numpy` and `pillow` (`pip install numpy pillow`). `convert_32x_image.ps1` is the PowerShell
version of the splash converter.

## Acknowledgments
Chilly Willy, aka Joeseph Fenton for the advice and support.

At the heart of this project beats this critical code, Thank you!

https://forums.sonicretro.org/threads/sega-cd-mode-1-player.27372/

https://gendev.spritesmind.net/forum/viewtopic.php?t=1018
