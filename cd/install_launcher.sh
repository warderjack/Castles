#!/bin/sh
# (Re)write the castles-cd command so that it starts cd/run_cd.sh.   usage: cd/install_launcher.sh [game-folder]
HERE=$(cd "$(dirname "$0")/.." && pwd); G="${1:-$HOME/Castles-game/CD/Castles}"
BIN="${XDG_BIN_HOME:-$HOME/.local/bin}"; mkdir -p "$BIN"
printf '#!/bin/sh\n# Castles CD-ROM release (experimental)\nexec "%s/cd/run_cd.sh" "%s" "$@"\n' "$HERE" "$G" > "$BIN/castles-cd"
chmod +x "$BIN/castles-cd"; echo "castles-cd now starts $HERE/cd/run_cd.sh"
