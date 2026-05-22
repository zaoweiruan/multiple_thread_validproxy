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

add_executable(validproxy
    src/main.cpp
    src/ConfigGenerator.cpp
    src/ConfigReader.cpp
    src/XrayApi.cpp
    src/SubitemUpdaterV2.cpp
    src/PortManager.cpp
    src/Logger.cpp
    src/XrayInstance.cpp
    src/XrayManager.cpp
    src/ProxyTester.cpp
    src/UrlFetcher.cpp
    src/ProxyBatchTester.cpp
    src/ProxyFinder.cpp
    src/Utils.cpp
    src/ShareLink.cpp
)

target_link_libraries(validproxy PRIVATE
    Threads::Threads
    CURL::libcurl
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
    D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a
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
        src/ui/SubscriptionPanel.h
        src/ui/SubscriptionPanel.cpp
        src/ui/ProxyListPanel.h
        src/ui/ProxyListPanel.cpp
        src/ui/ProxyDetailPanel.h
        src/ui/ProxyDetailPanel.cpp
        src/ui/TestPanel.h
        src/ui/TestPanel.cpp
        src/ui/LogPanel.h
        src/ui/LogPanel.cpp
        src/ui/ConfigDialog.h
        src/ui/ConfigDialog.cpp
        src/ui/TrayIcon.h
        src/ui/TrayIcon.cpp
        src/ui/UIApp.h
src/ui/UIApp.cpp
        src/ui/TestLogMediator.cpp
        )
    
    target_sources(validproxy PRIVATE ${UI_SOURCES})
    
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
    
    # Add include directory for UI source headers (so main.cpp can #include "UIApp.h" etc.)
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
             # Debug DLLs have 'd' suffix: wxbase32ud_gcc_custom.dll, libpcre2-16d.dll, libzlibd1.dll
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
    
    # Copy ICO/PNG icons to bin/docs/design/ui/
    add_custom_command(TARGET validproxy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/bin/docs/design/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/docs/design/ui/icon.ico"
            "${CMAKE_SOURCE_DIR}/bin/docs/design/ui/icon.ico"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/docs/design/ui/clear.ico"
            "${CMAKE_SOURCE_DIR}/bin/docs/design/ui/clear.ico"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/docs/design/ui/clear.png"
            "${CMAKE_SOURCE_DIR}/bin/docs/design/ui/clear.png"
        COMMENT "Copying icons to bin/docs/design/ui/"
    )

    # Compile definition for GUI mode
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
add_test(NAME CurlEasyHandleTest COMMAND test_curl_easy_handle)

add_executable(test_dedup tests/test_dedup.cpp)
target_include_directories(test_dedup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_dedup PRIVATE
    gtest_main
    gtest
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
)
add_test(NAME DedupTest COMMAND test_dedup)

add_executable(test_model tests/test_model.cpp)
target_include_directories(test_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_model PRIVATE
    gtest_main
    gtest
    E:/vcpkg/installed/x64-mingw-static/lib/libspdlog.a
    E:/vcpkg/installed/x64-mingw-static/lib/libfmt.a
)
add_test(NAME ProfileitemTest COMMAND test_model)

install(TARGETS validproxy DESTINATION bin)
