@echo off
rem scripts\build.bat - configure + build app and UITests (default profile: msvcrt-win32, Debug).
setlocal
cd /d "%~dp0.."
cmake --preset debug || exit /b 1
cmake --build --preset debug --parallel 6 --target validproxy validproxy-cli UITests || exit /b 1
echo [PASS] build
exit /b 0
