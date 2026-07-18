@echo off
REM M8 validation: OMEGA=1.0 reference baked Cornell render (shader #define temporarily 1.0).
for %%I in ("%~dp0..") do set VIXEN_ROOT=%%~fI
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_PERF_CSV=%VIXEN_ROOT%\temp_bench\m8_o10\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=%VIXEN_ROOT%\temp_bench\m8_o10
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d %VIXEN_ROOT%\VIXEN
binaries\VIXEN.exe > %VIXEN_ROOT%\temp_bench\m8_o10\run.log 2>&1
