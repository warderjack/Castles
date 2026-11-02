#!/bin/sh
# One-step setup for Castles: Siege & Conquest (native Linux port).
#   ./install.sh                   check/install build tools, build, find your game files, add a launcher
#   ./install.sh --game DIR        use this game folder (the one containing "Castles Data", "Castles Movies", ...)
#   ./install.sh --yes             do not ask questions (installs missing packages with sudo)
#   ./install.sh --no-deps         never try to install packages
#   ./install.sh --uninstall       remove the launcher and menu entry (keeps your game files)
# Afterwards start the game with the command `castles` or from your application menu (Games).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
YES=0; DEPS=1; GAME=""; UNINSTALL=0
while [ $# -gt 0 ]; do
  case "$1" in
    --yes|-y) YES=1;; --no-deps) DEPS=0;; --uninstall) UNINSTALL=1;;
    --game) shift; GAME="$1";; --game=*) GAME="${1#--game=}";;
    -h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
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
  rm -f "$BIN/castles" "$APPS/castles.desktop"; say "Removed launcher and menu entry. (Settings kept in $CFG; game files untouched.)"; exit 0
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
  if ask "Run it now?"; then $SUDO sh -c "$CMD"; else say "Install those packages, then run ./install.sh again."; exit 1; fi
  have_cc && have_sdl || { say "Packages still missing after install; see messages above."; exit 1; }
fi

# ---- 2. build ----------------------------------------------------------------------------
say "Building (about a minute)..."
( cd "$HERE/port" && ./build_native.sh ) || { say "Build failed. Please report the messages above."; exit 1; }
EXE="$HERE/port/build/castles_native"

# ---- 3. find the game files --------------------------------------------------------------
valid() { [ -d "$1/Castles Data" ] || [ -d "$1/Castles Movies" ] || ls "$1"/*.bin >/dev/null 2>&1; }
if [ -z "$GAME" ] && [ -r "$CFG/game" ]; then GAME=$(cat "$CFG/game"); valid "$GAME" || GAME=""; fi
if [ -z "$GAME" ]; then
  say "Looking for your copy of the game..."
  FOUND=$(find "$HOME" -maxdepth 6 -type d -name 'Castles Data' 2>/dev/null | head -5 | while read -r d; do dirname "$d"; done)
  N=$(printf '%s\n' "$FOUND" | grep -c . || true)
  if [ "$N" = 1 ]; then GAME="$FOUND"; say "Found: $GAME"; ask "Use this folder?" || GAME=""; fi
  if [ -z "$GAME" ] && [ "$YES" = 0 ]; then
    [ "$N" -gt 1 ] && { say "Found several:"; printf '%s\n' "$FOUND"; }
    printf 'Type the full path of your game folder (it contains "Castles Data" and "Castles Movies"; leave empty to skip): '
    read -r GAME || GAME=""
    GAME=$(printf '%s' "$GAME" | sed "s/^'//; s/'\$//; s/^\"//; s/\"\$//; s|^~|$HOME|")
  fi
fi
if [ -n "$GAME" ] && ! valid "$GAME"; then say "Warning: $GAME does not look like a game folder (no 'Castles Data' inside)."; fi
mkdir -p "$CFG" "$BIN" "$APPS"
[ -n "$GAME" ] && printf '%s\n' "$GAME" > "$CFG/game"

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
cat > "$APPS/castles.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Castles: Siege & Conquest
Comment=Classic Mac strategy game (native port)
Exec=$BIN/castles
Icon=applications-games
Terminal=false
Categories=Game;StrategyGame;
EOF
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true

say ""
say "Done."
case ":$PATH:" in *":$BIN:"*) ;; *) say "Note: $BIN is not on your PATH; use $BIN/castles or log out and back in.";; esac
if [ -n "$GAME" ]; then say "Start the game with:  castles      (or from your application menu)"; else say "Set your game folder once with:  castles \"/path/to/game folder\""; fi
