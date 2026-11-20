#!/usr/bin/env bash
# Historical entry point; acceptance now uses disposable X11/XDG state.
set -euo pipefail
cd "$(dirname "$0")/.."
exec make test
