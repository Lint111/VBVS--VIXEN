@echo off
REM Baked (stored-SDF) Cornell capability baseline: perf CSV + tick-150 capture + CornellDiag.
REM
REM Baked-Perf M6 Task 6.5 (inventory #12): VIXEN_RENDER_SCALE in (0,1] shrinks the offscreen
REM render target the compute dispatch writes into relative to the swapchain size (M4's
REM render-scale decoupling; ComputeDispatchNode/BlitNode blit it back up to display size).
REM Not set here -- this script always runs the DEFAULT scale=1.0 (full resolution); scale is
REM a merged, validated 27%%-dispatch-cut dial currently sitting UNUSED at 1.0 in the shipped
REM app. To bench a different scale, copy this script and add:
REM     set VIXEN_RENDER_SCALE=0.75
REM (or 0.5, etc.) before the binaries\VIXEN.exe line. Recorded capability curve (Cornell
REM baked, frames 31-160 excl. 150/151, warm run, this machine, 2026-07-17):
REM
REM   scale | cpu_frame_ms | gpu_span_ms | esvo_ms | spatial_reuse_ms | probe_update_ms | OOB
REM   ------|--------------|-------------|---------|------------------|-----------------|------
REM   1.0   |   101.88     |   102.75    |  68.39  |       3.67       |      30.58      | 0
REM   0.75  |    89.44     |    90.56    |  57.58  |       2.88       |      30.00      | 0
REM   0.5   |    72.74     |    73.41    |  28.23  |       2.11       |      42.95      | 0
REM
REM esvo/spatial_reuse scale down with resolution (fewer pixels to march/shade) as expected;
REM probe_update does NOT scale with render-scale (it dispatches at a fixed probe count via
REM ProbeGridConfigNode, independent of render-target resolution -- M4b's per-ray-type LOD
REM affects probe-ray MARCH COST, not probe COUNT) -- at 0.5 scale it becomes the largest
REM single pass (42.95 ms vs esvo's 28.23 ms), the inverse of the 1.0/0.75 ordering. All three
REM scales: 8/8 bodies present, OOB 0 (correctness invariant, independent of render-scale).
REM DEFAULT STAYS 1.0 -- this curve informs the realtime-target definition only; nothing in
REM the shipped app's default path changes.
REM M6b Task 6b.0 fix: this script previously hardcoded the MAIN CHECKOUT path
REM (C:\cpp\VBVS--VIXEN\VIXEN), not this worktree -- so it ran the main checkout's
REM stale binary instead of a worktree build, silently. Self-resolve via %~dp0
REM (this script's own directory), matching run_baked_hooks_on.bat / run_parity_check.bat.
for %%I in ("%~dp0..") do set VIXEN_ROOT=%%~fI
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_PERF_CSV=%VIXEN_ROOT%\temp_bench\baked\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=%VIXEN_ROOT%\temp_bench\baked
set VIXEN_EXIT_AFTER_FRAMES=160
REM Baked-Perf M7 Task 7.5: per-body occupancy stats JSON (occupied/bounding-volume
REM bricks + voxel fill ratio per body), written next to the other bench artifacts.
set VIXEN_OCCUPANCY_JSON=%VIXEN_ROOT%\temp_bench\occupancy.json
cd /d %VIXEN_ROOT%\VIXEN
binaries\VIXEN.exe > %VIXEN_ROOT%\temp_bench\baked\run.log 2>&1
