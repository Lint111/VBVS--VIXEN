@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cd /d C:\vixen-native\vxtrain4\VIXEN
set "VIXEN_GPUYIELD_TRACE=1"
set "VIXEN_EDITOR_TEST_CAMERA=top-down"
set "VIXEN_TEST_CELSHADE_LAMBERT_GGX=0"
call temp\run_editor_script.bat
if errorlevel 1 exit /b %errorlevel%
set "VIXEN_EDITOR_CAPTURE_DIR=%CD%\temp"
"C:\Program Files\CMake\bin\ctest.exe" --test-dir C:\vixen-native\vxtrain4\build\vxtrain4-native -C Release -R "EditorToggleUndoCapture.GpuSubmissionsRespectDeclaredBudget" --output-on-failure --parallel 1
exit /b %errorlevel%
