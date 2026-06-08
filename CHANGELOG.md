# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows the existing GTUltra/GoatTracker versioning scheme.

## [Unreleased] - 2026-06-05

### Added

- Added `gtultra2raster`, an experimental converter from GTUltra/GoatTracker `.sng` files to Hermit's 1raster tracker formats.
- Added binary 1raster export for `.orm` and `.orb`, selectable with `--format orm|orb|both`.
- Added Kick Assembler `.asm` export from `gtultra2raster`, with selectable player+data and data-only output via `--asm`, `--asm-mode`, `--asm-data`, and `--asm-format`.
- Added 1raster conversion analysis mode with fit checks and conversion-loss warnings.
- Added embedded ORM/ORB 1raster player assets and SID frequency tables for converter output.
- Added GTUltra `.sng` parsing support for GTS3, GTS4, and GTS5 files, including 3-channel and 6-channel song detection.
- Added channel-assignment analysis for mapping GTUltra source channels onto the 3-channel 1raster player model.
- Added `docs/gtultra-to-1raster.md` with composition guidelines for near-lossless conversion to 1raster.
- Added macOS build script `build-macos.sh` that builds into `build/macos`.
- Added Windows command prompt build script `build-windows.bat`.
- Added MSYS2 build script `build-windows-msys2.sh` that builds GTUltra, helper tools, `gtasm`, and `gtultra2raster` into `build/windows`.
- Added `gtasm`, a command-line wrapper around the embedded assembler with raw, PRG, and PSID output modes.
- Added `.gitignore` entries for local build products, object files, BME helper binaries, and the local converter binary.
- Added `FORMAT_ASM` to the packer/relocator export formats.
- Added packer/relocator ASM source output, including author-info patching for generated assembler source.
- Added persistent relocator SID2, SID3, and SID4 address settings to the GTUltra configuration file.

### Changed

- Updated `README.md` with macOS, Windows, MSYS2, and 1raster converter build/use instructions.
- Changed local build outputs to use `build/macos` and `build/windows` so development builds do not overwrite tracked release binaries.
- Changed common make rules to allow configurable `DATAFILE`, `DAT2INC`, `STRIP`, and `STRIP_FAIL_OK` tools.
- Changed C++ make flags so C++ sources no longer inherit C-only options such as `-std=gnu17`.
- Changed Windows make rules to use the full GTUltra application object list for `gtultra.exe`.
- Changed Windows builds to tolerate blocked `strip` operations and leave the executable unstripped instead of failing the build.
- Changed MSYS2 builds to remove stale object files before compiling to avoid mixing macOS/Linux and Windows objects.
- Changed MSYS2 SDL2 linking to include the required Windows system libraries for static SDL2 builds.
- Changed the relocator UI/title string building to use bounded string appends.
- Changed relocator SID address handling to validate SID2/SID3/SID4 addresses and preserve legacy packed SID address compatibility.
- Changed ASM label and address emission in the relocator to avoid fixed 80-byte temporary buffers.
- Changed the embedded assembler wrapper to disable parser debug spew during normal assembly.

### Fixed

- Fixed Windows build failures caused by stale object files from non-Windows builds.
- Fixed Windows link failures caused by missing SDL2 system libraries.
- Fixed Windows link failures caused by missing GTUltra application objects in the custom `gtultra.exe` target.
- Fixed MSYS2 build warnings from passing `-std=gnu17` to C++ compilation.
- Fixed `gt2reloc` warning for `temppackedsongname` in `GT2RELOC` builds.
- Fixed palette-name allocation to reserve space for the terminating NUL and preserve the previous value on allocation failure.
- Fixed pattern-command info lookup so invalid commands report an unknown command instead of indexing past the info string table.
- Fixed `bme_io` reads to validate inputs, clamp reads to available data, and initialize short-read byte buffers.
- Fixed 64-bit pointer logging warnings in the embedded assembler by using `%p`.
- Fixed assembler uninitialized debug variable warnings.
- Fixed generated scanner fallback output/noise and unused generated-function warnings in normal builds.
- Fixed reSID register-update precedence warnings by adding explicit bitwise parentheses.
- Fixed reSID-fp filter initialization so `nonlinearity` is initialized before `set_w0()`.
- Fixed SSE pointer-alignment warning in reSID-fp by using `uintptr_t`.
- Fixed impossible pattern-pointer sentinel comparisons in the display code.
- Fixed top-bar and relocator title string overflow warnings for long song filenames.
- Fixed special-note-name generation to avoid `strncpy` truncation warnings and allocate enough space for multi-digit octave names.
- Fixed macOS makefile `sdl2-config` syntax and newline issues.
