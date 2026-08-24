@echo off
rem scripts\build.bat - configure + build app and UITests (Debug, Ninja).
setlocal
cd /d "%~dp0.."
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug || exit /b 1
cmake --build build --parallel 8 --target validproxy validproxy-cli UITests || exit /b 1
echo [PASS] build
exit /b 0
