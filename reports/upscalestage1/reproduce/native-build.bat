@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
set "VULKAN_SDK=C:\VulkanSDK\1.4.350.0"
set "PATH=C:\Program Files\CMake\bin;%PATH%"
"C:\Program Files\CMake\bin\cmake.exe" --build C:\vixen-native\upscalestage1\build\stage1-merged-native --target vixen_lookdev_capture --parallel 4
exit /b %errorlevel%
