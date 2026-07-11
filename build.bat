@echo off
setlocal EnableDelayedExpansion
REM ===========================================================================
REM VIXEN Windows build launcher (tracked, path-agnostic).
REM
REM Discovers the toolchain instead of hardcoding paths, so it works on any
REM machine / VS edition / clone location:
REM   - vcvars64.bat  : located via vswhere (ships with every VS 2017+ at a
REM                      fixed Installer path); no VS year/edition assumptions.
REM   - cmake         : taken from PATH (where cmake), with a well-known
REM                      install-dir fallback.
REM   - repo root     : derived from this script's own location (%~dp0).
REM   - sccache       : SCCACHE_DIR / SCCACHE_CACHE_SIZE default here but a
REM                     user-set value wins (shared compiler cache; see the
REM                     vixen-ninja preset, which sets /Z7 so MSVC debug builds
REM                     are cacheable).
REM
REM Usage:
REM   build.bat [configure^|build^|all] [preset-name] [target-name]
REM     configure   run only the CMake configure/generate for the preset
REM     build       run only the build for the preset (configure first)
REM     all         configure then build (DEFAULT)
REM   preset-name defaults to vixen-ninja.
REM   target-name  optional CMake target to build instead of the full graph
REM                (e.g. VixenApp, or a single test binary). Omit to build
REM                everything, as before.
REM
REM The gitignored _ninja_*.bat launchers are personal overrides; this is the
REM shared, committed entry point.
REM
REM Env vars honored by the build step (see run_build_with_summary.ps1):
REM   VIXEN_BUILD_LOCK_TIMEOUT  seconds to wait for the machine-wide build lock
REM                             before giving up (default 1800 = 30 min)
REM   VIXEN_SKIP_BUILD_LOCK=1   bypass the lock entirely
REM   VIXEN_MAX_BUILD_JOBS      cap on concurrent ninja jobs (default ~75% of
REM                             logical cores) so a build leaves the machine
REM                             usable instead of pegging every core
REM ===========================================================================

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=all"
set "PRESET=%~2"
if "%PRESET%"=="" set "PRESET=vixen-ninja"
set "TARGET=%~3"

REM --- repo root = this script's directory (VIXEN/ is the CMake source dir) ---
set "REPO_ROOT=%~dp0"
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"
set "SRC_DIR=%REPO_ROOT%\VIXEN"
if not exist "%SRC_DIR%\CMakePresets.json" (
    echo [build] ERROR: CMakePresets.json not found under "%SRC_DIR%".
    echo [build] Run this script from the repository root ^(it locates itself via %%~dp0^).
    exit /b 1
)

REM --- discover vcvars64.bat via vswhere ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build] ERROR: vswhere.exe not found at "%VSWHERE%".
    echo [build] Install Visual Studio 2017 or newer ^(the C++ workload^), or run from a
    echo [build] Developer Command Prompt so vcvars is already in the environment.
    exit /b 1
)
set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" (
    echo [build] ERROR: no Visual Studio install with the C++ x64 toolchain was found.
    echo [build] Install the "Desktop development with C++" workload.
    exit /b 1
)
set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [build] ERROR: vcvars64.bat not found at "%VCVARS%".
    exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 (echo [build] ERROR: vcvars64.bat failed. & exit /b 1)

REM --- discover cmake (PATH first, then a well-known install) ---
set "CMAKE_EXE="
for /f "usebackq tokens=*" %%c in (`where cmake 2^>nul`) do (set "CMAKE_EXE=%%c" & goto :cmake_found)
if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
:cmake_found
if "%CMAKE_EXE%"=="" (
    echo [build] ERROR: cmake not found on PATH or under "%ProgramFiles%\CMake\bin".
    echo [build] Install CMake 3.21+ and ensure it is on PATH.
    exit /b 1
)

REM --- shared compiler-cache defaults (user-set values win) ---
if not defined SCCACHE_DIR set "SCCACHE_DIR=%SystemDrive%\sccache"
if not defined SCCACHE_CACHE_SIZE set "SCCACHE_CACHE_SIZE=20G"

echo [build] VS       : %VSINSTALL%
echo [build] cmake    : %CMAKE_EXE%
echo [build] source   : %SRC_DIR%
echo [build] preset   : %PRESET%
echo [build] action   : %ACTION%
if not "%TARGET%"=="" (echo [build] target   : %TARGET%) else (echo [build] target   : ^(full build^))
echo [build] sccache  : %SCCACHE_DIR% ^(max %SCCACHE_CACHE_SIZE%^)

cd /d "%SRC_DIR%"

if /i "%ACTION%"=="configure" goto :do_configure
if /i "%ACTION%"=="build"     goto :do_build
if /i "%ACTION%"=="all"       goto :do_all
echo [build] ERROR: unknown action "%ACTION%" ^(expected configure^|build^|all^).
exit /b 1

:do_configure
"%CMAKE_EXE%" --preset "%PRESET%"
exit /b %errorlevel%

:do_build
call :run_build_locked
exit /b %errorlevel%

:do_all
"%CMAKE_EXE%" --preset "%PRESET%"
if errorlevel 1 (echo [build] configure FAILED. & exit /b 1)
call :run_build_locked
exit /b %errorlevel%

REM ---------------------------------------------------------------------------
REM run_build_locked: run the build (Fix 7/8), holding the machine-wide build
REM lock for its duration (Fix 9 — only one build runs at a time across all
REM worktrees/agents on this machine; concurrent builds contend for the same
REM CPU/IO and make ALL of them slower, not faster). The lock acquire/release
REM lives INSIDE run_build_with_summary.ps1 (same process, plain try/finally on
REM a Mutex) rather than a separate wrapper script shelling out to this one —
REM an earlier design with a separate acquire_build_lock.ps1 passing the build
REM command through as a string hung indefinitely on nested argument quoting
REM across 3 process layers. VIXEN_BUILD_LOCK_TIMEOUT overrides the default
REM 30-minute wait; set VIXEN_SKIP_BUILD_LOCK=1 to bypass entirely (e.g. a
REM machine known to be otherwise idle, or CI with its own scheduling).
REM ---------------------------------------------------------------------------
:run_build_locked
if not defined VIXEN_BUILD_LOCK_TIMEOUT set "VIXEN_BUILD_LOCK_TIMEOUT=1800"
set "SKIP_LOCK_ARG="
if "%VIXEN_SKIP_BUILD_LOCK%"=="1" set "SKIP_LOCK_ARG=-SkipLock"
set "TARGET_ARG="
if not "%TARGET%"=="" set "TARGET_ARG=-Target \"%TARGET%\""
powershell -ExecutionPolicy Bypass -File "%SRC_DIR%\scripts\build\run_build_with_summary.ps1" -CMakeExe "%CMAKE_EXE%" -Preset "%PRESET%" -LockTimeoutSeconds %VIXEN_BUILD_LOCK_TIMEOUT% %SKIP_LOCK_ARG% %TARGET_ARG%
exit /b %errorlevel%
