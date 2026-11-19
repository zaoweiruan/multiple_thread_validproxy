cmake_minimum_required(VERSION 3.20)

# ===== Toolchain selection (must run before project()) =====
# VALIDPROXY_TOOLCHAIN selects the MinGW toolchain via a CMake toolchain file:
#   -DVALIDPROXY_TOOLCHAIN=<tc>  or  $ENV{VALIDPROXY_TOOLCHAIN}=<tc>
#   <tc> = clion | ucrt
# Resolution order: -D cache value > $ENV{VALIDPROXY_TOOLCHAIN} > default "mingw-ucrt".
#   clion : CLion bundled MinGW (E:/clion/bin/mingw)  GCC 15.2.0, MSVCRT
#           (zero-risk baseline; wx/Catch2 from the vcpkg msvcrt tree)
#   ucrt  : UCRT MinGW (E:/mingw64)                    GCC 16.2.0, UCRT
#           (the GUI flash-crash fix toolchain; wx/Catch2 from temp/vcpkg-ucrt-root)
# Each toolchain file sets the compiler, the CRT label (VALIDPROXY_TC_CRT) and
# per-toolchain dependency roots (VALIDPROXY_TC_WX_DIR / VALIDPROXY_TC_CATCH2_DIR).
# Dependency policy (user directive 2026-11-13): wxWidgets = DYNAMIC; every
# other dependency (boost / sqlite3 / gtest / Catch2 / curl / zlib / ...) =
# STATIC. cmake/AbiGuard.cmake (included after project()) FATALs if the compiler
# CRT and the wx DLL CRT diverge, and warns on Catch2 major-version drift.
#
# NOTE: switching VALIDPROXY_TOOLCHAIN changes CMAKE_TOOLCHAIN_FILE, so use a
# FRESH build directory per toolchain (or delete CMakeCache.txt) — the cached
# compiler of a previously-configured dir would otherwise win.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
    set(_VALIDPROXY_TC "$ENV{VALIDPROXY_TOOLCHAIN}")
    if(DEFINED VALIDPROXY_TOOLCHAIN AND NOT "${VALIDPROXY_TOOLCHAIN}" STREQUAL "")
        set(_VALIDPROXY_TC "${VALIDPROXY_TOOLCHAIN}")
    endif()
    if("${_VALIDPROXY_TC}" STREQUAL "")
        set(_VALIDPROXY_TC "mingw-ucrt")
    endif()

    if(_VALIDPROXY_TC STREQUAL "clion-msvcrt")
        set(CMAKE_TOOLCHAIN_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchains/mingw-clion.cmake"
            CACHE FILEPATH "CMake toolchain file (mingw-clion)" FORCE)
    elseif(_VALIDPROXY_TC STREQUAL "mingw-ucrt")
        set(CMAKE_TOOLCHAIN_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchains/mingw-ucrt.cmake"
            CACHE FILEPATH "CMake toolchain file (mingw-ucrt)" FORCE)
    else()
        message(FATAL_ERROR
            "VALIDPROXY_TOOLCHAIN='${_VALIDPROXY_TC}' is unknown. Valid values: clion-msvcrt, mingw-ucrt.")
    endif()
    set(VALIDPROXY_TOOLCHAIN "${_VALIDPROXY_TC}" CACHE STRING
        "selected MinGW toolchain (clion-msvcrt|mingw-ucrt)" FORCE)
    unset(_VALIDPROXY_TC)
endif()


project(multiple_thread_validproxy VERSION 1.6.1 LANGUAGES CXX)

# Partial static linking: link libgcc/libstdc++ into the exe so the release no
# longer depends on libgcc_s_seh-1.dll / libstdc++-6.dll (only libwinpthread-1.dll
# remains). Applies to every target (GUI, CLI, tests).
add_link_options(-static-libgcc -static-libstdc++)

# ===== Static wxWidgets option (release builds) =====
# When ON, use the statically-built wxWidgets tree (x64-mingw-static) instead of
# the dynamic one, and link wx statically. Keeps the dynamic tree as the default
# so existing debug/ucrt workflows are untouched.
option(VALIDPROXY_STATIC_WX "Use statically-built wxWidgets (release)" OFF)
if(VALIDPROXY_STATIC_WX)
    if(NOT DEFINED VCPKG_ROOT OR "${VCPKG_ROOT}" STREQUAL "")
        set(_vp_vcpkg_root "D:/vcpkg")
    else()
        set(_vp_vcpkg_root "${VCPKG_ROOT}")
    endif()
    set(_vp_static_wx "${_vp_vcpkg_root}/installed/x64-mingw-static")
    if(NOT EXISTS "${_vp_static_wx}/share/wxwidgets/wxWidgetsConfig.cmake")
        message(FATAL_ERROR "VALIDPROXY_STATIC_WX=ON but static wx tree not found at ${_vp_static_wx}. Build it first (vcpkg install wxwidgets:x64-mingw-static).")
    endif()
    set(WX_VCPKG_DIR "${_vp_static_wx}" CACHE PATH "vcpkg wxWidgets (static) install dir" FORCE)
    set(wxWidgets_USE_STATIC ON CACHE BOOL "Link wxWidgets statically" FORCE)
    message(STATUS "[wx] using STATIC tree: ${WX_VCPKG_DIR}")

    # ── Win7 SP1 runtime provisioning ───────────────────────────────────────
    # `-static-libgcc -static-libstdc++` still leaves libwinpthread-1.dll as a
    # dynamic dependency, and the libwinpthread shipped by BOTH toolchains
    # (E:\clion\bin\mingw 72 KB, E:\mingw64 56 KB) imports the UCRT API sets
    # (`api-ms-win-crt-convert/heap/runtime/stdio/string/...`). Those DLLs do not
    # exist on a plain Windows 7 SP1 without the Universal CRT update
    # (KB2999226), so the app fails at load time with:
    #   "无法启动此程序，因为计算机中丢失 api-ms-win-crt-convert-l1-1-0.dll"
    # `-static` is NOT an option here: this toolchain's libpthread.a is a UCRT
    # build (references `__intrinsic_setjmpex`, only defined in libucrt*.a).
    # Fix: ship an MSVCRT-only libwinpthread next to the exe. Verified to export
    # every symbol the exe imports (incl. clock_gettime64 / nanosleep64 /
    # pthread_cond_timedwait64) and to import only KERNEL32 + msvcrt.
    # Override with -DVALIDPROXY_MSVCRT_WINPTHREAD_DLL=<path> if needed.
    set(VALIDPROXY_MSVCRT_WINPTHREAD_DLL
        "${CMAKE_SOURCE_DIR}/resources/win7/libwinpthread-1.dll"
        CACHE FILEPATH "MSVCRT-only libwinpthread-1.dll shipped with the Win7 package")
    if(VALIDPROXY_MSVCRT_WINPTHREAD_DLL AND EXISTS "${VALIDPROXY_MSVCRT_WINPTHREAD_DLL}")
        message(STATUS "[win7] runtime: ${VALIDPROXY_MSVCRT_WINPTHREAD_DLL} (MSVCRT-only libwinpthread)")
    else()
        message(WARNING
            "[win7] VALIDPROXY_MSVCRT_WINPTHREAD_DLL not found; the package will fall back "
            "to the toolchain's UCRT-based libwinpthread-1.dll and will NOT start on a "
            "Windows 7 SP1 without KB2999226. See scripts/check-win7-deps.ps1.")
    endif()

    unset(_vp_vcpkg_root)
    unset(_vp_static_wx)
endif()

# ===== Per-toolchain dependency roots (defaults; overridable via -D) =====
# wxWidgets = DYNAMIC; every other dependency = STATIC (user directive).
# The toolchain file provides the matching roots; -DWX_VCPKG_DIR /
# -DCATCH2_VCPKG_DIR still override for a custom dependency build.
# Guard on non-empty so a custom -DCMAKE_TOOLCHAIN_FILE (no per-tc roots)
# falls through to the vcpkg-tree defaults set later in this file.
if(VALIDPROXY_TC_WX_DIR)
    set(WX_VCPKG_DIR "${VALIDPROXY_TC_WX_DIR}" CACHE PATH
        "vcpkg wxWidgets (dynamic) install dir — defaults to the selected toolchain's tree")
endif()
if(VALIDPROXY_TC_CATCH2_DIR)
    set(CATCH2_VCPKG_DIR "${VALIDPROXY_TC_CATCH2_DIR}" CACHE PATH
        "vcpkg Catch2 (static) install dir — defaults to the selected toolchain's tree")
endif()

# Fallback defaults for a custom -DCMAKE_TOOLCHAIN_FILE (e.g. CLion's vcpkg
# toolchain): the per-tc roots above are unset, so default to the vcpkg msvcrt
# tree. Set BEFORE AbiGuard so its authoritative wx DLL CRT check can run.
if(NOT DEFINED WX_VCPKG_DIR OR "${WX_VCPKG_DIR}" STREQUAL "")
    if(DEFINED VCPKG_ROOT AND NOT "${VCPKG_ROOT}" STREQUAL "")
        set(_abi_vcpkg_root "${VCPKG_ROOT}")
    else()
        set(_abi_vcpkg_root "D:/vcpkg")
    endif()
    set(WX_VCPKG_DIR "${_abi_vcpkg_root}/installed/x64-mingw-dynamic" CACHE PATH
        "vcpkg wxWidgets (dynamic) install dir — defaults to the vcpkg msvcrt tree")
    unset(_abi_vcpkg_root)
endif()
if(NOT DEFINED CATCH2_VCPKG_DIR OR "${CATCH2_VCPKG_DIR}" STREQUAL "")
    if(DEFINED VCPKG_ROOT AND NOT "${VCPKG_ROOT}" STREQUAL "")
        set(_abi_vcpkg_root "${VCPKG_ROOT}")
    else()
        set(_abi_vcpkg_root "D:/vcpkg")
    endif()
    set(CATCH2_VCPKG_DIR "${_abi_vcpkg_root}/installed/x64-mingw-static" CACHE PATH
        "vcpkg Catch2 (static) install dir — defaults to the vcpkg msvcrt tree")
    unset(_abi_vcpkg_root)
endif()

# ===== ABI gate (configure-time toolchain/dependency consistency) =====
# FATAL if compiler CRT != wx DLL CRT (the 0xC0000374 failure mode); warns on
# Catch2 major-version drift. Escape hatch: -DVALIDPROXY_ALLOW_ABI_MISMATCH=ON.
include(cmake/AbiGuard.cmake)

# ===== Output isolation (per-toolchain) =====
# VALIDPROXY_BIN_DIR / VALIDPROXY_TEST_DIR choose where the GUI/CLI exe + runtime
# DLLs and the test exes land. Default = the in-tree bin/ and tests/ (current,
# zero-change behaviour, keeps UI tests / build-and-test.bat working).
# Set -DVALIDPROXY_ISOLATE_OUTPUTS=ON (with VALIDPROXY_TOOLCHAIN set) to place
# them under <build>/out/{bin,tests} so different toolchains never share them.
set(VALIDPROXY_ISOLATE_OUTPUTS OFF CACHE BOOL
    "isolate GUI/CLI and test outputs under <build>/out/{bin,tests}")
if(VALIDPROXY_ISOLATE_OUTPUTS)
    set(VALIDPROXY_BIN_DIR "${CMAKE_BINARY_DIR}/out/bin" CACHE PATH
        "directory for GUI/CLI exe + runtime DLL outputs (isolated)")
    set(VALIDPROXY_TEST_DIR "${CMAKE_BINARY_DIR}/out/tests" CACHE PATH
        "directory for test exe outputs (isolated)")
else()
    set(VALIDPROXY_BIN_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH
        "directory for GUI/CLI exe + runtime DLL outputs")
    set(VALIDPROXY_TEST_DIR "${CMAKE_SOURCE_DIR}/tests" CACHE PATH
        "directory for test exe outputs")
endif()
message(STATUS "Toolchain: ${VALIDPROXY_TC_LABEL} | bin=${VALIDPROXY_BIN_DIR} | tests=${VALIDPROXY_TEST_DIR}")

# ===== Rule 5: only use environment DLLs, never copy dependency DLLs =====
# The vcpkg toolchain (CMAKE_TOOLCHAIN_FILE) injects a z-applocal POST_BUILD
# that copies runtime DLLs from <triplet>/debug/bin next to each executable
# (docs/DEV-PROCESS.md Rule 5 forbids this). Force-disable it so DLLs are only
# resolved via PATH, never copied into ${CMAKE_SOURCE_DIR}/bin.
set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Disable vcpkg applocal DLL copy (Rule 5)" FORCE)

# iphlpapi provides GetExtendedTcpTable (used by utils::isPortAvailable to
# detect occupied ports via the system TCP table). Link it for every target
# defined below that compiles src/Utils.cpp.
link_libraries(iphlpapi)

