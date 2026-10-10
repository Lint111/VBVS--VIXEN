@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cd /d C:\vixen-native\gpuyield
set "TEMP=%CD%\.tmp\native-temp"
set "TMP=%TEMP%"
set "NUGET_PACKAGES=%CD%\.tmp\native-nuget"
set "DOTNET_CLI_HOME=%CD%\.tmp\native-dotnet-home"
set "VIXEN_FETCHCONTENT_CACHE=%CD%\.tmp\native-fetch"
set "GIT_CONFIG_COUNT=2"
set "GIT_CONFIG_KEY_0=safe.directory"
set "GIT_CONFIG_VALUE_0=//wsl.localhost/Ubuntu/home/liory/projects/Yeroket-Fantasy"
set "VULKAN_SDK=C:\VulkanSDK\1.4.350.0"
set "PATH=C:\Program Files\CMake\bin;C:\Users\liory\AppData\Local\Microsoft\WinGet\Links;C:\Program Files\Git\cmd;%PATH%"
set "GIT_CONFIG_KEY_1=safe.directory"
set "GIT_CONFIG_VALUE_1=//wsl.localhost/Ubuntu/home/liory/projects/VBVS--VIXEN/.claude-worktrees/gpuyield"
git merge --ff-only FETCH_HEAD
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\VIXEN\scripts\build\run_configure_locked.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset gpuyield-native -SourceDir "%CD%\VIXEN"
if errorlevel 1 exit /b %errorlevel%
set "VIXEN_MAX_BUILD_JOBS=4"
powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\VIXEN\scripts\build\run_build_with_summary.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset gpuyield-native -BuildDir "%CD%\build\gpuyield-native" -Target vixen_editor -BuildId gpuyield-native-run4-fulltile-recovery -StatusFile "%CD%\.tmp\native-temp\candidate-status.txt" -StreamProgress
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\VIXEN\scripts\build\run_build_with_summary.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset gpuyield-native -BuildDir "%CD%\build\gpuyield-native" -Target test_rayquery_feasibility -BuildId gpuyield-native-ray-run4-fulltile-recovery -StatusFile "%CD%\.tmp\native-temp\ray-status.txt" -StreamProgress
if errorlevel 1 exit /b %errorlevel%
exit /b %errorlevel%
