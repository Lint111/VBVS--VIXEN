@echo off
REM Hybrid-provider Cornell bench: M6b Task 6b.1 "hole in the wall" variant.
REM
REM rightWall is PROVIDER_STORED with a baked MODIFIED shape (RoundedBox minus a
REM through-hole Cylinder -- BuildRenderGraph.cpp's VIXEN_DDGI_CORNELL_HYBRID_DEMO
REM block); the other 7 bodies (leftWall/backWall/floor/ceiling/light/sphereObj/
REM boxObj) render PROVIDER_PROCEDURAL, same zero-bake mechanism as the virtual
REM demo. Gate (plan M6b): the hole is visibly correct (light passes through --
REM shadows/GI respond on the far side); instIdx map shows all 8 bodies; no
REM provider-boundary artifacts.
for %%I in ("%~dp0..") do set VIXEN_ROOT=%%~fI
set VIXEN_DDGI_CORNELL_HYBRID_DEMO=1
set VIXEN_PERF_CSV=%VIXEN_ROOT%\temp_bench\hybrid\perf.csv
set VIXEN_HUD_CAPTURE_FRAMES=150
set VIXEN_HUD_CAPTURE_DIR=%VIXEN_ROOT%\temp_bench\hybrid
set VIXEN_EXIT_AFTER_FRAMES=160
cd /d %VIXEN_ROOT%\VIXEN
binaries\VIXEN.exe > %VIXEN_ROOT%\temp_bench\hybrid\run.log 2>&1
