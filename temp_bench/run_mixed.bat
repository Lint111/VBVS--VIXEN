@echo off
REM Mixed-provider Cornell bench: M6b Task 6b.2 splits.
REM
REM Usage: run_mixed.bat [walls_stored|objects_stored]
REM   walls_stored   (default) -- 5 walls PROVIDER_STORED (baked, unmodified), light+sphereObj+
REM                  boxObj PROVIDER_PROCEDURAL.
REM   objects_stored -- the inverse: light+sphereObj+boxObj PROVIDER_STORED, 5 walls
REM                  PROVIDER_PROCEDURAL.
REM
REM Gate (plan M6b): instIdx map correct for all 8 bodies in every variant; per-pass timers
REM within the sum-of-parts envelope; no artifacts where lighting/shadow rays cross provider
REM kinds.
for %%I in ("%~dp0..") do set VIXEN_ROOT=%%~fI
set MIXED_MODE=%1
if "%MIXED_MODE%"=="" set MIXED_MODE=walls_stored
set VIXEN_DDGI_CORNELL_MIXED_DEMO=%MIXED_MODE%
if not exist "%VIXEN_ROOT%\temp_bench\mixed\%MIXED_MODE%" mkdir "%VIXEN_ROOT%\temp_bench\mixed\%MIXED_MODE%"
set VIXEN_PERF_CSV=%VIXEN_ROOT%\temp_bench\mixed\%MIXED_MODE%\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=%VIXEN_ROOT%\temp_bench\mixed\%MIXED_MODE%
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d %VIXEN_ROOT%\VIXEN
binaries\VIXEN.exe > %VIXEN_ROOT%\temp_bench\mixed\%MIXED_MODE%\run.log 2>&1
