cmake_minimum_required(VERSION 3.20)
project(multiple_thread_validproxy VERSION 1.0.3 LANGUAGES CXX)

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
add_custom_target(update_version_h ALL
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_version_h.py
        ${VERSION_H_IN}
        ${VERSION_H}
        ${CMAKE_BUILD_TYPE}
        ${VERSION_MAJOR}
        ${VERSION_MINOR}
        ${VERSION_PATCH}
        ${VERSION_TAG}
        ${VERSION_COMMIT}
        ${VERSION_DIRTY}
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
set(BOOST_ROOT "${BOOST_ROOT}" CACHE PATH "Boost installation root")
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
if(NOT BOOST_ROOT)
    set(BOOST_ROOT "D:/boost_1_88_0")
endif()
if(NOT VCPKG_ROOT)
    set(VCPKG_ROOT "E:/vcpkg")
endif()
if(NOT WX64DEVKIT_ROOT)
    set(WX64DEVKIT_ROOT "E:/w64devkit")
endif()
if(NOT GTEST_ROOT)
    set(GTEST_ROOT "E:/eclipse_workspace/googletest")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)
include_directories(${BOOST_ROOT}/include)
include_directories(${VCPKG_ROOT}/installed/x64-mingw-static/include)
link_directories(${BOOST_ROOT}/lib)
link_directories(${VCPKG_ROOT}/installed/x64-mingw-static/lib)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

