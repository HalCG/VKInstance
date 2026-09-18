@echo off
setlocal
cd /d "%~dp0.."

if not exist "Res" mkdir "Res"
if not exist "Res\Shaders" mkdir "Res\Shaders"

set SRC=..\0015\Res
if not exist "%SRC%\mitsuba.bvh" (
    echo [CopyRuntimeDeps] 未找到 %SRC%\mitsuba.bvh
    exit /b 1
)

copy /Y "%SRC%\mitsuba.bvh" "Res\mitsuba.bvh" >nul
copy /Y "%SRC%\mitsuba.nanitemesh" "Res\mitsuba.nanitemesh" >nul
echo [CopyRuntimeDeps] 已复制 mitsuba.bvh / mitsuba.nanitemesh
exit /b 0
