@echo off
REM M10 shadow diagnostic: baked provider, shadows ON, with per-pixel shadow-trace
REM capture armed for the target pixel (VIXEN_SHADOW_DBG_PX/_PY passed by the caller).
REM Writes to temp_bench\shadowdbg\ . Caller sets VIXEN_SHADOW_DBG_PX/_PY and OUTSUB.
for %%I in ("%~dp0..") do set VIXEN_ROOT=%%~fI
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_SHADOW_CONFIG_ENABLED=1
set VIXEN_PERF_CSV=%VIXEN_ROOT%\temp_bench\%OUTSUB%\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=%VIXEN_ROOT%\temp_bench\%OUTSUB%
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d %VIXEN_ROOT%\VIXEN
binaries\VIXEN.exe > %VIXEN_ROOT%\temp_bench\%OUTSUB%\run.log 2>&1
