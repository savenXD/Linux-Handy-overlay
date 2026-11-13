#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CRONQ_BIN_DIR:-$HERE/build-mingw}"
WORK=$(mktemp -d /tmp/cronq-god-copy.XXXXXX)
echo "Test artifacts: $WORK"
cleanup() {
 WINEPREFIX="$WORK/prefix" wineserver -k >/dev/null 2>&1 || true
 kill "$X_PID" >/dev/null 2>&1 || true
 echo "Test artifacts: $WORK"
}
Xephyr -displayfd 3 -screen 1280x800 -ac 3>"$WORK/display" >"$WORK/xephyr.log" 2>&1 &
X_PID=$!
trap cleanup EXIT
for _ in {1..30}; do [ -s "$WORK/display" ] && break; sleep 0.1; done
export DISPLAY=":$(cat "$WORK/display")" WINEPREFIX="$WORK/prefix" WINEDEBUG=-all
export WINEDLLOVERRIDES='mscoree,mshtml=' WINEARCH=win64
xdpyinfo >/dev/null
timeout 60 wineboot --init >"$WORK/wineboot.log" 2>&1 || cat "$WORK/wineboot.log"
wineserver -p || true
ctest --test-dir "$BIN" --output-on-failure --timeout 40
python3 - "$WINEPREFIX" <<'PY'
from pathlib import Path
import sys
for user in (Path(sys.argv[1])/'drive_c/users').iterdir():
 if user.name in ('Public','Default'): continue
 config=user/'AppData/Roaming/cronq/config.ini'
 config.parent.mkdir(parents=True,exist_ok=True)
 config.write_text('[Privacy]\nhide_from_capture = false\n')
PY
wine "$BIN/cronq.exe" --daemon >"$WORK/daemon.log" 2>&1 &
for _ in {1..30}; do
 if wine "$BIN/cronq.exe" status 2>/dev/null | rg -q VISIBLE=; then break; fi
 sleep 0.1
done
timeout 60 wine "$BIN/god_copy_runtime_test.exe" | tee "$WORK/runtime.log"
wine "$BIN/cronq.exe" quit >/dev/null
