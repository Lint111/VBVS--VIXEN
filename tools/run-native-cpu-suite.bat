@echo off
setlocal EnableExtensions EnableDelayedExpansion
REM Run the comparable CPU-only CTest list from a Windows-native checkout.
REM Overrides: VIXEN_NATIVE_BUILD_DIR (configured tree), VIXEN_NATIVE_CTEST (full ctest.exe path).

set "ROOT=%~dp0.."
if defined VIXEN_NATIVE_BUILD_DIR (
    set "BUILD_DIR=%VIXEN_NATIVE_BUILD_DIR%"
) else (
    set "BUILD_DIR=%ROOT%\build\ninja"
)
set "CTEST_EXE="
if defined VIXEN_NATIVE_CTEST set "CTEST_EXE=%VIXEN_NATIVE_CTEST%"
if not defined CTEST_EXE for /f "delims=" %%C in ('where ctest 2^>nul') do if not defined CTEST_EXE set "CTEST_EXE=%%C"
if not defined CTEST_EXE if exist "%ProgramFiles%\CMake\bin\ctest.exe" set "CTEST_EXE=%ProgramFiles%\CMake\bin\ctest.exe"
if not exist "%CTEST_EXE%" (
    echo [native-cpu-suite] ERROR: ctest.exe not found: "%CTEST_EXE%". Set VIXEN_NATIVE_CTEST to its full path, or put it on PATH / Program Files\CMake\bin.
    exit /b 2
)
if not exist "%BUILD_DIR%\CTestTestfile.cmake" (
    echo [native-cpu-suite] ERROR: build tree is not configured: %BUILD_DIR% ^(set VIXEN_NATIVE_BUILD_DIR to the configured tree^)
    echo [native-cpu-suite] Run build.bat all vixen-ninja first.
    exit /b 2
)

echo [native-cpu-suite] ctest=%CTEST_EXE%
echo [native-cpu-suite] build=%BUILD_DIR%
echo [native-cpu-suite] device test list: label cpu, excluding native and nightly
set "CASE_TIMEOUT=%VIXEN_CPU_CASE_TIMEOUT%"
if not defined CASE_TIMEOUT set "CASE_TIMEOUT=600"
set "STOP_ARG=--stop-on-failure"
if "%VIXEN_CPU_STOP_ON_FAILURE%"=="0" set "STOP_ARG="
if defined VIXEN_NATIVE_ARGS (
    set VIXEN_PHASE_ARGS=--test-dir "%BUILD_DIR%" -L "^cpu$" -LE "^(native^|nightly)$" --parallel !VIXEN_MAX_BUILD_JOBS! --timeout %CASE_TIMEOUT% %STOP_ARG% --no-tests=error --output-on-failure !VIXEN_NATIVE_ARGS!
) else (
    set VIXEN_PHASE_ARGS=--test-dir "%BUILD_DIR%" -L "^cpu$" -LE "^(native^|nightly)$" --parallel !VIXEN_MAX_BUILD_JOBS! --timeout %CASE_TIMEOUT% %STOP_ARG% --no-tests=error --output-on-failure %*
)
echo [native-cpu-suite] test jobs: %VIXEN_MAX_BUILD_JOBS% (source=queue grant)
powershell -ExecutionPolicy Bypass -File "%ROOT%\VIXEN\scripts\build\run_with_phase_progress.ps1" -Phase native-cpu-suite -Exe "%CTEST_EXE%" -ArgLineEnv VIXEN_PHASE_ARGS
exit /b %errorlevel%
