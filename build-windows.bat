@echo off
setlocal EnableExtensions

if /I "%~1"=="-h" goto usage
if /I "%~1"=="--help" goto usage
if "%~1"=="/?" goto usage
if not "%~2"=="" goto bad_args

set "TARGET=all"
if not "%~1"=="" (
    if /I "%~1"=="clean" (
        set "TARGET=clean"
    ) else (
        goto bad_args
    )
)

if not "%OS%"=="Windows_NT" (
    echo error: this script is intended for Windows. 1>&2
    exit /b 1
)

set "ROOT=%~dp0"
set "SRC_DIR=%ROOT%src"
set "BME_DIR=%SRC_DIR%\bme"
set "OUT_DIR=%ROOT%build\windows"

set "MAKE_CMD=%MAKE%"
if "%MAKE_CMD%"=="" call :find_tool mingw32-make MAKE_CMD
if "%MAKE_CMD%"=="" call :find_tool make MAKE_CMD
if "%MAKE_CMD%"=="" (
    echo error: GNU make was not found. Install MSYS2 MinGW and add it to PATH. 1>&2
    exit /b 1
)

call :require_tool gcc "Install MSYS2 MinGW gcc and add mingw64\bin or ucrt64\bin to PATH." || exit /b 1
call :require_tool g++ "Install MSYS2 MinGW g++ and add mingw64\bin or ucrt64\bin to PATH." || exit /b 1
call :require_tool windres "Install MinGW binutils and add mingw64\bin or ucrt64\bin to PATH." || exit /b 1
call :require_tool strip "Install MinGW binutils and add mingw64\bin or ucrt64\bin to PATH." || exit /b 1
if "%GTULTRA_VIDEO%"=="1" (
    call :require_tool pkg-config "Install MSYS2 MinGW pkg-config and FFmpeg development libraries, then add mingw64\bin or ucrt64\bin to PATH." || exit /b 1
    pkg-config --exists libavformat libavcodec libavutil libswscale
    if errorlevel 1 (
        echo error: FFmpeg development libraries were not found by pkg-config. 1>&2
        exit /b 1
    )
)

if /I "%TARGET%"=="clean" goto clean

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo Building BME helper tools...
pushd "%BME_DIR%" || exit /b 1
"%MAKE_CMD%" -f makefile.win
if errorlevel 1 exit /b 1
popd

echo Building GTUltra Windows binaries...
pushd "%SRC_DIR%" || exit /b 1
if not "%SDL2_PREFIX%"=="" (
    set "SDL_LIBS=-L%SDL2_PREFIX%\lib -lmingw32 -mwindows -lSDL2main -lSDL2 -lwinmm -lsetupapi -lole32 -loleaut32 -limm32 -lversion -lcfgmgr32 -static-libstdc++ -static-libgcc -static"
    if "%GTULTRA_VIDEO%"=="1" set "SDL_LIBS=-L%SDL2_PREFIX%\lib -lmingw32 -mwindows -lSDL2main -lSDL2 -lwinmm -lsetupapi -lole32 -loleaut32 -limm32 -lversion -lcfgmgr32 -static-libstdc++ -static-libgcc"
    "%MAKE_CMD%" -f makefile.win PREFIX=../build/windows/ DATAFILE=./bme/datafile.exe DAT2INC=./bme/dat2inc.exe GTULTRA_VIDEO=%GTULTRA_VIDEO% "CFLAGS=-std=gnu17 -Ibme -Iasm -O3 -Wall" "LIBS=%SDL_LIBS%"
) else (
    set "SDL_LIBS=-lmingw32 -mwindows -lSDL2main -lSDL2 -lwinmm -lsetupapi -lole32 -loleaut32 -limm32 -lversion -lcfgmgr32 -static-libstdc++ -static-libgcc -static"
    if "%GTULTRA_VIDEO%"=="1" set "SDL_LIBS=-lmingw32 -mwindows -lSDL2main -lSDL2 -lwinmm -lsetupapi -lole32 -loleaut32 -limm32 -lversion -lcfgmgr32 -static-libstdc++ -static-libgcc"
    "%MAKE_CMD%" -f makefile.win PREFIX=../build/windows/ DATAFILE=./bme/datafile.exe DAT2INC=./bme/dat2inc.exe GTULTRA_VIDEO=%GTULTRA_VIDEO% "CFLAGS=-std=gnu17 -Ibme -Iasm -O3 -Wall" "LIBS=%SDL_LIBS%"
)
if errorlevel 1 exit /b 1
popd

echo Building gtultra2raster converter...
g++ -std=c++17 -O2 -Wall -Wextra "%ROOT%tools\gtultra2raster\gtultra2raster.cpp" -o "%OUT_DIR%\gtultra2raster.exe"
if errorlevel 1 exit /b 1

