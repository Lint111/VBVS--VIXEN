@echo off
REM Baked-Perf-Fix-Pipeline M2d Task 2d.2: post-capture visual-parity gate.
REM
REM Runs compare_parity.py in BOTH modes:
REM   same_path  -- fresh baked run (this .bat re-runs run_baked.bat) vs the committed
REM                 tools/bench/goldens/baked_instidx.txt golden. HARD GATE (exit 1 on FAIL).
REM   cross_path -- fresh baked vs fresh virtual (this .bat re-runs run_virtual.bat too).
REM                 REPORT-ONLY (never fails the .bat) until Milestone M5 flips
REM                 parity_thresholds.json's cross_path.enforced to true.
REM
REM Warm-run rule: this script does NOT itself cold-boot -- run it AFTER at least one
REM prior launch of VIXEN.exe in this session (temp_bench's own warm-run convention;
REM see Baked-Perf-Fix-Pipeline-Plan-2026-07.md's "Numbers discipline" section). If you
REM need a guaranteed-warm pair from scratch, run run_baked.bat/run_virtual.bat once each
REM first (discard), then run this script.
REM
REM Requires: Windows-native `python` on PATH with Pillow installed (both confirmed
REM present at authoring time -- python 3.13.7 + Pillow 12.1.1). No WSL bridge needed;
REM this script is plain Windows-native, unlike the kernel-framework codegen's WSL-bridge
REM cases -- compare_parity.py is pure Python 3 stdlib + Pillow, no cross-OS ELF concerns.

set VIXEN_ROOT=C:\cpp\VBVS--VIXEN\.claude\worktrees\baked-perf-pipeline
set BENCH_DIR=%VIXEN_ROOT%\temp_bench
set TOOLS_DIR=%VIXEN_ROOT%\VIXEN\tools\bench

REM setlocal/endlocal around EACH sub-run: run_baked.bat/run_virtual.bat each `set` their
REM own VIXEN_DDGI_CORNELL_*_DEMO var but never clear the other one, and plain `call` (no
REM scoping) lets both leak into this script's shared environment -- the app's
REM BuildRenderGraph.cpp checks BAKED_DEMO first in an else-if chain, so once both vars are
REM set the virtual run silently re-renders the BAKED scene instead (caught live 2026-07-16:
REM the "virtual" capture came back byte-identical to the baked one). setlocal/endlocal
REM scopes each `set` to its own call so the two demo env vars can never coexist.
echo [parity-check] Re-running baked capture (fresh, for same_path + cross_path)...
setlocal
call "%BENCH_DIR%\run_baked.bat"
set BAKED_EXIT=%ERRORLEVEL%
endlocal & set BAKED_EXIT=%BAKED_EXIT%
if not "%BAKED_EXIT%"=="0" goto :fail_baked_run

echo [parity-check] Re-running virtual capture (fresh, for cross_path)...
setlocal
call "%BENCH_DIR%\run_virtual.bat"
set VIRTUAL_EXIT=%ERRORLEVEL%
endlocal & set VIRTUAL_EXIT=%VIRTUAL_EXIT%
if not "%VIRTUAL_EXIT%"=="0" goto :fail_virtual_run
goto :run_checks

:fail_baked_run
echo [parity-check] run_baked.bat failed -- aborting.
exit /b 1

:fail_virtual_run
echo [parity-check] run_virtual.bat failed -- aborting.
exit /b 1

:run_checks

echo [parity-check] same_path: fresh baked run vs committed golden (HARD GATE)...
python "%TOOLS_DIR%\compare_parity.py" --mode same_path ^
    --run "%BENCH_DIR%\baked" ^
    --golden "%TOOLS_DIR%\goldens\baked_instidx.txt" ^
    --json-out "%BENCH_DIR%\parity_same_path_report.json"
set SAME_PATH_EXIT=%ERRORLEVEL%

echo [parity-check] cross_path: fresh baked vs fresh virtual (REPORT-ONLY)...
python "%TOOLS_DIR%\compare_parity.py" --mode cross_path ^
    --run-a "%BENCH_DIR%\baked" ^
    --run-b "%BENCH_DIR%\virtual" ^
    --json-out "%BENCH_DIR%\parity_cross_path_report.json"
set CROSS_PATH_EXIT=%ERRORLEVEL%

echo.
echo [parity-check] same_path exit=%SAME_PATH_EXIT% -- 0=PASS, 1=FAIL, this is the hard gate
echo [parity-check] cross_path exit=%CROSS_PATH_EXIT% -- always 0 while report-only; nonzero means a tool/input error, not a divergence failure

if not "%SAME_PATH_EXIT%"=="0" goto :fail_same_path
if not "%CROSS_PATH_EXIT%"=="0" goto :fail_cross_path
echo [parity-check] OVERALL: PASS
exit /b 0

:fail_same_path
echo [parity-check] OVERALL: FAIL -- same_path hard gate did not pass.
exit /b 1

:fail_cross_path
echo [parity-check] OVERALL: FAIL -- cross_path tool invocation errored, not a divergence report, check the output above.
exit /b 1
