@echo off
REM ============================================================================
REM build.bat - Build the 64-bit Windows nd100x.exe using w64devkit.
REM
REM Usage:
REM   build.bat              - debug build (default, matches `make debug`)
REM   build.bat debug        - debug build, explicit
REM   build.bat release      - release build
REM   build.bat clean        - delete build\ and build_release\
REM
REM Requirements:
REM   - w64devkit (64-bit) extracted somewhere on disk.
REM     Default path: C:\Utils\w64devkit
REM     Override by setting the W64DEVKIT env var before running this script:
REM       set W64DEVKIT=D:\tools\w64devkit
REM       build.bat
REM
REM   - Nothing else. HTTP image loads (floppy-DB downloads) use WinHTTP,
REM     a Windows system DLL, so no libcurl package is needed and no DLL
REM     or certificate file is staged next to the exe.
REM
REM This script does NOT permanently modify your PATH. It only prepends
REM w64devkit\bin for the duration of this build.
REM ============================================================================

setlocal enableextensions enabledelayedexpansion

REM --- Locate w64devkit ------------------------------------------------------
if "%W64DEVKIT%"=="" set "W64DEVKIT=C:\Utils\w64devkit"

if not exist "%W64DEVKIT%\bin\gcc.exe" (
    echo ERROR: w64devkit not found at "%W64DEVKIT%".
    echo.
    echo Either install w64devkit there, or point this script at your install:
    echo     set W64DEVKIT=D:\path\to\w64devkit
    echo     build.bat
    exit /b 1
)

REM Prepend w64devkit\bin so gcc, make, cmake, ninja, sh, pkg-config resolve
REM to w64devkit first (ahead of any MSYS2 / Cygwin installs already on PATH).
set "PATH=%W64DEVKIT%\bin;%PATH%"

REM --- Sanity-check the toolchain --------------------------------------------
where gcc   >nul 2>&1 || ( echo ERROR: gcc not found on PATH after adding w64devkit. & exit /b 1 )
where make  >nul 2>&1 || ( echo ERROR: make not found on PATH after adding w64devkit. & exit /b 1 )
where sh    >nul 2>&1 || ( echo ERROR: sh not found on PATH after adding w64devkit. & exit /b 1 )
where ninja >nul 2>&1 || ( echo ERROR: ninja not found on PATH after adding w64devkit. & exit /b 1 )
where cmake >nul 2>&1 || ( echo ERROR: cmake not found on PATH after adding w64devkit. & exit /b 1 )

echo.
echo Using w64devkit at: %W64DEVKIT%
for /f "tokens=*" %%v in ('gcc --version ^| findstr /r "^gcc"') do echo   %%v
echo.

REM --- Pick the target -------------------------------------------------------
set "TARGET=debug"
if /i "%~1"=="debug"   set "TARGET=debug"
if /i "%~1"=="release" set "TARGET=release"
if /i "%~1"=="clean"   set "TARGET=clean"

if "%TARGET%"=="clean" (
    echo Cleaning build directories...
    make clean
    if errorlevel 1 exit /b 1
    echo Done.
    exit /b 0
)

REM --- Build -----------------------------------------------------------------
echo Building %TARGET%...
make %TARGET%
if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    exit /b 1
)

if "%TARGET%"=="release" (
    set "OUTDIR=build_release\bin"
) else (
    set "OUTDIR=build\bin"
)

echo.
echo ============================================================
echo Build complete.
echo   Binary: %OUTDIR%\nd100x.exe
echo.
echo Run it:
echo   Boot from SMD disk:
echo     %OUTDIR%\nd100x.exe --boot=smd
echo   Boot from floppy:
echo     %OUTDIR%\nd100x.exe --boot=floppy --image=FLOPPY.IMG
echo ============================================================

endlocal
