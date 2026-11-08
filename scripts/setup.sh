#!/usr/bin/env bash
# Sets up the host build of Mote (engine + emulator) without root.
#
# The Mote emulator needs cmake and SDL2. This sandbox runs as an unprivileged
# user with no sudo and no system SDL2, so both are installed under ./toolsrc and
# only ever used from there. Re-run after the worktree is restored: system
# packages and build output do not survive archiving, but the sources do.
#
#   scripts/setup.sh              # cmake + SDL2 + build_host + ARM toolchain
#   scripts/setup.sh --host-only  # skip the ~141 MB ARM toolchain download
#
# The ARM toolchain is what builds the deployable .mote (the host .so is only
# for the emulator). It goes under toolsrc/ like everything else, so nothing is
# installed system-wide.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/toolsrc"
CMAKE_VER=3.30.5
SDL_VER=2.30.9
ARM_VER=13.3.rel1
ARM_DIR="arm-gnu-toolchain-$ARM_VER-x86_64-arm-none-eabi"
HOST_ONLY=""
[ "${1:-}" = "--host-only" ] && HOST_ONLY=1

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

if [ -z "$HOST_ONLY" ] && [ ! -x "$SRC/$ARM_DIR/bin/arm-none-eabi-gcc" ]; then
    echo "== downloading the ARM GNU toolchain $ARM_VER (141 MB, for the .mote)"
    curl -sL -o armgnu.tar.xz \
        "https://developer.arm.com/-/media/Files/downloads/gnu/$ARM_VER/binrel/arm-gnu-toolchain-$ARM_VER-x86_64-arm-none-eabi.tar.xz"
    tar xf armgnu.tar.xz
    rm -f armgnu.tar.xz
fi

echo "== building mote_host"
"$CMAKE" -S "$ROOT/vendor/mote" -B "$ROOT/vendor/mote/build_host" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$SRC/sdl-install" > /dev/null
"$CMAKE" --build "$ROOT/vendor/mote/build_host" --target mote_host -j"$(nproc)"

cat <<EOF

Ready.

  extract data :  python3 tools/extract.py
  build game   :  scripts/build.sh            (host module)
  build device :  scripts/build.sh --device   (deployable .mote)
  unit tests   :  scripts/test.sh
  run (visible):  scripts/run.sh
  screenshot   :  scripts/shot.sh artifacts/overworld.png 45
  perf         :  scripts/perf.sh 900
EOF
