@echo off
REM Baked-perf-pipeline M2b Task 2b.2 POISONING TEST: same scene, cache dir left as-is from the
REM cache-warm run (populated with hooks-OFF .spv entries), but flip VIXEN_DEBUG_CAPTURE=1 --
REM this injects "#define VIXEN_GPU_TRACE_HOOKS 1" into the source BEFORE Build() hashes it, so
REM the cache key must differ and force a genuine recompile (miss), not silently serve the
REM stale hooks-off .spv.
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_DEBUG_CAPTURE=1
set VIXEN_EXIT_AFTER_FRAMES=20
cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\VIXEN
binaries\VIXEN.exe > C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\m2b_poison_traceon\run.log 2>&1
