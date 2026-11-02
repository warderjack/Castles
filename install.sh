#!/bin/sh
# One-step setup for Castles: Siege & Conquest (native Linux port).
#   ./install.sh                   check/install build tools, build, find your game files, add a launcher
#   ./install.sh --game DIR        use this game folder (the one containing "Castles Data", "Castles Movies", ...)
#   ./install.sh --yes             do not ask questions (installs missing packages with sudo)
#   ./install.sh --no-deps         never try to install packages
#   ./install.sh --no-shortcut     do not put an icon on the Desktop
#   ./install.sh --uninstall       remove the launcher and menu entry (keeps your game files)
# Afterwards start the game with the command `castles` or from your application menu (Games).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
YES=0; DEPS=1; GAME=""; UNINSTALL=0; NOSHORT=0; SHORT=0
while [ $# -gt 0 ]; do
  case "$1" in
    --yes|-y) YES=1;; --no-shortcut) NOSHORT=1;; --no-deps) DEPS=0;; --uninstall) UNINSTALL=1;;
    --game) shift; GAME="$1";; --game=*) GAME="${1#--game=}";;
    -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
    *) echo "unknown option: $1 (try --help)"; exit 2;;
  esac; shift
done
BIN="${XDG_BIN_HOME:-$HOME/.local/bin}"; CFG="${XDG_CONFIG_HOME:-$HOME/.config}/castles"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
say() { printf '%s\n' "$*"; }
ask() { # ask "question" -> 0 for yes (default yes)
  [ "$YES" = 1 ] && return 0; printf '%s [Y/n] ' "$1"; read -r a || a=y; case "$a" in n|N|no|NO) return 1;; *) return 0;; esac
}

if [ "$UNINSTALL" = 1 ]; then
  DESK=""; command -v xdg-user-dir >/dev/null 2>&1 && DESK=$(xdg-user-dir DESKTOP 2>/dev/null); [ -z "$DESK" ] && DESK="$HOME/Desktop"
  rm -f "$BIN/castles" "$APPS/castles.desktop" "$DESK/castles.desktop" "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps/castles.png"; say "Removed launcher and menu entry. (Settings kept in $CFG; game files untouched.)"; exit 0
fi

# ---- 1. build tools ----------------------------------------------------------------------
have_cc() { command -v gcc >/dev/null 2>&1 || command -v cc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1; }
have_sdl() { command -v sdl2-config >/dev/null 2>&1 || { command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2 2>/dev/null; }; }
if ! have_cc || ! have_sdl; then
  ID=""; LIKE=""; [ -r /etc/os-release ] && . /etc/os-release && ID="$ID" && LIKE="$ID_LIKE"
  CMD=""
  case " $ID $LIKE " in
    *" debian "*|*" ubuntu "*|*" linuxmint "*) CMD="apt-get install -y build-essential libsdl2-dev";;
    *" fedora "*|*" rhel "*|*" centos "*) CMD="dnf install -y gcc make SDL2-devel";;
    *" arch "*|*" manjaro "*) CMD="pacman -S --needed --noconfirm base-devel sdl2";;
    *" suse "*|*" opensuse "*) CMD="zypper install -y gcc make SDL2-devel";;
    *" alpine "*) CMD="apk add build-base sdl2-dev";;
    *" void "*) CMD="xbps-install -Sy gcc make SDL2-devel";;
    *" gentoo "*) CMD="emerge --noreplace sys-devel/gcc media-libs/libsdl2";;
  esac
  say "Missing: $(have_cc || printf 'C compiler  ')$(have_sdl || printf 'SDL2 development files')"
  if [ -z "$CMD" ]; then
    say "Could not tell which package manager to use. Please install a C compiler (gcc) and the SDL2"
    say "development package for your distribution, then run ./install.sh again."; exit 1
  fi
  SUDO=""; [ "$(id -u)" != 0 ] && SUDO="sudo"
  if [ "$DEPS" = 0 ]; then say "Run:  $SUDO $CMD"; exit 1; fi
  say "This needs:  $SUDO $CMD"
  if ask "Run it now?"; then $SUDO sh -c "$CMD" || { say "That failed. If your package list is out of date, update it first (e.g. apt update, or pacman -Syu on Arch) and run ./install.sh again."; exit 1; }; else say "Install those packages, then run ./install.sh again."; exit 1; fi
  have_cc && have_sdl || { say "Packages still missing after install; see messages above."; exit 1; }