set "SDL_DLL_COPIED=0"
if exist "%ROOT%win32\SDL2.dll" (
    copy /Y "%ROOT%win32\SDL2.dll" "%OUT_DIR%\SDL2.dll" >nul
    set "SDL_DLL_COPIED=1"
)
if "%SDL_DLL_COPIED%"=="0" if not "%SDL2_PREFIX%"=="" if exist "%SDL2_PREFIX%\bin\SDL2.dll" (
    copy /Y "%SDL2_PREFIX%\bin\SDL2.dll" "%OUT_DIR%\SDL2.dll" >nul
    set "SDL_DLL_COPIED=1"
)
if "%SDL_DLL_COPIED%"=="0" (
    echo warning: SDL2.dll was not found. Copy it beside gtultra.exe before running. 1>&2
)

if "%GTULTRA_VIDEO%"=="1" (
    call :copy_ffmpeg_runtime
)

if exist "%ROOT%win32\gtultra.cfg" copy /Y "%ROOT%win32\gtultra.cfg" "%OUT_DIR%\gtultra.cfg" >nul

echo Built Windows binaries in "%OUT_DIR%".
exit /b 0

:copy_ffmpeg_runtime
set "FFMPEG_RUNTIME_LIST=%ROOT%win32\ffmpeg-runtime-dlls.txt"
if "%FFMPEG_PREFIX%"=="" (
    for /F "delims=" %%P in ('pkg-config --variable=prefix libavformat 2^>nul') do set "FFMPEG_PREFIX=%%P"
)
if not exist "%FFMPEG_RUNTIME_LIST%" (
    echo warning: %FFMPEG_RUNTIME_LIST% was not found. Falling back to FFmpeg prefix runtime DLLs. 1>&2
    goto copy_ffmpeg_prefix_dlls
)
for /F "usebackq delims=" %%D in ("%FFMPEG_RUNTIME_LIST%") do (
    if not "%FFMPEG_PREFIX%"=="" if exist "%FFMPEG_PREFIX%\bin\%%D" (
        copy /Y "%FFMPEG_PREFIX%\bin\%%D" "%OUT_DIR%\%%D" >nul
    )
    if not exist "%OUT_DIR%\%%D" if exist "%ROOT%win32\%%D" (
        copy /Y "%ROOT%win32\%%D" "%OUT_DIR%\%%D" >nul
    )
    if not exist "%OUT_DIR%\%%D" (
        echo warning: FFmpeg runtime DLL %%D was not found. 1>&2
    )
)
:copy_ffmpeg_prefix_dlls
if not "%FFMPEG_PREFIX%"=="" if exist "%FFMPEG_PREFIX%\bin\*.dll" (
    for %%D in ("%FFMPEG_PREFIX%\bin\*.dll") do (
        copy /Y "%%~fD" "%OUT_DIR%\%%~nxD" >nul
    )
)
exit /b 0

:clean
echo Cleaning Windows build outputs...
pushd "%SRC_DIR%" || exit /b 1
"%MAKE_CMD%" -f makefile.win PREFIX=../build/windows/ DATAFILE=./bme/datafile.exe DAT2INC=./bme/dat2inc.exe "CFLAGS=-std=gnu17 -Ibme -Iasm -O3 -Wall" clean
if errorlevel 1 exit /b 1
popd
if exist "%OUT_DIR%\gtultra2raster.exe" del "%OUT_DIR%\gtultra2raster.exe"
echo Clean complete.
exit /b 0

:find_tool
where %~1 >nul 2>nul
if not errorlevel 1 set "%~2=%~1"
exit /b 0

:require_tool
where %~1 >nul 2>nul
if errorlevel 1 (
    echo error: %~1 was not found. %~2 1>&2
    exit /b 1
)
exit /b 0

:usage
call :usage_text
exit /b 0

:usage_text
echo Usage: build-windows.bat [clean]
echo.
echo Builds GTUltra, gtasm, and gtultra2raster for Windows into build\windows.
echo.
echo Requirements:
echo   - MSYS2 MinGW or another MinGW-w64 toolchain on PATH
echo   - GNU make, gcc, g++, windres, strip
echo   - SDL2 MinGW development libraries
echo   - Optional MP4 video support: FFmpeg development libraries and pkg-config
echo.
echo If SDL2 is not on the default MinGW library path, set SDL2_PREFIX first:
echo   set SDL2_PREFIX=C:\msys64\mingw64
echo.
echo To build optional muted MP4 video support:
echo   set GTULTRA_VIDEO=1
echo   set FFMPEG_PREFIX=C:\msys64\ucrt64
exit /b 0

:bad_args
echo error: unsupported arguments. 1>&2
echo.
call :usage_text
exit /b 2
