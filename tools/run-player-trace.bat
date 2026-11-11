@echo off
setlocal EnableExtensions
REM %~dp0 must be read before any shift (shift moves %0 too).
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
REM usage: run-player-trace.bat --evidence-root ROOT NAME FRAMES [KEY:VALUE ...]
REM   Runs the native player (VIXEN.exe: its own main(), default scene, no capture test, no PNG or file
REM   output) for FRAMES frames with the frame trace and the perf CSV written into ROOT\NAME\.
REM   ROOT\NAME must not exist (earlier evidence is never overwritten). Measurement helper for the
REM   benchmark build (UNDERSET_NATIVE_BENCHMARK); offscreen presentation (see below).
set "CAPTURE_ROOT="
if /i "%~1"=="--evidence-root" (
    set "CAPTURE_ROOT=%~2"
    shift
    shift
)
if not defined CAPTURE_ROOT if defined VIXEN_CAPTURE_EVIDENCE_ROOT set "CAPTURE_ROOT=%VIXEN_CAPTURE_EVIDENCE_ROOT%"
if not defined CAPTURE_ROOT (
    echo run-player-trace: --evidence-root ROOT is required 1>&2
    exit /b 2
)
set "OUT=%CAPTURE_ROOT%\%~1"
if exist "%OUT%" (
    echo run-player-trace: %OUT% already exists; refusing to overwrite earlier evidence 1>&2
    exit /b 2
)
mkdir "%OUT%"
REM Offscreen presentation (no window, no swapchain): a hidden window reports a zero-extent surface on
REM the workstation driver, so the player cannot start with one. Everything else is the player's path.
set VIXEN_HUD_OFFSCREEN_CAPTURE=1
set VIXEN_HUD_SUPPRESS=1
set "VIXEN_EXIT_AFTER_FRAMES=%~2"
set "VIXEN_PERF_CSV=%OUT%\perf.csv"
set "VIXEN_FRAME_TRACE=%OUT%\frame-trace.csv"
set "VIXEN_PHASE_TIMING=1"
shift
shift
:args
if "%~1"=="" goto run
for /f "tokens=1,* delims=:" %%A in ("%~1") do set "%%A=%%B"
shift
goto args
:run
set "PLAYER_DIR=%ROOT%\build\ninja-release\binaries"
cd /d "%PLAYER_DIR%"
"%PLAYER_DIR%\VIXEN.exe" > "%OUT%\run.log" 2>&1
echo exit %errorlevel%
exit /b %errorlevel%
