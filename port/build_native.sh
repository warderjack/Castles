#!/bin/sh
# Standalone build: no Musashi, no 68k interpreter. All guest code runs as lifted C (gen/lift_*.c).
# Needs only gcc (+ SDL2 for video/audio). Produces build/castles_native.
set -e
cd "$(dirname "$0")"
CC=${CC:-$(command -v gcc || command -v cc || command -v clang)}
[ -n "$CC" ] || { echo "error: no C compiler found (install gcc or clang)"; exit 1; }
export CC
SDLC=""; SDLL=""
if command -v sdl2-config >/dev/null 2>&1; then SDLC="-DHAVE_SDL $(sdl2-config --cflags)"; SDLL="$(sdl2-config --libs)"
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2 2>/dev/null; then SDLC="-DHAVE_SDL $(pkg-config --cflags sdl2)"; SDLL="$(pkg-config --libs sdl2)"
elif [ -x ../tools/sdl2-install/bin/sdl2-config ]; then P=$(cd ../tools/sdl2-install && pwd); SDLC="-DHAVE_SDL -I$P/include/SDL2 -D_REENTRANT"; SDLL="-L$P/lib -Wl,-rpath,$P/lib -lSDL2"; fi
if [ -z "$SDLC" ] && [ -z "$NO_SDL" ]; then
  echo "error: SDL2 development files not found (needed for the window and sound)."
  echo "       Run ../install.sh to install dependencies, or install your distro's SDL2 dev package"
  echo "       (Debian/Ubuntu/Mint: libsdl2-dev, Fedora: SDL2-devel, Arch: sdl2, openSUSE: libSDL2-devel, Alpine: sdl2-dev)."
  echo "       To build a window-less test binary anyway: NO_SDL=1 ./build_native.sh"
  exit 1
fi
# Optional pixel-exact classic Mac fonts: drop the fonts .zip (or ChicagoFLF.ttf + geneva_9.ttf) into <repo>/fonts/ and rebuild.
FD=../fonts
if [ -d "$FD" ]; then
  for z in "$FD"/*.zip; do [ -f "$z" ] && [ ! -d "$FD/_unz_$(basename "$z" .zip)" ] && command -v unzip >/dev/null 2>&1 && unzip -qn "$z" -d "$FD/_unz_$(basename "$z" .zip)" || true; done
  CH=$(find "$FD" -iname 'ChicagoFLF.ttf' 2>/dev/null | head -1); GV=$(find "$FD" -iname 'geneva_9.ttf' 2>/dev/null | head -1)
  if [ -n "$CH" ] && [ -n "$GV" ] && command -v python3 >/dev/null 2>&1; then
    T=$(mktemp -d); cp "$CH" "$T/ChicagoFLF.ttf"; cp "$GV" "$T/geneva_9.ttf"
    if python3 gen/genfont_local.py "$T" >/dev/null 2>&1; then echo "using local classic Mac fonts from $FD"; else echo "note: fonts found in $FD but conversion failed (needs python3 + Pillow: pip install pillow); using bundled fonts"; rm -f fontdata_local.h; fi
    rm -rf "$T"
  fi
fi
./build_lift.sh
"$CC" -O2 -w $SDLC -DCASTLES_NATIVE_BUILD -I native -I. -Ibuild -o build/castles_native host.c build/lift_*.o $SDLL -lm
echo "built build/castles_native   (run: build/castles_native path/to/rsrc.fork [game folder])"
