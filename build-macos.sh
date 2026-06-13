#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: ./build-macos.sh

Builds GTUltra for macOS into build/macos.

Requirements:
  - Xcode command line tools
  - SDL2 development package, for example: brew install sdl2
  - FFmpeg and pkg-config for default MP4 video support: brew install ffmpeg pkg-config

MP4 video sync and ProTracker MOD audio preview are built by default.
Set GTULTRA_VIDEO=0 to build without MP4 video support.
Set GTULTRA_LIBXMP=0 to build without MOD audio preview.
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

export GTULTRA_VIDEO="${GTULTRA_VIDEO:-1}"

if [[ "$GTULTRA_VIDEO" == "1" ]]; then
    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "error: pkg-config was not found. Install video dependencies with: brew install ffmpeg pkg-config, or set GTULTRA_VIDEO=0" >&2
        exit 1
    fi
    if ! pkg-config --exists libavformat libavcodec libavutil libswscale; then
        echo "error: FFmpeg development libraries were not found. Install them with: brew install ffmpeg, or set GTULTRA_VIDEO=0" >&2
        exit 1
    fi
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$ROOT_DIR/src"
BME_DIR="$SRC_DIR/bme"
OUT_DIR="$ROOT_DIR/build/macos"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || printf '4')"
export GTULTRA_LIBXMP="${GTULTRA_LIBXMP:-1}"

if [[ "$GTULTRA_LIBXMP" == "1" && ! -f "$ROOT_DIR/3rdparty/libxmp/include/xmp.h" ]]; then
    echo "error: vendored libxmp was not found at 3rdparty/libxmp" >&2
    exit 1
fi

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

echo "Removing stale object files from previous non-macOS builds..."
find "$SRC_DIR" -name '*.o' -type f -delete
if [[ -d "$ROOT_DIR/3rdparty/libxmp" ]]; then
    find "$ROOT_DIR/3rdparty/libxmp" -name '*.o' -type f -delete
fi

(
    cd "$SRC_DIR"
    make -f makefile.mac -j "$JOBS" PREFIX=../build/macos/
)

echo "Built macOS binaries in $OUT_DIR"
