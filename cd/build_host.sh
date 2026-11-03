#!/bin/sh
# Experimental: build the interpreter-based host for the CD release into cd/build/ (does not touch port/build/ outputs).
# Reads the shared port sources and the already-built floppy lift objects (unused at run time: run with NOLIFT=1).
set -e
mkdir -p "$(dirname "$0")/build"
HERE=$(cd "$(dirname "$0")" && pwd); P="$HERE/../port"; cd "$P"
# the interpreter host needs the Musashi 68k core + the compiled floppy lift objects; port/build.sh fetches/builds them (needs git + internet once)
if [ ! -f build/m68kops.c ] || [ ! -d musashi-upstream ] || ! ls build/lift_*.o >/dev/null 2>&1; then
  command -v git >/dev/null 2>&1 || [ -d musashi-upstream ] || { echo "error: git is needed once to fetch the Musashi 68k core (install the package \"git\")"; exit 1; }
  echo "Fetching/building the 68k core (a few minutes the first time) ..."; ./build.sh >/dev/null
fi
SDLC=""; SDLL=""
if command -v sdl2-config >/dev/null 2>&1; then SDLC="-DHAVE_SDL $(sdl2-config --cflags)"; SDLL="$(sdl2-config --libs)"
elif [ -x ../tools/sdl2-install/bin/sdl2-config ]; then Q=$(cd ../tools/sdl2-install && pwd); SDLC="-DHAVE_SDL -I$Q/include/SDL2 -D_REENTRANT"; SDLL="-L$Q/lib -Wl,-rpath,$Q/lib -lSDL2"; fi
LIFTOBJ="build/lift_*.o"; [ -f ../cd/build/lift_cd1.o ] && [ -f ../cd/build/lift_cd_tab.o ] && { LIFTOBJ="../cd/build/lift_cd1.o ../cd/build/lift_cd_tab.o"; echo "using the translated CD code"; }
gcc ${CDDEBUG:--O2} -w $SDLC -DMUSASHI_CNF='"castles_conf.h"' -I. -Imusashi-upstream -Ibuild -o "$HERE/build/host_cd" \
    host.c $LIFTOBJ musashi-upstream/m68kcpu.c build/m68kops.c musashi-upstream/softfloat/softfloat.c $SDLL -lm
echo "built cd/build/host_cd"
