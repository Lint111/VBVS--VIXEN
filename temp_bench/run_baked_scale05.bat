@echo off
REM Baked-Perf M6 Task 6.5: render-scale capability curve @ 0.5.
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_RENDER_SCALE=0.5
set VIXEN_PERF_CSV=C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\scale05\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\scale05
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\VIXEN
binaries\VIXEN.exe > C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\scale05\run.log 2>&1
