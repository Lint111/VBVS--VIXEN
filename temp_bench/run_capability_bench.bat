@echo off
REM Sequential Windows-native capability baseline: virtual then baked.
echo [bench] virtual start > C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\status.txt
call C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\run_virtual.bat
echo [bench] virtual done, baked start >> C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\status.txt
call C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\run_baked.bat
echo [bench] all done >> C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline\temp_bench\status.txt
