@echo off
setlocal

powershell -NoProfile -ExecutionPolicy Bypass -Command "Stop-Process -Name clipboardpp -Force -ErrorAction SilentlyContinue; exit 0"

set "NINJA_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%NINJA_EXE%" set "NINJA_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%NINJA_EXE%" set "NINJA_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%NINJA_EXE%" set "NINJA_EXE=ninja.exe"

"%NINJA_EXE%" %*
exit /b %ERRORLEVEL%
