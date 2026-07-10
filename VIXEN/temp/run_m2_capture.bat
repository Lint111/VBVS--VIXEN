@echo off
REM Sampled Lighting Inc0 M2 -- live gate: an unattended windowed run of vixen_editor.exe that
REM captures a single frame of the default scene, so a before/after shader-diff proves the GGX
REM BRDF swap actually changed the render.
REM
REM Same env-in-batch pattern as Inc-2b's run_editor_script.bat (WSL-env-vars-dont-reach-windows-exe
REM rule) and the same frame-5 baseline-capture timing (Update() ticks before the render loop's
REM first Render() call, so a capture at tick 0 reads a never-rendered all-black image).
REM
REM %1 = output filename (relative to CAPTURE_DIR) written by CaptureFrameToPng at frame 5,
REM      e.g. "editor_capture_5.png" always -- the caller renames/copies it after this returns.
set VIXEN_EDITOR_CAPTURE_FRAMES=5
set VIXEN_EDITOR_CAPTURE_DIR=temp
set VIXEN_EXIT_AFTER_FRAMES=20

cd /d %~dp0..
if not exist temp mkdir temp

binaries\vixen_editor.exe > temp\run_m2_capture.log 2>&1
set VIXEN_EDITOR_EXIT_CODE=%ERRORLEVEL%

echo vixen_editor exited with code %VIXEN_EDITOR_EXIT_CODE% (see temp\run_m2_capture.log)
exit /b %VIXEN_EDITOR_EXIT_CODE%
