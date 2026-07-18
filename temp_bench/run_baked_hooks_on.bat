@echo off
REM Baked-perf-pipeline M2 Task 2.3: hooks-ON A/B variant of run_baked.bat.
REM Same scene/config as run_baked.bat, plus VIXEN_DEBUG_CAPTURE=1 which is the single
REM env-var that gates BOTH the GPU-side VIXEN_GPU_TRACE_HOOKS #define injection
REM (BuildRenderGraph.cpp) AND the pre-existing CPU-side RayTraceBuffer readback/export
REM (DebugBufferReaderNode) -- see BuildRenderGraph.cpp's debugCaptureEnabled wiring.
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_DEBUG_CAPTURE=1
set VIXEN_PERF_CSV=C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\baked_hookson\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\baked_hookson
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\VIXEN
binaries\VIXEN.exe > C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\baked_hookson\run.log 2>&1
