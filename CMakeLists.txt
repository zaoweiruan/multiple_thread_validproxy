cmake_minimum_required(VERSION 3.20)
project(multiple_thread_validproxy VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
)

target_link_libraries(validproxy PRIVATE
    Threads::Threads
    CURL::libcurl
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
    D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a
)

install(TARGETS validproxy DESTINATION bin)
