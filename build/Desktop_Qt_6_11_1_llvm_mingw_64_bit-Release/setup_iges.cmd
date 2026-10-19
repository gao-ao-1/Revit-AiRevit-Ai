@echo off
setlocal
cd /d "%~dp0"
if not exist ".cad-venv\Scripts\python.exe" python -m venv .cad-venv
if errorlevel 1 exit /b 1
".cad-venv\Scripts\python.exe" -m pip install -r cad-requirements.txt
if errorlevel 1 exit /b 1
echo IGES converter ready. Python 3.11 x64 is recommended.