fi

# ---- 2. build ----------------------------------------------------------------------------
say "Building (about a minute)..."
( cd "$HERE/port" && ./build_native.sh ) || { say "Build failed. Please report the messages above."; exit 1; }
EXE="$HERE/port/build/castles_native"

# ---- 3. find the game files --------------------------------------------------------------
has_app() { for n in "Castles - Siege & Conquest" "Castles - Siege and Conquest" "Castles"; do [ -f "$1/$n.bin" ] && return 0; done; return 1; }
valid() { [ -d "$1" ] && has_app "$1" && [ -d "$1/Castles Data" ]; }
pm_cmd() { # pm_cmd <what>: install command for the extraction tool on this distribution
  ID=""; LIKE=""; [ -r /etc/os-release ] && . /etc/os-release && LIKE="$ID_LIKE"
  case " $ID $LIKE " in
    *" debian "*|*" ubuntu "*|*" linuxmint "*) echo "apt-get install -y unar";;
    *" fedora "*|*" rhel "*|*" centos "*) echo "dnf install -y unar";;
    *" arch "*|*" manjaro "*) echo "pacman -S --needed --noconfirm unarchiver";;
    *" suse "*|*" opensuse "*) echo "zypper install -y unar";;
    *" alpine "*) echo "apk add unar";;
    *" void "*) echo "xbps-install -Sy unarchiver";;
    *" gentoo "*) echo "emerge --noreplace app-arch/unar";;
  esac
}
diagnose() { # explain what is missing from a game folder
  D="$1"
  if [ ! -d "$D" ]; then say "  - $D is not a folder."; return; fi
  has_app "$D" || say "  - the game program (\"Castles - Siege & Conquest.bin\") is missing"
  [ -d "$D/Castles Data" ] || say "  - the \"Castles Data\" folder is missing (required)"
  [ -d "$D/Castles Movies" ] || say "  - note: no \"Castles Movies\" folder, so the game will run without movies"
  [ -d "$D/Castle Plans" ] || say "  - note: no \"Castle Plans\" folder (castle design plans will be unavailable)"
  if ! has_app "$D" && { [ -f "$D/Castles - Siege & Conquest" ] || [ -f "$D/Castles - Siege and Conquest" ] || find "$D" -maxdepth 1 \( -name 'Castles*.rsrc' -o -name '._Castles*' \) 2>/dev/null | grep -q .; }; then
    say "  This looks like files unpacked from a Mac archive without their resource forks joined to the program."
    say "  I can try to repair that (it does not change your original archive)."; FIXABLE=1
  fi
}
hfsx_tool() { [ -x "$HERE/port/build/hfsx" ] || { mkdir -p "$HERE/port/build"; "${CC:-$(command -v gcc || command -v cc || command -v clang)}" -O2 -w -o "$HERE/port/build/hfsx" "$HERE/port/hfsx.c"; }; echo "$HERE/port/build/hfsx"; }
macbin_tool() { [ -x "$HERE/port/build/macbin" ] || "${CC:-$(command -v gcc || command -v cc || command -v clang)}" -O2 -w -o "$HERE/port/build/macbin" "$HERE/port/macbin.c"; echo "$HERE/port/build/macbin"; }
convert_forks() { # join Mac resource forks (._name AppleDouble, name.rsrc, or __MACOSX/...) into MacBinary name.bin files
  ROOT="$1"; MB=$(macbin_tool); n=0
  find "$ROOT" -type f ! -name '._*' ! -name '*.rsrc' ! -name '*.bin' 2>/dev/null | while read -r f; do
    d=$(dirname "$f"); b=$(basename "$f"); rel="${d#$ROOT}"; r=""
    for c in "$d/._$b" "$d/$b.rsrc" "$ROOT/__MACOSX$rel/._$b" "$ROOT/../__MACOSX/$(basename "$ROOT")$rel/._$b"; do [ -f "$c" ] && { r="$c"; break; }; done
    [ -n "$r" ] || continue
    "$MB" "$f" "$f.bin" "$r" && rm -f "$f" && echo x >> "$ROOT/.converted"
  done
  # resource-fork-only files (e.g. the game program: its data fork is empty, so the unpacker wrote only name.rsrc)
  find "$ROOT" -type f \( -name '*.rsrc' -o -name '._*' \) 2>/dev/null | while read -r r; do
    d=$(dirname "$r"); b=$(basename "$r")
    case "$b" in ._*) base="$d/${b#._}";; *) base="$d/${b%.rsrc}";; esac
    [ -e "$base" ] || [ -e "$base.bin" ] && continue
    "$MB" "$base" "$base.bin" "$r"
  done
  find "$ROOT" -name '._*' -type f -delete 2>/dev/null; find "$ROOT" -name '*.rsrc' -type f -delete 2>/dev/null
  rm -rf "$ROOT/__MACOSX" 2>/dev/null; rm -f "$ROOT/.converted"
}
cd_extract() { # cd_extract IMAGE -> game files from a Mac CD image into $OUT/_cd
  HX=$(hfsx_tool); say "Reading the CD image $(basename "$1") (this can take a minute) ..."
  HFSX_LIST=1 "$HX" "$1" "$OUT/_cd" > "$OUT/_cdlist.txt" 2> "$OUT/hfsx.log" || { say "Could not read the CD image:"; tail -n 4 "$OUT/hfsx.log" | sed 's/^/    /'; return; }
  P=$(awk '{ l = tolower($0); i = index(l, "castles data/"); if (i) { print substr($0, 1, i - 1); exit } }' "$OUT/_cdlist.txt")
  if [ -z "$P" ] && ! grep -qi '^castles data/' "$OUT/_cdlist.txt"; then say "The CD has no \"Castles Data\" folder. Files on it start like this:"; head -n 15 "$OUT/_cdlist.txt" | sed 's/^/    /'; return; fi
  say "  game found on the disc in: \"${P:-(top level)}\"; copying it out ..."
  HFSX_FILTER="${P}Castle" "$HX" "$1" "$OUT/_cd" > /dev/null 2>> "$OUT/hfsx.log" || { say "Copying from the CD failed:"; tail -n 4 "$OUT/hfsx.log" | sed 's/^/    /'; return; }
}
cd_image_of() { # prints the CD image file for a .cue/.iso or a big .bin, nothing otherwise
  case "$1" in
    *.cue|*.CUE) f=$(sed -n 's/^[[:space:]]*FILE[[:space:]]*"\(.*\)".*/\1/p' "$1" | head -1); [ -n "$f" ] && [ -f "$(dirname "$1")/$f" ] && echo "$(dirname "$1")/$f";;
    *.iso|*.ISO) echo "$1";;
    *.bin|*.BIN) [ "$(wc -c < "$1" 2>/dev/null || echo 0)" -gt 50000000 ] && echo "$1";;
  esac
}
unpack_archive() { # unpack_archive FILE -> sets GAME to the unpacked game folder (empty on failure)
  A="$1"; GAME=""; ARCHDIR=$(dirname "$A"); ARCHNAME="$A"; CDIMG=$(cd_image_of "$A")
  if [ -z "$CDIMG" ] && ! command -v unar >/dev/null 2>&1; then
    UC=$(pm_cmd); SUDO=""; [ "$(id -u)" != 0 ] && SUDO="sudo"
    if [ -n "$UC" ] && [ "$DEPS" = 1 ]; then say "To unpack Mac archives I need the tool 'unar':  $SUDO $UC"; ask "Install it now?" && $SUDO sh -c "$UC"; fi
  fi
  [ -n "$CDIMG" ] || command -v unar >/dev/null 2>&1 || { say "Can't unpack $A without the 'unar' tool. Install it (package \"unar\" or \"unarchiver\") and run ./install.sh again."; return; }
  OUT="$HOME/Castles-game/$(basename "$A" | sed 's/\.[^.]*$//')"; rm -rf "$OUT"; mkdir -p "$OUT"
  if [ -n "$CDIMG" ]; then cd_extract "$CDIMG"
  else
    say "Unpacking $A into $OUT ..."
    unar -q -f -o "$OUT" "$A" >/dev/null 2>&1 || { say "Could not unpack $A. If it is a disk image (.dmg) copy the game files out of it first, or extract it with another program."; return; }
  fi
  conv=$(find "$OUT" -type d -name 'Castles Data' 2>/dev/null | head -1)
  if [ -z "$conv" ] && find "$OUT" -type f -iname '*.image' 2>/dev/null | grep -q .; then
    # the floppy release: Disk Copy images of Mac disks, each holding one piece of a StuffIt installer. Read the disks, drop the
    # 100-byte header of each piece, join the pieces and unpack the result.
    say "This is the floppy-disk set (disk images); reading the disks and joining the installer pieces..."
    HX=$(hfsx_tool); DD=$(dirname "$(find "$OUT" -type f -iname '*.image' | head -1)"); J="$OUT/_joined.sit"; : > "$J"; NP=0
    for f in $(ls "$DD" | grep -i '\.image$' | sed 's/ /@@/g' | sort -V); do
      f=$(printf '%s' "$f" | sed 's/@@/ /g'); T="$OUT/_disks/$(printf '%s' "$f" | tr -c 'A-Za-z0-9\n' _)"
      "$HX" "$DD/$f" "$T" >/dev/null 2>&1 || { say "  could not read $f"; continue; }
      big=$(ls -S "$T" | grep -v '\.rsrc$' | head -1); [ -n "$big" ] || continue
      tail -c +101 "$T/$big" >> "$J"; NP=$((NP+1))
    done
    say "  joined $NP disk(s); unpacking the installer inside..."
    if unar -q -f -o "$OUT/_inner" "$J" > "$OUT/unar.log" 2>&1; then rm -rf "$J" "$OUT/_disks"
    else
      say "Could not unpack the joined disks. unar said:"; tail -n 5 "$OUT/unar.log" | sed 's/^/    /'
      say "  First bytes of the joined file:"; head -c 64 "$J" | od -An -c | head -4 | sed 's/^/    /'
    fi
    conv=$(find "$OUT/_inner" -type d -name 'Castles Data' 2>/dev/null | head -1)
  fi
  if [ -z "$conv" ]; then
    # a CD image (.cue/.bin, .iso): read the Mac disc directly and take out only the game's folder
    IMG=$(find "$OUT" -type f \( -iname '*.iso' -o -iname '*.bin' -o -iname '*.img' \) -size +50000k 2>/dev/null | head -1)
    [ -n "$IMG" ] && cd_extract "$IMG"
    conv=$(find "$OUT/_cd" -type d -name 'Castles Data' 2>/dev/null | head -1)
  fi
  [ -n "$conv" ] || { say "Unpacked, but no \"Castles Data\" folder was found inside. (Send me the output of: find ~/Castles-game -maxdepth 3)"; return; }
  conv=$(dirname "$conv"); convert_forks "$conv"
  if valid "$conv"; then
    # tidy: keep only the game folder, as <archive name>/Castles (drops the disk images and installer leftovers)
    NEWT="$HOME/Castles-game/.tidy_$$"; rm -rf "$NEWT"
    if [ "$conv" != "$OUT/Castles" ] && mv "$conv" "$NEWT" 2>/dev/null; then rm -rf "$OUT"; mkdir -p "$OUT"; mv "$NEWT" "$OUT/Castles"; conv="$OUT/Castles"; fi
    GAME="$conv"; say "Game folder ready: $GAME"
  else FIXABLE=0; diagnose "$conv"; GAME="$conv"; fi
}
add_song() { # the intro music lives in "Castles Data/IntroSong"; some archives lack it, so borrow it from another copy
  [ -n "$GAME" ] && [ -d "$GAME/Castles Data" ] || return 0
  [ -f "$GAME/Castles Data/IntroSong" ] && return 0
  WHAT="${ARCHNAME:-$GAME}"; say "Note: \"$WHAT\" does not contain the intro music (Castles Data/IntroSong)."
  S=$(find "$HOME" -maxdepth 8 -type f -name IntroSong -path '*Castles Data*' 2>/dev/null | grep -v "^$GAME/" | head -1)
  if [ -z "$S" ] && [ -n "$ARCHDIR" ] && command -v unar >/dev/null 2>&1; then
    T="$HOME/Castles-game/_song"; rm -rf "$T"
    find "$ARCHDIR" -maxdepth 1 -type f \( -iname '*castle*.sit' -o -iname '*castle*.sitx' -o -iname '*castle*.zip' -o -iname '*castle*.hqx' \) 2>/dev/null | while read -r a; do
      [ -n "$(find "$T" -name IntroSong 2>/dev/null | head -1)" ] && break
      unar -q -f -o "$T/$(basename "$a")" "$a" >/dev/null 2>&1 || true
    done
    S=$(find "$T" -type f -name IntroSong 2>/dev/null | head -1)
  fi
  if [ -n "$S" ]; then cp "$S" "$GAME/Castles Data/IntroSong" && say "Added the intro music (IntroSong) from $S"
  else say "Note: the intro music file (IntroSong) was not found; the game will run without the opening song."; fi
  rm -rf "$HOME/Castles-game/_song" 2>/dev/null
}
FIXABLE=0
case "$GAME" in *.sit|*.sitx|*.hqx|*.cpt|*.sea|*.zip|*.7z|*.rar|*.tar|*.tgz|*.gz|*.bz2|*.xz|*.SIT|*.ZIP|*.cue|*.CUE|*.iso|*.ISO|*.bin|*.BIN) [ -f "$GAME" ] && unpack_archive "$GAME";; esac
if [ -z "$GAME" ] && [ -r "$CFG/game" ]; then GAME=$(cat "$CFG/game"); valid "$GAME" || GAME=""; fi
if [ -z "$GAME" ]; then
  say "Looking for your copy of the game..."
  FOLD=$(find "$HOME" -maxdepth 6 -type d -name 'Castles Data' 2>/dev/null | head -9 | while read -r d; do echo "folder|$(dirname "$d")"; done)
  ARCH=$(find "$HOME" -maxdepth 4 -type f \( -iname '*castle*.sit' -o -iname '*castle*.sitx' -o -iname '*castle*.hqx' -o -iname '*castle*.cpt' -o -iname '*castle*.sea' -o -iname '*castle*.zip' -o -iname '*castle*.7z' -o -iname '*castle*.rar' \) 2>/dev/null | head -5 | while read -r f; do echo "archive|$f"; done)
  FOUND=$(printf '%s\n%s\n' "$FOLD" "$ARCH" | grep . || true)
  N=$(printf '%s\n' "$FOUND" | grep -c . || true)
  PICK=""
  if [ "$N" = 1 ] && [ "$YES" = 0 ]; then PICK="$FOUND"; say "Found: ${PICK#*|}"; ask "Use this?" || PICK=""
  elif [ "$N" -ge 1 ] && [ "$YES" = 0 ]; then
    say "Found:"; i=1; printf '%s\n' "$FOUND" | while IFS='|' read -r k p; do say "  $i) [$k] $p"; i=$((i+1)); done
    printf 'Pick a number, or press Enter to type a path yourself: '; read -r pick || pick=""
    case "$pick" in ''|*[!0-9]*) ;; *) PICK=$(printf '%s\n' "$FOUND" | sed -n "${pick}p");; esac
  elif [ "$N" -ge 1 ]; then PICK=$(printf '%s\n' "$FOUND" | grep '^folder' | head -1)
  fi
  case "$PICK" in
    folder\|*) GAME="${PICK#*|}";;
    archive\|*) unpack_archive "${PICK#*|}";;
  esac
  if [ -z "$GAME" ] && [ "$YES" = 0 ]; then
    printf 'Type the full path of your game folder or archive (.sit, .zip, ...), or leave empty to skip: '
    read -r GAME || GAME=""
    GAME=$(printf '%s' "$GAME" | sed "s/^'//; s/'\$//; s/^\"//; s/\"\$//; s|^~|$HOME|")
    if [ -f "$GAME" ]; then unpack_archive "$GAME"; fi
  fi
