# Toolchain: UCRT MinGW (E:/mingw64), GCC 16.2.0, UCRT.
#
# This is the toolchain that fixes the GUI flash-crash (0xC0000374): the exe
# and the wxWidgets DLLs are both UCRT, so no cross-CRT boundary exists
# (see docs/bugfix/2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md).
# wxWidgets and Catch2 come from the UCRT rebuilds under temp/vcpkg-ucrt-root.
# The configure-time ABI gate (cmake/AbiGuard.cmake) verifies compiler CRT ==
# wx DLL CRT (and warns on Catch2 major-version drift) before any build.
#
# Select with:  -DVALIDPROXY_TOOLCHAIN=mingw-ucrt   or   $ENV{VALIDPROXY_TOOLCHAIN}=mingw-ucrt
#
# Override the dependency roots per-build with:
#   -DWX_VCPKG_DIR=<dir>        (wx dynamic install dir, UCRT tree)
#   -DCATCH2_VCPKG_DIR=<dir>    (Catch2 static install dir, UCRT tree)

set(VALIDPROXY_TC_ROOT "E:/mingw64" CACHE PATH "MinGW root for the UCRT toolchain")

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER "${VALIDPROXY_TC_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${VALIDPROXY_TC_ROOT}/bin/g++.exe")
set(CMAKE_RC_COMPILER "${VALIDPROXY_TC_ROOT}/bin/windres.exe")

# Identity labels consumed by the selector + ABI gate.
set(VALIDPROXY_TC_LABEL "ucrt-gcc16-ucrt" CACHE STRING "toolchain identity label")
set(VALIDPROXY_TC_CRT "ucrt" CACHE STRING "CRT family of the selected toolchain (msvcrt|ucrt)")

# Per-toolchain dependency roots (defaults; overridable via -DWX_VCPKG_DIR/-DCATCH2_VCPKG_DIR).
# The UCRT wx/Catch2 rebuilds live under temp/vcpkg-ucrt-root (isolation root,
# built with E:\mingw64 + custom triplets x64-mingw-dynamic-ucrt / -static-ucrt).
set(VALIDPROXY_TC_WX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../temp/vcpkg-ucrt-root/x64-mingw-dynamic-ucrt" CACHE PATH "per-toolchain wxWidgets (dynamic, UCRT) install dir")
set(VALIDPROXY_TC_CATCH2_DIR "${CMAKE_CURRENT_LIST_DIR}/../../temp/vcpkg-ucrt-root/x64-mingw-static-ucrt" CACHE PATH "per-toolchain Catch2 (static, UCRT) install dir")

# windres include root (GCC sysroot layout <root>/<triple>/include).
set(WX64DEVKIT_ROOT "${VALIDPROXY_TC_ROOT}" CACHE PATH "MinGW root for windres includes")
