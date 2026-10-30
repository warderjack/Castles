#!/bin/sh
# Standalone build: no Musashi, no 68k interpreter. All guest code runs as lifted C (gen/lift_*.c).
# Needs only gcc (+ SDL2 for video/audio). Produces build/castles_native.
set -e
cd "$(dirname "$0")"
SDLC=""; SDLL=""
if command -v sdl2-config >/dev/null 2>&1; then SDLC="-DHAVE_SDL $(sdl2-config --cflags)"; SDLL="$(sdl2-config --libs)"
elif [ -x ../tools/sdl2-install/bin/sdl2-config ]; then P=$(cd ../tools/sdl2-install && pwd); SDLC="-DHAVE_SDL -I$P/include/SDL2 -D_REENTRANT"; SDLL="-L$P/lib -Wl,-rpath,$P/lib -lSDL2"; fi
./build_lift.sh
gcc -O2 -w $SDLC -DCASTLES_NATIVE_BUILD -I native -I. -Ibuild -o build/castles_native host.c build/lift_*.o $SDLL -lm
echo "built build/castles_native   (run: build/castles_native path/to/rsrc.fork [game folder])"
