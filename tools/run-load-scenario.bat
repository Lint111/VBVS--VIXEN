@echo off
setlocal EnableExtensions
REM %~dp0 must be read before any shift (shift moves %0 too).
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
REM usage: run-load-scenario.bat --evidence-root ROOT NAME [KEY:VALUE ...]
REM   Runs vixen_load_scenario (deterministic heavy dynamic load timeline) with the perf CSV, the
REM   frame trace and phase timing written into ROOT\NAME\. ROOT\NAME must not exist (earlier
REM   evidence is never overwritten). KEY:VALUE pairs become environment variables for the run
REM   (resolution VIXEN_WINDOW_WIDTH/HEIGHT and the illumination feature settings).
set "CAPTURE_ROOT="
if /i "%~1"=="--evidence-root" (
    set "CAPTURE_ROOT=%~2"
    shift
    shift
)
if not defined CAPTURE_ROOT if defined VIXEN_CAPTURE_EVIDENCE_ROOT set "CAPTURE_ROOT=%VIXEN_CAPTURE_EVIDENCE_ROOT%"
if not defined CAPTURE_ROOT (
    echo run-load-scenario: --evidence-root ROOT is required 1>&2
    exit /b 2
)
set "OUT=%CAPTURE_ROOT%\%~1"
if exist "%OUT%" (
    echo run-load-scenario: %OUT% already exists; refusing to overwrite earlier evidence 1>&2
    exit /b 2
)
mkdir "%OUT%"
set "EXE=%ROOT%\build\ninja-release\application\main\vixen_load_scenario.exe"
set VIXEN_HIDDEN_WINDOW=1
set VIXEN_HDR_EXPOSURE=1
set VIXEN_HUD_SUPPRESS=1
set "VIXEN_PERF_CSV=%OUT%\perf.csv"
set "VIXEN_FRAME_TRACE=%OUT%\frame-trace.csv"
set "VIXEN_PHASE_TIMING=1"
shift
:args
if "%~1"=="" goto run
for /f "tokens=1,* delims=:" %%A in ("%~1") do set "%%A=%%B"
shift
goto args
:run
REM sn-gpumem1: tell the queue which Windows tree this run is, so its per-job CPU and VRAM are attributed (no-op when run by hand).
if defined UT_QUEUE_WINROOT_FILE powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\VIXEN\scripts\build\Register-NativeRunRoot.ps1" >nul 2>&1
REM sn-gpumem1: VIXEN_GPU_LEDGER:1 loads the GPU ledger layer (benchmark builds) and writes its CSVs next to perf.csv. An elevated
REM process ignores VK_LAYER_PATH, so the layer manifest is registered (HKLM ExplicitLayers) for the length of this run only.
set "LEDGER_JSON=%ROOT%\build\ninja-release\gpu-ledger-layer\VkLayer_vixen_gpu_ledger.json"
if defined VIXEN_GPU_LEDGER (
    set "VIXEN_GPU_LEDGER_DIR=%OUT%"
    set "VK_INSTANCE_LAYERS=VK_LAYER_VIXEN_gpu_ledger"
    reg add "HKLM\SOFTWARE\Khronos\Vulkan\ExplicitLayers" /v "%LEDGER_JSON%" /t REG_DWORD /d 0 /f >nul 2>&1
)
cd /d "%ROOT%\build\ninja-release\application\main"
"%EXE%" --out "%OUT%" > "%OUT%\run.log" 2>&1
set RC=%errorlevel%
if defined VIXEN_GPU_LEDGER reg delete "HKLM\SOFTWARE\Khronos\Vulkan\ExplicitLayers" /v "%LEDGER_JSON%" /f >nul 2>&1
echo exit %RC%
exit /b %RC%
