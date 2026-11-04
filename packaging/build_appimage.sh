#!/bin/sh
# Build a single-file AppImage of the native (floppy-release) port.   packaging/build_appimage.sh
# The AppImage holds only the program: your game folder is never included. On first start it asks for the game folder
# (the one with "Castles Data", "Castles Movies", ...) and remembers it, the same file the `castles` launcher uses.
# Needs: gcc, SDL2 dev files (to build), and internet once to fetch appimagetool (APPDIR_ONLY=1 skips that step).
# Result: dist/Castles-x86_64.AppImage
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd); cd "$HERE"
[ -x port/build/castles_native ] || port/build_native.sh
OUT="$HERE/dist"; AD="$OUT/Castles.AppDir"; rm -rf "$AD"; mkdir -p "$AD/usr/bin" "$AD/usr/lib" "$OUT"
cp port/build/castles_native "$AD/usr/bin/castles_native"
# bundle the system SDL2 (it loads X11/Wayland/ALSA/PulseAudio/PipeWire itself at run time)
SO=$(ldconfig -p 2>/dev/null | awk '/libSDL2-2.0.so.0 /{print $NF; exit}'); [ -n "$SO" ] || SO=$(ldd port/build/castles_native | awk '/libSDL2-2.0.so.0/{print $3; exit}')
[ -f "$SO" ] || { echo "libSDL2-2.0.so.0 not found"; exit 1; }
cp -L "$SO" "$AD/usr/lib/libSDL2-2.0.so.0"
cat > "$AD/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
CFG="${XDG_CONFIG_HOME:-$HOME/.config}/castles"; mkdir -p "$CFG"
GAME=""
[ -n "$1" ] && [ -d "$1" ] && { GAME="$1"; shift; printf '%s\n' "$GAME" > "$CFG/game"; }
[ -z "$GAME" ] && [ -r "$CFG/game" ] && GAME=$(cat "$CFG/game")
if [ -z "$GAME" ] || [ ! -d "$GAME" ]; then
  MSG="Choose your Castles game folder (the one that contains \"Castles Data\" and \"Castles Movies\")."
  if command -v zenity >/dev/null 2>&1; then GAME=$(zenity --file-selection --directory --title="$MSG" 2>/dev/null)
  elif command -v kdialog >/dev/null 2>&1; then GAME=$(kdialog --getexistingdirectory "$HOME" --title "$MSG" 2>/dev/null)
  else echo "No game folder set. Start with:  Castles-x86_64.AppImage \"/path/to/game folder\"" >&2; exit 1; fi
  [ -d "$GAME" ] || exit 1
  printf '%s\n' "$GAME" > "$CFG/game"
fi
exec "$HERE/usr/bin/castles_native" "$GAME" "$@"
EOF
chmod +x "$AD/AppRun"
cat > "$AD/castles.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Castles: Siege & Conquest
Comment=Classic Mac strategy game (native port)
Exec=AppRun
Icon=castles
Terminal=false
Categories=Game;StrategyGame;
EOF
cp packaging/castles.png "$AD/castles.png"
[ -n "$APPDIR_ONLY" ] && { echo "AppDir ready: $AD"; exit 0; }
AT="$OUT/appimagetool"
[ -x "$AT" ] || { echo "Fetching appimagetool ..."; curl -fL -o "$AT" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage; chmod +x "$AT"; }
ARCH=x86_64 "$AT" --appimage-extract-and-run "$AD" "$OUT/Castles-x86_64.AppImage"
echo "built $OUT/Castles-x86_64.AppImage"
