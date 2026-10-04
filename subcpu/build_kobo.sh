#!/bin/bash
# build_kobo.sh - full Kobo 32XCD build.
# Put this file in the sega folder (the one containing Demo/ and d32xr-master/) and run:
#     bash build_kobo.sh
# It uses its own folder, so no drive letter or /mnt path is needed.
# Same steps as before: SH2 build -> copy D32XR.32x -> Sega CD build -> mixed bin/cue.

SEGA="$(cd "$(dirname "$0")" && pwd)"   # the folder this script is in
set -e                                   # stop at the first step that fails
echo "sega folder: $SEGA"
[ -d "$SEGA/Demo" ] && [ -d "$SEGA/d32xr-master" ] || { echo "*** put build_kobo.sh in the folder that has Demo/ and d32xr-master/ ***"; exit 1; }
trap 'echo; echo "*** BUILD FAILED in step: $STEP ***"' ERR

STEP="SH2 build (d32xr-master)"
cd "$SEGA/d32xr-master"
make clean
make

STEP="copy D32XR.32x to Demo"
cp -v D32XR.32x "$SEGA/Demo/D32XR.32x"

STEP="Sega CD build (Demo)"
cd "$SEGA/Demo"
touch files.s
make clean
make cd

STEP="mixed-mode disc image (make_mixed_cd2.py)"
python3 make_mixed_cd2.py

echo
echo "=== BUILD OK - load $SEGA/Demo/CDROMPlayer_mixed.cue ==="
