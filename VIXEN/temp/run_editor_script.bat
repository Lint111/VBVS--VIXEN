@echo off
REM Inc-2b Task 5 -- the authoritative windowed gate: an unattended, frame-scripted run of the
REM real windowed vixen_editor.exe that exercises toggle -> undo -> redo through the ActionStack
REM (the exact path a user's click / Ctrl+Z / Ctrl+Y takes) and dumps 4 PNGs for the post-run
REM assertion gtest (test_editor_toggle_undo_capture) to diff.
REM
REM Env-in-batch (WSL-env-vars-dont-reach-windows-exe rule): every VIXEN_EDITOR_* knob is set
REM HERE, not in the calling shell. Unset (a normal interactive launch) => zero behaviour change
REM (EditorApplication::Update parses empty scripts/capture-frame lists and no-ops both blocks).
REM
REM Layer index 2 is the cut layer (Cylinder Subtract) in the golden sample_tri_layer.vxd -- same
REM index test_appflow_editor_toggle_render.cpp (the M4 headless gate) disables to punch the bore.
REM
REM Baseline capture is frame 5, not 0: EditorApplication::Update() ticks BEFORE the render loop's
REM first Render() call (main.cpp: `Update(); Render();` per iteration), and CaptureFrameToPng
REM reads compute_render_target's PREVIOUS-frame content (M2's KI-007-safe ring-slot timing, see
REM RenderTargetReadback.h) -- so a capture at tick 0 reads a freshly-allocated, never-rendered
REM (all-zero) image, not the actual initial scene (found live via this gate: editor_capture_0.png
REM was solid black while every later capture had real content). Frame 5 gives a few completed
REM Render() calls' worth of margin before the baseline capture.
set VIXEN_EDITOR_SCRIPT=toggle:2@30,undo@60,redo@90
set VIXEN_EDITOR_CAPTURE_FRAMES=5,45,75,105
set VIXEN_EDITOR_CAPTURE_DIR=temp
set VIXEN_EXIT_AFTER_FRAMES=120

cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\appflow-inc2b\VIXEN
if not exist temp mkdir temp

binaries\vixen_editor.exe > temp\run_editor_script.log 2>&1
set VIXEN_EDITOR_EXIT_CODE=%ERRORLEVEL%

echo vixen_editor exited with code %VIXEN_EDITOR_EXIT_CODE% (see temp\run_editor_script.log)
exit /b %VIXEN_EDITOR_EXIT_CODE%
