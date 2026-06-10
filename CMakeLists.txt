cmake_minimum_required(VERSION 3.20)
project(multiple_thread_validproxy VERSION 1.0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
    add_link_options(/UTF-8)
else()
    add_compile_options(-Wall -Wextra -pedantic -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(D:/boost_1_88_0/include)
include_directories(E:/vcpkg/installed/x64-mingw-static/include)
link_directories(D:/boost_1_88_0/lib)
link_directories(E:/vcpkg/installed/x64-mingw-static/lib)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

# Shared core sources (compiled into both CLI and GUI targets)
set(CORE_SOURCES
    src/ConfigGenerator.cpp
    src/ConfigReader.cpp
    src/Logger.cpp
    src/PortManager.cpp
    src/ProxyBatchTester.cpp
    src/ProxyFinder.cpp
    src/ProxyTester.cpp
    src/ShareLink.cpp
    src/SubitemUpdaterV2.cpp
    src/UrlFetcher.cpp
    src/Utils.cpp
    src/XrayApi.cpp
    src/XrayInstance.cpp
    src/XrayManager.cpp
)

# ===== CLI target (CONSOLE subsystem) =====
add_executable(validproxy-cli
    src/main_cli.cpp
    ${CORE_SOURCES}
)

target_link_libraries(validproxy-cli PRIVATE
    Threads::Threads
    CURL::libcurl
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
    D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a
)

# Force CLI output to bin (override global setting for tests)
set_target_properties(validproxy-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
)

# -- wxWidgets (GUI mode) --
# Find wxWidgets as a CMake CONFIG package from vcpkg (dynamic variant, which has proper cmake config)
# Pre-find NanoSVG dependency so wxWidgets config can find it
find_package(NanoSVG CONFIG QUIET
    PATHS "E:/vcpkg/installed/x64-mingw-dynamic/share/nanosvg"
    NO_DEFAULT_PATH
)
if(NOT NanoSVG_FOUND)
    message(STATUS "NanoSVG not found - wxWidgets will not be available")
endif()

find_package(wxWidgets CONFIG QUIET
    PATHS "E:/vcpkg/installed/x64-mingw-dynamic/share/wxwidgets"
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

    # RC flags: let windres find data files relative to CMAKE_SOURCE_DIR
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_SOURCE_DIR}")

    # ===== GUI target (WIN32 subsystem) =====
    add_executable(validproxy
        src/main_gui.cpp
        ${CORE_SOURCES}
        ${UI_SOURCES}
    )

    target_link_libraries(validproxy PRIVATE
        Threads::Threads
        CURL::libcurl
        E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
        D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a
    )

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
    target_include_directories(validproxy PRIVATE "E:/vcpkg/installed/x64-mingw-dynamic/include")

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
            set(wx_bin_dir "E:/vcpkg/installed/x64-mingw-dynamic/debug/bin")
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
            set(wx_bin_dir "E:/vcpkg/installed/x64-mingw-dynamic/bin")
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

enable_testing()

# -- GTest (local source) --
add_subdirectory(E:/eclipse_workspace/googletest ${CMAKE_BINARY_DIR}/googletest)

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
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
)
set_target_properties(test_dedup PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME DedupTest COMMAND test_dedup)

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
add_executable(test_logger tests/test_logger.cpp src/Logger.cpp)
target_include_directories(test_logger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_logger PRIVATE gtest_main gtest)
set_target_properties(test_logger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME LoggerTest COMMAND test_logger)

# -- ShareLink unit tests (pure functions, no external deps beyond gtest) --
add_executable(test_sharelink tests/test_sharelink.cpp src/ShareLink.cpp)
target_include_directories(test_sharelink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_sharelink PRIVATE gtest_main gtest)
set_target_properties(test_sharelink PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ShareLinkTest COMMAND test_sharelink)

# -- Utils unit tests (pure functions, no external deps beyond gtest) --
add_executable(test_utils tests/test_utils.cpp src/Utils.cpp)
target_include_directories(test_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_utils PRIVATE gtest_main gtest)
set_target_properties(test_utils PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME UtilsTest COMMAND test_utils)

# -- ConfigReader unit tests (includes load() tests with error reporter override) --
add_executable(test_config_reader tests/test_config_reader.cpp tests/test_config_reader_load.cpp src/ConfigReader.cpp src/Utils.cpp src/Logger.cpp)
target_include_directories(test_config_reader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_config_reader PRIVATE gtest_main gtest D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a)
set_target_properties(test_config_reader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ConfigReaderTest COMMAND test_config_reader)

# -- ConfigGenerator unit tests (SQLite + boost_json) --
add_executable(test_config_generator tests/test_config_generator.cpp src/ConfigGenerator.cpp src/Logger.cpp)
target_include_directories(test_config_generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_config_generator PRIVATE gtest_main gtest
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
    D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a
)
set_target_properties(test_config_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME ConfigGeneratorTest COMMAND test_config_generator)

# -- Delete Subscription tests --
add_executable(test_delete_subscription tests/test_delete_subscription.cpp src/Logger.cpp)
target_include_directories(test_delete_subscription PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_delete_subscription PRIVATE gtest_main gtest
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
)
set_target_properties(test_delete_subscription PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME DeleteSubscriptionTest COMMAND test_delete_subscription)

install(TARGETS validproxy validproxy-cli DESTINATION bin)
