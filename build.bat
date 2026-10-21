@echo off
rem ------------------------------------------------------------------
rem  KT Delay - configure + build (Visual Studio 2022, x64, Release)
rem  usage:  build.bat            -> Release
rem          build.bat Debug      -> Debug
rem ------------------------------------------------------------------
setlocal
cd /d "%~dp0"

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

if not exist build\CMakeCache.txt (
    cmake -G "Visual Studio 17 2022" -A x64 -S . -B build
    if errorlevel 1 exit /b 1
)

cmake --build build --config %CONFIG% --target KTDelay
if errorlevel 1 exit /b 1

echo.
echo Done: build\VST3\%CONFIG%\KTDelay.vst3
echo Copy that folder to  %%COMMONPROGRAMFILES%%\VST3\  to install it.
endlocal
