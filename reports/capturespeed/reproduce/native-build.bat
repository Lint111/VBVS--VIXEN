@echo off
setlocal
if "%~1"=="" (
  echo Usage: native-build.bat ^<40-character VIXEN commit^>
  exit /b 2
)
set "VIXEN_NATIVE_BUILD_KEY=%~1"
for /f "delims=" %%I in ("%~dp0..\..\..") do set "NATIVE_ROOT=%%~fI"
set "VIXEN_NATIVE_BUILD_DIR=%NATIVE_ROOT%\build\native\%VIXEN_NATIVE_BUILD_KEY%"
set "READY_FILE=%VIXEN_NATIVE_BUILD_DIR%\.nativerunner-build-ready"
if exist "%READY_FILE%" (
  set /p READY_SHA=<"%READY_FILE%"
  if /I "%READY_SHA%"=="%VIXEN_NATIVE_BUILD_KEY%" (
    echo [nativerunner] Reusing completed native build %VIXEN_NATIVE_BUILD_KEY%.
    exit /b 0
  )
)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cd /d "%NATIVE_ROOT%"
if errorlevel 1 exit /b %errorlevel%
set "TEMP=%NATIVE_ROOT%\.tmp\native-temp"
set "TMP=%TEMP%"
set "NUGET_PACKAGES=%NATIVE_ROOT%\.tmp\native-nuget"
set "DOTNET_CLI_HOME=%NATIVE_ROOT%\.tmp\native-dotnet-home"
set "VIXEN_FETCHCONTENT_CACHE=%NATIVE_ROOT%\.tmp\native-fetch"
set "VIXEN_NATIVE_KERNEL_CACHE_DIR=%NATIVE_ROOT%\.tmp\native-kernel-cache"
set "GIT_CONFIG_COUNT=1"
set "GIT_CONFIG_KEY_0=safe.directory"
set "GIT_CONFIG_VALUE_0=//wsl.localhost/Ubuntu/home/liory/projects/Yeroket-Fantasy"
set "VULKAN_SDK=C:\VulkanSDK\1.4.350.0"
set "PATH=C:\Program Files\CMake\bin;C:\Users\liory\AppData\Local\Microsoft\WinGet\Links;C:\Program Files\Git\cmd;%PATH%"
if not exist "%TEMP%" mkdir "%TEMP%"
if not exist "%VIXEN_NATIVE_BUILD_DIR%" mkdir "%VIXEN_NATIVE_BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%
copy /Y "%NATIVE_ROOT%\reports\capturespeed\reproduce\CMakeUserPresets.json" "%NATIVE_ROOT%\VIXEN\CMakeUserPresets.json" >NUL
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -ExecutionPolicy Bypass -File "%NATIVE_ROOT%\VIXEN\scripts\build\run_configure_locked.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset capturespeed-native -SourceDir "%NATIVE_ROOT%\VIXEN"
if errorlevel 1 exit /b %errorlevel%
set "VIXEN_MAX_BUILD_JOBS=4"
set "VIXEN_KERNEL_CACHE_DIR=%VIXEN_NATIVE_KERNEL_CACHE_DIR%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%NATIVE_ROOT%\VIXEN\scripts\build\run_build_with_summary.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset capturespeed-native -BuildDir "%VIXEN_NATIVE_BUILD_DIR%" -Target VIXEN -BuildId "nativerunner-hud-%VIXEN_NATIVE_BUILD_KEY%" -StatusFile "%TEMP%\build-hud-%VIXEN_NATIVE_BUILD_KEY%.txt" -MaxParallelJobs 4
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -ExecutionPolicy Bypass -File "%NATIVE_ROOT%\VIXEN\scripts\build\run_build_with_summary.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset capturespeed-native -BuildDir "%VIXEN_NATIVE_BUILD_DIR%" -Target vixen_lookdev_capture -BuildId "nativerunner-lookdev-%VIXEN_NATIVE_BUILD_KEY%" -StatusFile "%TEMP%\build-lookdev-%VIXEN_NATIVE_BUILD_KEY%.txt" -MaxParallelJobs 4
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -ExecutionPolicy Bypass -File "%NATIVE_ROOT%\VIXEN\scripts\build\run_build_with_summary.ps1" -CMakeExe "C:\Program Files\CMake\bin\cmake.exe" -Preset capturespeed-native -BuildDir "%VIXEN_NATIVE_BUILD_DIR%" -Target vixen_planet_capture -BuildId "nativerunner-planet-%VIXEN_NATIVE_BUILD_KEY%" -StatusFile "%TEMP%\build-planet-%VIXEN_NATIVE_BUILD_KEY%.txt" -MaxParallelJobs 4
if errorlevel 1 exit /b %errorlevel%
>"%READY_FILE%" echo %VIXEN_NATIVE_BUILD_KEY%
exit /b 0