# ===== Git tag version detection (overrides project() VERSION) =====
include(cmake/GetGitVersion.cmake)
get_git_version()

if(DEFINED VERSION_MAJOR)
    set(PROJECT_VERSION_MAJOR "${VERSION_MAJOR}")
    set(PROJECT_VERSION_MINOR "${VERSION_MINOR}")
    set(PROJECT_VERSION_PATCH "${VERSION_PATCH}")
    set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
    message(STATUS "Git version: ${PROJECT_VERSION} (tag: ${VERSION_TAG})")
endif()

set(VERSION_H ${CMAKE_BINARY_DIR}/include/version.h)
set(VERSION_H_IN ${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in)

# Build-time version header custom target — an ALL target that runs the
# generator on every `cmake --build`, so APP_BUILD_TIME reflects the actual
# build moment instead of the last configure time.
#
# Prefer configure-time Python discovery: `find_program` embeds the full
# interpreter path into the generated build file, so the target no longer
# depends on a bare `python` being on PATH at build time.  CLion's build
# process (and any non-login shell that omits Python from PATH) previously
# broke the `update_version_h` -> `version.h` edge and the dependent
# `OBJECT_DEPENDS` on src/main_gui.cpp / src/main_cli.cpp / src/ui/MainFrame.cpp.
#
# Discovery chain (first hit wins, so CI / non-default installs are honoured):
#   1. `$ENV{VALIDPROXY_PYTHON}`   — explicit user override (env var, CMake 3.23+)
#   2. `py.exe` / `python3` / `python` on CMAKE_PROGRAM_PATH / PATH
#   3. `python` as a last-resort bare name + configure-time WARNING
# No version pin: any CPython 3.x works (the script is 2/3-compatible), so
# `find_program` does NOT pass a `VERSION` argument.
find_program(VALIDPROXY_PYTHON
    NAMES py.exe python3 python
    ENV VALIDPROXY_PYTHON
)
if(VALIDPROXY_PYTHON)
    message(STATUS "version.h generator: using ${VALIDPROXY_PYTHON}")
    set(VERSION_H_PYTHON_COMMAND ${VALIDPROXY_PYTHON})
else()
    message(WARNING
        "No Python interpreter found for the update_version_h target "
        "(searched py.exe, python3, python; $ENV{VALIDPROXY_PYTHON} unset). "
        "Falling back to a bare `python` on PATH at build time. "
        "Set VALIDPROXY_PYTHON (absolute path) before re-configuring to pin a specific interpreter.")
    set(VERSION_H_PYTHON_COMMAND python)
endif()

# BYPRODUCTS declares ${VERSION_H} as a known output so Ninja has a rule for it
# even in a FRESH build dir where the file does not yet exist (the ALL target
# alone declares no output edge -> ninja "missing and no known rule to make it"
# on the OBJECT_DEPENDS edge). The ALL target still runs the generator on every
# `cmake --build`, so APP_BUILD_TIME keeps reflecting the build moment.
add_custom_target(update_version_h ALL
    COMMAND ${VERSION_H_PYTHON_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_version_h.py
        ${VERSION_H_IN}
        ${VERSION_H}
        ${CMAKE_BUILD_TYPE}
        ${VERSION_MAJOR}
        ${VERSION_MINOR}
        ${VERSION_PATCH}
        ${VERSION_TAG}
        ${VERSION_COMMIT}
        ${VERSION_DIRTY}
    BYPRODUCTS ${VERSION_H}
    COMMENT "[version.h] Regenerating build-time version header..."
    VERBATIM
)

# version.h is written directly by the script (an ALL custom target declares no
# output edge), and the Ninja _unscanned compile rules do not track header
# dependencies, so a regenerated version.h alone would not trigger the TUs that
# embed APP_BUILD_TIME to rebuild.  Pin the generated header as an explicit
# OBJECT_DEPENDS on those sources so every build re-embeds the new timestamp.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include)
set_property(SOURCE
    src/main_gui.cpp
    src/main_cli.cpp
    src/ui/MainFrame.cpp
    APPEND PROPERTY OBJECT_DEPENDS ${VERSION_H})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===== Environment-based paths (configurable via CMakePresets.json or env vars) =====
# Boost is managed by vcpkg (boost-json etc.), no standalone BOOST_ROOT.
set(VCPKG_ROOT "${VCPKG_ROOT}" CACHE PATH "vcpkg installation root")
set(WX64DEVKIT_ROOT "${WX64DEVKIT_ROOT}" CACHE PATH "w64devkit installation root")
set(GTEST_ROOT "${GTEST_ROOT}" CACHE PATH "Google Test installation root")

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
    add_link_options(/UTF-8)
else()
    add_compile_options(-Wall -Wextra -pedantic -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# ===== Sanitizer options (Debug) =====
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(USE_GRPC_API "Use direct gRPC API without Xray process" ON)
if(ENABLE_SANITIZERS AND NOT MSVC)
    message(STATUS "Sanitizers enabled: AddressSanitizer, UndefinedBehaviorSanitizer")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Debug flags belong only in CMAKE_CXX_FLAGS_DEBUG
# Purge any stale -g3/-O0 leaked into base CMAKE_CXX_FLAGS from cached config
string(REGEX REPLACE "(^| )-g[0-3]( |$)" " " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "(^| )-O0( |$)" " " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(STRIP "${CMAKE_CXX_FLAGS}" CMAKE_CXX_FLAGS)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "Flags used by the compiler during all build types." FORCE)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0")

# ===== Coverage options =====
if(ENABLE_SANITIZERS AND ENABLE_COVERAGE)
    message(FATAL_ERROR "ENABLE_SANITIZERS and ENABLE_COVERAGE conflict at link time. Enable only one.")
endif()
option(ENABLE_COVERAGE "Enable gcov coverage instrumentation" OFF)
if(ENABLE_COVERAGE AND NOT MSVC)
    message(STATUS "Coverage instrumentation enabled")
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# ===== Default path fallbacks =====
if(NOT VCPKG_ROOT)
    set(VCPKG_ROOT "D:/vcpkg")
endif()
if(NOT WX64DEVKIT_ROOT)
    set(WX64DEVKIT_ROOT "E:/w64devkit")
endif()
if(NOT GTEST_ROOT)
    set(GTEST_ROOT "E:/eclipse_workspace/googletest")
endif()

# vcpkg tree quirk (2026-11-10): the zlib port installed its static lib as
# libzs.a while its config files still point at libzlib.a, so module-mode
# FindZLIB finds the header (1.3.2) but no library. Seed the cache before
# find_package(CURL), whose CURLConfig.cmake runs find_dependency(ZLIB).
if(NOT ZLIB_LIBRARY
        AND EXISTS "${VCPKG_ROOT}/installed/x64-mingw-static/lib/libzs.a")
    set(ZLIB_INCLUDE_DIR "${VCPKG_ROOT}/installed/x64-mingw-static/include"
        CACHE PATH "zlib include dir" FORCE)
    set(ZLIB_LIBRARY "${VCPKG_ROOT}/installed/x64-mingw-static/lib/libzs.a"
        CACHE FILEPATH "zlib library" FORCE)
endif()

# ---- Embedded bootstrap schema (resources/schema_init.sql -> DatabaseSchemaInit.cpp) ----
# The generated file is compiled into every target that links CORE_SOURCES, plus
# the DatabaseBootstrap unit-test target. CMAKE_CONFIGURE_DEPENDS makes editing the
# SQL trigger a reconfigure; without it the stale text would be embedded silently.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resources)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/resources/schema_init.sql SCHEMA_INIT_SQL)
set(SCHEMA_INIT_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/DatabaseSchemaInit.cpp)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/DatabaseSchemaInit.cpp.in
    ${SCHEMA_INIT_GENERATED}
    @ONLY
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/schema_init.sql)

# ---- Embedded WebDAV credential blob (gitignored; empty when not provisioned) ----
set(WEBDAV_SECRET_INC ${CMAKE_CURRENT_SOURCE_DIR}/resources/webdav_secret.inc)
if(EXISTS ${WEBDAV_SECRET_INC})
    file(READ ${WEBDAV_SECRET_INC} WEBDAV_SECRET_BLOB)
    message(STATUS "WebDAV credentials: provisioned")
else()
    set(WEBDAV_SECRET_BLOB "")
    message(STATUS "WebDAV credentials: NOT provisioned (cloud sync disabled)")
endif()
string(STRIP "${WEBDAV_SECRET_BLOB}" WEBDAV_SECRET_BLOB)
set(WEBDAV_SECRET_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/WebDavSecret.cpp)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/WebDavSecret.cpp.in
    ${WEBDAV_SECRET_GENERATED}
    @ONLY
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WEBDAV_SECRET_INC})

# ---- Upload gate token (from the environment, or gitignored .inc) ----
set(UPLOAD_TOKEN_INC ${CMAKE_CURRENT_SOURCE_DIR}/resources/upload_token.inc)
# The "upload to cloud" entry points only work when VALIDPROXY_UPLOAD_TOKEN
# matches this value at runtime. Precedence:
#   1. -DVALIDPROXY_UPLOAD_TOKEN=...   (explicit cache variable)
#   2. $ENV{VALIDPROXY_UPLOAD_TOKEN}   (the value set in the environment)
#   3. resources/upload_token.inc      (gitignored file, script-provisioned)
#   4. empty                           (= upload permanently disabled)
if(DEFINED VALIDPROXY_UPLOAD_TOKEN AND NOT "${VALIDPROXY_UPLOAD_TOKEN}" STREQUAL "")
    set(UPLOAD_TOKEN "${VALIDPROXY_UPLOAD_TOKEN}")
    set(UPLOAD_TOKEN_SOURCE "-DVALIDPROXY_UPLOAD_TOKEN")
elseif(DEFINED ENV{VALIDPROXY_UPLOAD_TOKEN} AND NOT "$ENV{VALIDPROXY_UPLOAD_TOKEN}" STREQUAL "")
    set(UPLOAD_TOKEN "$ENV{VALIDPROXY_UPLOAD_TOKEN}")
    set(UPLOAD_TOKEN_SOURCE "environment")
elseif(EXISTS ${UPLOAD_TOKEN_INC})
    file(READ ${UPLOAD_TOKEN_INC} UPLOAD_TOKEN)
    set(UPLOAD_TOKEN_SOURCE "resources/upload_token.inc")
else()
    set(UPLOAD_TOKEN "")
    set(UPLOAD_TOKEN_SOURCE "")
endif()
string(STRIP "${UPLOAD_TOKEN}" UPLOAD_TOKEN)
if(UPLOAD_TOKEN STREQUAL "")
    message(STATUS "Upload gate: NOT provisioned (upload disabled)")
else()
    message(STATUS "Upload gate: provisioned from ${UPLOAD_TOKEN_SOURCE}")
endif()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/UploadGateToken.h.in
    ${CMAKE_BINARY_DIR}/include/UploadGateToken.h
    @ONLY
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${UPLOAD_TOKEN_INC})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)
include_directories(${VCPKG_ROOT}/installed/x64-mingw-static/include)
link_directories(${VCPKG_ROOT}/installed/x64-mingw-static/lib)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
# SQLite3 is a static vcpkg dependency (all deps static except wxWidgets).
# Seed the CMake search paths with the vcpkg static tree so a fresh configure
# needs no manual -DSQLite3_LIBRARY/-DSQLite3_INCLUDE_DIR (previously cached
# only via command line). find_library/find_path re-search would otherwise
# override a cached hint with NOTFOUND.
list(APPEND CMAKE_LIBRARY_PATH "${VCPKG_ROOT}/installed/x64-mingw-static/lib")
list(APPEND CMAKE_INCLUDE_PATH "${VCPKG_ROOT}/installed/x64-mingw-static/include")
find_package(SQLite3 REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_BIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_BIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_BIN_DIR})

