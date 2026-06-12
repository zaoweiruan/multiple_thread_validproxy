---
title: "report: C++ debugging tools assessment"
type: report
status: completed
date: 2026-06-11
---

# C++ Debugging Tools Assessment Report

## Executive Summary

Project `validproxy` (v1.0.3) is a C++17/20 proxy validation tool targeting Windows/MinGW/GCC with wxWidgets GUI. Evaluation reveals **basic debugging infrastructure** in place but **significant gaps** in memory safety, crash diagnostics, and static analysis capabilities.

## Current State Analysis

### ✅ Available Debugging Capabilities

| Capability | Implementation | Files |
|------------|---------------|-------|
| **Logging System** | 6-level hierarchical logging | `include/Logger.h`, `src/Logger.cpp` |
| **Unit Testing** | Google Test framework integration | `tests/*.cpp`, `CMakeLists.txt:226-335` |
| **Debug Build Mode** | CMake Debug configuration | `CMakeLists.txt` |

### Log Levels (LogLevel enum)

```cpp
enum class LogLevel {
    TRACE = 0,   // Detailed debugging
    DEBUG = 1,   // Debug information  
    INFO = 2,    // Normal flow
    REPORT = 3,  // Statistics/reports
    WARN = 4,    // Warning conditions
    ERR = 5      // Errors
};
```

### Test Framework Setup

- **Framework**: Google Test (gtest_main, gtest)
- **Test Count**: 8 test executables configured
- **Tests**: `test_logger`, `test_dedup`, `test_sharelink`, `test_utils`, `test_config_reader`, `test_config_generator`, `test_delete_subscription`

## Missing Debugging Capabilities

### Critical Gaps

| Capability | Priority | Risk | Recommendation |
|------------|----------|------|----------------|
| **AddressSanitizer** | HIGH | Memory leaks, buffer overflows | Add `-fsanitize=address` to Debug builds |
| **UndefinedBehaviorSanitizer** | HIGH | UB detection | Add `-fsanitize=undefined` |
| **Stack Unwinding** | HIGH | Crash diagnosis | Implement SEH/minidump capture |
| **Static Analysis** | MEDIUM | Code quality | Integrate clang-tidy or cppcheck |
| **Coverage Analysis** | MEDIUM | Test gaps | Add gcov/lcov instrumentation |

## Proposed CMake Enhancements

```cmake
# In CMakeLists.txt after line 14

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
if(ENABLE_SANITIZERS AND NOT MSVC)
    message(STATUS "Sanitizers enabled: AddressSanitizer, UndefinedBehaviorSanitizer")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Debug-specific settings
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0")
```

## Risk Assessment

| Risk Area | Current Mitigation | Exposure |
|-----------|-------------------|----------|
| Memory leaks | None | HIGH |
| Buffer overflows | None | HIGH |
| Race conditions | Basic atomic flags | MEDIUM |
| Logic errors | Logger + unit tests | LOW |

## Recommendations Summary

1. **Immediate**: Enable AddressSanitizer in Debug builds
2. **Short-term**: Add crash dump generation via Windows MiniDumpWriteDump
3. **Long-term**: Integrate clang-tidy CI pipeline, coverage tracking

## Files Analyzed

- `CMakeLists.txt` - Build configuration
- `include/Logger.h` - Log level definitions
- `src/Logger.cpp` - Logging implementation
- `include/ProxyFinder.h` - Test subject with known bug
- `src/ProxyFinder.cpp` - Bug: findWorkingProxy doesn't re-inject winning proxy