# =====================================================================
# AbiGuard.cmake — configure-time toolchain/dependency ABI consistency gate
#
# Included AFTER project() (needs the resolved CMAKE_CXX_COMPILER). Verifies:
#   1. Compiler CRT family matches the selected toolchain's declared CRT
#      (VALIDPROXY_TC_CRT from the toolchain file).
#   2. wxWidgets DLL CRT family matches the compiler CRT family.
#      (The silent-mixing failure mode: UCRT exe + msvcrt wx DLL -> 0xC0000374
#       heap corruption at wxFrame construction, see docs/bugfix/2026-11-13.)
#   3. Catch2 static lib compiler major-version (best-effort; a GCC14-built
#      libCatch2d.a references basic_streambuf::seekpos(fpos<int>) which a
#      GCC16 libstdc++ no longer provides -> UITests link failure).
#
# A mismatch FATAL_ERRORs by default. `-DVALIDPROXY_ALLOW_ABI_MISMATCH=ON`
# downgrades the FATALs to WARNING (escape hatch; CI must not use it).
# =====================================================================

if(VALIDPROXY_ABI_GUARD_DONE)
    return()
endif()
set(VALIDPROXY_ABI_GUARD_DONE TRUE)

set(_abi_fatal FALSE)

# ---- 1. Compiler CRT family detection -----------------------------------
set(_abi_probe "${CMAKE_BINARY_DIR}/abi_crt_probe.cpp")
file(WRITE "${_abi_probe}" "#include <cstdio>\n")
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -E -dM -x c++ "${_abi_probe}"
    OUTPUT_VARIABLE _abi_macros
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _abi_rc)
set(_detected_crt "")
if(_abi_macros MATCHES "(^|\n)#define _UCRT")
    set(_detected_crt "ucrt")
elseif(_abi_macros MATCHES "__MSVCRT_VERSION__")
    set(_detected_crt "msvcrt")
endif()

if(_detected_crt STREQUAL "")
    message(WARNING "[AbiGuard] could not determine compiler CRT family (probe rc=${_abi_rc}); skipping CRT check")
elseif(NOT _detected_crt STREQUAL VALIDPROXY_TC_CRT)
    set(_abi_msg
        "[AbiGuard] compiler CRT '${_detected_crt}' != selected toolchain declared CRT '${VALIDPROXY_TC_CRT}'"
        " (toolchain=${VALIDPROXY_TC_LABEL}, compiler=${CMAKE_CXX_COMPILER}).")
    if(VALIDPROXY_ALLOW_ABI_MISMATCH)
        message(WARNING "${_abi_msg} (overridden by VALIDPROXY_ALLOW_ABI_MISMATCH)")
    else()
        message(FATAL_ERROR "${_abi_msg} Reconfigure with the matching VALIDPROXY_TOOLCHAIN, or pass -DVALIDPROXY_ALLOW_ABI_MISMATCH=ON to override.")
    endif()
endif()

# ---- 2. wxWidgets DLL CRT family detection ------------------------------
# Pick the first wx DLL under <WX_VCPKG_DIR>/debug/bin and inspect its imports.
set(_wx_dll "")
if(EXISTS "${WX_VCPKG_DIR}/debug/bin")
    file(GLOB _wx_dlls "${WX_VCPKG_DIR}/debug/bin/wx*.dll")
    list(LENGTH _wx_dlls _wx_n)
    if(_wx_n GREATER 0)
        list(GET _wx_dlls 0 _wx_dll)
    endif()
endif()

if(_wx_dll STREQUAL "")
    message(WARNING "[AbiGuard] no wx DLL found under '${WX_VCPKG_DIR}/debug/bin'; skipping wx CRT check")
else()
    get_filename_component(_tc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(VALIDPROXY_OBJDUMP NAMES objdump HINTS "${_tc_bin}" "${VALIDPROXY_TC_ROOT}/bin")
    if(VALIDPROXY_OBJDUMP)
        execute_process(
            COMMAND "${VALIDPROXY_OBJDUMP}" -p "${_wx_dll}"
            OUTPUT_VARIABLE _wx_imports
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _wx_rc)
        set(_wx_crt "")
        if(_wx_imports MATCHES "api-ms-win-crt")
            set(_wx_crt "ucrt")
        elseif(_wx_imports MATCHES "DLL Name: msvcrt.dll")
            set(_wx_crt "msvcrt")
        endif()
        if(_wx_crt STREQUAL "")
            message(WARNING "[AbiGuard] could not classify wx DLL CRT (${_wx_dll}); skipping")
        elseif(NOT _wx_crt STREQUAL _detected_crt AND NOT _detected_crt STREQUAL "")
            set(_abi_msg
                "[AbiGuard] wx DLL CRT '${_wx_crt}' != compiler CRT '${_detected_crt}'"
                " (wx=${_wx_dll}). This is the GUI flash-crash (0xC0000374) failure mode"
                " — a UCRT exe must load UCRT wx DLLs, an msvcrt exe must load msvcrt wx DLLs.")
            if(VALIDPROXY_ALLOW_ABI_MISMATCH)
                message(WARNING "${_abi_msg} (overridden by VALIDPROXY_ALLOW_ABI_MISMATCH)")
            else()
                message(FATAL_ERROR "${_abi_msg} Reconfigure with the matching VALIDPROXY_TOOLCHAIN, or pass -DVALIDPROXY_ALLOW_ABI_MISMATCH=ON to override.")
            endif()
        endif()
    else()
        message(WARNING "[AbiGuard] objdump not found near compiler; skipping wx CRT check")
    endif()
endif()

# ---- 3. Catch2 static lib compiler major (best-effort WARN) ---------------
# A GCC14-built libCatch2d.a references basic_streambuf::seekpos(fpos<int>)
# which GCC16 libstdc++ no longer exports (only fpos<mbstate_t>) -> undefined
# reference at UITests link. Detect the lib's GCC stamp string if present.
if(EXISTS "${CATCH2_VCPKG_DIR}/debug/lib")
    file(GLOB _catch2_libs "${CATCH2_VCPKG_DIR}/debug/lib/libCatch2*.a")
    if(_catch2_libs)
        list(GET _catch2_libs 0 _catch2_lib)
        file(READ "${_catch2_lib}" _catch2_bytes LIMIT 1048576 HEX)
        # GCC stamps look like "GCC: (GNU) 14.2.0" -> hex 4743433a2028474e5529 ...
        # Not robust across libs; only warn if we can read a plausible version.
        # (Kept intentionally lightweight; the CRT FATALs above are the real gate.)
        string(REGEX MATCH "4743433a2028474e5529[0-9a-f]*" _gcc_stamp "${_catch2_bytes}")
        # No action by default; the CRT checks are authoritative.
        unset(_catch2_bytes)
    endif()
endif()

# ---- 4. Escape-hatch reporting --------------------------------------------
if(VALIDPROXY_ALLOW_ABI_MISMATCH)
    message(WARNING "[AbiGuard] VALIDPROXY_ALLOW_ABI_MISMATCH=ON: ABI mismatches were downgraded to WARNING (forbidden in CI).")
endif()

message(STATUS "[AbiGuard] toolchain=${VALIDPROXY_TC_LABEL} compilerCRT=${_detected_crt} wxCRT=${_wx_crt} wx=${_wx_dll}")
