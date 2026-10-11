@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cd /d C:\vixen-native\travdefault
set "TEMP=%CD%\.tmp\native-temp"
set "TMP=%TEMP%"
set "NUGET_PACKAGES=%CD%\.tmp\native-nuget"
set "DOTNET_CLI_HOME=%CD%\.tmp\native-dotnet-home"
set "VIXEN_FETCHCONTENT_CACHE=%CD%\.tmp\native-fetch"
set "GIT_CONFIG_COUNT=2"
set "GIT_CONFIG_KEY_0=safe.directory"
set "GIT_CONFIG_VALUE_0=//wsl.localhost/Ubuntu/home/liory/projects/Yeroket-Fantasy"
set "GIT_CONFIG_KEY_1=safe.directory"
set "GIT_CONFIG_VALUE_1=//wsl.localhost/Ubuntu/home/liory/projects/VBVS--VIXEN/.claude-worktrees/travdefault"
set "VULKAN_SDK=C:\VulkanSDK\1.4.350.0"
set "PATH=C:\Program Files\CMake\bin;C:\Users\liory\AppData\Local\Microsoft\WinGet\Links;C:\Program Files\Git\cmd;%PATH%"
if not exist "%TEMP%" mkdir "%TEMP%"
copy /Y "%CD%\reports\travdefault\reproduce\CMakeUserPresets.json" "%CD%\VIXEN\CMakeUserPresets.json" >NUL
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\VIXEN\scripts\build\run_configure_locked.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset travdefault-native -SourceDir "%CD%\VIXEN"
if errorlevel 1 exit /b %errorlevel%
set "VIXEN_MAX_BUILD_JOBS=4"
powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\VIXEN\scripts\build\run_build_with_summary.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset travdefault-native -BuildDir "%CD%\build\travdefault-native" -Target vixen_lookdev_capture -BuildId travdefault-native -StatusFile "%CD%\.tmp\native-temp\build-status.txt"
exit /b %errorlevel%
