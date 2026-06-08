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
    "%MAKE_CMD%" -f makefile.win PREFIX=../build/windows/ DATAFILE=./bme/datafile.exe DAT2INC=./bme/dat2inc.exe "CFLAGS=-std=gnu17 -Ibme -Iasm -O3 -Wall" "LIBS=%SDL_LIBS%"
) else (
    "%MAKE_CMD%" -f makefile.win PREFIX=../build/windows/ DATAFILE=./bme/datafile.exe DAT2INC=./bme/dat2inc.exe "CFLAGS=-std=gnu17 -Ibme -Iasm -O3 -Wall" "LIBS=-lmingw32 -mwindows -lSDL2main -lSDL2 -lwinmm -lsetupapi -lole32 -loleaut32 -limm32 -lversion -lcfgmgr32 -static-libstdc++ -static-libgcc -static"
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

if exist "%ROOT%win32\gtultra.cfg" copy /Y "%ROOT%win32\gtultra.cfg" "%OUT_DIR%\gtultra.cfg" >nul

echo Built Windows binaries in "%OUT_DIR%".
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
echo.
echo If SDL2 is not on the default MinGW library path, set SDL2_PREFIX first:
echo   set SDL2_PREFIX=C:\msys64\mingw64
exit /b 0

:bad_args
echo error: unsupported arguments. 1>&2
echo.
call :usage_text
exit /b 2
