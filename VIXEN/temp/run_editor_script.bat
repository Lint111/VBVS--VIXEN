@echo off
REM Inc-2b Task 5 / Inc-4 R6 -- the authoritative windowed gate: an unattended, frame-scripted run
REM of the real windowed vixen_editor.exe that exercises toggle -> undo -> redo through the
REM ActionStack (the exact path a user's click / Ctrl+Z / Ctrl+Y takes) and emits the log +
REM captures the post-run gtest (test_editor_toggle_undo_capture) asserts.
REM
REM R6: the gate asserts (a) the editor's own "[EDITOR/state] <op> mask=.. undoDepth=.. redoDepth=.."
REM trail from run_editor_script.log (mask 7->3->7->3 = undo/redo work, windowed), (b) a one-shot
REM residency smoke check that the FIRST edit reached the render pipeline (capture_5 != capture_45),
REM and (c) back-button->Return (afterBack). NOTE: a byte-exact VISUAL undo/redo round-trip is NOT
REM asserted -- the editor body renders the mask-INVARIANT mip-fallback path at an orbit camera, so
REM the layer mask has ~0 visible effect (proven via GPU-memory checksums + a shader-output tap;
REM the mask is correct at CPU + GPU-buffer + shader-input layers). See
REM Vixen-Docs/01-Architecture/R6-Editor-Render-Mask-Invisible-Finding-2026-07.md.
REM
REM Env-in-batch (WSL-env-vars-dont-reach-windows-exe rule): every VIXEN_EDITOR_* knob is set
REM HERE, not in the calling shell. Unset (a normal interactive launch) => zero behaviour change
REM (EditorApplication::Update parses empty scripts/capture-frame lists and no-ops both blocks).
REM
REM Layer index 2 is the cut layer (Cylinder Subtract) in the golden sample_tri_layer.vxd -- same
REM index test_appflow_editor_toggle_render.cpp (the M4 headless gate) disables to punch the bore.
REM
REM Baseline capture is frame 5, not 0: EditorApplication::Update() ticks BEFORE the render loop's
REM first Render() call (VulkanApplicationBase::Tick(): PreTick() -> Update() -> Render() ->
REM PostTick(), per iteration), and CaptureFrameToPng reads compute_render_target's PREVIOUS-frame
REM content (M2's KI-007-safe ring-slot timing, see RenderTargetReadback.h) -- so a capture at tick
REM 0 reads a freshly-allocated, never-rendered (all-zero) image, not the actual initial scene
REM (found live via this gate: editor_capture_0.png was solid black while every later capture had
REM real content). Frame 5 gives a few completed Render() calls' worth of margin before the
REM baseline capture.
REM
REM Inc-4 R6a: settings@100,back@110 exercise the back-button->Return edge in the RUNNING editor
REM (design D15/R6 -- prove Esc AND back-button both reach Return live, not just the FSM unit
REM test). settings@100 calls NavTo(Settings) directly (no "open settings" UI/selector exists
REM yet); back@110 dispatches the real "back-button" selector and logs the post-dispatch FSM
REM state as "[EDITOR/state] afterBack=<FlowStateId>" for the gtest to parse out of this run's
REM log (a windowed capture can't cheaply show a state pop). Both land well after the toggle/
REM undo/redo window (30/60/90) and well before exit (120), so they don't interact with it.
REM Captures: 5 (pre-first-edit baseline) and 45 (post-first-edit, resident) feed the R6 residency
REM smoke check. 75/105 are kept for diagnostics (post-undo / post-redo) but are byte-identical to
REM 45 by design (mask invisible) so the gate no longer diffs them.
set VIXEN_EDITOR_SCRIPT=toggle:2@30,undo@60,redo@90,settings@100,back@110
set VIXEN_EDITOR_CAPTURE_FRAMES=5,45,75,105
set VIXEN_EDITOR_CAPTURE_DIR=temp
set VIXEN_EXIT_AFTER_FRAMES=120

REM cd to the VIXEN dir this .bat lives under (temp\..) -- NOT a hardcoded worktree path, so the
REM script works unmodified in whichever worktree/checkout invokes it.
cd /d %~dp0..
if not exist temp mkdir temp

binaries\vixen_editor.exe > temp\run_editor_script.log 2>&1
set VIXEN_EDITOR_EXIT_CODE=%ERRORLEVEL%

echo vixen_editor exited with code %VIXEN_EDITOR_EXIT_CODE% (see temp\run_editor_script.log)
exit /b %VIXEN_EDITOR_EXIT_CODE%
