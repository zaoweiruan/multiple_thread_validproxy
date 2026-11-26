@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."
echo ========================================
echo  wxWidgets UI Test
echo ========================================

echo [1/7] Checking toolchain...
where gcc >nul 2>nul || (echo [FAIL] MinGW-w64 gcc not in PATH & exit /b 1)
where cmake >nul 2>nul || (echo [FAIL] cmake not in PATH & exit /b 1)
if not defined VCPKG_ROOT (
  if exist "D:\vcpkg\vcpkg.exe" (set "VCPKG_ROOT=D:\vcpkg") else (
    echo [FAIL] vcpkg not found ^(set VCPKG_ROOT^) & exit /b 1
  )
)
echo [PASS] toolchain

echo [2/7] Checking dependencies...
if not exist "%VCPKG_ROOT%\installed\x64-mingw-static\share\catch2" (
  "%VCPKG_ROOT%\vcpkg.exe" install catch2:x64-mingw-static || exit /b 1
)
echo [PASS] Catch2 3.x

rem ---- i18n 门禁（Spec 2026-11-18 §11）：生成表与 strings.json 同步、源码无中文字面量 ----
rem 两个脚本互为补充：-Check 守生成产物，-Mode cjk 守源码字面量。
set "PS=powershell"
where pwsh >nul 2>nul && set "PS=pwsh"

echo [3/7] Checking i18n string table...
%PS% -NoProfile -File scripts\gen-i18n.ps1 -Check || (echo [FAIL] i18n table out of sync ^(run scripts\gen-i18n.ps1^) & exit /b 1)
echo [PASS] i18n table

echo [4/7] Checking for unregistered CJK literals...
%PS% -NoProfile -File scripts\check-ui-literals.ps1 -Mode cjk -AllowList scripts\i18n-allowlist.txt || (echo [FAIL] CJK literals outside the string table & exit /b 1)
%PS% -NoProfile -File scripts\check-ui-literals.ps1 || (echo [FAIL] narrow non-ASCII literals in src/ui & exit /b 1)
echo [PASS] literal gates

echo [5/7] Building...
call scripts\build.bat || exit /b 1
echo [PASS] Application + UITests

rem 加载器门禁：链接动态 wx 的测试 exe 必须嵌入 Common Controls v6 清单，
rem 否则运行时会弹「无法找到入口」模态框把 ctest 挂死（需人工点确定）。
%PS% -NoProfile -File scripts\check-test-manifests.ps1 || (echo [FAIL] wx test exe missing Common Controls v6 manifest & exit /b 1)
echo [PASS] test manifests

echo [6/7] Running UI Tests...
%PS% -NoProfile -File scripts\config-guard.ps1 -Mode Snapshot || exit /b 1
call scripts\prepare-ui-sandbox.bat || exit /b 1
rem UI_FLOATINGWIDGET 已在 CMakeLists 中标记 DISABLED（已知环境 flaky，
rem 业主裁决 2026-11-25 跳过）。--repeat until-pass:2 对单个瞬时失败用例
rem 自动重试一次，提升对单项测试失败的容忍度；确定性失败重试后仍会失败。
ctest --test-dir build --output-on-failure -R "^UI_" --repeat until-pass:2
set "RC=%ERRORLEVEL%"
if "%RC%"=="0" (
  %PS% -NoProfile -File scripts\config-guard.ps1 -Mode Verify
  if errorlevel 1 set "RC=1"
)

echo [7/7] Result summary...
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
