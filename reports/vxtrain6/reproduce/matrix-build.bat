@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cd /d C:\vixen-native\vxtrain6
set "ROOT=%CD%"
set "TEMP=%CD%\.tmp\matrix-temp"
set "TMP=%TEMP%"
set "NUGET_PACKAGES=%CD%\.tmp\native-nuget"
set "DOTNET_CLI_HOME=%CD%\.tmp\native-dotnet-home"
set "VIXEN_FETCHCONTENT_CACHE=%CD%\.tmp\native-fetch"
set "GIT_CONFIG_COUNT=2"
set "GIT_CONFIG_KEY_0=safe.directory"
set "GIT_CONFIG_VALUE_0=//wsl.localhost/Ubuntu/home/liory/projects/Yeroket-Fantasy"
set "GIT_CONFIG_KEY_1=safe.directory"
set "GIT_CONFIG_VALUE_1=//wsl.localhost/Ubuntu/home/liory/projects/VBVS--VIXEN/.claude-worktrees/vxtrain6"
set "VULKAN_SDK=C:\VulkanSDK\1.4.350.0"
set "PATH=C:\Program Files\CMake\bin;C:\Users\liory\AppData\Local\Microsoft\WindowsApps;C:\Users\liory\AppData\Local\Microsoft\WinGet\Links;C:\Program Files\Git\cmd;%PATH%"
if not exist "%TEMP%" mkdir "%TEMP%"
copy /Y "%ROOT%\.tmp\vxtrain6-harness\CMakeUserPresets.json" "%ROOT%\VIXEN\CMakeUserPresets.json" >NUL
if errorlevel 1 exit /b %errorlevel%
cd /d "%ROOT%\VIXEN"
"C:\Program Files\CMake\bin\cmake.exe" --preset vxtrain6-native
if errorlevel 1 exit /b %errorlevel%
"C:\Program Files\CMake\bin\cmake.exe" --build ..\build\vxtrain6-native --parallel 4 --target vixen_lookdev_capture
exit /b %errorlevel%
