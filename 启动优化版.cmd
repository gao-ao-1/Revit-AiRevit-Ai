@echo off
setlocal
cd /d "%~dp0"
if not exist "build\optimized-msvc\Release\RecvtConsole.exe" (
  echo Run build_msvc.cmd first.
  pause
  exit /b 1
)
start "" "%~dp0build\optimized-msvc\Release\RecvtConsole.exe"
