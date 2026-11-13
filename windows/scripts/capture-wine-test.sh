#!/usr/bin/env bash
# Isolated capture/routing regressions. Wine does not validate native DXGI or WGC pixels.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CRONQ_BIN_DIR:-$HERE/build-mingw}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/cronq-capture.XXXXXX")"
X_PID=""
cleanup() {
    WINEPREFIX="$WORK/prefix" wineserver -k >/dev/null 2>&1 || true
    if [ -n "$X_PID" ]; then kill "$X_PID" >/dev/null 2>&1 || true; fi
    echo "Capture test output: $WORK"
}
trap cleanup EXIT
Xephyr -displayfd 3 -screen 1280x800 -ac 3>"$WORK/display" >"$WORK/xephyr.log" 2>&1 &
X_PID=$!
for _ in {1..30}; do [ -s "$WORK/display" ] && break; sleep 0.1; done
export DISPLAY=":$(cat "$WORK/display")" WINEPREFIX="$WORK/prefix" WINEDEBUG=-all
export WINEDLLOVERRIDES='mscoree,mshtml=' WINEARCH=win64
xdpyinfo >/dev/null
timeout 60 wineboot --init >"$WORK/wineboot.log" 2>&1 || cat "$WORK/wineboot.log"
wineserver -p || true
ctest --test-dir "$BIN" --output-on-failure --timeout 40
timeout 40 wine "$BIN/capture_runtime_test.exe" --wgc --wgc-window
python3 - "$WINEPREFIX" <<'PY'
from pathlib import Path
import sys
for user in (Path(sys.argv[1]) / 'drive_c/users').iterdir():
    if user.name in ('Public', 'Default'): continue
    config = user / 'AppData/Roaming/cronq/config.ini'
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text('[Privacy]\nhide_from_capture = false\n[Snap]\nsnap_save = true\nsnap_scope = monitor\n')
PY
wine "$BIN/cronq.exe" --daemon >"$WORK/daemon.log" 2>&1 &
for _ in {1..30}; do
    if wine "$BIN/cronq.exe" status 2>/dev/null | rg -q 'VISIBLE='; then break; fi
    sleep 0.1
done
timeout 40 wine "$BIN/capture_runtime_test.exe" --daemon-snap
python3 - "$WINEPREFIX" "$WORK" <<'PY'
from pathlib import Path
import hashlib, sys
base = next((Path(sys.argv[1])/'drive_c/users').glob('*/AppData/Local/cronq'))
(base/'latest_snap_wgc.png').write_bytes(b'preserve-monitor')
(base/'latest_snap_wgc_window.png').write_bytes(b'preserve-window')
(Path(sys.argv[2])/'normal-hash').write_text(hashlib.sha256((base/'latest_snap.png').read_bytes()).hexdigest())
PY
wine "$BIN/sendkeys.exe" ctrl+alt+o ctrl+alt+shift+o alt+k alt+o alt+shift+o >/dev/null
if rg -q 'WGC .*experimental capture requested' "$WORK/daemon.log"; then
    echo 'FAIL modifier filtering or lock'; exit 1
fi
wine "$BIN/sendkeys.exe" alt+k+x >/dev/null
wine "$BIN/cronq.exe" hide >/dev/null
wine "$BIN/sendkeys.exe" alt+o >/dev/null
for _ in {1..60}; do rg -q 'WGC test:' "$WORK/daemon.log" && break; sleep 0.1; done
wine "$BIN/sendkeys.exe" alt+shift+o >/dev/null
for _ in {1..60}; do rg -q 'WGC window test:' "$WORK/daemon.log" && break; sleep 0.1; done
python3 - "$WINEPREFIX" "$WORK" <<'PY'
from pathlib import Path
import hashlib, sys
work = Path(sys.argv[2]); log = (work/'daemon.log').read_text()
assert log.count('WGC experimental capture requested') == 1
assert log.count('WGC window experimental capture requested') == 1
experimental = log.split('WGC experimental capture requested', 1)[1]
assert 'WGC test:' in experimental and 'WGC window test:' in experimental
assert 'trying GDI' not in experimental and 'DXGI duplicate' not in experimental
base = next((Path(sys.argv[1])/'drive_c/users').glob('*/AppData/Local/cronq'))
assert (base/'latest_snap_wgc.png').read_bytes() == b'preserve-monitor'
assert (base/'latest_snap_wgc_window.png').read_bytes() == b'preserve-window'
assert hashlib.sha256((base/'latest_snap.png').read_bytes()).hexdigest() == (work/'normal-hash').read_text()
print('PASS WGC shortcuts, modifiers, lock, no fallback, and image preservation under unavailable Wine APIs')
PY
timeout 40 wine "$BIN/capture_runtime_test.exe" --daemon-snap
wine "$BIN/cronq.exe" quit >/dev/null
echo 'PASS normal capture and clipboard publication after both experimental failures'
