#!/usr/bin/env bash
# Runs the Windows build under Wine on a throw-away nested X display (Xephyr) and checks behaviour.
# Usage: windows/scripts/wine-smoke.sh            (after windows/scripts/build-mingw.sh)
# A Xephyr window briefly appears on your desktop. Nothing else on your system is touched:
# Wine uses its own prefix under ~/.cache/cronq-wine-prefix (safe to delete).
#
# What this CAN check: window creation, IPC/CLI, hotkey hook (via SendInput), geometry, opacity, ghost
# click-through style, supervisor restart. What it CANNOT check: WebView2, real focus behaviour of other
# applications, DPI scaling, UAC/admin windows, games. Those need a real Windows machine.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CRONQ_BIN_DIR:-$HERE/build-mingw}"
[ -f "$BIN/cronq.exe" ] || { echo "Build first: windows/scripts/build-mingw.sh"; exit 2; }

HOST_DISPLAY="${DISPLAY:-:0}"          # the real display Xephyr opens its window on
TEST_DISPLAY=":97"
WORK="${XDG_CACHE_HOME:-$HOME/.cache}/cronq-wine-test"
mkdir -p "$WORK"
export WINEPREFIX="$WORK/prefix"
export WINEDEBUG=-all
export WINEDLLOVERRIDES="mscoree,mshtml="   # no Mono/Gecko install prompts
PASS=0; FAIL=0
XEPHYR_PID=""

cleanup() {
    wineserver -k 2>/dev/null || true
    if [ -n "$XEPHYR_PID" ]; then kill "$XEPHYR_PID" 2>/dev/null || true; wait "$XEPHYR_PID" 2>/dev/null || true; fi
}
trap cleanup EXIT
cleanup

DISPLAY="$HOST_DISPLAY" Xephyr "$TEST_DISPLAY" -screen 1280x800 -ac >"$WORK/xephyr.log" 2>&1 &
XEPHYR_PID=$!
for _ in $(seq 1 20); do DISPLAY="$TEST_DISPLAY" xdpyinfo >/dev/null 2>&1 && break; sleep 0.5; done
DISPLAY="$TEST_DISPLAY" xdpyinfo >/dev/null 2>&1 || { echo "Xephyr did not start (is DISPLAY set?)"; exit 2; }
export DISPLAY="$TEST_DISPLAY"
echo "== preparing Wine prefix (first run takes a while)"
timeout 300 wine wineboot --init >/dev/null 2>&1

cli()  { timeout 30 wine "$BIN/cronq.exe" "$@" 2>/dev/null | tr -d '\r'; }
keys() { timeout 30 wine "$BIN/sendkeys.exe" "$@" 2>/dev/null; }
check() {  # check "name" "actual" "expected-substring"
    if [[ "$2" == *"$3"* ]]; then echo "PASS  $1"; PASS=$((PASS+1));
    else echo "FAIL  $1   (got: '$2', wanted to contain: '$3')"; FAIL=$((FAIL+1)); fi
}
# expect "name" "expected-substring" command...   Polls for up to ~6 s: Wine processes are slow to start
# and the hotkey hook posts its action to the UI thread, so the state changes shortly after the keys.
expect() {
    local name="$1" want="$2" got=""; shift 2
    for _ in $(seq 1 15); do got="$("$@")"; [[ "$got" == *"$want"* ]] && break; sleep 0.4; done
    check "$name" "$got" "$want"
}

echo "== config parser unit test"
fixture="Z:$(echo "$HERE/../shared/fixtures/config_defaults.ini" | tr / '\\')"
for test in config_test parity_config_test; do
    if out=$(timeout 60 wine "$BIN/$test.exe" "$fixture" 2>/dev/null | tr -d '\r'); then
        check "$test" "$out" "all checks passed"
    else
        echo "FAIL  $test (exit failure): $out"; FAIL=$((FAIL+1))
    fi
done

