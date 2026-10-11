@echo off
setlocal
pushd "%~dp0..\.."
if errorlevel 1 exit /b %errorlevel%
"C:\Program Files\CMake\bin\cmake.exe" -P "%CD%\.tmp\gpuyield-run2\native-lock-probe.cmake"
set "LOCK_RESULT=%errorlevel%"
popd
exit /b %LOCK_RESULT%
