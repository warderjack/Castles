#!/bin/sh
# Build the Castles host. Requires: gcc, git. Fetches Musashi (68k core) if missing.
set -e
cd "$(dirname "$0")"
[ -d musashi-upstream ] || git clone --depth 1 https://github.com/kstenerud/Musashi.git musashi-upstream
mkdir -p build
[ -f castles_conf.h ] || { echo "castles_conf.h missing"; exit 1; }
gcc -O2 -o build/m68kmake musashi-upstream/m68kmake.c
(cd build && ./m68kmake . ../musashi-upstream/m68k_in.c)
gcc -O2 -w -DMUSASHI_CNF='"castles_conf.h"' -I. -Imusashi-upstream -Ibuild -o build/castles_host \
    host.c musashi-upstream/m68kcpu.c build/m68kops.c musashi-upstream/softfloat/softfloat.c -lm
echo "built build/castles_host   (run: build/castles_host path/to/rsrc.fork)"
