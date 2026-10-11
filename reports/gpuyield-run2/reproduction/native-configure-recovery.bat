@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
set "GIT_CONFIG_COUNT=2"
set "GIT_CONFIG_KEY_0=safe.directory"
set "GIT_CONFIG_VALUE_0=//wsl.localhost/Ubuntu/home/liory/projects/VBVS--VIXEN/.claude-worktrees/gpuyield/.tmp/gpuyield-run2/native-fetch/*"
set "GIT_CONFIG_KEY_1=safe.directory"
set "GIT_CONFIG_VALUE_1=//wsl.localhost/Ubuntu/home/liory/projects/Yeroket-Fantasy"
set "VULKAN_SDK=C:\VulkanSDK\1.4.350.0"
set "PATH=C:\Program Files\CMake\bin;C:\Users\liory\AppData\Local\Microsoft\WinGet\Links;%PATH%"
pushd "%~dp0..\.."
if errorlevel 1 exit /b %errorlevel%
set "VIXEN_FETCHCONTENT_CACHE=%CD%\.tmp\gpuyield-run2\native-fetch"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\VIXEN\scripts\build\run_configure_locked.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset gpuyield-native -SourceDir "%CD%\VIXEN"
if errorlevel 1 exit /b %errorlevel%
set "VIXEN_MAX_BUILD_JOBS=4"
if not exist "%CD%\.tmp\gpuyield-run2\native-temp" mkdir "%CD%\.tmp\gpuyield-run2\native-temp"
set "TEMP=%CD%\.tmp\gpuyield-run2\native-temp"
set "TMP=%TEMP%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\VIXEN\scripts\build\run_build_with_summary.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset gpuyield-native -BuildDir "%CD%\build\gpuyield-native-mapped" -Target vixen_editor -BuildId gpuyield-native-run2
set "TASK_RESULT=%errorlevel%"
popd
exit /b %TASK_RESULT%
