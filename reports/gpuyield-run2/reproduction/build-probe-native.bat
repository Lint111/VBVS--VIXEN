@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /O2 /EHsc /W4 /I"C:\VulkanSDK\1.4.350.0\Include" "%~dp0..\..\tools\gpu-cadence-probe.cpp" /Fo"%~dp0gpu-cadence-probe.obj" /Fe"%~dp0gpu-cadence-probe.exe" /link /LIBPATH:"C:\VulkanSDK\1.4.350.0\Lib" vulkan-1.lib
exit /b %errorlevel%
