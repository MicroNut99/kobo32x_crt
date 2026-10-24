#!/bin/bash
# build_fresh.sh - kept for old instructions: build.sh now works from a fresh download too, so this just runs it.
exec bash "$(cd "$(dirname "$0")" && pwd)/build.sh" "$@"
