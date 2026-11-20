#!/usr/bin/env bash
# All X11 operations and XDG files are isolated from the user's desktop/profile.
set -euo pipefail
if [ -z "${CRONQ_TEST_DBUS:-}" ]; then
    exec dbus-run-session -- env CRONQ_TEST_DBUS=1 bash "$0" "$@"
fi
cd "$(dirname "$0")/.."
work=$(mktemp -d /tmp/cronq-linux-test.XXXXXX)
echo "Test artifacts: $work"
Xephyr -displayfd 3 -screen 1280x900 -ac 3>"$work/display" >"$work/xephyr.log" 2>&1 &
xpid=$!
cleanup(){ kill "$xpid" 2>/dev/null || true; }
trap cleanup EXIT
for i in {1..40};do [ -s "$work/display" ]&&break;sleep .1;done
export DISPLAY=":$(cat "$work/display")" XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache" XDG_RUNTIME_DIR="$work/runtime" WEBKIT_DISABLE_DMABUF_RENDERER=1
mkdir -p "$XDG_CONFIG_HOME/cronq" "$XDG_RUNTIME_DIR";chmod 700 "$XDG_RUNTIME_DIR"
printf '[Appearance]\nmode=0\n[Privacy]\nhide_from_capture=false\n' > "$XDG_CONFIG_HOME/cronq/config.ini"
python3 tests/update_test.py
python3 tests/supervisor_test.py
timeout 90 tests/linux_runtime_test | tee "$work/runtime.log"

timeout 90 python3 tests/linux_cli_test.py | tee "$work/cli.log"
