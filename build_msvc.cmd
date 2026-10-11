@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0"
"D:\qt6\Tools\CMake_64\bin\cmake.exe" --preset qt6-msvc-release
if errorlevel 1 goto failed
"D:\qt6\Tools\CMake_64\bin\cmake.exe" --build --preset qt6-msvc-release
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
