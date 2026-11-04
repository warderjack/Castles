#!/bin/sh
# EXPERIMENTAL: set up the Castles CD-ROM release (MacPlay "Enhanced CD-ROM") next to the floppy port.
#   ./cd/install_cd.sh /path/to/CD.cue|.iso|.bin|.zip      (the floppy install is not touched)
# Stand-alone: needs neither ./install.sh nor the floppy game files. Adds a menu entry, a Desktop icon and the command castles-cd.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd); IMG="$1"
say() { printf '%s\n' "$*"; }

# ---- build tools (same idea as install.sh): gcc, SDL2 headers, python3, git ----
have_sdl() { command -v sdl2-config >/dev/null 2>&1 || { command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2 2>/dev/null; }; }
MISS=""; { command -v gcc >/dev/null 2>&1 || command -v cc >/dev/null 2>&1; } || MISS="$MISS gcc"; have_sdl || MISS="$MISS SDL2-dev"
command -v python3 >/dev/null 2>&1 || MISS="$MISS python3"; command -v git >/dev/null 2>&1 || MISS="$MISS git"
if [ -n "$MISS" ]; then
  ID=""; LIKE=""; [ -r /etc/os-release ] && . /etc/os-release && ID="$ID" && LIKE="$ID_LIKE"; CMD=""
  case " $ID $LIKE " in
    *" debian "*|*" ubuntu "*|*" linuxmint "*) CMD="apt-get install -y build-essential libsdl2-dev python3 git unzip";;
    *" fedora "*|*" rhel "*|*" centos "*) CMD="dnf install -y gcc make SDL2-devel python3 git unzip";;
    *" arch "*|*" manjaro "*) CMD="pacman -S --needed --noconfirm base-devel sdl2 python git unzip";;
    *" suse "*|*" opensuse "*) CMD="zypper install -y gcc make SDL2-devel python3 git unzip";;
  esac
  say "Missing:$MISS"; SUDO=""; [ "$(id -u)" != 0 ] && SUDO="sudo"
  [ -n "$CMD" ] || { say "Please install a C compiler, the SDL2 development files, python3 and git, then run this again."; exit 1; }
  say "This needs:  $SUDO $CMD"; printf 'Run it now? [Y/n] '; read -r a || a=y
  case "$a" in n|N|no|NO) exit 1;; esac
  $SUDO sh -c "$CMD" || { say "That failed (try updating your package list first, e.g. sudo apt update)."; exit 1; }
fi
[ -n "$IMG" ] || { say "usage: cd/install_cd.sh CD-image.cue|.iso|.bin|.zip   (or the folder that holds it)"; exit 2; }
if [ -d "$IMG" ]; then   # a folder: pick the CD image or zip inside it
  F=$(find "$IMG" -maxdepth 3 -type f \( -iname '*.cue' -o -iname '*.iso' -o -iname '*.zip' \) | head -1)
  [ -n "$F" ] || F=$(find "$IMG" -maxdepth 3 -type f -iname '*.bin' -size +50000k | head -1)
  [ -n "$F" ] && { say "Using $F"; IMG="$F"; }
fi
[ -f "$IMG" ] || { say "Not found: $IMG"; say "Give the real path of the CD .cue/.iso/.bin/.zip (or the folder holding it). Try:  find ~ -iname '*.cue' -o -iname 'CDMCR*'"; exit 2; }

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
"$HERE/cd/build_host.sh"
say "Translating the game code to C for speed (optional, about a minute) ..."
"$HERE/cd/lift_build.sh" "$G/Castles CD.rsrc" || say "(skipped: the game still runs, just a little slower)"
BIN="${XDG_BIN_HOME:-$HOME/.local/bin}"; mkdir -p "$BIN"
"$HERE/cd/install_launcher.sh" "$G"
say ""; say "Done. Start it from the application menu (Games), the Desktop icon, or:  $BIN/castles-cd"
