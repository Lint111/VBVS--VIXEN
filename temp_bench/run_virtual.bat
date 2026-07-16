@echo off
REM Virtual (analytic) Cornell capability baseline: perf CSV + tick-150 capture + CornellDiag.
set VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=1
set VIXEN_PERF_CSV=C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\virtual\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\virtual
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\VIXEN
binaries\VIXEN.exe > C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\virtual\run.log 2>&1