# Shared core sources (compiled into both CLI and GUI targets)
set(CORE_SOURCES
    src/CrashHandler.cpp
    src/ConfigGenerator.cpp
    src/ConfigReader.cpp
    src/config/ActiveConfigPath.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
    src/i18n/Language.cpp
    src/i18n/I18n.cpp
    src/i18n/StringTable.cpp
    src/i18n/LogBanner.cpp
    src/LogStatistics.cpp
    src/PortManager.cpp
    src/ProfileitemDAO.cpp
    src/SubitemDAO.cpp
    src/ProfileExItemDAO.cpp
    src/FullConfigTemplateDAO.cpp
    src/SqlQuery.cpp
    src/ProxyBatchTester.cpp
    src/ProxyFinder.cpp
    src/ProxyTester.cpp
    src/ShareLink.cpp
    src/SubitemUpdaterV2.cpp
    src/update/SubscriptionParser.cpp
    src/update/SubscriptionUpdater.cpp
    src/update/Deduplicator.cpp
    src/update/Importer.cpp
    src/UrlFetcher.cpp
    src/Utils.cpp
    src/PoolCandidate.cpp
    src/utils/Base64Utils.cpp
    src/utils/DnsCache.cpp

    src/XrayApi.cpp
    src/XrayInstance.cpp
    src/XrayManager.cpp
    src/RegionBatchResolver.cpp
    src/AutoTaskManager.cpp
    src/NetworkMonitor.cpp
    src/config/ConfigFileStore.cpp
    src/config/ConfigPathResolver.cpp
    src/config/ConfigJsonParser.cpp
    src/config/ConfigJsonSerializer.cpp
    src/config/ConfigValidator.cpp
    src/config/ConfigBuilder.cpp
    src/config/StepRegistry.cpp
    src/config/ConfigPipeline.cpp
    src/config/StepHandlers.cpp
    src/share/UriCodec.cpp
    src/share/Ipv6Formatter.cpp
    src/share/VLESSUriBuilder.cpp
    src/share/VMessUriBuilder.cpp
    src/share/TrojanUriBuilder.cpp
    src/share/SSUriBuilder.cpp
    src/share/Hysteria2UriBuilder.cpp
    src/share/TUICUriBuilder.cpp
    src/share/ShareLinkFactory.cpp
    src/config/ProfileConfigRepository.cpp
    src/config/ProfileNormalizer.cpp
    src/config/StreamSettingsBuilder.cpp
    src/config/OutboundBuilderFactory.cpp
    src/config/XrayConfigAssembler.cpp
    src/config/outbound/VLESSOutboundBuilder.cpp
    src/config/outbound/VMessOutboundBuilder.cpp
    src/config/outbound/SSOutboundBuilder.cpp
    src/config/outbound/TrojanOutboundBuilder.cpp
    src/config/outbound/SOCKSOutboundBuilder.cpp
    src/config/outbound/HTTPOutboundBuilder.cpp
    src/config/outbound/Hysteria2OutboundBuilder.cpp
    src/config/outbound/TUICOutboundBuilder.cpp
    src/config/outbound/WireGuardOutboundBuilder.cpp
    src/config/SingBoxOutboundBuilderFactory.cpp
    src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
    src/config/outbound/SingBoxVMessOutboundBuilder.cpp
    src/config/outbound/SingBoxSSOutboundBuilder.cpp
    src/config/outbound/SingBoxTrojanOutboundBuilder.cpp
    src/config/outbound/SingBoxHysteria2OutboundBuilder.cpp
    src/config/outbound/SingBoxTUICOutboundBuilder.cpp
    src/config/ComponentBundleDAO.cpp
    src/config/ComponentRegistry.cpp
    src/config/ComponentResolver.cpp
    src/config/ConflictDetector.cpp
    src/config/BundleChainResolver.cpp
    src/config/StepToComponentMapper.cpp
    src/config/ConfigGenerator.cpp
    src/config/SchemaValidator.cpp
    src/config/TemplateDecomposer.cpp
    src/test/ProxyBatchQuery.cpp
    src/test/XrayWorkerPool.cpp
    src/test/ProxyTestCounters.cpp
    src/test/ProxyTestResultSink.cpp
    src/service/DatabaseConnectionService.cpp
    src/service/DatabaseBootstrap.cpp
    src/net/WebDavClient.cpp
    src/net/WebDavCredentials.cpp
    src/service/CloudSyncService.cpp
    src/service/UploadGate.cpp
    ${SCHEMA_INIT_GENERATED}
    ${WEBDAV_SECRET_GENERATED}
    src/task/MutexPolicy.cpp
    src/task/TaskQueue.cpp
    src/task/TaskType.cpp
    src/task/TaskFormat.cpp
    src/service/ConfigService.cpp
    src/service/SubscriptionService.cpp
    src/service/ProxyListService.cpp
    src/service/ProxyTestService.cpp
    src/service/ShareLinkExportService.cpp
    src/service/DatabaseMaintenanceService.cpp
    src/service/AutoTaskService.cpp
    src/ProcessExitListener.cpp
    src/ProcessInspector.cpp
    src/ProxyRuntimeHistory.cpp
    src/ProxyScorer.cpp
    src/ProxyConnectivityVerifier.cpp
    src/ui/UiOperationRunner.cpp
    src/StandaloneProxyPool.cpp
    src/ProxyHealthEvaluator.cpp
    src/ProxyProbePool.cpp
)

# ===== CLI target (CONSOLE subsystem) =====
add_executable(validproxy-cli
    src/main_cli.cpp
    ${CORE_SOURCES}
)

target_link_libraries(validproxy-cli PRIVATE
    Threads::Threads
    CURL::libcurl
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    -ldbghelp
    -lws2_32
)
add_dependencies(validproxy-cli update_version_h)

# Force CLI output to bin (override global setting for tests)
set_target_properties(validproxy-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_BIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_BIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_BIN_DIR}
)

# Deploy the MSVCRT-only libwinpthread next to the CLI (see the Win7 SP1 note in
# the VALIDPROXY_STATIC_WX block: the toolchain's bundled DLL needs the UCRT).
if(VALIDPROXY_STATIC_WX AND VALIDPROXY_MSVCRT_WINPTHREAD_DLL
   AND EXISTS "${VALIDPROXY_MSVCRT_WINPTHREAD_DLL}")
    add_custom_command(TARGET validproxy-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VALIDPROXY_MSVCRT_WINPTHREAD_DLL}"
                "$<TARGET_FILE_DIR:validproxy-cli>/libwinpthread-1.dll"
        COMMENT "Deploying MSVCRT-only libwinpthread-1.dll (Win7 SP1)"
        VERBATIM)
endif()

# MinGW runtime DLLs (libstdc++/libgcc/libwinpthread) - see the same block
# under the validproxy GUI target: the loader must find the compiler's own
# runtime next to the exe because system PATH has Tesseract-OCR's OLDER
# MinGW runtime before the MinGW/vcpkg dirs (would deadlock at startup).
# Conditional for the same reason: a static-runtime toolchain (w64devkit)
# has no such DLLs and the copy would fail POST_BUILD.
get_filename_component(MINGW_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
if(EXISTS "${MINGW_RUNTIME_DIR}/libstdc++-6.dll")
    set(_mingw_runtime_dlls
        "${MINGW_RUNTIME_DIR}/libstdc++-6.dll"
        "${MINGW_RUNTIME_DIR}/libgcc_s_seh-1.dll")
    # Win7 SP1: the toolchain's libwinpthread-1.dll requires the UCRT api-sets.
    # The STATIC_WX configuration deploys the MSVCRT-only build instead
    # (resources/win7/libwinpthread-1.dll), so never clobber it here.
    if(NOT VALIDPROXY_STATIC_WX)
        list(APPEND _mingw_runtime_dlls "${MINGW_RUNTIME_DIR}/libwinpthread-1.dll")
    endif()
    add_custom_command(TARGET validproxy-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_mingw_runtime_dlls}
            ${VALIDPROXY_BIN_DIR}/
        COMMENT "Copy MinGW runtime DLLs to bin/ (PATH has Tesseract-OCR before MinGW)"
        VERBATIM
    )
    unset(_mingw_runtime_dlls)
endif()

if(USE_GRPC_API)
    target_compile_definitions(validproxy-cli PRIVATE USE_GRPC_API=1)
endif()

# -- wxWidgets (GUI mode) --
# vcpkg wxWidgets dynamic install dir. Defaults to the msvcrt tree; override
# with -DWX_VCPKG_DIR=<dir> to use a differently-built wx (e.g. the UCRT
# rebuild that fixes the GUI 0xC0000374 heap corruption, see
# docs/bugfix/2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md).
set(WX_VCPKG_DIR "${VCPKG_ROOT}/installed/x64-mingw-dynamic" CACHE PATH "vcpkg wxWidgets dynamic install dir")
# Find wxWidgets as a CMake CONFIG package from vcpkg (dynamic variant, which has proper cmake config)
# Pre-find NanoSVG dependency so wxWidgets config can find it
find_package(NanoSVG CONFIG QUIET
    PATHS "${WX_VCPKG_DIR}/share/nanosvg"
    NO_DEFAULT_PATH
)
if(NOT NanoSVG_FOUND)
    message(STATUS "NanoSVG not found - wxWidgets will not be available")
endif()

find_package(wxWidgets CONFIG QUIET
    PATHS "${WX_VCPKG_DIR}/share/wxwidgets"
    NO_DEFAULT_PATH
)

if(wxWidgets_FOUND)
    message(STATUS "wxWidgets found: ${wxWidgets_VERSION}")

    # Create UI source file list
    set(UI_SOURCES
        src/ui/Events.h
        src/ui/Events.cpp
        src/ui/AppController.h
        src/ui/AppController.cpp
        src/StandaloneProxyLogForwarder.cpp
        src/StandaloneConfigPort.cpp
        src/ui/MainFrame.h
        src/ui/MainFrame.cpp
        src/ui/SubscriptionListModel.h
        src/ui/SubscriptionListModel.cpp
        src/ui/SubscriptionPanel.h
        src/ui/SubscriptionPanel.cpp
        src/ui/ProxyListModel.h
        src/ui/ProxyListModel.cpp
        src/ui/ProxyListPanel.h
        src/ui/ProxyListPanel.cpp
        src/ui/ProxyDetailPanel.h
        src/ui/ProxyDetailPanel.cpp
        src/ui/LogPanel.h
        src/ui/LogPanel.cpp
        src/ui/TaskMonitorPanel.h
        src/ui/TaskMonitorPanel.cpp
        src/ui/BatchQueuePanel.h
        src/ui/BatchQueuePanel.cpp
        src/ui/UiControlIds.h
        src/ui/ConfigDialog.h
        src/ui/ConfigDialog.cpp
        src/ui/ComponentListPanel.h
        src/ui/ComponentListPanel.cpp
        src/ui/BundleChainEditor.h
        src/ui/BundleChainEditor.cpp
        src/ui/BundleSelectorDialog.h
        src/ui/BundleSelectorDialog.cpp
        src/ui/SqlQueryManageDialog.h
        src/ui/SqlQueryManageDialog.cpp
        src/ui/StandaloneFloatingWidget.h
        src/ui/StandaloneFloatingWidget.cpp
        src/ui/AddPoolMemberDialog.h
        src/ui/AddPoolMemberDialog.cpp
        src/ui/TestOnlineResultDialog.h
        src/ui/TestOnlineResultDialog.cpp
        src/ui/TrayIcon.h
        src/ui/TrayIcon.cpp
        src/ui/UIApp.h
        src/ui/UIApp.cpp
        src/ui/RefreshCommands.cpp
        src/ui/icons.rc
    )

    # RC flags: let windres find data files and MinGW headers
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_SOURCE_DIR} -I${WX64DEVKIT_ROOT}/x86_64-w64-mingw32/include")

    # ===== GUI target (WIN32 subsystem) =====
    add_executable(validproxy
        src/main_gui.cpp
        ${CORE_SOURCES}
        ${UI_SOURCES}
    )

    target_link_libraries(validproxy PRIVATE
        Threads::Threads
        CURL::libcurl
        ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
        ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
        -ldbghelp
        -lws2_32
    )