fi
if [ -n "$GAME" ] && [ ! -e "$GAME" ]; then
  say "Error: \"$GAME\" does not exist. Check the path (use the real location of your game folder or archive, in quotes)."; GAME=""
fi
if [ -n "$GAME" ] && [ -f "$GAME" ]; then
  say "Error: \"$GAME\" is a file but not a type of archive I can unpack (.sit, .hqx, .cpt, .sea, .zip, .7z, .rar, or a CD image .cue/.iso)."; GAME=""
fi
if [ -n "$GAME" ] && ! valid "$GAME"; then
  say "The folder $GAME is not a complete game folder:"; diagnose "$GAME"
  if [ "$FIXABLE" = 1 ] && ask "Try to repair it?"; then convert_forks "$GAME"; fi
  if valid "$GAME"; then say "Repaired: $GAME"; else say "I'll remember it anyway; fix the items above and start the game again."; fi
fi
valid "$GAME" && add_song
mkdir -p "$CFG" "$BIN" "$APPS"
[ -n "$GAME" ] && [ -d "$GAME" ] && printf '%s\n' "$GAME" > "$CFG/game"

# ---- 4. launcher + menu entry ------------------------------------------------------------
cat > "$BIN/castles" <<EOF
#!/bin/sh
# Launcher for Castles: Siege & Conquest.  castles [game folder]   (remembers the folder you give it)
CFG="\${XDG_CONFIG_HOME:-\$HOME/.config}/castles"
if [ -n "\$1" ] && [ -d "\$1" ]; then mkdir -p "\$CFG"; printf '%s\n' "\$1" > "\$CFG/game"; shift; fi
[ -r "\$CFG/game" ] && GAME=\$(cat "\$CFG/game")
if [ -z "\$GAME" ] || [ ! -d "\$GAME" ]; then
  echo "No game folder set. Run:  castles \"/path/to/your/game folder\"" >&2
  command -v zenity >/dev/null 2>&1 && zenity --error --text="No game folder set.\nRun:  castles /path/to/game/folder" 2>/dev/null
  exit 1
