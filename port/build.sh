#!/bin/sh
# Build the Castles host. Requires: gcc, git. Fetches Musashi (68k core) if missing.
set -e
cd "$(dirname "$0")"
[ -d musashi-upstream ] || git clone --depth 1 https://github.com/kstenerud/Musashi.git musashi-upstream
mkdir -p build
[ -f castles_conf.h ] || { echo "castles_conf.h missing"; exit 1; }
gcc -O2 -o build/m68kmake musashi-upstream/m68kmake.c
(cd build && ./m68kmake . ../musashi-upstream/m68k_in.c)
SDLC=""; SDLL=""
if command -v sdl2-config >/dev/null 2>&1; then SDLC="-DHAVE_SDL $(sdl2-config --cflags)"; SDLL="$(sdl2-config --libs)"
elif [ -x ../tools/sdl2-install/bin/sdl2-config ]; then P=$(cd ../tools/sdl2-install && pwd); SDLC="-DHAVE_SDL -I$P/include/SDL2 -D_REENTRANT"; SDLL="-L$P/lib -Wl,-rpath,$P/lib -lSDL2"; fi
./build_lift.sh
gcc -O2 -w $SDLC -DMUSASHI_CNF='"castles_conf.h"' -I. -Imusashi-upstream -Ibuild -o build/castles_host \
    host.c build/lift_*.o musashi-upstream/m68kcpu.c build/m68kops.c musashi-upstream/softfloat/softfloat.c $SDLL -lm
echo "built build/castles_host   (run: build/castles_host path/to/rsrc.fork)"
