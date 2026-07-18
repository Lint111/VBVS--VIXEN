@echo off
REM Poisoning-test follow-up: run VIXEN_DEBUG_CAPTURE=1 a SECOND time -- should now HIT the
REM hooks-on .spv entries the previous run just stored, proving the busted cache line is itself
REM reusable (not just "always miss when the env var is set").
set VIXEN_DDGI_CORNELL_BAKED_DEMO=1
set VIXEN_DEBUG_CAPTURE=1
set VIXEN_EXIT_AFTER_FRAMES=20
cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\VIXEN
binaries\VIXEN.exe > C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\m2b_poison_traceon_2\run.log 2>&1
