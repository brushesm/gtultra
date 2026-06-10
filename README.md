# GTUltra 1.1.0 - Based on GoatTracker v2.76 Stereo
------------------------
## Building on macOS

Install Xcode command line tools and SDL2, then build from the repository root:

```sh
brew install sdl2
./build-macos.sh
```

The script writes binaries to `build/macos` so local builds do not overwrite the tracked release binaries in `mac/`.

Optional muted MP4 video sync support can be enabled with FFmpeg:

```sh
brew install ffmpeg pkg-config
GTULTRA_VIDEO=1 ./build-macos.sh
```

## Building on Windows

Install MSYS2 MinGW with GCC, GNU Make, binutils, and SDL2 development libraries:

```sh
pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-SDL2
```

From an MSYS2 shell, run:

```sh
./build-windows-msys2.sh
```

The script writes binaries to `build/windows` so local builds do not overwrite the tracked release binaries in `win32/`.

From a Windows command prompt with the MinGW `bin` directory on `PATH`, run:

```bat
build-windows.bat
```

If SDL2 is installed outside the default MinGW search path for the batch script, set `SDL2_PREFIX` first, for example `set SDL2_PREFIX=C:\msys64\mingw64`.

Optional muted MP4 video sync support can be enabled from MSYS2 with FFmpeg. This is only required when `GTULTRA_VIDEO=1` is set; normal Windows builds do not need FFmpeg.

