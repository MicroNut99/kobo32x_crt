#!/bin/bash
# Kobo Deluxe 32X - the STANDALONE cartridge (KOBO32X_STANDALONE.32x): no Sega CD needed - music in the
# ROM, high scores and options saved in the cartridge.  Runs build.sh with STANDALONE=1.
# Afterwards run `bash build.sh` again if you want sh2/D32XR.* to be the normal cartridge's.
STANDALONE=1 exec bash "$(cd "$(dirname "$0")" && pwd)/build.sh" "$@"