# Shared core sources (compiled into both CLI and GUI targets)
set(CORE_SOURCES
    src/CrashHandler.cpp
    src/ConfigGenerator.cpp
    src/ConfigReader.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
    src/LogStatistics.cpp
    src/PortManager.cpp
    src/ProfileitemDAO.cpp
    src/SubitemDAO.cpp
    src/ProfileExItemDAO.cpp
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
    src/test/ProxyBatchQuery.cpp
    src/test/XrayWorkerPool.cpp
    src/test/ProxyTestCounters.cpp
    src/test/ProxyTestResultSink.cpp
    src/service/DatabaseConnectionService.cpp
    src/service/ConfigService.cpp
    src/service/SubscriptionService.cpp
    src/service/ProxyListService.cpp
    src/service/ProxyTestService.cpp
    src/service/ShareLinkExportService.cpp
    src/service/DatabaseMaintenanceService.cpp
    src/service/AutoTaskService.cpp
    src/ui/UiOperationRunner.cpp
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
    ${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a
    -ldbghelp
    -lws2_32
)
add_dependencies(validproxy-cli update_version_h)

# Force CLI output to bin (override global setting for tests)
set_target_properties(validproxy-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
)

if(USE_GRPC_API)
    target_compile_definitions(validproxy-cli PRIVATE USE_GRPC_API=1)
endif()

# -- wxWidgets (GUI mode) --
# Find wxWidgets as a CMake CONFIG package from vcpkg (dynamic variant, which has proper cmake config)
# Pre-find NanoSVG dependency so wxWidgets config can find it
find_package(NanoSVG CONFIG QUIET
    PATHS "${VCPKG_ROOT}/installed/x64-mingw-dynamic/share/nanosvg"
    NO_DEFAULT_PATH
)
if(NOT NanoSVG_FOUND)
    message(STATUS "NanoSVG not found - wxWidgets will not be available")
endif()

find_package(wxWidgets CONFIG QUIET
    PATHS "${VCPKG_ROOT}/installed/x64-mingw-dynamic/share/wxwidgets"
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
        src/ui/ConfigDialog.h
        src/ui/ConfigDialog.cpp
        src/ui/TrayIcon.h
        src/ui/TrayIcon.cpp
        src/ui/UIApp.h
        src/ui/UIApp.cpp
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
        ${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a
        -ldbghelp
        -lws2_32
    )
add_dependencies(validproxy update_version_h)

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
    target_include_directories(validproxy PRIVATE "${VCPKG_ROOT}/installed/x64-mingw-dynamic/include")

    # Add include directory for UI source headers
    target_include_directories(validproxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)

    # Copy wx DLLs to output directory
    function(copy_wx_dlls TARGET_NAME)
        get_target_property(_bin_dir ${TARGET_NAME} RUNTIME_OUTPUT_DIRECTORY)
        if(NOT _bin_dir)
            set(_bin_dir "${CMAKE_SOURCE_DIR}/bin")
        endif()

        # Determine if we're building Debug or Release to choose correct DLL directory
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(wx_bin_dir "${VCPKG_ROOT}/installed/x64-mingw-dynamic/debug/bin")
            # Debug DLLs have 'd' suffix
            set(WX_DLLS
                wxbase32ud_gcc_custom.dll
                wxbase32ud_net_gcc_custom.dll
                wxmsw32ud_adv_gcc_custom.dll
                wxmsw32ud_aui_gcc_custom.dll
                wxmsw32ud_core_gcc_custom.dll
                wxmsw32ud_html_gcc_custom.dll
                wxmsw32ud_propgrid_gcc_custom.dll
                libpcre2-16d.dll
                libzlibd1.dll
                libjpeg-62.dll
                libpng16d.dll
                libtiffd.dll
                liblzma.dll
            )
        else()
            set(wx_bin_dir "${VCPKG_ROOT}/installed/x64-mingw-dynamic/bin")
            # Release DLLs
            set(WX_DLLS
                wxbase32u_gcc_custom.dll
                wxbase32u_net_gcc_custom.dll
                wxmsw32u_adv_gcc_custom.dll
                wxmsw32u_aui_gcc_custom.dll
                wxmsw32u_core_gcc_custom.dll
                wxmsw32u_html_gcc_custom.dll
                wxmsw32u_propgrid_gcc_custom.dll
                libpcre2-16.dll
                libzlib1.dll
                libjpeg-62.dll
                libpng16.dll
                libtiff.dll
                liblzma.dll
            )
        endif()

        foreach(dll ${WX_DLLS})
            set(src_dll "${wx_bin_dir}/${dll}")
            if(EXISTS "${src_dll}")
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${src_dll}"
                        "${_bin_dir}/${dll}"
                    COMMENT "Copying ${dll} to ${_bin_dir}"
                )
            endif()
        endforeach()
    endfunction()

    copy_wx_dlls(validproxy)

    # WIN32_EXECUTABLE creates GUI subsystem executable (no console window)
    set_target_properties(validproxy PROPERTIES
        WIN32_EXECUTABLE TRUE
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
    )
    target_compile_definitions(validproxy PRIVATE HAS_WXWIDGETS=1)
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
target_include_directories(test_curl_easy_handle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_curl_easy_handle PRIVATE CURL::libcurl)
set_target_properties(test_curl_easy_handle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME CurlEasyHandleTest COMMAND test_curl_easy_handle)

add_executable(test_dedup tests/test_dedup.cpp)
target_include_directories(test_dedup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_dedup PRIVATE
    gtest_main
    gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
)
set_target_properties(test_dedup PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME DedupTest COMMAND test_dedup)

add_executable(test_profile_ex_item_dao tests/test_profile_ex_item_dao.cpp src/ProfileExItemDAO.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_profile_ex_item_dao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_profile_ex_item_dao PRIVATE
    gtest_main
    gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
)
set_target_properties(test_profile_ex_item_dao PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ProfileExItemDAOTest COMMAND test_profile_ex_item_dao)

# -- Profileitem model tests - DISABLED until test_model.cpp is created
# add_executable(test_model tests/test_model.cpp)
# target_include_directories(test_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# target_link_libraries(test_model PRIVATE
#     gtest_main
#     gtest
#     E:/vcpkg/installed/x64-mingw-static/lib/libspdlog.a
#     E:/vcpkg/installed/x64-mingw-static/lib/libfmt.a
# )
# add_test(NAME ProfileitemTest COMMAND test_model)

# -- Logger unit tests (zero external deps beyond gtest) --
add_executable(test_logger tests/test_logger.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_logger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_logger PRIVATE gtest_main gtest)
set_target_properties(test_logger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
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
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ShareLinkTest COMMAND test_sharelink)

# -- UriCodec unit tests (encoding utilities extracted from ShareLink) --
add_executable(test_uri_codec tests/test_uri_codec.cpp src/share/UriCodec.cpp)
target_include_directories(test_uri_codec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_uri_codec PRIVATE gtest_main gtest)
set_target_properties(test_uri_codec PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME UriCodecTest COMMAND test_uri_codec)

# -- AutoTaskManager unit tests --
add_executable(test_autotask tests/test_autotask.cpp src/AutoTaskManager.cpp ${CORE_SOURCES})
target_include_directories(test_autotask PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_autotask PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a
    Threads::Threads
    CURL::libcurl
    -ldbghelp
    -lws2_32
)
set_target_properties(test_autotask PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME AutoTaskTest COMMAND test_autotask)

# -- Utils unit tests (pure functions, no external deps beyond gtest) --
add_executable(test_utils tests/test_utils.cpp src/Utils.cpp)
target_include_directories(test_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_utils PRIVATE gtest_main gtest -lws2_32)
set_target_properties(test_utils PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME UtilsTest COMMAND test_utils)

# -- ConfigReader unit tests (includes load() tests with error reporter override) --
add_executable(test_config_reader tests/test_config_reader.cpp tests/test_config_reader_load.cpp
    src/ConfigReader.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp
    src/config/ConfigFileStore.cpp src/config/ConfigPathResolver.cpp
    src/config/ConfigJsonParser.cpp src/config/ConfigJsonSerializer.cpp
    src/config/ConfigValidator.cpp)
target_include_directories(test_config_reader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_config_reader PRIVATE gtest_main gtest ${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a -lws2_32)
set_target_properties(test_config_reader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ConfigReaderTest COMMAND test_config_reader)

# -- ConfigGenerator unit tests (SQLite + boost_json, now delegates to extracted components) --
add_executable(test_config_generator
    tests/test_config_generator.cpp
    src/ConfigGenerator.cpp
    src/ProfileitemDAO.cpp

    src/utils/DnsCache.cpp
    src/ProfileExItemDAO.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
    src/Utils.cpp
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
)
target_include_directories(test_config_generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_config_generator PRIVATE gtest_main gtest
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    ${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a
    -lws2_32
)
set_target_properties(test_config_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ConfigGeneratorTest COMMAND test_config_generator)

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
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME DeleteSubscriptionTest COMMAND test_delete_subscription)

# -- SubscriptionParser unit tests --
add_executable(test_subscription_parser tests/test_subscription_parser.cpp src/update/SubscriptionParser.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_subscription_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_subscription_parser PRIVATE gtest_main gtest ${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a -lws2_32)
set_target_properties(test_subscription_parser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME SubscriptionParserTest COMMAND test_subscription_parser)

# -- ProxyBatchTester components tests (pure logic, no external deps) --
add_executable(test_proxy_batch_components tests/test_proxy_batch_components.cpp)
target_include_directories(test_proxy_batch_components PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_proxy_batch_components PRIVATE gtest_main gtest)
set_target_properties(test_proxy_batch_components PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ProxyBatchComponentsTest COMMAND test_proxy_batch_components)

# -- ProxyBatchQuery integration tests (uses test DB) --
add_executable(test_proxy_batch_query tests/test_proxy_batch_query.cpp src/test/ProxyBatchQuery.cpp
    src/ConfigGenerator.cpp src/ProfileitemDAO.cpp src/utils/DnsCache.cpp src/ProfileExItemDAO.cpp
    src/Logger.cpp src/LoggerInstance.cpp src/Utils.cpp
    src/config/ProfileConfigRepository.cpp src/config/ProfileNormalizer.cpp
    src/config/StreamSettingsBuilder.cpp src/config/OutboundBuilderFactory.cpp
    src/config/XrayConfigAssembler.cpp
    src/config/outbound/VLESSOutboundBuilder.cpp
    src/config/outbound/VMessOutboundBuilder.cpp
    src/config/outbound/SSOutboundBuilder.cpp
    src/config/outbound/TrojanOutboundBuilder.cpp
    src/config/outbound/SOCKSOutboundBuilder.cpp
    src/config/outbound/HTTPOutboundBuilder.cpp
    src/config/outbound/Hysteria2OutboundBuilder.cpp
    src/config/outbound/TUICOutboundBuilder.cpp
    src/config/outbound/WireGuardOutboundBuilder.cpp)
target_include_directories(test_proxy_batch_query PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_proxy_batch_query PRIVATE gtest_main gtest
    ${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a
    ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libsqlite3.a
    -lws2_32
)
set_target_properties(test_proxy_batch_query PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ProxyBatchQueryTest COMMAND test_proxy_batch_query)

# -- NetworkMonitor unit tests --
add_executable(test_network_monitor tests/test_network_monitor.cpp src/NetworkMonitor.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_network_monitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_network_monitor PRIVATE gtest_main gtest Threads::Threads CURL::libcurl)
set_target_properties(test_network_monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME NetworkMonitorTest COMMAND test_network_monitor)

# -- Ipv6Formatter unit tests (pure functions, no external deps beyond gtest) --
add_executable(test_ipv6_formatter tests/test_ipv6_formatter.cpp src/share/Ipv6Formatter.cpp)
target_include_directories(test_ipv6_formatter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_ipv6_formatter PRIVATE gtest_main gtest)
set_target_properties(test_ipv6_formatter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME Ipv6FormatterTest COMMAND test_ipv6_formatter)

# -- LogStatistics unit tests (pure file parser, no external deps beyond gtest) --
add_executable(test_log_statistics tests/test_log_statistics.cpp src/LogStatistics.cpp)
target_include_directories(test_log_statistics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_log_statistics PRIVATE gtest_main gtest)
set_target_properties(test_log_statistics PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
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
    )
    target_compile_definitions(test_log_statistics_event PRIVATE HAS_WXWIDGETS=1)
    set_target_properties(test_log_statistics_event PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
    )
    add_test(NAME LogStatisticsEventTest COMMAND test_log_statistics_event)
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
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
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
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
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
    ${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a
)
set_target_properties(test_singbox_vless_builder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
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
        ${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a
        -lws2_32
    )
    set_target_properties(test_xray_api_direct PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
    )
    add_test(NAME XrayApiDirectTest COMMAND test_xray_api_direct)
endif()

# -- XrayInstance stderr/exit-code observability tests (compiled fake xray) --
add_executable(test_xray_instance_stderr
    tests/test_xray_instance_stderr.cpp
    src/XrayInstance.cpp
    src/Logger.cpp
    src/LoggerInstance.cpp
)
target_include_directories(test_xray_instance_stderr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_xray_instance_stderr PRIVATE gtest_main gtest)
set_target_properties(test_xray_instance_stderr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME XrayInstanceStderrTest COMMAND test_xray_instance_stderr)

# Helper executable standing in for xray.exe during XrayInstance tests.
# Not a gtest binary: it only writes PANIC to stderr and exits with a
# deterministic code (7 for "startup" mode, 42 after ~8s for runtime mode).
# The test copies it into a temp dir under the mode-selecting name, so it
# must land in the same output directory as test_xray_instance_stderr.
add_executable(fake_xray_helper tests/fake_xray_helper.cpp)
set_target_properties(fake_xray_helper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)

# -- TestResultQueue unit tests (retry logic, concurrent enqueue) --
add_executable(test_result_queue tests/test_result_queue_test.cpp
    src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_result_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_result_queue PRIVATE gtest_main gtest)
set_target_properties(test_result_queue PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ResultQueueTest COMMAND test_result_queue)

add_executable(test_port_manager tests/test_port_manager.cpp
    src/PortManager.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp)
target_include_directories(test_port_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_port_manager PRIVATE gtest_main gtest -lws2_32)
set_target_properties(test_port_manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME PortManagerTest COMMAND test_port_manager)

install(TARGETS validproxy validproxy-cli DESTINATION bin)