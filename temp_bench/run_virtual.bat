@echo off
REM Virtual (analytic) Cornell capability baseline: perf CSV + tick-150 capture + CornellDiag.
REM M6b Task 6b.0 fix: self-resolve via %~dp0 -- see run_baked.bat's identical fix comment.
for %%I in ("%~dp0..") do set VIXEN_ROOT=%%~fI
set VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=1
set VIXEN_PERF_CSV=%VIXEN_ROOT%\temp_bench\virtual\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=%VIXEN_ROOT%\temp_bench\virtual
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d %VIXEN_ROOT%\VIXEN
binaries\VIXEN.exe > %VIXEN_ROOT%\temp_bench\virtual\run.log 2>&1
