#!/bin/sh
# Standalone build: no Musashi, no 68k interpreter. All guest code runs as lifted C (gen/lift_*.c).
# Needs only gcc (+ SDL2 for video/audio). Produces build/castles_native.
set -e
cd "$(dirname "$0")"
SDLC=""; SDLL=""
if command -v sdl2-config >/dev/null 2>&1; then SDLC="-DHAVE_SDL $(sdl2-config --cflags)"; SDLL="$(sdl2-config --libs)"
elif [ -x ../tools/sdl2-install/bin/sdl2-config ]; then P=$(cd ../tools/sdl2-install && pwd); SDLC="-DHAVE_SDL -I$P/include/SDL2 -D_REENTRANT"; SDLL="-L$P/lib -Wl,-rpath,$P/lib -lSDL2"; fi
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
gcc -O2 -w $SDLC -DCASTLES_NATIVE_BUILD -I native -I. -Ibuild -o build/castles_native host.c build/lift_*.o $SDLL -lm
echo "built build/castles_native   (run: build/castles_native path/to/rsrc.fork [game folder])"
