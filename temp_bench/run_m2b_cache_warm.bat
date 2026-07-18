@echo off
REM Baked-perf-pipeline M2b Task 2b.2: cache-WARM boot (same cache dir as the cold run, now populated).
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_PERF_CSV=C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\m2b_cache_warm\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\m2b_cache_warm
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\VIXEN
binaries\VIXEN.exe > C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\m2b_cache_warm\run.log 2>&1
