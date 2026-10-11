@echo off
set "VK_ICD_FILENAMES="
set "VK_DRIVER_FILES="
"%~dp0gpu-cadence-probe.exe" "%~dp0..\gpuyield\gpu-cadence-probe.spv" "%~dp0..\..\reports\gpuyield-run2\native-after.csv" 15 16666666
exit /b %errorlevel%
