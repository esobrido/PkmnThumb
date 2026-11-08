#!/usr/bin/env bash
# Packages what is finished as a .mote you can drop on a Thumby Color.
#
#   scripts/export.sh              # -> dist/PolishedCrystal-<version>.mote + manifest
#   scripts/export.sh /tmp/out     # somewhere else
#
# What it does, in order:
#   1. regenerates the data tables (tools/extract.py), so the artefact is never
#      built from stale maps;
#   2. cross-compiles the device module (scripts/build.sh --device);
#   3. checks the module header — magic, embedded version, ABI against the SDK —
#      so a module the handheld would reject never gets packaged;
#   4. stages it with a manifest (sha256, size, version, commit) and the
#      reference screenshot, under the version read from the binary itself.
#
# It installs nothing and uploads nothing. To run it: copy the .mote to the
# device's /mote folder over USB (it mounts as mass storage and the file shows
# up in the MOTE launcher), or with the device docked run `mote push game`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/dist}"
NAME=PolishedCrystal
MOTE="$ROOT/game/build/$NAME.mote"

cd "$ROOT"
python3 tools/extract.py
scripts/build.sh --device

if [ ! -f "$MOTE" ]; then
    echo "export: $MOTE was not produced" >&2
    exit 1
fi

VER="$(python3 tools/moteinfo.py "$MOTE" --json \
    | python3 -c 'import json,sys; print(json.load(sys.stdin)["version"])')"
if [ "$VER" = "0" ]; then
    echo "export: the module carries no version — set MOTE_GAME_VERSION in game/src/game.c" >&2
    exit 1
fi

mkdir -p "$OUT"
DEST="$OUT/$NAME-$VER.mote"
cp "$MOTE" "$DEST"

# Validate the copy that is actually shipped, and describe it next to itself.
python3 tools/moteinfo.py "$DEST" --require-version --manifest "$OUT/manifest.json"

# Reference screenshot at the panel's own size (128x128); the gallery scales it.
SHOT="$ROOT/docs/img/overworld.png"
if [ -f "$SHOT" ]; then
    python3 - "$SHOT" "$OUT/$NAME-$VER.png" <<'PY' 2>/dev/null || cp "$SHOT" "$OUT/$NAME-$VER.png"
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
if im.size != (128, 128):
    im = im.resize((128, 128), Image.NEAREST)
im.save(sys.argv[2])
PY
fi

echo
echo "Packaged in $OUT:"
printf '  %-30s %s\n' "$(basename "$DEST")" "the device module"
printf '  %-30s %s\n' "manifest.json" "size, sha256, version, commit"
[ -f "$OUT/$NAME-$VER.png" ] && \
    printf '  %-30s %s\n' "$NAME-$VER.png" "reference screenshot, 128x128"
cat <<EOF

Install: copy the .mote to the device's /mote folder over USB, or with the
device docked run:  ./vendor/mote/tools/mote push game

Note: this build embeds content extracted from Polished Crystal (Nintendo/Game
Freak IP + fan hack). Keep it personal — do not redistribute it.
EOF
