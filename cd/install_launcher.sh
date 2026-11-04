#!/bin/sh
# (Re)write the castles-cd command, the application-menu entry and a Desktop icon.   usage: cd/install_launcher.sh [game-folder]
HERE=$(cd "$(dirname "$0")/.." && pwd); G="${1:-$HOME/Castles-game/CD/Castles}"
BIN="${XDG_BIN_HOME:-$HOME/.local/bin}"; mkdir -p "$BIN"
printf '#!/bin/sh\n# Castles CD-ROM release (experimental)\nexec "%s/cd/run_cd.sh" "%s" "$@"\n' "$HERE" "$G" > "$BIN/castles-cd"
chmod +x "$BIN/castles-cd"; echo "castles-cd now starts $HERE/cd/run_cd.sh"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"; mkdir -p "$APPS"; ICON=applications-games
if [ -f "$HERE/packaging/castles.png" ]; then IDIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"; mkdir -p "$IDIR"; cp "$HERE/packaging/castles.png" "$IDIR/castles.png" && ICON=castles; fi
cat > "$APPS/castles-cd.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Castles: Siege & Conquest (CD)
Comment=Castles Enhanced CD-ROM release (experimental native port)
Exec="$BIN/castles-cd"
Icon=$ICON
Terminal=false
Categories=Game;StrategyGame;
EOF
DESK=""; command -v xdg-user-dir >/dev/null 2>&1 && DESK=$(xdg-user-dir DESKTOP 2>/dev/null); [ -z "$DESK" ] && DESK="$HOME/Desktop"
if [ -d "$DESK" ]; then cp "$APPS/castles-cd.desktop" "$DESK/castles-cd.desktop" && chmod +x "$DESK/castles-cd.desktop"; command -v gio >/dev/null 2>&1 && gio set "$DESK/castles-cd.desktop" metadata::trusted true 2>/dev/null || true; echo "A \"Castles (CD)\" icon was added to your Desktop (if it shows a warning, right-click it and choose Allow Launching)."; fi
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true
echo "It is also in your application menu under Games."
case ":$PATH:" in *":$BIN:"*) ;; *) echo "(The short command castles-cd works after you next log in; until then run: $BIN/castles-cd)";; esac
