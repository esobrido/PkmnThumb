#!/usr/bin/env bash
# Headless screenshot — the CI/dev way to see the port without a display.
#
#   scripts/shot.sh out.png [frame] [keys] [scale]
#
#   frame  which frame to capture (default 45)
#   keys   MOTE_KEYS script, e.g. "a:2-5,left:20-70" to press A then walk left
#   scale  integer upscale for viewing (default 3)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-artifacts/shot.png}"
frame="${2:-45}"
keys="${3:-a:2-5}"
scale="${4:-3}"

export LD_LIBRARY_PATH="$ROOT/toolsrc/sdl-install/lib"
# rebuild first: a stale .so silently screenshots the previous build
"$ROOT/scripts/build.sh" >/dev/null

tmp="$(mktemp /tmp/moteshotXXXX.ppm)"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    MOTE_KEYS="$keys" MOTE_SHOT="$tmp" MOTE_SHOT_FRAME="$frame" \
    "$ROOT/vendor/mote/build_host/mote_host" "$ROOT/game/build/PolishedCrystal.so" >/dev/null

mkdir -p "$(dirname "$out")"
python3 - "$tmp" "$out" "$scale" <<'PY'
import sys
from PIL import Image
src, dst, scale = sys.argv[1], sys.argv[2], int(sys.argv[3])
im = Image.open(src).convert("RGB")
im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(dst)
print(dst)
PY
rm -f "$tmp"
