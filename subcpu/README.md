# ROM Loader for the Mega 32X RAM Cart

This project shows an example of how to load files onto the 4MiB RAM cart. 

## Description

Make sure the lower switch on the cart is set to "boot off the CD" position. Write the ISO file of this project to a CD-R and boot using the SegaCD addon.

After initial loading, the program will list files it was able to find on the CD. Select a ROM file from the CD and press A to write it onto the RAM cart. Pressing B will verify ROM/RAM contents with the selected file.

To boot off the previously written ROM, you need to toggle the lower switch to the off position and power cycle the console. Pressing the RESET button on the console will not work!

## Building (in this repository)
This folder is **not part of the cartridge build**. It is kept for the files the cartridge uses:
- `ROMs/` holds the graphics (`KOBOGFX.BIN`), the splash (`IMAGE.RAW`) and the sound pack (`KOBOSFX.BIN`).
  `tools/make_cart.py` builds them into the ROM.
- `Track02-04.wav` and `CDROMPlayer.iso` make the optional Sega CD music disc (`KOBO32X_CART.cue`):
```bash
cd ..
python3 tools/make_mixed_cd2.py subcpu/CDROMPlayer.iso subcpu/Track02.wav subcpu/Track03.wav subcpu/Track04.wav --out KOBO32X_CART
```
  The WAVs are 48 kHz. The script converts them to 44.1 kHz with `audioop` (Python 3.12 and older) or
  numpy (3.13+). Options: `--out NAME`, `--mode2`, `--pregap N` (default 150).

Build the cartridge ROM from the top folder with **`bash build.sh`** (see `../README.md`).

## "ROM Loader for the Mega 32X RAM Cart" Credits
* Programming : Chilly Willy and Victor Luchits
* Base Docker Code : Victor Luchits
## 

## License
All original code is available under the MIT license.
