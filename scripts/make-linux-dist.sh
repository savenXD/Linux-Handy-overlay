#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arch=$(uname -m)
case "$arch" in x86_64|aarch64);; *) echo 'Unsupported release architecture' >&2;exit 1;; esac
[ "$(cat VERSION)" = "$(cat windows/VERSION)" ] || { echo 'Version mismatch' >&2;exit 1; }
mkdir -p dist
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
install -m755 bin/cronq scripts/cronq-supervisor scripts/cronq-update.py "$stage/"
install -m644 cronq.desktop assets/cronq.svg VERSION "$stage/"
tar -C "$stage" -czf "dist/cronq-linux-$arch.tar.gz" cronq cronq-supervisor cronq-update.py cronq.desktop cronq.svg VERSION
(cd dist && sha256sum "cronq-linux-$arch.tar.gz" > SHA256SUMS-linux.txt)