add_dependencies(validproxy update_version_h)

    # Deploy the MSVCRT-only libwinpthread next to the GUI exe (Win7 SP1: the
    # toolchain's bundled libwinpthread-1.dll requires the UCRT api-sets).
    if(VALIDPROXY_STATIC_WX AND VALIDPROXY_MSVCRT_WINPTHREAD_DLL
       AND EXISTS "${VALIDPROXY_MSVCRT_WINPTHREAD_DLL}")
        add_custom_command(TARGET validproxy POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VALIDPROXY_MSVCRT_WINPTHREAD_DLL}"
                    "$<TARGET_FILE_DIR:validproxy>/libwinpthread-1.dll"
            COMMENT "Deploying MSVCRT-only libwinpthread-1.dll (Win7 SP1)"
            VERBATIM)
    endif()

    # Link wxWidgets imported targets
    target_link_libraries(validproxy PRIVATE
        wx::wxbase
        wx::wxcore
        wx::wxadv
        wx::wxaui
        wx::wxhtml
        wx::wxpropgrid
        wx::wxnet
    )

    # Add include directory for wx headers (dynamic variant headers)
    target_include_directories(validproxy PRIVATE "${WX_VCPKG_DIR}/include")

    # Add include directory for UI source headers
    target_include_directories(validproxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)

    # Scheme A (PATH-based): wx DLLs are resolved via the PATH env var
    # pointing to ${WX_VCPKG_DIR}/{,debug/}bin.
    # No DLLs are copied to ${CMAKE_SOURCE_DIR}/bin (docs/DEV-PROCESS.md 规则 5).

    # WIN32_EXECUTABLE creates GUI subsystem executable (no console window)
    set_target_properties(validproxy PROPERTIES
        WIN32_EXECUTABLE TRUE
        RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_BIN_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_BIN_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_BIN_DIR}
    )
    target_compile_definitions(validproxy PRIVATE HAS_WXWIDGETS=1)

    # ===== MinGW runtime DLLs (libstdc++/libgcc/libwinpthread) =====
    # validproxy.exe is built with CLion's bundled MinGW (GCC 15.2.0) and
    # imports libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll.
    # The system PATH has "C:\Program Files (x86)\Tesseract-OCR" BEFORE the
    # MinGW/vcpkg dirs, so the loader picks Tesseract's OLDER MinGW runtime
    # at startup and validproxy dies in ntdll!NtRaiseHardError (entry point
    # not found -> hard-error dialog -> deadlock). Copy the compiler's own
    # runtime DLLs next to the exe: the loader searches the exe directory
    # FIRST, so the correct versions always load regardless of PATH order.
    # NOTE: this is an EXCEPTION to docs/DEV-PROCESS.md 规则 5 (which forbids
    # copying DEPENDENCY DLLs like wxWidgets) - the MinGW runtime is the
    # compiler's own C++ runtime, not a copyable dependency.
    # CONDITIONAL (2026-11-09): w64devkit links the C++ runtime STATICALLY
    # (objdump of validproxy.exe shows no libstdc++-6 import), so its bin/ dir
    # has no such DLLs to copy and an unconditional copy_if_different fails
    # POST_BUILD on every GUI link. CLion's bundled MinGW DOES ship them
    # (dynamic runtime), so keep the copy exactly for that toolchain.
    get_filename_component(MINGW_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(EXISTS "${MINGW_RUNTIME_DIR}/libstdc++-6.dll")
        set(_mingw_runtime_dlls
            "${MINGW_RUNTIME_DIR}/libstdc++-6.dll"
            "${MINGW_RUNTIME_DIR}/libgcc_s_seh-1.dll")
        # See the CLI block above: STATIC_WX deploys the MSVCRT-only
        # libwinpthread (Win7 SP1), so do not overwrite it with the UCRT one.
        if(NOT VALIDPROXY_STATIC_WX)
            list(APPEND _mingw_runtime_dlls "${MINGW_RUNTIME_DIR}/libwinpthread-1.dll")
        endif()
        add_custom_command(TARGET validproxy POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mingw_runtime_dlls}
                ${VALIDPROXY_BIN_DIR}/
            COMMENT "Copy MinGW runtime DLLs to bin/ (PATH has Tesseract-OCR before MinGW)"
            VERBATIM
        )
        unset(_mingw_runtime_dlls)
    endif()

    # ===== wxWidgets runtime DLLs (dynamic wx, GUI) =====
    # Rule 5 forbids copying dependency DLLs to bin/ and wx is resolved via
    # PATH (Scheme A) by default. BUT the Windows loader searches the exe
    # directory FIRST, so a plain double-click launch (no PATH override) loads
    # whatever wx it finds on PATH - which can be a DIFFERENT toolchain/CRT
    # than the exe (see docs/bugfix/2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md:
    # msvcrt wx + UCRT exe = heap corruption 0xC0000374). To make the GUI
    # self-contained and double-click-safe for a given wx build, set
    # -DVALIDPROXY_COPY_WX_RUNTIME=ON to copy ${WX_VCPKG_DIR}/debug/bin/*.dll
    # next to the exe (the loader picks the exe-dir copy first, so it always
    # matches the exe's CRT). OFF by default to preserve Rule 5 semantics.
    option(VALIDPROXY_COPY_WX_RUNTIME "Copy wxWidgets runtime DLLs to bin/ (exe dir) so the GUI loads a CRT-matching wx on double-click" OFF)
    if(VALIDPROXY_COPY_WX_RUNTIME AND EXISTS "${WX_VCPKG_DIR}/debug/bin")
        file(GLOB _wx_runtime_dlls "${WX_VCPKG_DIR}/debug/bin/*.dll")
        if(_wx_runtime_dlls)
            add_custom_command(TARGET validproxy POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_wx_runtime_dlls}
                    ${VALIDPROXY_BIN_DIR}/
                COMMENT "Copy wxWidgets runtime DLLs to ${VALIDPROXY_BIN_DIR}/ (exe-dir-first loader search, CRT-matched wx)"
                VERBATIM
            )
        endif()
        unset(_wx_runtime_dlls)
    endif()

    # ===== ProxyListModel evaluation-maps unit test (needs wx runtime) =====
    add_executable(test_proxy_list_model tests/test_proxy_list_model.cpp
        src/ui/ProxyListModel.cpp
        src/ProfileExItemDAO.cpp
        src/Logger.cpp
        src/LoggerInstance.cpp
        src/Utils.cpp
        tests/test_proxy_list_model.rc)
    target_include_directories(test_proxy_list_model PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        "${WX_VCPKG_DIR}/include")
    # 动态 wx 头文件必须优先于全局 static/include（否则 setup.h 按静态 wx ABI
    # 编译，链接动态 wx DLL 时产生不存在的导出符号，运行时报 0xC0000139）
    target_include_directories(test_proxy_list_model SYSTEM BEFORE PRIVATE
        "${WX_VCPKG_DIR}/include"
        "${WX_VCPKG_DIR}/lib/mswud")
    target_link_libraries(test_proxy_list_model PRIVATE
        gtest_main
        gtest
        wx::wxbase
        wx::wxcore
        wx::wxadv
        wx::wxaui
        wx::wxhtml
        wx::wxpropgrid
        wx::wxnet
        ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a)
    set_target_properties(test_proxy_list_model PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR})
    add_test(NAME ProxyListModelTest COMMAND test_proxy_list_model)

else()
    message(STATUS "wxWidgets not found - GUI mode (-ui) will not be available")
endif()

if(USE_GRPC_API)
    target_compile_definitions(validproxy PRIVATE USE_GRPC_API=1)
    message(STATUS "USE_GRPC_API enabled - using direct gRPC API instead of Xray process")
endif()

enable_testing()

# -- GTest (local source) --
add_subdirectory(${GTEST_ROOT} ${CMAKE_BINARY_DIR}/googletest)

add_executable(test_curl_easy_handle tests/test_curl_easy_handle.cpp)

add_executable(test_mutex_policy tests/test_mutex_policy.cpp src/task/MutexPolicy.cpp)
target_include_directories(test_mutex_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_mutex_policy PRIVATE gtest_main gtest)
add_test(NAME MutexPolicyTest COMMAND test_mutex_policy)

add_executable(test_task_queue tests/test_task_queue.cpp src/task/TaskQueue.cpp
    src/task/TaskType.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_task_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_task_queue PRIVATE gtest_main gtest)
add_test(NAME TaskQueueTest COMMAND test_task_queue)

# 队列侧契约: 排队任务必须出现在 snapshot().pending 且 targetId 透传 (bugfix 2026-11-04)
add_executable(test_pending_visibility tests/test_pending_visibility.cpp src/task/TaskQueue.cpp
    src/task/TaskType.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_pending_visibility PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_pending_visibility PRIVATE gtest_main gtest)
add_test(NAME PendingVisibilityTest COMMAND test_pending_visibility)

add_executable(test_task_type tests/test_task_type.cpp src/task/TaskType.cpp)
target_include_directories(test_task_type PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_task_type PRIVATE gtest_main gtest)
set_target_properties(test_task_type PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME TaskTypeLabelTest COMMAND test_task_type)

add_executable(test_task_format tests/test_task_format.cpp src/task/TaskFormat.cpp)
target_include_directories(test_task_format PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_task_format PRIVATE gtest_main gtest)
set_target_properties(test_task_format PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME TaskFormatTest COMMAND test_task_format)

add_executable(test_batch_pending tests/test_batch_pending.cpp)
target_include_directories(test_batch_pending PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_batch_pending PRIVATE gtest_main gtest)
set_target_properties(test_batch_pending PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME BatchPendingViewTest COMMAND test_batch_pending)

# ── AutoTask state-file path portability (2026-11-18: cross-machine absolute
#    paths spammed "failed to write state file") ──
add_executable(test_autotask_state_path tests/test_autotask_state_path.cpp)
target_include_directories(test_autotask_state_path PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_autotask_state_path PRIVATE gtest_main gtest)
set_target_properties(test_autotask_state_path PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME AutoTaskStatePathTest COMMAND test_autotask_state_path)

# ── UI string encoding regression guard (Win7 mojibake / wxMenu asserts) ──
# Fails when src/ui/** contains narrow (non-L, non-u8) non-ASCII literals:
# wxString would decode those UTF-8 bytes with the Windows ANSI code page.
add_executable(test_ui_string_encoding tests/test_ui_string_encoding.cpp)
target_include_directories(test_ui_string_encoding PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_ui_string_encoding PRIVATE
    VALIDPROXY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_ui_string_encoding PRIVATE gtest_main gtest)
set_target_properties(test_ui_string_encoding PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME UiStringEncodingTest COMMAND test_ui_string_encoding)

# ── i18n string table + language parsing (bilingual UI) ──
# 纯 C++（不含 wx），与 CORE_SOURCES 共用同一份生成表。
add_executable(test_i18n
    tests/test_i18n.cpp
    src/i18n/Language.cpp
    src/i18n/I18n.cpp
    src/i18n/StringTable.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
)
target_include_directories(test_i18n PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_i18n PRIVATE gtest_main gtest)
set_target_properties(test_i18n PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME I18nTest COMMAND test_i18n)

add_executable(test_task_cancel_reset tests/test_task_cancel_reset.cpp)
target_include_directories(test_task_cancel_reset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_task_cancel_reset PRIVATE
    VALIDPROXY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_task_cancel_reset PRIVATE gtest_main gtest)
add_test(NAME TaskCancelResetGuardTest COMMAND test_task_cancel_reset)
target_include_directories(test_curl_easy_handle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_curl_easy_handle PRIVATE CURL::libcurl)
set_target_properties(test_curl_easy_handle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME CurlEasyHandleTest COMMAND test_curl_easy_handle)

add_executable(test_connectivity_verify tests/test_connectivity_verify.cpp
    src/ProxyConnectivityVerifier.cpp
    src/UrlFetcher.cpp
    src/ProxyTester.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
)
target_include_directories(test_connectivity_verify PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_connectivity_verify PRIVATE
    gtest_main
    gtest
    CURL::libcurl
    -lws2_32
)
set_target_properties(test_connectivity_verify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ConnectivityVerifyTest COMMAND test_connectivity_verify)

add_executable(test_standalone_proxy_log_forwarder
    tests/test_standalone_proxy_log_forwarder.cpp
    src/StandaloneProxyLogForwarder.cpp
)
target_include_directories(test_standalone_proxy_log_forwarder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_standalone_proxy_log_forwarder PRIVATE
    gtest_main
    gtest
)
set_target_properties(test_standalone_proxy_log_forwarder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME StandaloneProxyLogForwarderTest COMMAND test_standalone_proxy_log_forwarder)

add_executable(test_standalone_config_port
    tests/test_standalone_config_port.cpp
    src/StandaloneConfigPort.cpp
)
target_include_directories(test_standalone_config_port PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_standalone_config_port PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    -lws2_32
)
set_target_properties(test_standalone_config_port PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME StandaloneConfigPortTest COMMAND test_standalone_config_port)

add_executable(test_dedup tests/test_dedup.cpp src/update/Deduplicator.cpp src/ProfileitemDAO.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_dedup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_dedup PRIVATE
    gtest_main
    gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    -lws2_32
)
set_target_properties(test_dedup PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME DedupTest COMMAND test_dedup)

add_executable(test_profile_ex_item_dao tests/test_profile_ex_item_dao.cpp src/ProfileExItemDAO.cpp src/Logger.cpp src/LoggerInstance.cpp src/Utils.cpp)
target_include_directories(test_profile_ex_item_dao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_profile_ex_item_dao PRIVATE
    gtest_main
    gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ws2_32
)
set_target_properties(test_profile_ex_item_dao PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ProfileExItemDAOTest COMMAND test_profile_ex_item_dao)

add_executable(test_sql_query tests/test_sql_query.cpp src/SqlQuery.cpp src/Logger.cpp src/LoggerInstance.cpp src/Utils.cpp)
target_include_directories(test_sql_query PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_sql_query PRIVATE
    gtest_main
    gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ws2_32
)
set_target_properties(test_sql_query PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME SqlQueryDAOTest COMMAND test_sql_query)

