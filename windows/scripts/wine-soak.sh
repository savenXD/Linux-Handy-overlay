#!/usr/bin/env bash
# Runs a soak test for Cronq under Wine to verify memory and handle stability.
# Usage:
#   windows/scripts/wine-soak.sh              # Default full run (1800s / 30m)
#   windows/scripts/wine-soak.sh --quick      # Quick test (20s)
#   windows/scripts/wine-soak.sh --duration N # Run for N seconds
set -Eeuo pipefail
trap 'echo "FAIL: soak aborted at line $LINENO (exit $?)" >&2' ERR

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CRONQ_BIN_DIR:-$HERE/build-mingw}"
[ -f "$BIN/cronq.exe" ] || { echo "Build first: windows/scripts/build-mingw.sh"; exit 2; }

DURATION=1800
if [ "${1:-}" = "--quick" ]; then
    DURATION=20
elif [ "${1:-}" = "--duration" ] && [ -n "${2:-}" ]; then
    DURATION="$2"
fi

[[ "$DURATION" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid duration"; exit 2; }

echo "== Starting Wine soak test for ${DURATION}s =="

HOST_DISPLAY="${DISPLAY:-:0}"
TEST_DISPLAY=":98"
WORK="${XDG_CACHE_HOME:-$HOME/.cache}/cronq-wine-soak"
mkdir -p "$WORK"
export WINEPREFIX="$WORK/prefix"
export WINEDEBUG=-all
export WINEDLLOVERRIDES="mscoree,mshtml="

XEPHYR_PID=""
cleanup() {
    wineserver -k 2>/dev/null || true
    if [ -n "$XEPHYR_PID" ]; then kill "$XEPHYR_PID" 2>/dev/null || true; wait "$XEPHYR_PID" 2>/dev/null || true; fi
}
trap cleanup EXIT
cleanup

# Keep the tested image fixed even if a developer rebuilds the original build directory.
RUNTIME_BIN="$WORK/bin"
if [ "$BIN" != "$RUNTIME_BIN" ]; then
    rm -rf "$RUNTIME_BIN"
    mkdir -p "$RUNTIME_BIN"
    for file in "$BIN"/*.exe "$BIN"/*.dll; do
        if [ -f "$file" ]; then cp -p "$file" "$RUNTIME_BIN/"; fi
    done
    if [ -d "$BIN/ui" ]; then cp -a "$BIN/ui" "$RUNTIME_BIN/"; fi
fi
BIN="$RUNTIME_BIN"
sha256sum "$BIN/cronq.exe" > "$WORK/binary.sha256"

DISPLAY="$HOST_DISPLAY" Xephyr "$TEST_DISPLAY" -screen 1280x800 -ac >"$WORK/xephyr.log" 2>&1 &
XEPHYR_PID=$!
for _ in $(seq 1 20); do DISPLAY="$TEST_DISPLAY" xdpyinfo >/dev/null 2>&1 && break; sleep 0.5; done
DISPLAY="$TEST_DISPLAY" xdpyinfo >/dev/null 2>&1 || { echo "Xephyr did not start"; exit 2; }
export DISPLAY="$TEST_DISPLAY"

timeout 300 wine wineboot --init >/dev/null 2>&1
: > "$WORK/cli-errors.log"

cli() {
    local out rc=0
    out=$(timeout 30 wine "$BIN/cronq.exe" "$@" 2>>"$WORK/cli-errors.log") || rc=$?
    if [ "$rc" -ne 0 ]; then echo "FAIL: CLI ($*) exited $rc: $out" >&2; return "$rc"; fi
    if { [ "${1:-}" = status ] || [ "${1:-}" = --get-geometry ]; } && [ -z "$out" ]; then echo "FAIL: empty query response ($*)" >&2;return 1;fi
    printf '%s\n' "$out" | tr -d '\r'
}

# Start daemon
( wine "$BIN/cronq.exe" --daemon --show >"$WORK/daemon.log" 2>&1 & )
for _ in $(seq 1 30); do [ -n "$(cli status 2>/dev/null)" ] && break; sleep 0.5; done

if [ -z "$(cli status)" ]; then
    echo "FAIL: daemon did not start"
    exit 1
fi

get_metrics() {
    timeout 30 wine "$BIN/runtime_test.exe" --metrics 2>/dev/null | tr -d '\r'
}

sleep 2
read -r INITIAL_MEM INITIAL_HANDLES < <(get_metrics)
[[ "$INITIAL_MEM" =~ ^[1-9][0-9]*$ && "$INITIAL_HANDLES" =~ ^[0-9]+$ ]] || { echo "FAIL: real process metrics unavailable ($INITIAL_MEM / $INITIAL_HANDLES)"; exit 1; }
[ "$INITIAL_HANDLES" -ne 0 ] || echo "NOTE: Wine reports zero handles; handle stability requires native Windows validation."
echo "Initial memory: ${INITIAL_MEM} KB; handles: ${INITIAL_HANDLES}"

START_TIME=$(date +%s)
ITER=0

while true; do
    NOW=$(date +%s)
    ELAPSED=$((NOW - START_TIME))
    [ "$ELAPSED" -ge "$DURATION" ] && break

    # Perform typical operations repeatedly
    cli status >/dev/null
    cli --size 360 400 >/dev/null
    cli --opacity 0.8 >/dev/null
    cli --mode exclusive >/dev/null
    cli --ghost on >/dev/null
    cli --get-geometry >/dev/null
    cli --ghost off >/dev/null
    cli --mode mirror >/dev/null
    cli --opacity 1.0 >/dev/null
    cli --size 360 280 >/dev/null

    ITER=$((ITER + 1))
    if [ $((ITER % 50)) -eq 0 ]; then
        CUR_MEM=$(get_metrics)
        echo "  [${ELAPSED}s / ${DURATION}s] Iteration $ITER, Memory KB / handles: ${CUR_MEM}"
    fi
    sleep 0.1
done

read -r FINAL_MEM FINAL_HANDLES < <(get_metrics)
[[ "$FINAL_MEM" =~ ^[1-9][0-9]*$ && "$FINAL_HANDLES" =~ ^[0-9]+$ ]] || { echo "FAIL: final process metrics unavailable"; exit 1; }
echo "Final memory after $ITER iterations: ${FINAL_MEM} KB; handles: ${FINAL_HANDLES}"

cli kill >/dev/null || true

# Memory check: fail if growth > 50%
MAX_ALLOWED=$(( INITIAL_MEM + INITIAL_MEM / 2 ))
echo "Allowed maximum: ${MAX_ALLOWED} KB"

if [ "$FINAL_MEM" -gt "$MAX_ALLOWED" ]; then
    echo "FAIL: Memory grew from ${INITIAL_MEM} KB to ${FINAL_MEM} KB (> 50% growth)"
    exit 1
fi

if [ "$INITIAL_HANDLES" -gt 0 ] && { [ "$FINAL_HANDLES" -eq 0 ] || [ "$FINAL_HANDLES" -gt "$((INITIAL_HANDLES + 20))" ]; }; then
    echo "FAIL: handle growth exceeds 20 ($INITIAL_HANDLES -> $FINAL_HANDLES)"; exit 1
fi

echo "PASS: Soak test completed successfully (${ITER} iterations, memory stable)"
exit 0
