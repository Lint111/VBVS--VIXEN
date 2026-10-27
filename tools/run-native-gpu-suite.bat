@echo off
setlocal EnableExtensions
REM Run the comparable correctness GPU list from a Windows-native checkout.
REM The build is intentionally separate: run build.bat all vixen-ninja-release first.

set "ROOT=%~dp0.."
if defined VIXEN_NATIVE_BUILD_DIR (
    set "BUILD_DIR=%VIXEN_NATIVE_BUILD_DIR%"
) else (
    set "BUILD_DIR=%ROOT%\build\ninja-release"
)
set "CTEST_EXE="
for /f "delims=" %%C in ('where ctest 2^>nul') do if not defined CTEST_EXE set "CTEST_EXE=%%C"
if not defined CTEST_EXE if exist "%ProgramFiles%\CMake\bin\ctest.exe" set "CTEST_EXE=%ProgramFiles%\CMake\bin\ctest.exe"
if not defined CTEST_EXE (
    echo [native-gpu-suite] ERROR: ctest.exe not found on PATH or under Program Files\CMake\bin.
    exit /b 2
)
if not exist "%BUILD_DIR%\CTestTestfile.cmake" (
    echo [native-gpu-suite] ERROR: build tree is not configured: %BUILD_DIR%
    echo [native-gpu-suite] Run build.bat all vixen-ninja-release first.
    exit /b 2
)

echo [native-gpu-suite] build=%BUILD_DIR%
echo [native-gpu-suite] device test list: label gpu, excluding native and nightly
"%CTEST_EXE%" --test-dir "%BUILD_DIR%" -L "^gpu$" -LE "^(native|nightly)$" -j 1 --no-tests=error --output-on-failure %*
exit /b %errorlevel%
