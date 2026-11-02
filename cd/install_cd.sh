#!/bin/sh
# EXPERIMENTAL: set up the Castles CD-ROM release (MacPlay "Enhanced CD-ROM") next to the floppy port.
#   ./cd/install_cd.sh /path/to/CD.cue|.iso|.bin|.zip      (the floppy install is not touched)
# Needs ./install.sh to have been run once (it builds the shared pieces). Start the game afterwards with:  castles-cd
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd); IMG="$1"
say() { printf '%s\n' "$*"; }
[ -n "$IMG" ] || { say "usage: cd/install_cd.sh CD-image.cue|.iso|.bin|.zip   (or the folder that holds it)"; exit 2; }
if [ -d "$IMG" ]; then   # a folder: pick the CD image or zip inside it
  F=$(find "$IMG" -maxdepth 3 -type f \( -iname '*.cue' -o -iname '*.iso' -o -iname '*.zip' \) | head -1)
  [ -n "$F" ] || F=$(find "$IMG" -maxdepth 3 -type f -iname '*.bin' -size +50000k | head -1)
  [ -n "$F" ] && { say "Using $F"; IMG="$F"; }
fi
[ -f "$IMG" ] || { say "Not found: $IMG"; say "Give the real path of the CD .cue/.iso/.bin/.zip (or the folder holding it). Try:  find ~ -iname '*.cue' -o -iname 'CDMCR*'"; exit 2; }
[ -f "$HERE/port/build/lift_seg1.o" ] || { say "Run ./install.sh first (it builds the shared parts)."; exit 1; }
OUT="$HOME/Castles-game/CD"; rm -rf "$OUT"; mkdir -p "$OUT"
case "$IMG" in
  *.zip|*.ZIP) command -v unzip >/dev/null 2>&1 || { say "Please install 'unzip'."; exit 1; }
    say "Unzipping ..."; unzip -q -o "$IMG" -d "$OUT/_z"
    IMG=$(find "$OUT/_z" -type f \( -iname '*.iso' -o -iname '*.bin' \) -size +50000k | head -1)
    [ -n "$IMG" ] || { say "No CD image (.bin/.iso) inside the zip."; exit 1; };;
  *.cue|*.CUE) f=$(sed -n 's/^[[:space:]]*FILE[[:space:]]*"\(.*\)".*/\1/p' "$IMG" | head -1); IMG="$(dirname "$IMG")/$f";;
esac
CC=${CC:-$(command -v gcc || command -v cc)}
HX="$HERE/port/build/hfsx"; { [ -x "$HX" ] && [ ! "$HERE/port/hfsx.c" -nt "$HX" ]; } || "$CC" -O2 -w -o "$HX" "$HERE/port/hfsx.c"
say "Reading the CD image (this can take a minute) ..."
HFSX_LIST=1 "$HX" "$IMG" "$OUT/_l" > "$OUT/list.txt" 2> "$OUT/hfsx.log" || { tail -n 4 "$OUT/hfsx.log"; exit 1; }
P=$(awk '{ l = tolower($0); i = index(l, "castles cd data/"); if (i) { print substr($0, 1, i - 1); exit } }' "$OUT/list.txt")
HFSX_FILTER="${P}Castle" "$HX" "$IMG" "$OUT/_x" >/dev/null 2>>"$OUT/hfsx.log"
SRC=$(find "$OUT/_x" -type d -name 'Castles CD Data' | head -1)
[ -n "$SRC" ] || { say "No \"Castles CD Data\" folder on that disc (is this the CD release?)."; exit 1; }
mv "$(dirname "$SRC")" "$OUT/Castles"; rm -rf "$OUT/_x" "$OUT/_l" "$OUT/_z"
G="$OUT/Castles"; [ -f "$G/Castles CD.rsrc" ] || { say "The program file \"Castles CD\" was not found."; exit 1; }
say "Building the CD host ..."
"$HERE/cd/build_host.sh" >/dev/null
BIN="${XDG_BIN_HOME:-$HOME/.local/bin}"; mkdir -p "$BIN"
cat > "$BIN/castles-cd" <<EOF
#!/bin/sh
# Castles CD-ROM release (experimental interpreter host)
cd "$HERE/port" 2>/dev/null
NOLIFT=1 exec "$HERE/cd/build/host_cd" "$G/Castles CD.rsrc" "$G" "\$@"
EOF
chmod +x "$BIN/castles-cd"
say "Done. Start it with:  $BIN/castles-cd"
