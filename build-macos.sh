#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: ./build-macos.sh

Builds GTUltra for macOS into build/macos.

Requirements:
  - Xcode command line tools
  - SDL2 development package, for example: brew install sdl2
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ "$#" -ne 0 ]]; then
    echo "error: unsupported arguments" >&2
    usage >&2
    exit 2
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: this build script is intended for macOS" >&2
    exit 1
fi

if ! command -v sdl2-config >/dev/null 2>&1; then
    echo "error: sdl2-config was not found. Install SDL2 with: brew install sdl2" >&2
    exit 1
fi

if ! command -v make >/dev/null 2>&1; then
    echo "error: make was not found. Install the Xcode command line tools." >&2
    exit 1
fi

if ! command -v cc >/dev/null 2>&1; then
    echo "error: cc was not found. Install the Xcode command line tools." >&2
    exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$ROOT_DIR/src"
BME_DIR="$SRC_DIR/bme"
OUT_DIR="$ROOT_DIR/build/macos"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || printf '4')"

mkdir -p "$OUT_DIR"

build_host_tool() {
    local output="$1"
    local needs_build=0
    shift

    if [[ ! -x "$output" ]]; then
        needs_build=1
    else
        local input
        for input in "$@"; do
            if [[ "$input" != -* && "$input" -nt "$output" ]]; then
                needs_build=1
                break
            fi
        done
    fi

    if [[ "$needs_build" -eq 1 ]]; then
        cc "$@" -o "$output"
    fi
}

(
    cd "$BME_DIR"
    build_host_tool dat2inc dat2inc.c
    build_host_tool datafile -I. -ISDL datafile.c bme_end.c
)

(
    cd "$SRC_DIR"
    make -f makefile.mac -j "$JOBS" PREFIX=../build/macos/
)

echo "Built macOS binaries in $OUT_DIR"
