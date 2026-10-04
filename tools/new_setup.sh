#!/bin/bash
# =============================================================================================
# migrate_kobo.sh - ONE-TIME move of the Kobo CD32X project into its own folder.
#
#   sega/kobo32x/
#     build.sh     one command: SH2 -> Sub-CPU -> disc image
#     sh2/         sh2_main.c + ONLY the d32xr-master files it needs (the compiler lists them)
#     subcpu/      main.c, hw_md.s, files.s + Chilly's framework, ROMS/, Track0x.wav, disc script
#     tools/       kobo_gfx.py, kobo_sfx.py (if present)
#
# SAFE: Demo/ and d32xr-master/ are only COPIED FROM - nothing there is changed or moved.
# Run it from anywhere:   bash migrate_kobo.sh
# =============================================================================================
set -e
SEGA="${SEGA:-/mnt/s/KoboPort/sega-toolchain-12.1/sega}"
OLD_SUB="$SEGA/Demo"
OLD_SH2="$SEGA/d32xr-master"
NEW="$SEGA/kobo32x"
SHGCC="${SHGCC:-/opt/toolchains/sega/sh-elf/bin/sh-elf-gcc}"

say() { echo "==> $*"; }
die() { echo "*** $*"; exit 1; }

[ -d "$OLD_SUB" ] || die "$OLD_SUB not found"
[ -d "$OLD_SH2" ] || die "$OLD_SH2 not found"
[ -f "$OLD_SH2/sh2_main.c" ] || die "$OLD_SH2/sh2_main.c not found"
[ -e "$NEW" ] && die "$NEW already exists - rename or delete it first (nothing was changed)"

mkdir -p "$NEW/sh2" "$NEW/subcpu" "$NEW/tools"

# ---- 1. SH2: sh2_main.c + the files it really uses ------------------------------------------
say "SH2: finding the headers sh2_main.c uses (compiler dependency list)"
cd "$OLD_SH2"
DEPS=$("$SHGCC" -MM -D__32X__ -DMARS sh2_main.c 2>/dev/null | sed 's/\\$//' | tr ' ' '\n' | grep -v ':$' | grep -v '^$' | grep -v '^/') \
    || die "could not list the dependencies ($SHGCC -MM failed)"
for f in $DEPS Makefile crt0.s sh2_sdram.ld; do
    [ -f "$f" ] || die "expected $OLD_SH2/$f"
    mkdir -p "$NEW/sh2/$(dirname "$f")"
    cp -v "$f" "$NEW/sh2/$f"
done
# anything crt0.s pulls in with .include
for f in $(grep -i '^\s*\.include' crt0.s | sed 's/.*"\(.*\)".*/\1/'); do
    [ -f "$f" ] && { mkdir -p "$NEW/sh2/$(dirname "$f")"; cp -v "$f" "$NEW/sh2/$f"; }
done

# ---- 2. Sub-CPU / 68K side: the whole Demo folder, minus build outputs -----------------------
say "Sub-CPU: copying Demo/ (framework + Kobo files + ROMS + tracks) - build outputs are skipped"
say "(the Track0x.wav files are large - copying them can take a minute; progress is listed)"
cd "$OLD_SUB"
# tar -h copies the REAL file behind a link (a copied link would point nowhere); excludes: build outputs (old disc images, objects, the temp cd/ folder) are never
# copied - they are big and are rebuilt anyway.  -v lists each file as it goes.
tar -chf - \
    --exclude='./cd' --exclude='*.o' --exclude='*.o80' --exclude='*.elf' --exclude='*.map' \
    --exclude='*.log' --exclude='*.iso' --exclude='./temp.bin' --exclude='./CDROMPlayer.bin' \
    --exclude='./CDROMPlayer_mixed.*' --exclude='./D32XR.32x' --exclude='*.cue' \
    . | (cd "$NEW/subcpu" && tar -xvf -)
cd "$NEW/subcpu"

# ---- 3. fix the relative paths ----------------------------------------------------------------
say "fixing paths in subcpu/Makefile"
sed -i 's#^SH2_BUILD *= *\.\./d32xr-master/D32XR\.32x#SH2_BUILD   = ../sh2/D32XR.32x#' Makefile
sed -i 's#-I\.\./include#-I../../include#g' Makefile
grep -q '^SH2_BUILD   = ../sh2/D32XR.32x' Makefile || die "could not update SH2_BUILD in subcpu/Makefile"
sed -i 's#the SH2 program is built in \.\./d32xr-master#the SH2 program is built in ../sh2#; s#build d32xr-master first#build ../sh2 first#' Makefile

# ---- 4. tools -----------------------------------------------------------------------------------
[ -d "$SEGA/tools" ] && cp -a "$SEGA/tools/." "$NEW/tools/" && say "tools copied"

# ---- 5. build.sh --------------------------------------------------------------------------------
cat > "$NEW/build.sh" <<'EOF'
#!/bin/bash
# Kobo Deluxe CD32X - full build: SH2 program -> Sub-CPU/68K program -> mixed-mode disc image.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
echo "==> SH2";         cd "$HERE/sh2"    && make clean && make
echo "==> Sub-CPU";     cd "$HERE/subcpu" && make clean && make cd
echo "==> disc image";  python3 make_mixed_cd2.py CDROMPlayer.iso Track02.wav Track03.wav Track04.wav
echo "==> done: disc image is in $HERE/subcpu/"
ls -1 "$HERE/subcpu" | grep -i "mixed" || true
EOF
chmod +x "$NEW/build.sh"

# ---- 6. report anything else that still points outside the new folder --------------------------
say "checking for other relative paths that leave the project folder:"
grep -n '\.\./' "$NEW/sh2/Makefile" "$NEW/subcpu/Makefile" "$NEW/subcpu/files.s" 2>/dev/null \
    | grep -v '\.\./sh2/D32XR\.32x' | grep -v '\.\./\.\./include' || echo "    none"

say "DONE.  New project: $NEW"
say "Build it with:   bash $NEW/build.sh"
say "Demo/ and d32xr-master/ were not changed."
