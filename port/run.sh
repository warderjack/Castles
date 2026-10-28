#!/bin/sh
# build + run + show tail of the log
cd "$(dirname "$0")"
SDLC=""; SDLL=""
if command -v sdl2-config >/dev/null 2>&1; then SDLC="-DHAVE_SDL $(sdl2-config --cflags)"; SDLL="$(sdl2-config --libs)"
elif [ -x ../tools/sdl2-install/bin/sdl2-config ]; then P=$(cd ../tools/sdl2-install && pwd); SDLC="-DHAVE_SDL -I$P/include/SDL2 -D_REENTRANT"; SDLL="-L$P/lib -Wl,-rpath,$P/lib -lSDL2"; fi
gcc -O2 -w $SDLC -DMUSASHI_CNF='"castles_conf.h"' -I. -Imusashi-upstream -Ibuild -o build/castles_host host.c musashi-upstream/m68kcpu.c build/m68kops.c musashi-upstream/softfloat/softfloat.c $SDLL -lm 2>&1 | head -30
(CASTLES_HEADLESS=1 timeout ${T:-60} ./build/castles_host ../work/castles/rsrc.fork "../extract/Castles - Siege and Conquest" > build/run.log 2>&1; true)
grep -c "^\[trap" build/run.log | sed 's/^/traps logged: /'
tail -${N:-5} build/run.log | cut -c1-260
