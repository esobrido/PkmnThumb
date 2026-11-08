#!/usr/bin/env bash
# Run the game in the Mote emulator window (needs a display; won't work headless).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export LD_LIBRARY_PATH="$ROOT/toolsrc/sdl-install/lib"
"$ROOT/scripts/build.sh"
exec "$ROOT/vendor/mote/build_host/mote_host" "$ROOT/game/build/PolishedCrystal.so"