echo "== Phase 2 pure logic tests"
for test in browser_logic_test jsonlite_test log_rotate_test geometry_test ghost_logic_test snap_logic_test ini_writer_test key_translate_test input_logic_test lock_logic_test recovery_logic_test purge_logic_test update_logic_test image_logic_test image_codec_test crash_report_test; do
    if out=$(timeout 60 wine "$BIN/$test.exe" 2>/dev/null | tr -d '\r'); then
        printf '%s\n' "$out"
        check "$test" "$out" "all checks passed"
    else
        echo "FAIL  $test (exit failure): $out"; FAIL=$((FAIL+1))
    fi
done

set_timing() {
    python3 - "$WINEPREFIX/drive_c/users/$USER/AppData/Roaming/cronq/config.ini" "$1" <<'PYCONFIG'
import configparser, pathlib, sys
path=pathlib.Path(sys.argv[1]);path.parent.mkdir(parents=True,exist_ok=True)
c=configparser.ConfigParser(interpolation=None);c.read(path)
c['GhostTiming']={'human':sys.argv[2],'speed':'0','safe':'0'}
# Wine has no WDA_EXCLUDEFROMCAPTURE, so Cronq would refuse to start (fail-closed). Test without it.
c['Privacy']={'hide_from_capture':'false'}
with path.open('w') as f:c.write(f)
PYCONFIG
}
set_timing 1

echo "== daemon"
( wine "$BIN/cronq.exe" --daemon --show >"$WORK/daemon.log" 2>&1 & )
for _ in $(seq 1 30); do [ -n "$(cli status)" ] && break; sleep 0.5; done
check "status reports running + visible" "$(cli status)" "VISIBLE=TRUE"
check "default geometry is compact 360 wide" "$(cli --get-geometry)" "W=360 H=280"

echo "== hotkeys through the low-level hook (SendInput)"
keys alt+x;  expect "Alt+X hides"      "VISIBLE=FALSE" cli status
keys alt+x;  expect "Alt+X shows"      "VISIBLE=TRUE" cli status
keys alt+g;  expect "Alt+G ghost on"   "GHOST=GHOST" cli status
keys alt+g;  expect "Alt+G ghost off"  "GHOST=SOLID" cli status
keys alt+m;  expect "Alt+M exclusive"  "MODE=EXCLUSIVE" cli status
keys alt+m;  expect "Alt+M mirror"     "MODE=MIRROR" cli status
keys alt+f;  expect "Alt+F fullscreen" "W=1280 H=800" cli --get-geometry
keys alt+f;  expect "Alt+F restores"   "W=360 H=280" cli --get-geometry
keys alt+d;  expect "Alt+D default size" "W=360 H=640" cli --get-geometry
before=$(grep -c "Toggling overlay" "$WORK/daemon.log")
keys ctrl+alt+x; sleep 2
after=$(grep -c "Toggling overlay" "$WORK/daemon.log")
check "Ctrl+Alt+X does NOT trigger" "$before" "$after"

echo "== CLI / IPC"
cli --opacity 0.5 >/dev/null;  check "opacity command accepted (no crash)" "$(cli status)" "MODE="
cli --size 10 10 >/dev/null;   expect "size clamps to minimum"  "W=260 H=180" cli --get-geometry
cli --size 9999 9999 >/dev/null; expect "size clamps to monitor" "W=1280 H=800" cli --get-geometry
check "unknown command" "$(cli --status)" "MODE="

