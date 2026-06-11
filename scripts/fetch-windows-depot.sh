#!/usr/bin/env bash
# Fetch the CURRENT Windows depot for Mina the Hollower so signature work can target the live
# shipping build instead of an outdated reference PE. Re-run after a game update.
#
#   scripts/fetch-windows-depot.sh <steam-username> [outdir]
#
# The first run logs in interactively (Steam Guard 2FA prompt). steamcmd caches credentials after a
# successful login, so later runs are non-interactive. Only the ~10 MB Windows binaries depot is
# pulled (not the 850 MB shared content), so it is fast.
#
# App/depot IDs (from `steamcmd +app_info_print 1875580`):
#   app 1875580; depot 1875582 = oslist "windows" (MinaTheHollower.exe, handler.exe, steam_api64.dll).
set -euo pipefail

APP=1875580       # Mina the Hollower
WIN_DEPOT=1875582 # the oslist=windows binaries depot
STEAMCMD=${STEAMCMD:-/usr/games/steamcmd}
STEAM_USER=${1:?usage: fetch-windows-depot.sh <steam-username> [outdir]}
OUTDIR=${2:-$HOME/minathehollower/windows-current}

echo ">> steamcmd: downloading app $APP windows depot $WIN_DEPOT (login: $STEAM_USER)"
# @sSteamCmdForcePlatformType MUST precede +login so the Windows depot variant is selected on Linux.
"$STEAMCMD" +@sSteamCmdForcePlatformType windows +login "$STEAM_USER" \
    +download_depot "$APP" "$WIN_DEPOT" +quit | tee /tmp/steamcmd_fetch.log

# download_depot stages under <steam home>/steamapps/content/app_<APP>/depot_<DEPOT>/ and prints the
# path -- but in windows-platform mode steamcmd prints it with backslashes and quotes, so normalize.
SRC=$(sed -n 's/^Depot download complete : //p' /tmp/steamcmd_fetch.log | tr -d '\r"' | tr '\\' '/' | tail -1)
if [ -z "${SRC:-}" ] || [ ! -d "$SRC" ]; then
    SRC=$(find "$HOME/.local/share/Steam/steamcmd" "$HOME/.steam" "$HOME/Steam" \
        -type d -path "*content/app_${APP}/depot_${WIN_DEPOT}" 2>/dev/null | head -1)
fi
[ -n "${SRC:-}" ] && [ -d "$SRC" ] || { echo "!! could not locate downloaded depot dir"; exit 1; }

EXE=$(find "$SRC" -iname MinaTheHollower.exe | head -1)
[ -n "$EXE" ] || { echo "!! MinaTheHollower.exe not found under $SRC"; exit 1; }

mkdir -p "$OUTDIR"
cp -f "$EXE" "$OUTDIR/MinaTheHollower.exe"
for f in handler.exe steam_api64.dll; do
    g=$(find "$SRC" -iname "$f" | head -1) || true
    [ -n "${g:-}" ] && cp -f "$g" "$OUTDIR/$f" || true
done

# Build id = TimeDateStamp:SizeOfImage (hex) -- the same value the mod logs as build_id=.
BID=$(python3 - "$OUTDIR/MinaTheHollower.exe" <<'PY'
import sys, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
print(f"{pe.FILE_HEADER.TimeDateStamp:x}:{pe.OPTIONAL_HEADER.SizeOfImage:08x}")
PY
)
echo ">> staged $OUTDIR/MinaTheHollower.exe   build_id=$BID"
echo ">> next: regenerate Windows signatures against this binary (see CLAUDE.md runbook)."
