#!/usr/bin/env bash
# Cross-compiles cronq.exe, cronqw.exe, sendkeys.exe and config_test.exe on Linux with llvm-mingw.
# Output: windows/build-mingw/   (git-ignored)
# Needs: cmake, and llvm-mingw in $LLVM_MINGW (default: ~/.local/llvm-mingw).
# CRONQ_WEBVIEW2=ON windows/scripts/build-mingw.sh   also builds with WebView2 (downloads the SDK once).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="${LLVM_MINGW:-$HOME/.local/llvm-mingw}"
if [ ! -x "$ROOT/bin/x86_64-w64-mingw32-clang++" ]; then
    echo "llvm-mingw not found at $ROOT (set LLVM_MINGW=/path/to/llvm-mingw)." >&2
    exit 1
fi
export PATH="$ROOT/bin:$PATH"
EXTRA=(-DCRONQ_WEBVIEW2="${CRONQ_WEBVIEW2:-ON}")
cmake -S "$HERE" -B "$HERE/build-mingw" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$HERE/cmake/llvm-mingw.cmake" "${EXTRA[@]}" >/dev/null
cmake --build "$HERE/build-mingw" -j"$(nproc)"
echo
ls -la "$HERE"/build-mingw/*.exe
