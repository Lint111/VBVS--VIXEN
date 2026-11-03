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
REM A lost device leaves every later case hanging until its timeout (T-7061): bound each case, and stop at
REM the first failure or timeout so the queue slot is released. VIXEN_GPU_STOP_ON_FAILURE=0 keeps going.
set "CASE_TIMEOUT=%VIXEN_GPU_CASE_TIMEOUT%"
if not defined CASE_TIMEOUT set "CASE_TIMEOUT=600"
set "STOP_ARG=--stop-on-failure"
if "%VIXEN_GPU_STOP_ON_FAILURE%"=="0" set "STOP_ARG="
"%CTEST_EXE%" --test-dir "%BUILD_DIR%" -L "^gpu$" -LE "^(native|nightly)$" -j 1 --timeout %CASE_TIMEOUT% %STOP_ARG% --no-tests=error --output-on-failure %*
exit /b %errorlevel%
