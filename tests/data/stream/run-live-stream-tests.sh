#!/usr/bin/env bash
# Downloads MediaMTX (pinned, checksum checked), makes a throw-away TLS certificate and runs the going-live
# tests against it. Usage: run-live-stream-tests.sh <test-binary> <mediamtx-asset-suffix> [exe-name]
set -euo pipefail
binary=$1; suffix=$2; exe=${3:-mediamtx}
version=v1.21.4
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
cd "$work"
asset=mediamtx_${version}_${suffix}
curl -fsSL -o "$asset" "https://github.com/bluenviron/mediamtx/releases/download/${version}/${asset}"
curl -fsSL -o checksums.sha256 "https://github.com/bluenviron/mediamtx/releases/download/${version}/checksums.sha256"
grep -E "[ *]${asset}\$" checksums.sha256 | sed "s#\*# #" | sha256sum -c -
case "$asset" in *.zip) unzip -q "$asset" ;; *) tar -xzf "$asset" ;; esac
# MSYS_NO_PATHCONV: Git Bash on Windows would turn "/CN=..." into a file path.
MSYS_NO_PATHCONV=1 openssl req -x509 -newkey rsa:2048 -nodes -keyout server.key -out server.crt -days 2 -subj "/CN=127.0.0.1" 2>&1 | tail -n 3
test -s server.crt -a -s server.key
cp "$here/mediamtx.yml" .
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else echo "$1"; fi; }
export PUSHFPS_STREAM_SERVER="$(native "$work/$exe")" PUSHFPS_STREAM_CONFIG="$(native "$work/mediamtx.yml")" PUSHFPS_STREAM_FIXTURE="$(native "$here/../stream-test.flv")"
"$binary"
