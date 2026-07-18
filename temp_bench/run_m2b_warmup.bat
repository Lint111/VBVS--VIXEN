@echo off
REM Throwaway OS-file-cache warmup run (not measured) -- isolates the shader-disk-cache effect
REM from ordinary OS-level cold-start effects (first launch of a fresh binary is always slow).
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_EXIT_AFTER_FRAMES=20
cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\VIXEN
binaries\VIXEN.exe > C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\m2b_warmup\run.log 2>&1