# -- Profileitem model tests - DISABLED until test_model.cpp is created
# add_executable(test_model tests/test_model.cpp)
# target_include_directories(test_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# target_link_libraries(test_model PRIVATE
#     gtest_main
#     gtest
#     D:/vcpkg/installed/x64-mingw-static/lib/libspdlog.a
#     D:/vcpkg/installed/x64-mingw-static/lib/libfmt.a
# )
# add_test(NAME ProfileitemTest COMMAND test_model)

# -- Logger unit tests (zero external deps beyond gtest) --
add_executable(test_logger tests/test_logger.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_logger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_logger PRIVATE gtest_main gtest)
set_target_properties(test_logger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME LoggerTest COMMAND test_logger)

# -- ShareLink unit tests (now delegates to extracted builders, includes factory + 6 builders) --
add_executable(test_sharelink
    tests/test_sharelink.cpp
    src/ShareLink.cpp
    src/share/ShareLinkFactory.cpp
    src/share/UriCodec.cpp
    src/share/Ipv6Formatter.cpp
    src/share/VLESSUriBuilder.cpp
    src/share/VMessUriBuilder.cpp
    src/share/TrojanUriBuilder.cpp
    src/share/SSUriBuilder.cpp
    src/share/Hysteria2UriBuilder.cpp
    src/share/TUICUriBuilder.cpp
)
target_include_directories(test_sharelink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_sharelink PRIVATE gtest_main gtest)
set_target_properties(test_sharelink PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ShareLinkTest COMMAND test_sharelink)

# -- UriCodec unit tests (encoding utilities extracted from ShareLink) --
add_executable(test_uri_codec tests/test_uri_codec.cpp src/share/UriCodec.cpp)
target_include_directories(test_uri_codec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_uri_codec PRIVATE gtest_main gtest)
set_target_properties(test_uri_codec PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME UriCodecTest COMMAND test_uri_codec)

# -- AutoTaskManager unit tests --
add_executable(test_autotask tests/test_autotask.cpp tests/test_sync_region.cpp src/AutoTaskManager.cpp ${CORE_SOURCES})
target_include_directories(test_autotask PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_autotask PRIVATE TEST_DB_EMPTY_PATH="${CMAKE_SOURCE_DIR}/test/guiNDB_empty.db")
if(USE_GRPC_API)
    target_compile_definitions(test_autotask PRIVATE USE_GRPC_API=1)
endif()
target_link_libraries(test_autotask PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    Threads::Threads
    CURL::libcurl
    -ldbghelp
    -lws2_32
)
set_target_properties(test_autotask PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME AutoTaskTest COMMAND test_autotask)

