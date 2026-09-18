@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."
echo ========================================
echo  wxWidgets UI Test
echo ========================================

echo [1/5] Checking toolchain...
where gcc >nul 2>nul || (echo [FAIL] MinGW-w64 gcc not in PATH & exit /b 1)
where cmake >nul 2>nul || (echo [FAIL] cmake not in PATH & exit /b 1)
if not defined VCPKG_ROOT (
  if exist "D:\vcpkg\vcpkg.exe" (set "VCPKG_ROOT=D:\vcpkg") else (
    echo [FAIL] vcpkg not found ^(set VCPKG_ROOT^) & exit /b 1
  )
)
echo [PASS] toolchain

echo [2/5] Checking dependencies...
if not exist "%VCPKG_ROOT%\installed\x64-mingw-static\share\catch2" (
  "%VCPKG_ROOT%\vcpkg.exe" install catch2:x64-mingw-static || exit /b 1
)
echo [PASS] Catch2 3.x

echo [3/5] Building...
call scripts\build.bat || exit /b 1
echo [PASS] Application + UITests

echo [4/5] Running UI Tests...
call scripts\prepare-ui-sandbox.bat || exit /b 1
ctest --test-dir build --output-on-failure -R "^UI_"
set "RC=%ERRORLEVEL%"

echo [5/5] Result summary...
if "%RC%"=="0" (
  echo ========================================
  echo  ALL TESTS PASSED
  echo ========================================
) else (
  echo ========================================
  echo  UI TEST FAILED ^(exit %RC%^)
  echo  Artifacts: test-results\ui-artifacts\
  echo ========================================
)
exit /b %RC%
