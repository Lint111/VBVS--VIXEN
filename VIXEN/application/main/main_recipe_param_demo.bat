@echo off
REM Recipe-Parameterization M3 Task 8 -- live-render capture: params drive visible geometry.
REM Worktree-portable: %~dp0 resolves to this .bat's own directory (application/main/), NEVER a
REM hardcoded worktree path (see the graph.Run() gotcha).
REM
REM Frame timeline: the ReadParam demo body's radius sweeps as
REM   radius = kReadParamDemoBaseRadius(6.0) + 3.0*sin(tick*0.05)
REM (see VulkanGraphApplication::PreTick's "Recipe-Parameterization M3 Task 8" block). Frames
REM 5/20/35 span sin(0.25)=~0.247, sin(1.0)=~0.841, sin(1.75)=~0.984 -- three visibly distinct
REM sweep phases (near-baseline, mid-swing, near-max), not a near-identical pair. Exits shortly
REM after the last capture.
cd /d "%~dp0..\.."

if not exist temp mkdir temp

set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
set VIXEN_PROCEDURAL_UBER_DEMO=3
set VIXEN_HUD_CAPTURE_FRAMES=5,20,35
set VIXEN_HUD_CAPTURE_DIR=temp
set VIXEN_EXIT_AFTER_FRAMES=50

binaries\VIXEN.exe > temp\recipe_param_demo_run.log 2>&1
exit /b %errorlevel%
