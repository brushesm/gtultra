#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: ./build-windows-msys2.sh [clean]

Builds GTUltra, gtasm, and gtultra2raster for Windows into build/windows from an MSYS2 shell.

Requirements:
  pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-SDL2

Default MP4 video support:
  pacman -S --needed mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-pkgconf

MP4 video sync and ProTracker MOD audio preview are built by default.
Set GTULTRA_VIDEO=0 to build without MP4 video support.
Set GTULTRA_LIBXMP=0 to build without MOD audio preview.

The script also works with mingw64 or clang64 toolchains if those are installed.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ "$#" -gt 1 ]]; then
    echo "error: unsupported arguments" >&2
    usage >&2
    exit 2
fi

target="${1:-all}"
if [[ "$target" != "all" && "$target" != "clean" ]]; then
    echo "error: unsupported target: $target" >&2
    usage >&2
    exit 2
fi

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *)
        echo "error: this script is intended for an MSYS2 shell on Windows" >&2
        exit 1
        ;;
esac

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
src_dir="$root_dir/src"
bme_dir="$src_dir/bme"
out_dir="$root_dir/build/windows"
export GTULTRA_VIDEO="${GTULTRA_VIDEO:-1}"
export GTULTRA_LIBXMP="${GTULTRA_LIBXMP:-1}"

have_mingw_gcc() {
    command -v gcc >/dev/null 2>&1 && gcc -dumpmachine 2>/dev/null | grep -qi 'mingw'
}

activate_mingw_toolchain() {
    if have_mingw_gcc; then
        return
    fi

    if [[ -n "${MINGW_PREFIX:-}" && -x "$MINGW_PREFIX/bin/gcc.exe" ]]; then
        export PATH="$MINGW_PREFIX/bin:$PATH"
    fi

    if have_mingw_gcc; then
        return
    fi

    local prefix
    for prefix in /ucrt64 /mingw64 /clang64 /clangarm64 /mingw32; do
        if [[ -x "$prefix/bin/gcc.exe" ]]; then
            export PATH="$prefix/bin:$PATH"
            break
        fi
    done

    if ! have_mingw_gcc; then
        cat >&2 <<'ERROR'
error: MinGW-w64 gcc was not found.

Install a Windows-targeting MSYS2 toolchain, for example:
  pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-SDL2

Then run this script again from the MSYS2 shell.
ERROR
        exit 1
    fi
}

find_make() {
    if command -v mingw32-make >/dev/null 2>&1; then
        printf '%s\n' mingw32-make
    elif command -v make >/dev/null 2>&1; then
        printf '%s\n' make
    else
        echo "error: GNU make was not found. Install MSYS2 base-devel." >&2
        exit 1
    fi
}

require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: $1 was not found on PATH" >&2
        exit 1
    fi
}

copy_runtime_dll_by_name() {
    local dll="$1"
    local prefix

    [[ -n "$dll" ]] || return 1

    for prefix in "${MINGW_PREFIX:-}" /ucrt64 /mingw64 /clang64 /clangarm64 /mingw32; do
        [[ -n "$prefix" ]] || continue
        if [[ -f "$prefix/bin/$dll" ]]; then
            cp -f "$prefix/bin/$dll" "$out_dir/$dll"
            return 0
        fi
    done

    if [[ -f "$root_dir/win32/$dll" ]]; then
        cp -f "$root_dir/win32/$dll" "$out_dir/$dll"
        return 0
    fi

    return 1
}

