@echo off
REM M8 validation: committed OMEGA=1.5 baked Cornell render.
for %%I in ("%~dp0..") do set VIXEN_ROOT=%%~fI
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_PERF_CSV=%VIXEN_ROOT%\temp_bench\m8_o15\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=%VIXEN_ROOT%\temp_bench\m8_o15
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d %VIXEN_ROOT%\VIXEN
binaries\VIXEN.exe > %VIXEN_ROOT%\temp_bench\m8_o15\run.log 2>&1