# -- 批量更新订阅拆解契约（spec 2026-11-03 v1.2.5 §13，owner 2026-11-05）--
# SubitemUpdaterV2 步骤观测器的发布时序/终态不变量（任务页子行数据源）。
add_executable(test_update_breakdown tests/test_update_breakdown.cpp ${CORE_SOURCES})
target_include_directories(test_update_breakdown PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(USE_GRPC_API)
    target_compile_definitions(test_update_breakdown PRIVATE USE_GRPC_API=1)
endif()
target_link_libraries(test_update_breakdown PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    Threads::Threads
    CURL::libcurl
    -ldbghelp
    -lws2_32
)
set_target_properties(test_update_breakdown PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME UpdateBreakdownTest COMMAND test_update_breakdown)

# -- Utils unit tests (pure functions, no external deps beyond gtest) --
add_executable(test_utils tests/test_utils.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# P1-3 (Spec §6 Task 6 Step 2): exposes utils::testing::g_tcpTableEnumerationCount so
# T5 SnapshotCache_EnumeratesOnceWithinTtl can observe snapshot refreshes.
target_compile_definitions(test_utils PRIVATE TEST_BUILD=1)
target_link_libraries(test_utils PRIVATE gtest_main gtest -lws2_32)
set_target_properties(test_utils PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME UtilsTest COMMAND test_utils)

# -- PoolCandidate unit tests (sort comparator + health formula, pure functions) --
add_executable(test_pool_candidate tests/test_pool_candidate.cpp src/PoolCandidate.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_pool_candidate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_pool_candidate PRIVATE gtest_main gtest -lws2_32)
set_target_properties(test_pool_candidate PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME PoolCandidateTest COMMAND test_pool_candidate)

# -- ConfigReader unit tests (includes load() tests with error reporter override) --
add_executable(test_config_reader tests/test_config_reader.cpp tests/test_config_reader_load.cpp
    src/ConfigReader.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp
    src/config/ConfigFileStore.cpp src/config/ConfigPathResolver.cpp
    src/config/ConfigJsonParser.cpp src/config/ConfigJsonSerializer.cpp
    src/config/ConfigValidator.cpp
    # ConfigReader::load() 下发 config::setEchEnabled()，实现位于此文件
    src/config/StreamSettingsBuilder.cpp
    # UiConfigParser 归一化 ui.language 需要 i18n::parseLanguage / languageCode
    src/i18n/Language.cpp)
target_include_directories(test_config_reader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_config_reader PRIVATE gtest_main gtest ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a -lws2_32)
set_target_properties(test_config_reader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ConfigReaderTest COMMAND test_config_reader)

# -- RefreshCommands unit tests (command constants + status text mapping, pure functions) --
add_executable(test_refresh_commands tests/test_refresh_commands.cpp src/ui/RefreshCommands.cpp
    src/i18n/Language.cpp src/i18n/I18n.cpp src/i18n/StringTable.cpp)
target_include_directories(test_refresh_commands PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_refresh_commands PRIVATE gtest_main gtest)
set_target_properties(test_refresh_commands PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME RefreshCommandsTest COMMAND test_refresh_commands)

# -- Toolbar region icon file-level constraints (square 1:1 + RGBA alpha from
#    background removal; pure file I/O PNG IHDR parse, no wx) --
add_executable(test_toolbar_region_icon tests/test_toolbar_region_icon.cpp)
target_include_directories(test_toolbar_region_icon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_toolbar_region_icon PRIVATE gtest_main gtest)
set_target_properties(test_toolbar_region_icon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ToolbarRegionIconTest COMMAND test_toolbar_region_icon)
# IHDR-anchored file contract; relative path needs repo-root cwd (same as UI suites).
set_tests_properties(ToolbarRegionIconTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# -- FloatingWidget pure-policy unit tests (no wx, no external deps beyond gtest) --
add_executable(test_floating_widget_state tests/test_floating_widget_state.cpp)
target_include_directories(test_floating_widget_state PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_floating_widget_state PRIVATE gtest_main gtest)
set_target_properties(test_floating_widget_state PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME FloatingWidgetStateTest COMMAND test_floating_widget_state)

# -- DatabaseBootstrap unit tests (sqlite3 + gtest only; no wx, no DAO) --
add_executable(test_database_bootstrap
    tests/test_database_bootstrap.cpp
    src/service/DatabaseBootstrap.cpp
    ${SCHEMA_INIT_GENERATED}
)
target_include_directories(test_database_bootstrap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_database_bootstrap PRIVATE
    DB_SCHEMA_BASELINE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/db_schema_baseline.sql")
target_link_libraries(test_database_bootstrap PRIVATE
    gtest_main
    gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ws2_32
)
set_target_properties(test_database_bootstrap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME DatabaseBootstrapTest COMMAND test_database_bootstrap)

# -- Cloud sync unit tests (pure logic: no network, no sqlite) --
add_executable(test_cloud_sync
    tests/test_cloud_sync.cpp
    src/service/CloudSyncService.cpp
    src/net/WebDavClient.cpp
    src/net/WebDavCredentials.cpp
    src/SubitemDAO.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
    src/Utils.cpp
    ${SCHEMA_INIT_GENERATED}
    ${WEBDAV_SECRET_GENERATED}
)
target_include_directories(test_cloud_sync PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_cloud_sync PRIVATE
    gtest_main
    gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libcurl.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libzs.a
    ws2_32
    crypt32
    secur32
    bcrypt
    wintrust
)
set_target_properties(test_cloud_sync PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME CloudSyncTest COMMAND test_cloud_sync)

# -- Upload gate unit tests (pure token predicate + real env-var read) --
add_executable(test_upload_gate
    tests/test_upload_gate.cpp
    src/service/UploadGate.cpp
)
target_include_directories(test_upload_gate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(test_upload_gate PRIVATE gtest_main gtest)
set_target_properties(test_upload_gate PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME UploadGateTest COMMAND test_upload_gate)

# -- ConfigGenerator unit tests (SQLite + boost_json, now delegates to extracted components) --
add_executable(test_config_generator
    tests/test_config_generator.cpp
    tests/test_config_builder.cpp
    tests/test_component_bundle.cpp
    tests/test_component_registry.cpp
    tests/test_component_resolver.cpp
    tests/test_conflict_detector.cpp
    tests/test_bundle_chain_resolver.cpp
    tests/test_step_to_component_mapper.cpp
    tests/test_config_generator_phase9.cpp
    tests/test_schema_validator.cpp
    tests/test_ech_switch.cpp
    tests/test_template_decomposer.cpp
    tests/test_phase11_integration.cpp
    tests/test_phase11_cross_bundle.cpp
    tests/test_phase11_performance.cpp
    src/ConfigGenerator.cpp
    src/config/ConfigBuilder.cpp
    src/config/StepRegistry.cpp
    src/config/ConfigPipeline.cpp
    src/config/StepHandlers.cpp
    src/config/ComponentBundleDAO.cpp
    src/config/ComponentRegistry.cpp
    src/config/ComponentResolver.cpp
    src/config/ConflictDetector.cpp
    src/config/BundleChainResolver.cpp
    src/config/StepToComponentMapper.cpp
    src/config/ConfigGenerator.cpp
    src/config/SchemaValidator.cpp
    src/config/TemplateDecomposer.cpp
    src/config/StreamSettingsBuilder.cpp
    src/ConfigReader.cpp
    src/FullConfigTemplateDAO.cpp
    src/SqlQuery.cpp
    src/ProfileitemDAO.cpp
    # ConfigReader → UiConfigParser 依赖 i18n::parseLanguage / languageCode
    src/i18n/Language.cpp

    src/utils/DnsCache.cpp
    src/utils/Base64Utils.cpp
    src/ProfileExItemDAO.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
    src/Utils.cpp
    src/config/ProfileConfigRepository.cpp
    src/config/ProfileNormalizer.cpp
    src/config/StreamSettingsBuilder.cpp
    src/config/OutboundBuilderFactory.cpp
    src/config/SingBoxOutboundBuilderFactory.cpp
    src/config/XrayConfigAssembler.cpp
    src/config/ConfigPathResolver.cpp
    src/config/ConfigFileStore.cpp
    src/config/ConfigJsonParser.cpp
    src/config/ConfigValidator.cpp
    src/config/ConfigJsonSerializer.cpp
    src/config/outbound/VLESSOutboundBuilder.cpp
    src/config/outbound/VMessOutboundBuilder.cpp
    src/config/outbound/SSOutboundBuilder.cpp
    src/config/outbound/TrojanOutboundBuilder.cpp
    src/config/outbound/SOCKSOutboundBuilder.cpp
    src/config/outbound/HTTPOutboundBuilder.cpp
    src/config/outbound/Hysteria2OutboundBuilder.cpp
    src/config/outbound/TUICOutboundBuilder.cpp
    src/config/outbound/WireGuardOutboundBuilder.cpp
    src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
    src/config/outbound/SingBoxVMessOutboundBuilder.cpp
    src/config/outbound/SingBoxSSOutboundBuilder.cpp
    src/config/outbound/SingBoxTrojanOutboundBuilder.cpp
    src/config/outbound/SingBoxHysteria2OutboundBuilder.cpp
    src/config/outbound/SingBoxTUICOutboundBuilder.cpp
)
target_include_directories(test_config_generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_config_generator PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    -lws2_32
)
set_target_properties(test_config_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ConfigGeneratorTest COMMAND test_config_generator)

# -- Standalone Proxy Pool unit tests (offline: config JSON + parser + pool construction) --
add_executable(test_standalone_proxy_pool
    tests/TestStandaloneProxyPool.cpp
    src/ConfigGenerator.cpp
    src/ConfigReader.cpp
    src/i18n/Language.cpp
    src/StandaloneProxyPool.cpp
    src/PortManager.cpp
    src/ProxyHealthEvaluator.cpp
    src/ProxyProbePool.cpp
    src/XrayInstance.cpp
    src/XrayApi.cpp
    src/ProfileitemDAO.cpp
    src/ProfileExItemDAO.cpp
    src/FullConfigTemplateDAO.cpp
    src/SqlQuery.cpp
    src/config/ConfigBuilder.cpp
    src/config/StepRegistry.cpp
    src/config/ConfigPipeline.cpp
    src/config/StepHandlers.cpp
    src/config/ConfigFileStore.cpp
    src/config/ConfigPathResolver.cpp
    src/config/ConfigJsonParser.cpp
    src/config/ConfigJsonSerializer.cpp
    src/config/ConfigValidator.cpp
    src/config/OutboundBuilderFactory.cpp
    src/config/SingBoxOutboundBuilderFactory.cpp
    src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
    src/config/outbound/SingBoxVMessOutboundBuilder.cpp
    src/config/outbound/SingBoxSSOutboundBuilder.cpp
    src/config/outbound/SingBoxTrojanOutboundBuilder.cpp
    src/config/outbound/SingBoxHysteria2OutboundBuilder.cpp
    src/config/outbound/SingBoxTUICOutboundBuilder.cpp
    src/utils/DnsCache.cpp
    src/utils/Base64Utils.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
    src/Utils.cpp
    src/config/ProfileConfigRepository.cpp
    src/config/ProfileNormalizer.cpp
    src/config/StreamSettingsBuilder.cpp
    src/config/XrayConfigAssembler.cpp
    src/config/outbound/VLESSOutboundBuilder.cpp
    src/config/outbound/VMessOutboundBuilder.cpp
    src/config/outbound/SSOutboundBuilder.cpp
    src/config/outbound/TrojanOutboundBuilder.cpp
    src/config/outbound/SOCKSOutboundBuilder.cpp
    src/config/outbound/HTTPOutboundBuilder.cpp
    src/config/outbound/Hysteria2OutboundBuilder.cpp
    src/config/outbound/TUICOutboundBuilder.cpp
    src/config/outbound/WireGuardOutboundBuilder.cpp
    src/config/ComponentBundleDAO.cpp
    src/config/ComponentResolver.cpp
    src/config/ConflictDetector.cpp
    src/config/BundleChainResolver.cpp
    src/config/StepToComponentMapper.cpp
    src/config/ConfigGenerator.cpp
    src/config/SchemaValidator.cpp
    src/config/TemplateDecomposer.cpp
    src/config/ComponentRegistry.cpp
)
target_include_directories(test_standalone_proxy_pool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_standalone_proxy_pool PRIVATE USE_GRPC_API=1)
target_link_libraries(test_standalone_proxy_pool PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    CURL::libcurl
    -lws2_32
)
set_target_properties(test_standalone_proxy_pool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME StandaloneProxyPoolTest COMMAND test_standalone_proxy_pool)

# -- Proxy Probe Pool unit tests (resident xray probe workers) --
add_executable(test_proxy_probe_pool
    tests/TestProxyProbePool.cpp
    src/ProxyProbePool.cpp
    src/ProxyHealthEvaluator.cpp
    src/XrayInstance.cpp
    src/XrayApi.cpp
    src/PortManager.cpp
    src/ConfigReader.cpp
    src/i18n/Language.cpp
    src/FullConfigTemplateDAO.cpp
    src/SqlQuery.cpp
    src/config/ConfigBuilder.cpp
    src/config/StepRegistry.cpp
    src/config/ConfigPipeline.cpp
    src/config/StepHandlers.cpp
    src/config/ConfigFileStore.cpp
    src/config/ConfigPathResolver.cpp
    src/config/ConfigJsonParser.cpp
    src/config/ConfigJsonSerializer.cpp
    src/config/ConfigValidator.cpp
    src/config/OutboundBuilderFactory.cpp
    src/config/SingBoxOutboundBuilderFactory.cpp
    src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
    src/config/outbound/SingBoxVMessOutboundBuilder.cpp
    src/config/outbound/SingBoxSSOutboundBuilder.cpp
    src/config/outbound/SingBoxTrojanOutboundBuilder.cpp
    src/config/outbound/SingBoxHysteria2OutboundBuilder.cpp
    src/config/outbound/SingBoxTUICOutboundBuilder.cpp
    src/Utils.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
    src/config/ProfileConfigRepository.cpp
    src/config/ProfileNormalizer.cpp
    src/config/StreamSettingsBuilder.cpp
    src/config/XrayConfigAssembler.cpp
    src/config/outbound/VLESSOutboundBuilder.cpp
    src/config/outbound/VMessOutboundBuilder.cpp
    src/config/outbound/SSOutboundBuilder.cpp
    src/config/outbound/TrojanOutboundBuilder.cpp
    src/config/outbound/SOCKSOutboundBuilder.cpp
    src/config/outbound/HTTPOutboundBuilder.cpp
    src/config/outbound/Hysteria2OutboundBuilder.cpp
    src/config/outbound/TUICOutboundBuilder.cpp
    src/config/outbound/WireGuardOutboundBuilder.cpp
    src/utils/Base64Utils.cpp
    src/ProfileitemDAO.cpp
    src/ProfileExItemDAO.cpp
    src/utils/DnsCache.cpp
    src/config/ComponentBundleDAO.cpp
    src/config/ComponentResolver.cpp
    src/config/ConflictDetector.cpp
    src/config/BundleChainResolver.cpp
    src/config/StepToComponentMapper.cpp
    src/config/ConfigGenerator.cpp
    src/config/SchemaValidator.cpp
    src/config/TemplateDecomposer.cpp
    src/config/ComponentRegistry.cpp
)
target_include_directories(test_proxy_probe_pool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(test_proxy_probe_pool PRIVATE USE_GRPC_API=1)
target_link_libraries(test_proxy_probe_pool PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    CURL::libcurl
    -lws2_32
)
set_target_properties(test_proxy_probe_pool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ProxyProbePoolTest COMMAND test_proxy_probe_pool)

# -- Delete Subscription tests --
add_executable(test_delete_subscription tests/test_delete_subscription.cpp     src/ProfileitemDAO.cpp

    src/utils/DnsCache.cpp
    src/SubitemDAO.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_delete_subscription PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_delete_subscription PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    -lws2_32
)
set_target_properties(test_delete_subscription PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME DeleteSubscriptionTest COMMAND test_delete_subscription)

# -- SubscriptionParser unit tests --
add_executable(test_subscription_parser tests/test_subscription_parser.cpp src/update/SubscriptionParser.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_subscription_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_subscription_parser PRIVATE gtest_main gtest ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a -lws2_32)
set_target_properties(test_subscription_parser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME SubscriptionParserTest COMMAND test_subscription_parser)

# -- ProxyBatchTester components tests (pure logic, no external deps) --
add_executable(test_proxy_batch_components tests/test_proxy_batch_components.cpp)
target_include_directories(test_proxy_batch_components PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_proxy_batch_components PRIVATE gtest_main gtest)
set_target_properties(test_proxy_batch_components PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ProxyBatchComponentsTest COMMAND test_proxy_batch_components)

# -- Batch worker lifecycle policy tests (pure logic, no external deps) --
add_executable(test_batch_worker_policy tests/test_batch_worker_policy.cpp)
target_include_directories(test_batch_worker_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_batch_worker_policy PRIVATE gtest_main gtest)
set_target_properties(test_batch_worker_policy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME BatchWorkerPolicyTest COMMAND test_batch_worker_policy)

# -- ado payload normalization tests (pure logic, boost::json only) --
add_executable(test_outbound_payload tests/test_outbound_payload.cpp)
target_include_directories(test_outbound_payload PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_outbound_payload PRIVATE gtest_main gtest ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a)
set_target_properties(test_outbound_payload PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME OutboundPayloadTest COMMAND test_outbound_payload)

# -- ProxyBatchQuery integration tests (uses test DB) --
add_executable(test_proxy_batch_query tests/test_proxy_batch_query.cpp src/test/ProxyBatchQuery.cpp
    src/ConfigGenerator.cpp src/ProfileitemDAO.cpp src/utils/DnsCache.cpp src/ProfileExItemDAO.cpp
    src/ConfigReader.cpp src/FullConfigTemplateDAO.cpp src/SqlQuery.cpp src/config/ConfigBuilder.cpp
    src/i18n/Language.cpp
    src/config/StepRegistry.cpp src/config/ConfigPipeline.cpp src/config/StepHandlers.cpp
    src/config/ConfigFileStore.cpp src/config/ConfigPathResolver.cpp
    src/config/ConfigJsonParser.cpp src/config/ConfigJsonSerializer.cpp src/config/ConfigValidator.cpp
    src/Logger.cpp src/LoggerInstance.cpp src/Utils.cpp
    src/config/ProfileConfigRepository.cpp src/config/ProfileNormalizer.cpp
    src/config/StreamSettingsBuilder.cpp src/config/OutboundBuilderFactory.cpp
    src/config/SingBoxOutboundBuilderFactory.cpp
    src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
    src/config/outbound/SingBoxVMessOutboundBuilder.cpp
    src/config/outbound/SingBoxSSOutboundBuilder.cpp
    src/config/outbound/SingBoxTrojanOutboundBuilder.cpp
    src/config/outbound/SingBoxHysteria2OutboundBuilder.cpp
    src/config/outbound/SingBoxTUICOutboundBuilder.cpp
    src/utils/Base64Utils.cpp
    src/config/XrayConfigAssembler.cpp
    src/config/outbound/VLESSOutboundBuilder.cpp
    src/config/outbound/VMessOutboundBuilder.cpp
    src/config/outbound/SSOutboundBuilder.cpp
    src/config/outbound/TrojanOutboundBuilder.cpp
    src/config/outbound/SOCKSOutboundBuilder.cpp
    src/config/outbound/HTTPOutboundBuilder.cpp
    src/config/outbound/Hysteria2OutboundBuilder.cpp
    src/config/outbound/TUICOutboundBuilder.cpp
    src/config/outbound/WireGuardOutboundBuilder.cpp
    src/config/ComponentBundleDAO.cpp
    src/config/ComponentResolver.cpp
    src/config/ConflictDetector.cpp
    src/config/BundleChainResolver.cpp
    src/config/StepToComponentMapper.cpp
    src/config/ConfigGenerator.cpp
    src/config/SchemaValidator.cpp
    src/config/TemplateDecomposer.cpp
    src/config/ComponentRegistry.cpp
)
target_include_directories(test_proxy_batch_query PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_proxy_batch_query PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    -lws2_32
)
set_target_properties(test_proxy_batch_query PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ProxyBatchQueryTest COMMAND test_proxy_batch_query)

# -- NetworkMonitor unit tests --
add_executable(test_network_monitor tests/test_network_monitor.cpp src/NetworkMonitor.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_network_monitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_network_monitor PRIVATE gtest_main gtest Threads::Threads CURL::libcurl)
set_target_properties(test_network_monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME NetworkMonitorTest COMMAND test_network_monitor)

# -- Ipv6Formatter unit tests (pure functions, no external deps beyond gtest) --
add_executable(test_ipv6_formatter tests/test_ipv6_formatter.cpp src/share/Ipv6Formatter.cpp)
target_include_directories(test_ipv6_formatter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_ipv6_formatter PRIVATE gtest_main gtest)
set_target_properties(test_ipv6_formatter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME Ipv6FormatterTest COMMAND test_ipv6_formatter)

# -- LogStatistics unit tests (pure file parser, no external deps beyond gtest) --
add_executable(test_log_statistics tests/test_log_statistics.cpp src/LogStatistics.cpp)
target_include_directories(test_log_statistics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_log_statistics PRIVATE gtest_main gtest)
set_target_properties(test_log_statistics PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME LogStatisticsTest COMMAND test_log_statistics)

# -- LogStatisticsEvent unit test (requires wxWidgets for Events.h) --
if(wxWidgets_FOUND)
    add_executable(test_log_statistics_event
        tests/test_log_statistics_event.cpp
        src/ui/Events.cpp
    )
    target_include_directories(test_log_statistics_event PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    )
    target_link_libraries(test_log_statistics_event PRIVATE
        gtest_main gtest
        wx::wxbase
        wx::wxcore
        # NOTE: wxcore pulls in comctl32!GetWindowSubclass (delay-loaded), which
        # requires a Common Controls v6 manifest. Test executables do not embed
        # src/ui/app.manifest, so on this Release tree the exe dies at load with
        # 0xC0000139 (STATUS_ENTRYPOINT_NOT_FOUND) against comctl32 5.82.
        # Pre-existing test-infra gap, unrelated to the app itself (see
        # docs/bugfix/2026-11-17-Bugfix-Win7-UIEncoding-MenuAssertCrash-v1.0.md §5.1).
    )
    target_compile_definitions(test_log_statistics_event PRIVATE HAS_WXWIDGETS=1)
    set_target_properties(test_log_statistics_event PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
    )
    # Scheme A (PATH-based): rely on PATH env var; do not copy wx DLLs to tests/.
    add_test(NAME LogStatisticsEventTest COMMAND test_log_statistics_event)
endif()

# -- UnifiedMonitorRows unit test (pure merge function; requires wxWidgets for AppController.h types) --
if(wxWidgets_FOUND)
    add_executable(test_unified_monitor_rows
        tests/test_unified_monitor_rows.cpp
    )
    target_include_directories(test_unified_monitor_rows PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    )
    target_link_libraries(test_unified_monitor_rows PRIVATE
        gtest_main gtest
        wx::wxbase
        wx::wxcore
    )
    target_compile_definitions(test_unified_monitor_rows PRIVATE HAS_WXWIDGETS=1)
    set_target_properties(test_unified_monitor_rows PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
    )
    # Scheme A (PATH-based): rely on PATH env var; do not copy wx DLLs to tests/.
    add_test(NAME UnifiedMonitorRowsTest COMMAND test_unified_monitor_rows)
endif()

# -- ShareUriBuilders unit tests (6 protocol URI builders, no external deps beyond gtest) --
add_executable(test_share_uri_builders
    tests/test_vless_uri_builder.cpp
    tests/test_vmess_uri_builder.cpp
    tests/test_trojan_uri_builder.cpp
    tests/test_ss_uri_builder.cpp
    tests/test_hysteria2_uri_builder.cpp
    tests/test_tuic_uri_builder.cpp
    src/share/VLESSUriBuilder.cpp
    src/share/VMessUriBuilder.cpp
    src/share/TrojanUriBuilder.cpp
    src/share/SSUriBuilder.cpp
    src/share/Hysteria2UriBuilder.cpp
    src/share/TUICUriBuilder.cpp
    src/share/UriCodec.cpp
)
target_include_directories(test_share_uri_builders PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_share_uri_builders PRIVATE gtest_main gtest)
set_target_properties(test_share_uri_builders PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ShareUriBuildersTest COMMAND test_share_uri_builders)

# -- ShareLinkFactory unit tests (routing logic, includes 6 builders) --
add_executable(test_sharelink_factory
    tests/test_sharelink_factory.cpp
    src/share/ShareLinkFactory.cpp
    src/share/UriCodec.cpp
    src/share/VLESSUriBuilder.cpp
    src/share/VMessUriBuilder.cpp
    src/share/TrojanUriBuilder.cpp
    src/share/SSUriBuilder.cpp
    src/share/Hysteria2UriBuilder.cpp
    src/share/TUICUriBuilder.cpp
)
target_include_directories(test_sharelink_factory PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_sharelink_factory PRIVATE gtest_main gtest)
set_target_properties(test_sharelink_factory PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ShareLinkFactoryTest COMMAND test_sharelink_factory)

# -- Sing-box VLESS outbound builder unit tests --
add_executable(test_singbox_vless_builder
    tests/test_singbox_vless_builder.cpp
    src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
    src/utils/Base64Utils.cpp
)
target_include_directories(test_singbox_vless_builder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_singbox_vless_builder PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
)
set_target_properties(test_singbox_vless_builder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME SingBoxVLESSBuilderTest COMMAND test_singbox_vless_builder)

# -- XrayApi Direct gRPC helpers unit tests (requires USE_GRPC_API=1) --
if(USE_GRPC_API)
    add_executable(test_xray_api_direct
        tests/test_xray_api_direct.cpp
        src/XrayApi.cpp
        src/Logger.cpp
        src/LoggerInstance.cpp
    )
    target_include_directories(test_xray_api_direct PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(test_xray_api_direct PRIVATE USE_GRPC_API=1 TEST_BUILD=1)
    target_link_libraries(test_xray_api_direct PRIVATE gtest_main gtest
        ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
        -lws2_32
    )
    set_target_properties(test_xray_api_direct PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
    )
    add_test(NAME XrayApiDirectTest COMMAND test_xray_api_direct)
endif()

# -- XrayInstance stderr/exit-code observability tests (compiled fake xray) --
add_executable(test_xray_instance_stderr
    tests/test_xray_instance_stderr.cpp
    src/XrayInstance.cpp
    src/ConfigReader.cpp
    src/i18n/Language.cpp
    src/FullConfigTemplateDAO.cpp
    src/SqlQuery.cpp
    src/config/ConfigBuilder.cpp
    src/config/StepRegistry.cpp
    src/config/ConfigPipeline.cpp
    src/config/StepHandlers.cpp
    src/config/ConfigFileStore.cpp
    src/config/ConfigPathResolver.cpp
    src/config/ConfigJsonParser.cpp
    src/config/ConfigJsonSerializer.cpp
    src/config/ConfigValidator.cpp
    src/config/OutboundBuilderFactory.cpp
    src/config/SingBoxOutboundBuilderFactory.cpp
    src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
    src/config/outbound/SingBoxVMessOutboundBuilder.cpp
    src/config/outbound/SingBoxSSOutboundBuilder.cpp
    src/config/outbound/SingBoxTrojanOutboundBuilder.cpp
    src/config/outbound/SingBoxHysteria2OutboundBuilder.cpp
    src/config/outbound/SingBoxTUICOutboundBuilder.cpp
    src/config/outbound/VLESSOutboundBuilder.cpp
    src/config/outbound/VMessOutboundBuilder.cpp
    src/config/outbound/SSOutboundBuilder.cpp
    src/config/outbound/TrojanOutboundBuilder.cpp
    src/config/outbound/SOCKSOutboundBuilder.cpp
    src/config/outbound/HTTPOutboundBuilder.cpp
    src/config/outbound/Hysteria2OutboundBuilder.cpp
    src/config/outbound/TUICOutboundBuilder.cpp
    src/config/outbound/WireGuardOutboundBuilder.cpp
    src/utils/Base64Utils.cpp
    src/Utils.cpp
    src/ProfileitemDAO.cpp
    src/ProfileExItemDAO.cpp
    src/config/ProfileConfigRepository.cpp
    src/config/ProfileNormalizer.cpp
    src/config/StreamSettingsBuilder.cpp
    src/config/XrayConfigAssembler.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
    src/config/ComponentBundleDAO.cpp
    src/config/ComponentResolver.cpp
    src/config/ConflictDetector.cpp
    src/config/BundleChainResolver.cpp
    src/config/StepToComponentMapper.cpp
    src/config/ConfigGenerator.cpp
    src/config/SchemaValidator.cpp
    src/config/TemplateDecomposer.cpp
    src/config/ComponentRegistry.cpp
)
target_include_directories(test_xray_instance_stderr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_xray_instance_stderr PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    -lws2_32)
set_target_properties(test_xray_instance_stderr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME XrayInstanceStderrTest COMMAND test_xray_instance_stderr)

# Helper executable standing in for xray.exe during XrayInstance tests.
# Not a gtest binary: it only writes PANIC to stderr and exits with a
# deterministic code (7 for "startup" mode, 42 after ~8s for runtime mode).
# The test copies it into a temp dir under the mode-selecting name, so it
# must land in the same output directory as test_xray_instance_stderr.
add_executable(fake_xray_helper tests/fake_xray_helper.cpp)
set_target_properties(fake_xray_helper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)

# -- XrayManager crash-aware, bounded startup regression test --
# Verifies that XrayManager::start() fails fast (does NOT block for the full
# readiness timeout) when the backing process flash-crashes shortly after spawn
# but before its API port opens. Uses the same fake_xray_helper "latecrash" mode.
add_executable(test_xray_manager_startup
    tests/test_xray_manager_startup.cpp
    src/XrayManager.cpp
    src/XrayInstance.cpp
    src/PortManager.cpp
    src/ConfigReader.cpp
    src/i18n/Language.cpp
    src/FullConfigTemplateDAO.cpp
    src/SqlQuery.cpp
    src/config/ConfigBuilder.cpp
    src/config/StepRegistry.cpp
    src/config/ConfigPipeline.cpp
    src/config/StepHandlers.cpp
    src/config/ConfigFileStore.cpp
    src/config/ConfigPathResolver.cpp
    src/config/ConfigJsonParser.cpp
    src/config/ConfigJsonSerializer.cpp
    src/config/ConfigValidator.cpp
    src/config/OutboundBuilderFactory.cpp
    src/config/SingBoxOutboundBuilderFactory.cpp
    src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
    src/config/outbound/SingBoxVMessOutboundBuilder.cpp
    src/config/outbound/SingBoxSSOutboundBuilder.cpp
    src/config/outbound/SingBoxTrojanOutboundBuilder.cpp
    src/config/outbound/SingBoxHysteria2OutboundBuilder.cpp
    src/config/outbound/SingBoxTUICOutboundBuilder.cpp
    src/config/outbound/VLESSOutboundBuilder.cpp
    src/config/outbound/VMessOutboundBuilder.cpp
    src/config/outbound/SSOutboundBuilder.cpp
    src/config/outbound/TrojanOutboundBuilder.cpp
    src/config/outbound/SOCKSOutboundBuilder.cpp
    src/config/outbound/HTTPOutboundBuilder.cpp
    src/config/outbound/Hysteria2OutboundBuilder.cpp
    src/config/outbound/TUICOutboundBuilder.cpp
    src/config/outbound/WireGuardOutboundBuilder.cpp
    src/utils/Base64Utils.cpp
    src/ProfileitemDAO.cpp
    src/ProfileExItemDAO.cpp
    src/config/ProfileConfigRepository.cpp
    src/config/ProfileNormalizer.cpp
    src/config/StreamSettingsBuilder.cpp
    src/config/XrayConfigAssembler.cpp
    src/Utils.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
    src/config/ComponentBundleDAO.cpp
    src/config/ComponentResolver.cpp
    src/config/ConflictDetector.cpp
    src/config/BundleChainResolver.cpp
    src/config/StepToComponentMapper.cpp
    src/config/ConfigGenerator.cpp
    src/config/SchemaValidator.cpp
    src/config/TemplateDecomposer.cpp
    src/config/ComponentRegistry.cpp
)
target_include_directories(test_xray_manager_startup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_xray_manager_startup PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    -lws2_32)
set_target_properties(test_xray_manager_startup PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME XrayManagerStartupTest COMMAND test_xray_manager_startup)

# -- TestResultQueue unit tests (retry logic, concurrent enqueue) --
add_executable(test_result_queue tests/test_result_queue_test.cpp
    src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_result_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_result_queue PRIVATE gtest_main gtest)
set_target_properties(test_result_queue PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ResultQueueTest COMMAND test_result_queue)

add_executable(test_port_manager tests/test_port_manager.cpp
    src/PortManager.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_port_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_port_manager PRIVATE gtest_main gtest -lws2_32)
set_target_properties(test_port_manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME PortManagerTest COMMAND test_port_manager)

install(TARGETS validproxy validproxy-cli DESTINATION bin)

# -- ProxyScorer unit tests (three-factor scoring engine) --------------------
add_executable(test_proxy_scorer tests/test_proxy_scorer.cpp
    src/ProxyScorer.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_proxy_scorer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_proxy_scorer PRIVATE gtest_main gtest)
set_target_properties(test_proxy_scorer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ProxyScorerTest COMMAND test_proxy_scorer)

# -- Runtime History DAO unit tests (SQLite, uses test DB) --------------------
add_executable(test_runtime_history_dao tests/test_runtime_history_dao.cpp
    src/ProxyRuntimeHistory.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_runtime_history_dao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_runtime_history_dao PRIVATE
    gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
)
set_target_properties(test_runtime_history_dao PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME RuntimeHistoryDAOTest COMMAND test_runtime_history_dao)

# -- Standalone adoption port policy unit tests (header-only, no deps) --------
add_executable(test_standalone_adoption_policy
    tests/test_standalone_adoption_policy.cpp)
target_include_directories(test_standalone_adoption_policy
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_standalone_adoption_policy PRIVATE gtest_main gtest)
set_target_properties(test_standalone_adoption_policy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME StandaloneAdoptionPolicyTest COMMAND test_standalone_adoption_policy)

# -- Database switch policy unit tests (header-only, no deps) -----------------
add_executable(test_database_switch_policy
    tests/test_database_switch_policy.cpp)
target_include_directories(test_database_switch_policy
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_database_switch_policy PRIVATE gtest_main gtest)
set_target_properties(test_database_switch_policy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME DatabaseSwitchPolicyTest COMMAND test_database_switch_policy)

# -- ProcessExitListener / ProcessInspector unit tests ------------------------
add_executable(test_process_exit_listener tests/test_process_exit_listener.cpp
    src/ProcessExitListener.cpp src/ProcessInspector.cpp
    src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_process_exit_listener PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_process_exit_listener PRIVATE
    gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    -lpsapi
    -lntdll
)
set_target_properties(test_process_exit_listener PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ProcessExitListenerTest COMMAND test_process_exit_listener)

# Standalone-config command-line extraction (extractConfigFileName /
# extractConfigFullPath) used to resolve ports of externally-launched proxies.
add_executable(test_process_inspector tests/test_process_inspector.cpp
    src/ProcessInspector.cpp)
target_include_directories(test_process_inspector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
   target_link_libraries(test_process_inspector PRIVATE
   gtest_main gtest
   -lpsapi
   -lntdll
   -lws2_32
   )

set_target_properties(test_process_inspector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME ProcessInspectorTest COMMAND test_process_inspector)

# -- RegionBatchResolver unit tests (ip-api.com region resolution) --------------------
add_executable(test_region_batch_resolver tests/test_region_batch_resolver.cpp
    src/RegionBatchResolver.cpp src/ConfigGenerator.cpp src/ProfileitemDAO.cpp
    src/ConfigReader.cpp src/FullConfigTemplateDAO.cpp src/SqlQuery.cpp src/config/ConfigBuilder.cpp
    src/i18n/Language.cpp
    src/config/StepRegistry.cpp src/config/ConfigPipeline.cpp src/config/StepHandlers.cpp
    # ConfigPipeline.cpp 调用的 StepToComponentMapper::convertAll 实现在此文件
    src/config/StepToComponentMapper.cpp
    # PipelineConfigGenerator 定义在 src/config/ConfigGenerator.cpp（非 src/ConfigGenerator.cpp）
    src/config/ConfigGenerator.cpp src/config/BundleChainResolver.cpp
    src/config/ComponentRegistry.cpp src/config/ComponentBundleDAO.cpp
    src/config/ConflictDetector.cpp src/config/ComponentResolver.cpp
    src/config/ConfigFileStore.cpp src/config/ConfigPathResolver.cpp
    src/config/ConfigJsonParser.cpp src/config/ConfigJsonSerializer.cpp src/config/ConfigValidator.cpp
    src/config/ProfileConfigRepository.cpp src/config/ProfileNormalizer.cpp
    src/config/StreamSettingsBuilder.cpp src/config/OutboundBuilderFactory.cpp
    src/config/SingBoxOutboundBuilderFactory.cpp
    src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
    src/config/outbound/SingBoxVMessOutboundBuilder.cpp
    src/config/outbound/SingBoxSSOutboundBuilder.cpp
    src/config/outbound/SingBoxTrojanOutboundBuilder.cpp
    src/config/outbound/SingBoxHysteria2OutboundBuilder.cpp
    src/config/outbound/SingBoxTUICOutboundBuilder.cpp
    src/utils/Base64Utils.cpp
    src/config/XrayConfigAssembler.cpp
    src/config/outbound/VLESSOutboundBuilder.cpp
    src/config/outbound/VMessOutboundBuilder.cpp
    src/config/outbound/SSOutboundBuilder.cpp
    src/config/outbound/TrojanOutboundBuilder.cpp
    src/config/outbound/SOCKSOutboundBuilder.cpp
    src/config/outbound/HTTPOutboundBuilder.cpp
    src/config/outbound/Hysteria2OutboundBuilder.cpp
    src/config/outbound/TUICOutboundBuilder.cpp
    src/config/outbound/WireGuardOutboundBuilder.cpp
    src/ProfileExItemDAO.cpp
    src/utils/DnsCache.cpp src/Logger.cpp src/LoggerInstance.cpp src/Utils.cpp
    # F7 (2026-10-19): RegionBatchResolver::networkDown() lives in the .cpp and
    # calls NetworkMonitor::IsConnected(), so this target needs the monitor linked.
    src/NetworkMonitor.cpp)
target_include_directories(test_region_batch_resolver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_region_batch_resolver PRIVATE
    gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a
    Threads::Threads
    CURL::libcurl
    -lws2_32
)
set_target_properties(test_region_batch_resolver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR}
)
add_test(NAME RegionBatchResolverTest COMMAND test_region_batch_resolver)

# ===== UI Automation Tests (Catch2 3.x + Windows UIA, Spec 2026-08-24) =====
# Independent target: real GUI driven via Windows UI Automation. Does NOT link
# wxWidgets (pure Win32/UIA/COM), so it cannot disturb existing GTest targets.
option(BUILD_UI_TESTS "Build wxWidgets UI automation tests (real GUI)" ON)
if(wxWidgets_FOUND AND BUILD_UI_TESTS)
    # vcpkg Catch2 (static) install dir. Defaults to the GCC14.2/msvcrt tree;
    # override with -DCATCH2_VCPKG_DIR=<dir> to use a same-toolchain Catch2
    # (e.g. the GCC16.2/UCRT rebuild that fixes the undefined
    # basic_streambuf::seekpos(fpos<int>) at UITests link, see
    # docs/bugfix/2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md §8).
    set(CATCH2_VCPKG_DIR "${VCPKG_ROOT}/installed/x64-mingw-static" CACHE PATH "vcpkg Catch2 (static) install dir")
    find_package(Catch2 CONFIG QUIET
        PATHS "${CATCH2_VCPKG_DIR}/share/catch2"
        NO_DEFAULT_PATH)
    if(NOT Catch2_FOUND)
        message(STATUS "Catch2 not installed (run: vcpkg install catch2:x64-mingw-static) - UI tests skipped")
    else()
        set(UI_FRAMEWORK_SOURCES
            tests/ui/framework/Wait.cpp
            tests/ui/framework/UIAutomation.cpp
            tests/ui/framework/UIElement.cpp
            tests/ui/framework/Application.cpp
            tests/ui/framework/Screenshot.cpp
            tests/ui/framework/Diagnostics.cpp
        )
        set(UI_TEST_SOURCES
            tests/ui/TestMain.cpp
            tests/ui/TestFrameworkUnit.cpp
            tests/ui/TestDiscovery.cpp
            tests/ui/TestMainWindow.cpp
            tests/ui/TestSearch.cpp
            tests/ui/TestClear.cpp
            tests/ui/TestStandaloneFloatingWidget.cpp
            tests/ui/TestStandaloneProxyPool.cpp
            tests/ui/TestStandalonePortClose.cpp
            tests/ui/TestExitAssert.cpp
            tests/ui/TestChainPreview.cpp
            tests/ui/TestWindowSensing.cpp
            tests/ui/TestBottomViewSwitch.cpp
            tests/ui/TestLayoutSensing.cpp
            tests/ui/TestMenuAction.cpp
            tests/ui/TestPopupGuard.cpp
            tests/ui/TestTaskLedgerContextMenu.cpp
            tests/ui/TestToolbarRegionButton.cpp
        )
        add_executable(UITests ${UI_FRAMEWORK_SOURCES} ${UI_TEST_SOURCES})
        target_include_directories(UITests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui)
        target_link_libraries(UITests PRIVATE
            Catch2::Catch2
            uiautomationcore oleaut32 uuid
            user32 gdi32 gdiplus shell32 ws2_32 iphlpapi)
        set_target_properties(UITests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${VALIDPROXY_TEST_DIR}
            RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VALIDPROXY_TEST_DIR}
            RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VALIDPROXY_TEST_DIR})

        # CTest entries for the three tagged UI suites. WORKING_DIRECTORY is
        # the repo root: Paths::* resolve absolute paths from the module
        # location, but test-results/ and artifacts land relative to it.
        add_test(NAME UI_MAINWINDOW COMMAND UITests "[mainwindow]")
        add_test(NAME UI_SEARCH     COMMAND UITests "[search]")
        add_test(NAME UI_CLEAR      COMMAND UITests "[clear]")
        add_test(NAME UI_FLOATINGWIDGET COMMAND UITests "[floatingwidget]")
        add_test(NAME UI_POOL COMMAND UITests "[pooldialog]")
        add_test(NAME UI_PORTCLOSE COMMAND UITests "[portclose]")
        add_test(NAME UI_EXITASSERT COMMAND UITests "[exitassert]")
        add_test(NAME UI_CHAINPREVIEW COMMAND UITests "[chainpreview]")
        add_test(NAME UI_WINDOWSENSING COMMAND UITests "[window]")
        add_test(NAME UI_LAYOUTSENSING COMMAND UITests "[layout]")
add_test(NAME UI_BOTTOMVIEW COMMAND UITests "[bottomview]")
        add_test(NAME UI_MENUACTION COMMAND UITests "[menu]")
        add_test(NAME UI_POPUPGUARD COMMAND UITests "[popup]")
        add_test(NAME UI_TASKLEDGER COMMAND UITests "[taskledger]")
        add_test(NAME UI_TOOLBARREGION COMMAND UITests "[toolbarregion]")
        set_tests_properties(UI_MAINWINDOW UI_SEARCH UI_CLEAR UI_CHAINPREVIEW PROPERTIES
            TIMEOUT 120
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        set_tests_properties(UI_FLOATINGWIDGET UI_POOL UI_PORTCLOSE UI_EXITASSERT PROPERTIES
            TIMEOUT 120
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        # The new sensing suites boot the GUI once per TEST_CASE, so they need a
        # longer budget than the single-fixture suites above.
        set_tests_properties(UI_WINDOWSENSING UI_LAYOUTSENSING UI_MENUACTION UI_POPUPGUARD PROPERTIES
            TIMEOUT 240
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        # V8 boots the GUI once and waits for a real DEDUP run + menu loop.
        # The prep fixture makes `ctest -R UI_TASKLEDGER` self-contained: the
        # baseline assertion needs a fresh sandbox DB (no finished rows).
        # ctest quotes every argument, and cmd.exe's quote-stripping rule
        # turns `cmd "/c" "path\x.bat"` into a malformed command line, so the
        # bat is driven through a `cmake -P` wrapper (see
        # scripts/run-ui-sandbox-prep.cmake); the bat stays the single source
        # of truth shared with build-and-test.bat.
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/scripts/prepare-ui-sandbox.bat"
             UI_SANDBOX_PREP_SCRIPT)
        add_test(NAME UI_SANDBOX_PREP
                 COMMAND ${CMAKE_COMMAND}
                         -DSANDBOX_BAT=${UI_SANDBOX_PREP_SCRIPT}
                         -P ${CMAKE_SOURCE_DIR}/scripts/run-ui-sandbox-prep.cmake)
        set_tests_properties(UI_SANDBOX_PREP PROPERTIES
            FIXTURES_SETUP ui_sandbox
            TIMEOUT 60)
        set_tests_properties(UI_TASKLEDGER PROPERTIES
            FIXTURES_REQUIRED ui_sandbox
            TIMEOUT 180
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        # V11 boots the GUI and waits for a real RESOLVE_REGION run
        # (network-bounded); same sandbox baseline as V8.
        set_tests_properties(UI_TOOLBARREGION PROPERTIES
            FIXTURES_REQUIRED ui_sandbox
            TIMEOUT 180
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        # Point the UI tests at the actual GUI exe for THIS build (VALIDPROXY_BIN_DIR).
        # Default (non-isolated) = <repo>/bin/validproxy.exe, same as the built-in
        # fallback; isolated = <build>/out/bin/validproxy.exe.
        file(TO_NATIVE_PATH "${VALIDPROXY_BIN_DIR}/validproxy.exe" _UI_APP_EXE)
        # VALIDPROXY_APP_ROOT re-anchors the UI framework's repo-root inference.
        # UITests.exe lives in VALIDPROXY_TEST_DIR (out/tests when isolated, the
        # in-tree tests/ otherwise); only the in-tree tests/ case lets the exe-dir
        # inference resolve the repo correctly, so always inject the real root.
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}" _UI_APP_ROOT)
        set_tests_properties(UI_MAINWINDOW UI_SEARCH UI_CLEAR UI_FLOATINGWIDGET
            UI_POOL UI_PORTCLOSE UI_EXITASSERT UI_CHAINPREVIEW UI_WINDOWSENSING
            UI_LAYOUTSENSING UI_BOTTOMVIEW UI_MENUACTION UI_POPUPGUARD
            UI_TASKLEDGER UI_TOOLBARREGION UI_SANDBOX_PREP
            PROPERTIES ENVIRONMENT
                "VALIDPROXY_APP_EXE=${_UI_APP_EXE};VALIDPROXY_APP_ROOT=${_UI_APP_ROOT}")
    endif()
endif()

# -- i18n 字符串表兜底链接 -------------------------------------------------
# 业务层（config / DAO / net / task）大量使用 i18n::u8 / i18n::w，而部分单元
# 测试只编译被测源文件、不走 CORE_SOURCES。这里为所有测试可执行文件统一补
# 一个静态库；已经通过 CORE_SOURCES 自行定义这些符号的目标不会再从库里
# 拉取同名成员（静态库按需提取），因此不会产生重复定义。
if(NOT TARGET i18n_core)
    add_library(i18n_core STATIC
        src/i18n/Language.cpp
        src/i18n/I18n.cpp
        src/i18n/StringTable.cpp)
    target_include_directories(i18n_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

get_property(_vp_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_vp_t ${_vp_targets})
    if(_vp_t MATCHES "^test_" OR _vp_t STREQUAL "UITests")
        get_target_property(_vp_type ${_vp_t} TYPE)
        if(_vp_type STREQUAL "EXECUTABLE")
            target_link_libraries(${_vp_t} PRIVATE i18n_core)
        endif()
    endif()
endforeach()
