@echo off
rem scripts\test-ui.bat - reset sandbox, then run the registered UI_* CTest entries.
setlocal
cd /d "%~dp0.."
call scripts\prepare-ui-sandbox.bat || exit /b 1
ctest --test-dir build --output-on-failure -R "^UI_"
exit /b %ERRORLEVEL%
