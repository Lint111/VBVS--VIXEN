@echo off
REM Task 10.1 shadow A/B: baked provider, shadows ON (default). Same shape as
REM run_baked.bat but writes to its own subdir so ON/OFF captures don't clobber
REM each other, and does NOT set VIXEN_OCCUPANCY_JSON (irrelevant to this A/B).
for %%I in ("%~dp0..") do set VIXEN_ROOT=%%~fI
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_SHADOW_CONFIG_ENABLED=1
set VIXEN_PERF_CSV=%VIXEN_ROOT%\temp_bench\baked_shadow_on\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=%VIXEN_ROOT%\temp_bench\baked_shadow_on
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d %VIXEN_ROOT%\VIXEN
binaries\VIXEN.exe > %VIXEN_ROOT%\temp_bench\baked_shadow_on\run.log 2>&1
