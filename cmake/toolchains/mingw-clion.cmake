# Toolchain: CLion bundled MinGW (E:/clion/bin/mingw), GCC 15.2.0, MSVCRT.
#
# This is the zero-risk baseline (spec 2026-11-11 §4.1 T1). wxWidgets and
# Catch2 come from the vcpkg msvcrt trees (D:/vcpkg/installed/...), which are
# the same CRT family (msvcrt) and the pre-existing, known-good dependency
# roots. The configure-time ABI gate (cmake/AbiGuard.cmake) verifies the
# compiler CRT and the wx DLL CRT actually match before any build.
#
# Select with:  -DVALIDPROXY_TOOLCHAIN=clion-msvcrt   or   $ENV{VALIDPROXY_TOOLCHAIN}=clion-msvcrt
#
# Override the dependency roots per-build with:
#   -DWX_VCPKG_DIR=<dir>        (wx dynamic install dir, msvcrt tree)
#   -DCATCH2_VCPKG_DIR=<dir>    (Catch2 static install dir, msvcrt tree)

set(VALIDPROXY_TC_ROOT "E:/clion/bin/mingw" CACHE PATH "MinGW root for the CLion toolchain")

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER "${VALIDPROXY_TC_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${VALIDPROXY_TC_ROOT}/bin/g++.exe")
set(CMAKE_RC_COMPILER "${VALIDPROXY_TC_ROOT}/bin/windres.exe")

# Identity labels consumed by the selector + ABI gate.
set(VALIDPROXY_TC_LABEL "clion-gcc15-msvcrt" CACHE STRING "toolchain identity label")
set(VALIDPROXY_TC_CRT "msvcrt" CACHE STRING "CRT family of the selected toolchain (msvcrt|ucrt)")

# Per-toolchain dependency roots (defaults; overridable via -DWX_VCPKG_DIR/-DCATCH2_VCPKG_DIR).
set(VALIDPROXY_TC_WX_DIR "D:/vcpkg/installed/x64-mingw-dynamic" CACHE PATH "per-toolchain wxWidgets (dynamic, msvcrt) install dir")
set(VALIDPROXY_TC_CATCH2_DIR "D:/vcpkg/installed/x64-mingw-static" CACHE PATH "per-toolchain Catch2 (static, msvcrt) install dir")

# windres include root (GCC sysroot layout <root>/<triple>/include).
set(WX64DEVKIT_ROOT "${VALIDPROXY_TC_ROOT}" CACHE PATH "MinGW root for windres includes")
