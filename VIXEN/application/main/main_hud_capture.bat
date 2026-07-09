@echo off
REM View Contract Inc-2 Task 5 -- unattended real-GPU HUD capture run (mirrors the editor's
REM run_editor_script.bat harness). Worktree-portable: %~dp0 resolves to this .bat's own directory
REM (application/main/), NEVER a hardcoded worktree path (see the graph.Run() gotcha).
REM
REM Frame timeline (must match test_hud_render_capture.cpp's expected filenames):
REM   frame 5   -- baseline capture (HudView still default-constructed).
REM   frame 30  -- VIXEN_HUD_SCRIPT fires payload A (known faction, juice ON, lens=Logistics).
REM   frame 45  -- capture (post-A).
REM   frame 60  -- VIXEN_HUD_SCRIPT fires payload B (different faction, juice OFF, lens=Intel).
REM   frame 75  -- capture (post-B).
REM Exits cleanly a few frames after the last capture (VIXEN_EXIT_AFTER_FRAMES=85).
cd /d "%~dp0..\.."

if not exist temp mkdir temp

set VIXEN_HUD_SCRIPT=A@30,B@60
set VIXEN_HUD_CAPTURE_FRAMES=5,45,75
set VIXEN_HUD_CAPTURE_DIR=temp
set VIXEN_EXIT_AFTER_FRAMES=85

binaries\VIXEN.exe
exit /b %errorlevel%