echo "== Phase 2 fallback / commands"
expect "WebView2 unavailable is logged without crashing" "WebView2 unavailable" cat "$WORK/daemon.log"
cli --app chatgpt >/dev/null; check "ChatGPT app expands" "$(cli --get-geometry)" "W=1280 H=640"
check "ChatGPT app state" "$(cli status)" "APP=chatgpt"
cli --back >/dev/null; check "Back without history returns Home" "$(cli status)" "APP=home"
cli hide >/dev/null; cli --load example.com >/dev/null
check "Load while hidden selects Browser" "$(cli status)" "VISIBLE=FALSE APP=browser"
check "No modal in fallback" "$(cli --has-modal)" "FALSE"
cli --close-modal >/dev/null; cli --reload >/dev/null
cli --ghost on >/dev/null; cli --mode exclusive >/dev/null; cli --reset >/dev/null
check "Reset restores Linux Home/Mirror/Solid" "$(cli status)" "MODE=MIRROR TARGET=NONE GHOST=SOLID GRAB=NONE VISIBLE=FALSE APP=home"
cli show >/dev/null

echo "== IPC reconnection stress"
if timeout 90 wine "$BIN/runtime_test.exe" --ipc-stress >"$WORK/ipc-stress.log" 2>&1; then
    check "5000 sequential IPC requests" "$(cat "$WORK/ipc-stress.log")" "5000 IPC requests passed"
else
    cat "$WORK/ipc-stress.log"; echo "FAIL  IPC reconnection stress"; FAIL=$((FAIL+1))
fi

echo "== native integration receiver"
if timeout 45 wine "$BIN/runtime_test.exe" >"$WORK/runtime.log" 2>&1; then
    check "external typing, cancel, reset and routing" "$(cat "$WORK/runtime.log")" "runtime_test: 0 failure(s)"
else
    cat "$WORK/runtime.log"; echo "FAIL  native runtime integration"; FAIL=$((FAIL+1))
fi

echo "== snap PNG"
cli snap >/dev/null
snap_path="$WINEPREFIX/drive_c/users/$USER/AppData/Local/cronq/latest_snap.png"
for _ in $(seq 1 30); do [ -s "$snap_path" ] && break; sleep 0.2; done
signature=$(od -An -tx1 -N8 "$snap_path" 2>/dev/null | tr -d ' \n')
check "snap produces a real PNG" "$signature" "89504e470d0a1a0a"

echo "== clean kill"
check "kill reply" "$(cli kill)" "Killed"
sleep 2
check "not running afterwards" "$(timeout 30 wine "$BIN/cronq.exe" status 2>&1 | tr -d '\r')" "not running"

echo "== instant Unicode integration"
set_timing 0
( wine "$BIN/cronq.exe" --daemon --show >>"$WORK/daemon.log" 2>&1 & )
for _ in $(seq 1 30); do [ -n "$(cli status)" ] && break; sleep 0.5; done
if timeout 30 wine "$BIN/runtime_test.exe" --instant >"$WORK/instant.log" 2>&1; then
    check "Instant mode sends exact Unicode text" "$(cat "$WORK/instant.log")" "runtime_test: 0 failure(s)"
else
    cat "$WORK/instant.log"; echo "FAIL  Instant Unicode integration"; FAIL=$((FAIL+1))
fi
cli kill >/dev/null; sleep 2

echo "== supervisor restarts a crashed daemon"
rm -f "$WINEPREFIX/drive_c/users/$USER/AppData/Local/cronq/crash.log"
( wine "$BIN/cronqw.exe" --supervise --show >/dev/null 2>&1 & )
for _ in $(seq 1 30); do [ -n "$(cli status)" ] && break; sleep 0.5; done
check "daemon up under supervisor" "$(cli status)" "VISIBLE=TRUE"
pid=$(timeout 30 wine tasklist 2>/dev/null | tr -d '\r' | awk '/cronqw.exe/ {print $2}' | sort -n | tail -1)
timeout 30 wine taskkill /F /PID "$pid" >/dev/null 2>&1
sleep 6
# A crash restart comes back hidden: it must never pop the overlay up on its own.
check "daemon restarted after being killed (hidden)" "$(cli status)" "VISIBLE=FALSE"
check "crash.log written" "$(cat "$WINEPREFIX/drive_c/users/$USER/AppData/Local/cronq/crash.log" 2>/dev/null)" "Restarting in 2s"
cli kill >/dev/null; sleep 3

echo
echo "Result: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