For the recommended UCRT64 MSYS2 shell, install the matching development packages:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-pkgconf
GTULTRA_VIDEO=1 ./build-windows-msys2.sh
```

If you see this error:

```text
error: FFmpeg development libraries were not found. Install the matching MSYS2 ffmpeg package for your MinGW environment.
```

then `GTULTRA_VIDEO=1` is enabled but `pkg-config` cannot find the FFmpeg headers/import libraries for the active MSYS2 environment. Install the package set that matches the shell/toolchain you are using:

- UCRT64: `mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-pkgconf`
- MINGW64: `mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-pkgconf`
- CLANG64: `mingw-w64-clang-x86_64-ffmpeg mingw-w64-clang-x86_64-pkgconf`

To build without MP4 support, leave `GTULTRA_VIDEO` unset:

```sh
unset GTULTRA_VIDEO
./build-windows-msys2.sh
```

From a Windows command prompt, use the same MSYS2 UCRT64 toolchain on `PATH` and set `GTULTRA_VIDEO=1` before running `build-windows.bat`:

```bat
set PATH=C:\msys64\ucrt64\bin;%PATH%
set GTULTRA_VIDEO=1
build-windows.bat
```

The video-enabled build still needs the MSYS2 FFmpeg development package for headers, import libraries, and `pkg-config`. The repository also carries a baseline runtime DLL set in `win32/`, derived from the MSYS2 UCRT64 `mingw-w64-ucrt-x86_64-ffmpeg` package. When `GTULTRA_VIDEO=1`, the MSYS2 build script copies the listed runtime DLLs and then discovers the actual DLL dependency closure from the built executables, so `build/windows` matches the installed MSYS2 package versions.

The vendored `win32/*.dll` files are runtime DLLs only. They do not replace the FFmpeg development headers, import libraries, or `pkg-config` metadata needed to compile a `GTULTRA_VIDEO=1` build.

Video-enabled Windows builds intentionally do not use the full `-static` linker flag. FFmpeg is linked through the MSYS2 import libraries and shipped with the DLLs listed below. This avoids pulling in FFmpeg's large static dependency graph, which can produce missing `-ldl`/`-lshaderc_shared` errors or Rust duplicate-symbol failures from optional codec libraries.

For a distributable Windows video build, include these files from `build/windows` together:

- `gtultra.exe`
- `gtultra.cfg`
- `SDL2.dll`
- every FFmpeg/runtime DLL copied from `win32/ffmpeg-runtime-dlls.txt`

If you intentionally refresh FFmpeg to a different MSYS2 package version, replace the DLLs in `win32/`, update `win32/ffmpeg-runtime-dlls.txt`, and rebuild. `FFMPEG_PREFIX` is only a fallback for missing vendored DLLs or for a deliberate refresh, for example `set FFMPEG_PREFIX=C:\msys64\ucrt64`. If a video-enabled Windows build succeeds but `gtultra.exe` does not start, rebuild with `GTULTRA_VIDEO=1 ./build-windows-msys2.sh` so the runtime DLL closure is regenerated in `build/windows`.

## Optional MP4 Video Sync

When built with `GTULTRA_VIDEO=1`, GTUltra can load a muted MP4 reference video in a separate SDL window. Music playback remains the master clock: video follows play, stop, restart, seek, rewind, fast-forward, and pattern loop resets.

- `Ctrl+F10`: load an MP4 video for the current session.
- `Ctrl+Shift+F10`: close the video window.
- Dragging an `.mp4` or `.m4v` file into GTUltra also loads it as video.
- Video paths are not saved in `.sng` files or `gtultra.cfg`.

## GTUltra to 1raster Conversion

An experimental converter for Hermit's 1raster tracker lives in `tools/gtultra2raster`.

```sh
make -C tools/gtultra2raster
tools/gtultra2raster/gtultra2raster examples/Jammer/\$3GarysGlitteringSaliva_4x.sng -o /tmp/gary --asm
```

By default the converter writes binary `.orm` and `.orb` files when the tune fits each target. `--format orm|orb|both` selects binary targets. `--asm` emits Kick Assembler source with the embedded 1raster player, while `--asm-mode data` or `--asm-data` emits only the tune data for inclusion with another player build.

See `docs/gtultra-to-1raster.md` for composing guidelines and conversion limits.

## Attribution
 - Original Editor by Lasse Öörni (loorni@gmail.com)
 - HardSID 4U support by Téli Sándor. 
 - Uses reSID engine by Dag Lem. 
 - Uses reSID distortion / nonlinearity by Antti Lankila. 
 - Uses 6510 crossassembler from Exomizer2 beta by Magnus Lind. 
 - Uses the SDL library. 
 - GoatTracker icon by Antonio Vera. 
 - Command quick reference by Simon Bennett. 
 - Patches by Stefan A. Haubenthal, Valerio Cannone, Raine M. Ekman and Tero Lindeman. 
 - Microtonal support by Birgit Jauernig.
 - GTUltra: Editor + 6510 code changes: Jason Page
 - Uses RtMidi library Distributed under GNU General Public License (see the file COPYING for details)
 - Covert BitOps homepage: http://covertbitops.c64.org
 - GoatTracker 2 SourceForge.net page: http://sourceforge.net/projects/goattracker2

GTUltra project page (inc. source code): https://github.com/jpage8580/GTUltra 
## GTUltra Command line differences: 
-cxx Max SID channels (3,6,9,12). 
-px UI palette preset (0-3) 
-vff.f master volume (floating point value) 
-dff.f detune (floating point-1 > 1) 
- See GTUltra.PDF for full list of changes Differences to normal version: (refer to normal GT2 for full documentation) 
- Only buffered write playroutine without zeropage ghostreg support can be used. 
- To play sound effects on the second SID, use channel indexes 21, 28 and 35 for voices 1-3 respectively (in the X register) 
- Command line differences: 
-Lxx (SID address parameter) takes addresses of both sids written one after another, right SID in the high word, for example D500D400 
-Hxx (use hardsid) is in hexadecimal format. High nybble specifies right hardsid ID and low nybble left hardsid ID. If right hardsid ID is omitted it is assumed to be left+1. For example 21 tells to use ID 1 for right & ID 0 for left. 
- Songdata is otherwise same as normally, but there are 6 orderlists for each subtune. 
- SHIFT+F9 Switch between mono and stereo mode In mono mode, an 'M' appears in the title row. 
- Submit a patch if there are bugs in the stereo hardsid output, I have no means to test it. 
***
## GoatTracker 2 changelogs
 - v2.59 
   - Fixed channels 4-6 not setting global tempo. 
   - Added missing channel 4-6 playback start checks to the editor playroutine. 
   - Mono songs can be loaded (detection relies on checking song order- list validity and is not 100% certain.) 
   - Vertical resolution increased. 
 - v2.6 
   - Fixed channel 4-6 filter commands. 
   - Fixed help screen mouse scrolling. 
   - Fixed pattern default length selection display when decrementing from a value of 100 or higher. 
   - Fixed mouse selection of pattern when adjusting an adjacent channel. 
   - Fixed help screen instructions. 
   - Changed resolution to 800x600 (pattern display tightened.) 
   - Changed speed of page up/page down scrolling to be faster. 
   - Optimized text output routines. 
 - v2.61 
   - Fixed SHIFT+channel number in orderlist edit mode (swap orderlists) to work with channels 4-6. 
   - Fixed muting. 
   - Added the backquote key (top-left on keyboard) to select channel in pattern edit mode, and to select table in table edit mode. Use with SHIFT to go backwards. 
   - Added SHIFT+channel number to mute channels in pattern edit mode. 
 - v2.62 
   - Added possibility for realtime calculated note independent (hifi) portamento & vibrato. Warning: has potential for huge rastertime increase. 
 - v2.63 
   - Fixed note independent portamento & vibrato to use the last note set in wavetable for calculations, instead of the last note in patterndata. 
 - v2.64 
   - Fixed paste in table (SHIFT+V) working also without SHIFT pressed.
 - v2.65 
   - Fixed raw keycodes over 511 interpreted as some other keys in the 0-511 range. 
 - v2.66 
   - Permit running without sound. 
 - v2.67
   - Cycle-exact HardSID playback. 
   - Configurable cycle-exact HardSID buffer length (separate for inter- active and playback mode, see /T and /U command line options) 
 - v2.68
   - Fixed sound uninit crash with multicore processors (?) 
   - SID register write order tweaked to resemble JCH NewPlayer 21. 
   - New reSID-fp engine (with distortion & nonlinearity) from Antti Lankila integrated. Activated with /I command line option parameters 2 & 3. 
   - Command quick reference by Simon Bennett included. 
 - v2.69 
   - Fixed click bug in reSID audio output. 
   - Newest reSID-fp code integrated. 
   - reSID-fp filter parameters adjustable from the configuration file. 
 - v2.70 
   - Fixed possible crash on some versions of the HardSID dll.
 - v2.71 
   - Added keycode fix patch from Valerio Cannone. 
   - Added fullscreen switch patch from Raine M. Ekman (see /X option) 
   - Added context mode to online help patch from Raine M. Ekman. 
   - Added /G command line option for setting A-4 pitch. 
 - v2.72 
   - Fixed incorrect transpose range determination in the relocator. 
   - Fixed crash in jam mode whan an illegal pattern command was executed from the wavetable. 
 - v2.73 
   - Fixed song init when several subtunes exist. 
 - v2.74 
   - Reverted to old playroutine timing. 
   - Write v3 format SID header for SidPlay to detect the stereo songs properly. 
 - v2.75 
   - Fixed track length not properly updated when swapping tracks. 
   - Added merge song function to the stereo version. 
   - Added dotted pattern display modes (-D2 and -D3.) 
 - v2.76 
   - Added /Q command line option for setting equal divisions per octave that differ from 12. 
   - Added /J command line option for setting different note names. 
   - Added /Y command line option for setting a path to a Scala tuning file. 
   - Added small color changes to the pattern table for better readability. 
   - Added isomorphic key layout. 
   - Added switch between mono mode and stereo mode (SHIFT+F9). 
   - Added /w command line option for setting 4 different window sizes.
