@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0
set VULKAN_SDK=C:\VulkanSDK\1.4.357.0
if not exist "%VULKAN_SDK%\Bin\glslc.exe" (
    echo [build] Vulkan SDK not found: %VULKAN_SDK%
    exit /b 1
)

echo ===================================================
echo Compiling Vulkan GLSL Shaders to SPIR-V...
echo ===================================================

cd /d "%ROOT%Res\Shaders"
"%VULKAN_SDK%\Bin\glslc.exe" RasterClear.comp -o RasterClear.spv
"%VULKAN_SDK%\Bin\glslc.exe" NodeAndClusterCull.comp -o NodeAndClusterCull.spv
"%VULKAN_SDK%\Bin\glslc.exe" ClusterCull.comp -o ClusterCull.spv
"%VULKAN_SDK%\Bin\glslc.exe" Visualization.comp -o Visualization.spv
"%VULKAN_SDK%\Bin\glslc.exe" HWRasterize.vert -o HWRasterizeVS.spv
"%VULKAN_SDK%\Bin\glslc.exe" HWRasterize.frag -o HWRasterizeFS.spv
"%VULKAN_SDK%\Bin\glslc.exe" FSQ.vert -o FSQVS.spv
"%VULKAN_SDK%\Bin\glslc.exe" FSQ.frag -o FSQFS.spv

if errorlevel 1 (
    echo Shader compile failed!
    exit /b 1
)
echo Shaders compiled successfully!

cd /d "%ROOT%"
echo ===================================================
echo Building C++ Vulkan Nanite Executable...
echo ===================================================

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

set INC=/I"%VULKAN_SDK%\Include" /I"src\App" /I"src\Scene" /I"src\Platform" /I"src\Math" /I"src\Camera" /I"src\Core"

cl.exe /nologo /EHsc /std:c++17 /utf-8 /O2 %INC% ^
src\App\main.cpp src\Platform\vulkan_context.cpp src\Platform\vulkan_utils.cpp src\Platform\vulkan_pipeline.cpp ^
src\Scene\scene.cpp src\Math\float4.cpp src\Math\matrix4.cpp src\Math\quaternion.cpp ^
src\Camera\trackball_camera.cpp src\Core\utils.cpp ^
/link /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib user32.lib gdi32.lib winmm.lib /OUT:NaniteVulkan.exe

if %errorlevel% equ 0 (
    echo ===================================================
    echo BUILD SUCCESSFUL: NaniteVulkan.exe
    echo ===================================================
) else (
    echo BUILD FAILED!
    exit /b 1
)
