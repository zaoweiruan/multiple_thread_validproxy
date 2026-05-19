---
title: "feat(Architecture): C++ proxy validation tool implementation plan"
type: plan
status: completed
date: 2026-05-14
origin: "gentle-panda.md (original .kilo plan)"
---

# Plan for C++ Proxy Validation Tool Implementation

## Phase 1: Requirements Analysis & Architecture Design

### Core Requirements from AGENTS.md:
1. **XrayManager** - Singleton manager for xray instances
   - `getInstance()` - get singleton instance
   - `start()` - start specified number of xray instances
   - `stop()` - stop all instances
   - `release()` - release singleton

2. **ProxyFinder** - Find available proxies
   - `findFirstWorkingProxy()` - find first working proxy (-F flag)
   - `findWorkingProxy()` - test all, return lowest latency (-FMIN flag)

3. **ConfigGenerator** - Generate JSON configs based on configType
   - Types: 3=SS, 1=VMess, 5=VLESS, 6=Trojan

4. **ConfigReader** - Read configuration files

5. **SubitemUpdater** - Update subscriptions

6. **ProxyBatchTester** - Multi-threaded concurrent proxy testing

### Key Design Decisions:
- C++17/20 with std::optional
- SQLite for data storage
- Multi-threaded proxy testing with thread pool
- Singleton pattern for XrayManager
- JSON configuration generation based on configType

## Phase 2: File Structure

```
multiple_thread_validproxy/
├── include/
│   ├── db/
│   │   ├── DatabaseManager.h
│   │   ├── models/
│   │   │   ├── Profileitem.h
│   │   │   ├── Subscription.h
│   │   │   └── ProxyItem.h
│   │   └── DAO/
│   │       ├── ProfileitemDAO.h
│   │       ├── SubscriptionDAO.h
│   │       └── ProxyItemDAO.h
│   ├── core/
│   │   ├── XrayManager.h
│   │   ├── ProxyFinder.h
│   │   ├── ConfigGenerator.h
│   │   ├── ConfigReader.h
│   │   ├── SubitemUpdater.h
│   │   └── ProxyBatchTester.h
│   └── utils/
│       ├── Logger.h
│       └── JsonHelper.h
├── src/
│   ├── core/
│   │   ├── XrayManager.cpp
│   │   ├── ProxyFinder.cpp
│   │   ├── ConfigGenerator.cpp
│   │   ├── ConfigReader.cpp
│   │   ├── SubitemUpdater.cpp
│   │   └── ProxyBatchTester.cpp
│   └── main.cpp
├── tests/
│   ├── test_XrayManager.cpp
│   ├── test_ProxyFinder.cpp
│   ├── test_ConfigGenerator.cpp
│   ├── test_ProxyBatchTester.cpp
│   └── test_main.cpp
├── CMakeLists.txt
└── AGENTS.md (existing)
```

## Phase 3: Implementation Order

### Step 1: Core Infrastructure (Week 1)
1. **Database Layer** (Highest priority - foundation)
   - DatabaseManager.h/cpp
   - SQLite connection management
   - Connection pooling
   
2. **Model Classes**
   - Profileitem.h/cpp - Main data structure
   - ProxyItem.h/cpp - Proxy configuration data
   - Subscription.h/cpp - Subscription management

3. **DAO Classes**
   - ProfileitemDAO.h/cpp
   - ProxyItemDAO.h/cpp
   - SubscriptionDAO.h/cpp

### Step 2: Core Modules (Week 2-3)
4. **XrayManager** (Singleton pattern)
   - Thread-safe singleton implementation
   - Instance lifecycle management
   - xray process management
   
5. **ConfigGenerator**
   - JSON generation for different config types
   - Template-based config generation
   - Validation logic

6. **ConfigReader**
   - JSON configuration parsing
   - Validation and error handling
   - Config type detection

### Step 3: Advanced Features (Week 4-5)
7. **ProxyFinder**
   - First working proxy detection
   - Full proxy testing with latency measurement
   - Integration with XrayManager

8. **ProxyBatchTester**
   - Thread pool implementation
   - Concurrent proxy testing
   - Result aggregation and sorting
   
9. **SubitemUpdater**
   - Subscription update logic
   - Error handling and retry
   - Integration with database

### Step 4: Testing & Main (Week 6)
10. **Test Suite**
    - Unit tests for all classes
    - Integration tests
    - Performance tests
    
11. **Main Application**
    - CLI argument parsing
    - Command routing
    - Integration with all modules

## Phase 4: Build System

### CMakeLists.txt Structure:
```cmake
cmake_minimum_required(VERSION 3.16)
project(multiple_thread_validproxy VERSION 1.0.3 LANGUAGES CXX)

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

install(TARGETS validproxy DESTINATION bin)
```

## Phase 5: Testing Strategy

### Unit Tests:
- XrayManager: Singleton behavior, lifecycle management
- ProxyFinder: First proxy finding, latency measurement
- ConfigGenerator: All config types (SS, VMess, VLESS, Trojan)
- ProxyBatchTester: Thread safety, result sorting
- Database: CRUD operations

### Integration Tests:
- End-to-end proxy validation flow
- Database integration
- Xray instance management
- CLI command integration

### Performance Tests:
- Concurrent proxy testing with large batches
- Memory usage under load
- Thread pool efficiency

## Phase 6: Development Tools & Practices

### Required Tools:
- CMake (build system)
- Ninja (build backend)
- GDB (debugging)
- Valgrind (memory checking)
- AddressSanitizer (memory safety)
- Google Test (unit testing)

### Code Standards:
- PascalCase for classes/structs
- camelCase for functions/methods
- snake_case for member variables
- UPPER_SNAKE_CASE for constants
- Include guards in all headers
- Proper namespace organization (db, core, utils)

## Phase 7: Error Handling Strategy

### Error Types:
1. Configuration errors (invalid JSON, missing fields)
2. Database errors (connection failures, query errors)
3. Network errors (proxy connection failures)
4. xray errors (process management failures)

### Error Handling Patterns:
- std::optional for optional values
- Exception handling for critical errors
- Error codes for non-critical failures
- Logging for debugging

## Phase 8: CLI Integration

### Command Structure:
```bash
# Main commands
./validproxy -c <config> -show-sub
./validproxy -G <id>           # Generate config
./validproxy -F                # Find first working proxy
./validproxy -FMIN             # Find min latency proxy
./validproxy -U <id>           # Update subscription
./validproxy -UA               # Update all subscriptions
./validproxy -T <id>           # Test subscription
./validproxy -h                 # Help
```

## Next Steps:
1. Start with Phase 1: Create database layer and models
2. Set up CMake build system
3. Implement basic unit tests
4. Progress through implementation phases incrementally
