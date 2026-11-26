# Toolchain: MinGW-Builds Win32-threads MinGW (D:/mingw64-win32-msvcrt), GCC 16.2.0.
#
# This is the DEFAULT toolchain (2026-11-26): --enable-threads=win32 + MSVCRT.
# Because the thread model is win32, there is NO libwinpthread dependency at
# all — `-static` links a standalone EXE importing only KERNEL32 + msvcrt
# (+ system DLLs). This removes the last MinGW runtime DLL from the release
# and shrinks the GUI exe from ~274 MB (old debug static-wx build) to ~30 MB.
# The statically-built wx/Catch2/rest come from the same vcpkg msvcrt tree
# used by the clion profile; AbiGuard re-checks at configure time.
#
# Select with: -DVALIDPROXY_TOOLCHAIN=msvcrt-win32 (default), or
#             cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-msvcrt-win32.cmake
set(VALIDPROXY_TC_ROOT "D:/mingw64-win32-msvcrt" CACHE PATH "MinGW root for the win32-threads MSVCRT toolchain")

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER "${VALIDPROXY_TC_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${VALIDPROXY_TC_ROOT}/bin/g++.exe")
set(CMAKE_RC_COMPILER "${VALIDPROXY_TC_ROOT}/bin/windres.exe")

# Identity labels consumed by the selector + ABI gate.
set(VALIDPROXY_TC_LABEL "msvcrt-win32-gcc16-msvcrt" CACHE STRING "toolchain identity label")
set(VALIDPROXY_TC_CRT "msvcrt" CACHE STRING "CRT family of the selected toolchain (msvcrt|ucrt)")

# Per-toolchain dependency roots (defaults; overridable via -DWX_VCPKG_DIR / -DCATCH2_VCPKG_DIR).
# The vcpkg x64-mingw-static triplet is compiled with GCC 15.2 msvcrt but ABI-compatible
# with GCC 16.2 win32-threads for static libs (verified 2026-11-26: full release build +
# CLI help smoke + GUI startup smoke all passed). Debug wx static libs live in debug/lib.
set(VALIDPROXY_TC_WX_DIR "D:/vcpkg/installed/x64-mingw-static" CACHE PATH "per-toolchain wxWidgets (static) install dir")
set(VALIDPROXY_TC_CATCH2_DIR "D:/vcpkg/installed/x64-mingw-static" CACHE PATH "per-toolchain Catch2 (static, msvcrt) install dir")

# windres include root (GCC sysroot layout <root>/<triple>/include).
set(WX64DEVKIT_ROOT "${VALIDPROXY_TC_ROOT}" CACHE PATH "MinGW root for windres includes")
