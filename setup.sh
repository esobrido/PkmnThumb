#!/usr/bin/env bash
# Sets up the host build of Mote (engine + emulator) without root.
#
# The Mote emulator needs cmake and SDL2. This sandbox runs as an unprivileged
# user with no sudo and no system SDL2, so both are installed under ./toolsrc and
# only ever used from there. Re-run after the worktree is restored: system
# packages and build output do not survive archiving, but the sources do.
#
#   ./setup.sh          # cmake + SDL2 + build_host
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/toolsrc"
CMAKE_VER=3.30.5
SDL_VER=2.30.9

mkdir -p "$SRC"
cd "$SRC"

if [ ! -x "cmake-$CMAKE_VER-linux-x86_64/bin/cmake" ]; then
    echo "== downloading cmake $CMAKE_VER"
    curl -sL -o cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-linux-x86_64.tar.gz"
    tar xzf cmake.tar.gz
fi
CMAKE="$SRC/cmake-$CMAKE_VER-linux-x86_64/bin/cmake"

if [ ! -f sdl-install/lib/libSDL2.so ]; then
    echo "== building SDL2 $SDL_VER (dummy video/audio only — this is a headless box)"
    [ -d "SDL2-$SDL_VER" ] || {
        curl -sL -o SDL2.tar.gz \
            "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
        tar xzf SDL2.tar.gz
    }
    "$CMAKE" -S "SDL2-$SDL_VER" -B sdl-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$SRC/sdl-install" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
        -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF \
        -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF \
        -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_HIDAPI=OFF \
        -DSDL_VIVANTE=OFF -DSDL_DIRECTFB=OFF > /tmp/sdl_cfg.log 2>&1
    "$CMAKE" --build sdl-build -j"$(nproc)" >> /tmp/sdl_cfg.log 2>&1
    "$CMAKE" --install sdl-build >> /tmp/sdl_cfg.log 2>&1
fi

echo "== building mote_host"
"$CMAKE" -S "$ROOT/vendor/mote" -B "$ROOT/vendor/mote/build_host" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$SRC/sdl-install" > /dev/null
"$CMAKE" --build "$ROOT/vendor/mote/build_host" --target mote_host -j"$(nproc)"

cat <<EOF

Ready.

  extract data :  python3 tools/extract.py
  build game   :  LD_LIBRARY_PATH=$SRC/sdl-install/lib ./vendor/mote/tools/mote build game
  run (visible):  ./run.sh
  screenshot   :  ./shot.sh artifacts/overworld.png 45
EOF