fi
exec "$EXE" "\$GAME" "\$@"
EOF
chmod +x "$BIN/castles"
ICON=applications-games
if [ -f "$HERE/packaging/castles.png" ]; then
  IDIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"; mkdir -p "$IDIR"
  cp "$HERE/packaging/castles.png" "$IDIR/castles.png" && ICON=castles
fi
cat > "$APPS/castles.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Castles: Siege & Conquest
Comment=Classic Mac strategy game (native port)
Exec="$BIN/castles"
Icon=$ICON
Terminal=false
Categories=Game;StrategyGame;
EOF
# double-clickable icon on the desktop (if there is a Desktop folder)
DESK=""; command -v xdg-user-dir >/dev/null 2>&1 && DESK=$(xdg-user-dir DESKTOP 2>/dev/null); [ -z "$DESK" ] && DESK="$HOME/Desktop"
if [ "$NOSHORT" = 0 ] && [ -d "$DESK" ]; then
  cp "$APPS/castles.desktop" "$DESK/castles.desktop" && chmod +x "$DESK/castles.desktop"
  command -v gio >/dev/null 2>&1 && gio set "$DESK/castles.desktop" metadata::trusted true 2>/dev/null || true
  SHORT=1
fi
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor" 2>/dev/null || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true

say ""
say "Done."
[ "$SHORT" = 1 ] && say "A \"Castles\" icon was added to your Desktop (if it shows a warning, right-click it and choose Allow Launching)."
say "You can also find it in your application menu under Games."
if [ -z "$GAME" ] && [ ! -r "$CFG/game" ]; then say ""; say "NOTE: no game folder is set yet, so the icon will not start the game. Run install.sh again with --game \"/real/path\"."; fi
if [ -n "$GAME" ]; then say "To start it from a terminal right now:  $BIN/castles"; else say "Set your game folder once with:  $BIN/castles \"/path/to/game folder\""; fi
case ":$PATH:" in *":$BIN:"*) ;; *) say "(After you next log in, the short command  castles  will work too.)";; esac
