#!/usr/bin/env bash
# Measures the module with Mote's perf() — the same numbers the device shows.
#
#   scripts/perf.sh              # 600 frames, walking east
#   scripts/perf.sh 1200         # more frames
#
# Runs the real module in the host emulator with the dummy video driver and
# prints the perf lines the game logs once a second:
#
#   perf fps=60 update_us=120 raster_us=830 flush_us=210 core0%=42 core1%=38
#
# `raster_us` is the banded map pass (the expensive one), `update_us` the game
# logic, `flush_us` the frame upload. On the host these are indicative only: the
# device runs at 240 MHz and the banded raster is split across two cores, so
# re-measure on hardware (docs/05-dev-workflow.md).
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export LD_LIBRARY_PATH="$ROOT/toolsrc/sdl-install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

FRAMES="${1:-600}"
SO="$ROOT/game/build/PolishedCrystal.so"

[ -f "$SO" ] || "$ROOT/scripts/build.sh" >/dev/null

echo "frames $FRAMES (walking east; the launcher boot takes the first ~5)"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    MOTE_KEYS="a:2-5,right:10-$FRAMES" \
    MOTE_SHOT="/tmp/pc_perf.ppm" MOTE_SHOT_FRAME="$FRAMES" \
    "$ROOT/vendor/mote/build_host/mote_host" "$SO" 2>/dev/null |
    grep '^perf ' | tail -5
