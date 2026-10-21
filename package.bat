@echo off
rem ------------------------------------------------------------------
rem  KT Delay - build a Release and pack it the same way as CI does
rem  usage:  package.bat [version]      e.g.  package.bat v1.0.0
rem  output: dist\KTDelay-<version>-win64.zip
rem ------------------------------------------------------------------
setlocal
cd /d "%~dp0"

set VERSION=%1
if "%VERSION%"=="" set VERSION=local

call "%~dp0build.bat" Release
if errorlevel 1 exit /b 1

set NAME=KTDelay-%VERSION%-win64
if exist dist rmdir /s /q dist
mkdir dist\%NAME%
xcopy /E /H /I /Y /Q build\VST3\Release\KTDelay.vst3 dist\%NAME%\KTDelay.vst3 >nul
copy /Y README.md dist\%NAME%\ >nul
copy /Y LICENSE dist\%NAME%\ >nul

powershell -NoProfile -Command "Compress-Archive -Force -Path 'dist\%NAME%\*' -DestinationPath 'dist\%NAME%.zip'"
if errorlevel 1 exit /b 1

echo.
echo Package: dist\%NAME%.zip
endlocal
