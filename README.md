# Kobo 32X (Emulator Fusion Edition)

## This is a specialized build of the Kobo Deluxe port for the Sega 32X. 
A standard Sega CD / 32XCD releases, 
This version is engineered specifically to execute entirely from the real hardware or a standard Everdrive ROM cartridge.

## Build Requirements
To compile this project from source, you must use Chilly Willy's Sega MD/CD/32X devkit (sh-elf + m68k-elf GCC).

Download the required toolchain from the 32XDK releases page: https://github.com/viciious/32XDK/releases

Ensure the toolchain is correctly extracted to `sega-toolchain-12.1/sega/kobo32x_crt` before building. 

Compile using the provided `make` files.

## Acknowledgments
Chilly Willy, aka Joeseph Fenton for the advice and support.
At the heart of this project beats this critical code, Thank you!
https://forums.sonicretro.org/threads/sega-cd-mode-1-player.27372/
https://gendev.spritesmind.net/forum/viewtopic.php?t=1018
