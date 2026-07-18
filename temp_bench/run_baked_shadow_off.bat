@echo off
REM Task 10.1 shadow A/B: baked provider, shadows OFF (VIXEN_SHADOW_CONFIG_ENABLED=0,
REM ShadowConfigNode.cpp:40-41). Own subdir, mirrors run_baked_shadow_on.bat otherwise.
for %%I in ("%~dp0..") do set VIXEN_ROOT=%%~fI
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_SHADOW_CONFIG_ENABLED=0
set VIXEN_PERF_CSV=%VIXEN_ROOT%\temp_bench\baked_shadow_off\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=%VIXEN_ROOT%\temp_bench\baked_shadow_off
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d %VIXEN_ROOT%\VIXEN
binaries\VIXEN.exe > %VIXEN_ROOT%\temp_bench\baked_shadow_off\run.log 2>&1
