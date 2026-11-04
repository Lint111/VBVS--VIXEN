@echo off
setlocal EnableExtensions
REM Run the comparable correctness GPU list from a Windows-native checkout.
REM Overrides: VIXEN_NATIVE_BUILD_DIR (configured tree), VIXEN_NATIVE_CTEST (full ctest.exe path).
REM The build is intentionally separate: run build.bat all vixen-ninja-release first.

set "ROOT=%~dp0.."
if defined VIXEN_NATIVE_BUILD_DIR (
    set "BUILD_DIR=%VIXEN_NATIVE_BUILD_DIR%"
) else (
    set "BUILD_DIR=%ROOT%\build\ninja-release"
)
set "CTEST_EXE="
if defined VIXEN_NATIVE_CTEST set "CTEST_EXE=%VIXEN_NATIVE_CTEST%"
if not defined CTEST_EXE for /f "delims=" %%C in ('where ctest 2^>nul') do if not defined CTEST_EXE set "CTEST_EXE=%%C"
if not defined CTEST_EXE if exist "%ProgramFiles%\CMake\bin\ctest.exe" set "CTEST_EXE=%ProgramFiles%\CMake\bin\ctest.exe"
if not exist "%CTEST_EXE%" (
    echo [native-gpu-suite] ERROR: ctest.exe not found: "%CTEST_EXE%". Set VIXEN_NATIVE_CTEST to its full path, or put it on PATH / Program Files\CMake\bin.
    exit /b 2
)
if not exist "%BUILD_DIR%\CTestTestfile.cmake" (
    echo [native-gpu-suite] ERROR: build tree is not configured: %BUILD_DIR% ^(set VIXEN_NATIVE_BUILD_DIR to the configured tree^)
    echo [native-gpu-suite] Run build.bat all vixen-ninja-release first.
    exit /b 2
)

echo [native-gpu-suite] ctest=%CTEST_EXE%
echo [native-gpu-suite] build=%BUILD_DIR%
echo [native-gpu-suite] device test list: label gpu, excluding native and nightly
"%CTEST_EXE%" --test-dir "%BUILD_DIR%" -L "^gpu$" -LE "^(native|nightly)$" -j 1 --no-tests=error --output-on-failure %*
exit /b %errorlevel%