copy_discovered_runtime_dependencies() {
    local -a queue=("$@")
    local -A scanned=()
    local file
    local dll
    local copied_path

    if ! command -v objdump >/dev/null 2>&1; then
        echo "warning: objdump was not found. Runtime DLL dependency discovery was skipped." >&2
        return
    fi

    while ((${#queue[@]})); do
        file="${queue[0]}"
        queue=("${queue[@]:1}")

        [[ -f "$file" ]] || continue
        [[ -z "${scanned[$file]:-}" ]] || continue
        scanned[$file]=1

        while IFS= read -r dll; do
            [[ -n "$dll" ]] || continue
            if copy_runtime_dll_by_name "$dll"; then
                copied_path="$out_dir/$dll"
                if [[ -f "$copied_path" && -z "${scanned[$copied_path]:-}" ]]; then
                    queue+=("$copied_path")
                fi
            fi
        done < <(objdump -p "$file" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name: //p')
    done
}

copy_runtime_files() {
    if [[ -f "$root_dir/win32/SDL2.dll" ]]; then
        cp -f "$root_dir/win32/SDL2.dll" "$out_dir/SDL2.dll"
    elif [[ -n "${MINGW_PREFIX:-}" && -f "$MINGW_PREFIX/bin/SDL2.dll" ]]; then
        cp -f "$MINGW_PREFIX/bin/SDL2.dll" "$out_dir/SDL2.dll"
    else
        local prefix
        for prefix in /ucrt64 /mingw64 /clang64 /clangarm64 /mingw32; do
            if [[ -f "$prefix/bin/SDL2.dll" ]]; then
                cp -f "$prefix/bin/SDL2.dll" "$out_dir/SDL2.dll"
                break
            fi
        done
    fi

    if [[ ! -f "$out_dir/SDL2.dll" ]]; then
        echo "warning: SDL2.dll was not found. Copy it beside gtultra.exe before running." >&2
    fi

    if [[ -f "$root_dir/win32/gtultra.cfg" ]]; then
        cp -f "$root_dir/win32/gtultra.cfg" "$out_dir/gtultra.cfg"
    fi

	if [[ "${GTULTRA_VIDEO:-0}" == "1" ]]; then
		local copied
		local dll
		local dll_list="$root_dir/win32/ffmpeg-runtime-dlls.txt"
		local prefix

		if [[ -f "$dll_list" ]]; then
			while IFS= read -r dll; do
				[[ -n "$dll" ]] || continue
				copied=0
				if copy_runtime_dll_by_name "$dll"; then
					copied=1
				fi
				if [[ "$copied" == "0" ]]; then
					echo "warning: FFmpeg runtime DLL $dll was not found." >&2
				fi
			done < "$dll_list"
		else
			echo "warning: $dll_list was not found. Discovering FFmpeg DLLs from built executables only." >&2
		fi

		copy_discovered_runtime_dependencies \
			"$out_dir/gtultra.exe" \
			"$out_dir/gt2reloc.exe"
	fi

}

activate_mingw_toolchain
make_cmd="$(find_make)"
require_tool gcc
require_tool g++
require_tool windres
require_tool strip
if [[ "$GTULTRA_VIDEO" == "1" ]]; then
	require_tool objdump
fi

if [[ "$GTULTRA_LIBXMP" == "1" && ! -f "$root_dir/3rdparty/libxmp/include/xmp.h" ]]; then
    echo "error: vendored libxmp was not found at 3rdparty/libxmp" >&2
    exit 1
fi

if [[ "$GTULTRA_VIDEO" == "1" ]]; then
    require_tool pkg-config
    if ! pkg-config --exists libavformat libavcodec libavutil libswscale; then
        echo "error: FFmpeg development libraries were not found. Install the matching MSYS2 ffmpeg package for your MinGW environment, or set GTULTRA_VIDEO=0." >&2
        exit 1
    fi
fi

if [[ "$target" == "clean" ]]; then
    echo "Cleaning Windows build outputs..."
    rm -f "$out_dir"/*.exe "$out_dir"/SDL2.dll "$out_dir"/gtultra.cfg
    find "$src_dir" -name '*.o' -type f -delete
    if [[ -d "$root_dir/3rdparty/libxmp" ]]; then
        find "$root_dir/3rdparty/libxmp" -name '*.o' -type f -delete
    fi
    echo "Clean complete."
    exit 0
fi

mkdir -p "$out_dir"

echo "Using toolchain: $(gcc -dumpmachine)"
echo "Removing stale object files from previous non-Windows builds..."
find "$src_dir" -name '*.o' -type f -delete
if [[ -d "$root_dir/3rdparty/libxmp" ]]; then
    find "$root_dir/3rdparty/libxmp" -name '*.o' -type f -delete
fi

echo "Building BME helper tools..."
(
    cd "$bme_dir"
    "$make_cmd" -f makefile.win
)

libs='-lmingw32 -mwindows -lSDL2main -lSDL2 -lwinmm -lsetupapi -lole32 -loleaut32 -limm32 -lversion -lcfgmgr32 -static-libstdc++ -static-libgcc -static'
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
    libs="$(pkg-config --static --libs sdl2) -lwinmm -lsetupapi -lole32 -loleaut32 -limm32 -lversion -lcfgmgr32 -static-libstdc++ -static-libgcc -static"
fi
if [[ "$GTULTRA_VIDEO" == "1" ]]; then
    libs=" $libs "
    libs="${libs// -static / }"
    libs="${libs# }"
    libs="${libs% }"
fi

echo "Building GTUltra Windows binaries..."
(
    cd "$src_dir"
    "$make_cmd" -f makefile.win \
        PREFIX=../build/windows/ \
        DATAFILE=./bme/datafile.exe \
        DAT2INC=./bme/dat2inc.exe \
        GTULTRA_VIDEO="$GTULTRA_VIDEO" \
        GTULTRA_LIBXMP="$GTULTRA_LIBXMP" \
        "CFLAGS=-std=gnu17 -Ibme -Iasm -O3 -Wall" \
        "LIBS=$libs"
)

echo "Building gtultra2raster converter..."
g++ -std=c++17 -O2 -Wall -Wextra \
    "$root_dir/tools/gtultra2raster/gtultra2raster.cpp" \
    -o "$out_dir/gtultra2raster.exe"
strip "$out_dir/gtultra2raster.exe" || echo "warning: could not strip gtultra2raster.exe; leaving it unstripped." >&2

copy_runtime_files

echo "Built Windows binaries in $out_dir"
