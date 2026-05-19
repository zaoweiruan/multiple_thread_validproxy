---
title: "fix(build): resolve wxmsw32ud DLL missing - CMakeLists fix for Debug/Release"
type: fix
status: completed
date: 2026-05-19
origin: "GUI build fails: wxmsw32ud_aui_gcc_custom.dll not found (Debug vs Release mismatch)"
---
# Fix Plan: wxmsw32ud_aui_gcc_custom.dll Missing

## Issue Description
When running `validproxy.exe -ui`, the application fails with error:
```
由于找不到wxmsw32ud_aui_gcc_custom.dll,无法继续执行代码
```
(Cannot continue executing code because wxmsw32ud_aui_gcc_custom.dll was not found)

## Root Cause Analysis

### 1. DLL Name Mismatch
- **Linked DLL**: `wxmsw32ud_aui_gcc_custom.dll` (contains 'd' suffix indicating Debug build)
- **Copied DLL**: `wxmsw32u_aui_gcc_custom.dll` (Release build, no 'd' suffix)

### 2. CMakeLists.txt Always Copied Release DLLs
The `copy_wx_dlls()` function in CMakeLists.txt (lines 117-150) **always** copied Release DLLs from `x64-mingw-dynamic/bin`, regardless of `CMAKE_BUILD_TYPE`.

### 3. Dependency Audit
- `dumpbin /dependents bin\validproxy.exe` showed `wxmsw32ud_aui_gcc_custom.dll` was required
- But `libpcre2-16d.dll` and `libzlibd1.dll` (debug versions) were also needed but not being copied

## Fix Implementation

### Changes Made

#### 1. Update CMakeLists.txt copy_wx_dlls() Function
Modified `docs/plans/2026-05-19_FIX-WXWIDGETS-DLL-ISSUE.md` in CMakeLists.txt to:
1. Check `CMAKE_BUILD_TYPE STREQUAL "Debug"` 
2. Use `x64-mingw-dynamic/debug/bin` for debug DLLs
3. Use debug DLL names: `wxmsw32ud_*`, `libpcre2-16d.dll`, `libzlibd1.dll`
4. Fall back to release DLLs from `x64-mingw-dynamic/bin` for non-Debug builds

```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(wx_bin_dir "E:/vcpkg/installed/x64-mingw-dynamic/debug/bin")
    set(WX_DLLS wxbase32ud_gcc_custom.dll wxbase32ud_net_gcc_custom.dll 
                wxmsw32ud_adv_gcc_custom.dll wxmsw32ud_aui_gcc_custom.dll
                libpcre2-16d.dll libzlibd1.dll ...)
else()
    set(wx_bin_dir "E:/vcpkg/installed/x64-mingw-dynamic/bin")
    set(WX_DLLS wxbase32u_gcc_custom.dll ... libpcre2-16.dll libzlib1.dll ...)
endif()
```

#### 2. XrayManager Thread Safety
Added `std::mutex instanceMutex_` to protect singleton operations in `XrayManager.h` and `XrayManager.cpp`.

#### 3. AppController Shutdown Fix
Modified `AppController.cpp`:
- Destructor now calls `XrayManager::release()` and detaches worker thread instead of blocking join
- `doFindFirstProxy()` and `doFindBestProxy()` check `isTestCancelled()` to respond to cancellation

#### 4. MainFrame onClose Simplification
Removed blocking `stopXray()` call - cleanup handled by destructor.

## Verification Steps

1. Build in Debug mode:
   ```bash
   cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
   cmake --build build --parallel 8
   ```

2. Verify DLLs exist in output directory:
   ```bash
   dir bin\*.dll | findstr wx
   # Should show: wxmsw32ud_*.dll (with 'd' suffix)
   ```

3. Run GUI mode:
   ```bash
   bin\validproxy.exe -ui
   ```

## Prevention

For Debug builds, always ensure:
- CMakeLists.txt detects `CMAKE_BUILD_TYPE=Debug`
- Debug DLLs from `x64-mingw-dynamic/debug/bin` are copied
- Dependency names match (e.g., `libpcre2-16d.dll` not `libpcre2-16.dll`)

## Verification Results

- **Build**: ✅ Success (Debug mode)
- **Dependencies**: `dumpbin /dependents bin\validproxy.exe` shows correct Debug DLLs:
  - `wxbase32ud_gcc_custom.dll`
  - `wxmsw32ud_aui_gcc_custom.dll`
  - `wxmsw32ud_core_gcc_custom.dll`
  - `wxmsw32ud_propgrid_gcc_custom.dll`
- **DLLs in bin/**: ✅ All required debug DLLs present
- **Tests**: ✅ 3/3 passed

## Additional Fix: libpng16d.dll and libtiffd.dll

### Issue
After initial fix, application failed with:
```
由于找不到libpng16d.dll,无法继续执行代码
由于找不到libtiffd.dll,无法继续执行代码
```

### Root Cause
CMakeLists.txt was using `libpng16.dll` and `libtiff.dll` for Debug builds, but the debug versions have 'd' suffix.

### Fix
Updated Debug DLL list in CMakeLists.txt:
- `libpng16.dll` → `libpng16d.dll`
- `libtiff.dll` → `libtiffd.dll`

### Files Modified
- `CMakeLists.txt` (lines 139-140)

### Verification
- Build: ✅ Success
- Tests: ✅ 3/3 passed
- GUI startup: ✅ Application starts correctly