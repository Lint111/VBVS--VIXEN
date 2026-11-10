@echo off
setlocal EnableExtensions
REM %~dp0 must be read before any shift (shift moves %0 too).
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
REM usage: run-capture.bat --evidence-root ROOT NAME STATE ANGLE [KEY:VALUE ...]
REM   --evidence-root ROOT   directory that receives NAME\ (required; VIXEN_CAPTURE_EVIDENCE_ROOT is
REM                          the fallback). ROOT\NAME must not exist: an earlier capture's evidence is
REM                          never deleted, a reused NAME fails with exit 2.
set "CAPTURE_ROOT="
if /i "%~1"=="--evidence-root" (
    set "CAPTURE_ROOT=%~2"
    shift
    shift
)
if not defined CAPTURE_ROOT if defined VIXEN_CAPTURE_EVIDENCE_ROOT set "CAPTURE_ROOT=%VIXEN_CAPTURE_EVIDENCE_ROOT%"
if not defined CAPTURE_ROOT (
    echo run-capture: --evidence-root ROOT is required 1>&2
    exit /b 2
)
set "OUT=%CAPTURE_ROOT%\%~1"
if exist "%OUT%" (
    echo run-capture: %OUT% already exists; refusing to overwrite earlier evidence 1>&2
    exit /b 2
)
mkdir "%OUT%"
set "EXE=%ROOT%\build\ninja-release\application\main\vixen_lookdev_capture.exe"
set VIXEN_HIDDEN_WINDOW=1
set VIXEN_HDR_EXPOSURE=1
set VIXEN_HUD_SUPPRESS=1
set VIXEN_PIXEL_GOLDEN_RECORD=1
set "VIXEN_LOOKDEV_CAPTURE_STATE=%~2"
set "VIXEN_LOOKDEV_CAPTURE_ANGLE=%~3"
set "VIXEN_LOOKDEV_CAPTURE_DIR=%OUT%"
set VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=900000
set "VIXEN_PERF_CSV=%OUT%\perf.csv"
set "VIXEN_PHASE_TIMING=1"
shift
shift
shift
if "%~1"=="-" shift
:args
if "%~1"=="" goto run
for /f "tokens=1,* delims=:" %%A in ("%~1") do set "%%A=%%B"
shift
goto args
:run
"%EXE%" --gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically > "%OUT%\run.log" 2>&1
echo exit %errorlevel%
exit /b %errorlevel%
