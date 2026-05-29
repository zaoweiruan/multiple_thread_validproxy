---
title: "refactor: Introduce CurlEasyHandle RAII wrapper for curl_easy_* API"
type: refactor
status: completed
date: 2026-05-07
origin: docs/superpowers/brainstorm/2026-05-07-curl-raii-wrapper-requirements.md
---

# refactor: Introduce CurlEasyHandle RAII wrapper for curl_easy_* API

## Summary

Replace raw `curl_easy_init()`/`curl_easy_cleanup()` usage at 6 sites across the codebase with a move-only `CurlEasyHandle` RAII wrapper class. The wrapper provides fluent API for setting CURL options, throws `std::runtime_error` on failures, and follows existing project RAII patterns (see `DatabaseHelper.h`, `XrayInstance.h`). This eliminates resource leak risks, reduces code duplication, and standardizes error handling.

---

## Problem Frame

The codebase has 6+ sites using raw `curl_easy_*` API with manual resource management. Each site repeats the same init-setopt-perform-cleanup pattern with inconsistent error handling (some check `curl_easy_init()` failure, others don't; `setopt()` return codes are ignored). This creates maintenance burden and potential resource leaks.

---

## Requirements

- R1. RAII wrapper calls `curl_easy_init()` in ctor, `curl_easy_cleanup()` in dtor (FR-1)
- R2. Wrapper throws `std::runtime_error` on `curl_easy_init()` failure (FR-2)
- R3. `setopt()` calls throw `std::runtime_error` on CURLerror (FR-3)
- R4. `perform()` throws `std::runtime_error` on CURLerror (FR-4)
- R5. Support fluent API (method chaining) for setting options (FR-5)
- R6. Support both HEAD mode (`CURLOPT_NOBODY`) and GET mode (write callback) (FR-6)
- R7. Provide `getTotalTime()` and `getResponseCode()` info getters (FR-7)
- R8. Provide `setWriteCallback()` for GET requests (FR-8)
- R9. Move-only semantics (no copy) (FR-9)
- R10. No connection reuse — simple RAII lifecycle (FR-10)
- R11. Must compile on Windows/MinGW-w64 with C++17 (NFR-2)

**Origin actors:** N/A (refactor, no new user-facing actors)
**Origin flows:** N/A
**Origin acceptance examples:** AE1 (no resource leaks), AE2 (exception on failure), AE3 (all 6 sites migrated)

---

## Scope Boundaries

- Create `CurlEasyHandle` class following existing RAII patterns (`DatabaseHelper.h`)
- Migrate all 6 CURL usage sites to use the wrapper
- Unify `WriteCallback` function (currently duplicated in `SubitemUpdaterV2.cpp` and `UrlFetcher.cpp`)
- Add `curl_global_init()`/`curl_global_cleanup()` to `main.cpp`
- Add tests for the wrapper class

- [Will NOT add connection pooling or handle reuse]
- [Will NOT change the external behavior of proxy testing or URL fetching]
- [Will NOT modify the `curl_easy` API itself]

### Deferred to Follow-Up Work

- [Consider adding `curl_global_init()` call in a more centralized location (e.g., dedicated init function)]: future cleanup
- [Add metrics/instrumentation to CurlEasyHandle]: future enhancement if needed

---

## Context & Research

### Relevant Code and Patterns

**Existing RAII patterns to follow:**

1. **`include/DatabaseHelper.h`** — `db::Database` class:
   ```cpp
   class Database {
       Database(const Database&) = delete;  // No copy
       Database& operator=(const Database&) = delete;
       ~Database() { close(); }  // RAII cleanup
       sqlite3* get() const { return db_; }  // Accessor
   };
   ```

2. **`include/XrayInstance.h`** — `XrayInstance` class:
   ```cpp
   ~XrayInstance() { stop(); }  // RAII cleanup
   // Uses HANDLE (Windows API) with manual cleanup
   ```

**Current CURL usage pattern (6 sites):**

| File | Function | Lines | Pattern |
|------|----------|-------|---------|
| `src/ProxyTester.cpp` | `test()` | 16-47 | HEAD request, proxy, get latency |
| `src/ProxyFinder.cpp` | `testProxyConnectivity()` | ~191-222 | HEAD request, proxy |
| `src/UrlFetcher.cpp` | `fetch()` | 15-30 | GET, write callback |
| `src/UrlFetcher.cpp` | `fetchViaProxy()` | 42-58 | GET, proxy, write callback |
| `src/SubitemUpdaterV2.cpp` | `fetchUrl()` | ~355-375 | GET, write callback |
| `src/SubitemUpdaterV2.cpp` | `fetchUrlViaProxy()` | ~378-402 | GET, proxy, write callback |

**Common CURL options used:**
- `CURLOPT_URL`, `CURLOPT_PROXY`, `CURLOPT_NOBODY`, `CURLOPT_WRITEFUNCTION`, `CURLOPT_WRITEDATA`
- `CURLOPT_FOLLOWLOCATION`, `CURLOPT_TIMEOUT_MS`/`CURLOPT_TIMEOUT`
- `CURLOPT_SSL_VERIFYPEER`, `CURLOPT_SSL_VERIFYHOST`

**Naming conventions (from `memory/project_knowledge.md`):**
- Classes: `PascalCase` (e.g., `CurlEasyHandle`)
- Methods: `camelCase` (e.g., `setUrl()`)
- Member variables: `snake_case` with trailing underscore (e.g., `curl_`)

### Institutional Learnings

No existing learnings in `docs/solutions/` (directory doesn't exist yet).

### External References

- CURL easy API: https://curl.se/libcurl/c/libcurl-easy.html
- RAII pattern: https://en.cppreference.com/w/cpp/language/raii

---

## Key Technical Decisions

- **Decision**: Throw `std::runtime_error` on CURL errors (setopt, perform)
  - **Rationale**: User chose "Exception on failure" for clear error propagation (see origin document Section 3.2)
  
- **Decision**: Fluent API with method chaining
  - **Rationale**: Ergonomic and readable, reduces repetitive code (see origin document Section 3.4)
  - **Example**: `curl.setUrl(url).setProxy(proxy).setTimeoutMs(5000).perform();`
  
- **Decision**: Store `CURL*` as member, provide `get()` accessor (like `Database::get()`)
  - **Rationale**: Follows existing project pattern, allows passing to curl_easy_getinfo()
  
- **Decision**: Unified `WriteCallback` as static method in `CurlEasyHandle` or free function in namespace
  - **Rationale**: Eliminates duplication between `SubitemUpdaterV2.cpp` and `UrlFetcher.cpp`

---

## Open Questions

### Resolved During Planning

- [How to handle move semantics?]: Implement move constructor and move assignment (no copy), following `std::unique_ptr` semantics
- [Where to place WriteCallback?]: Make it a static public method of `CurlEasyHandle` for unified access

### Deferred to Implementation

- [Exact method names for fluent API]: Can be adjusted during implementation if needed
- [Whether to add debug logging to wrapper]: Depends on how callers want to log CURL operations

---

## Implementation Units

- U1. **Create CurlEasyHandle RAII wrapper class**

**Goal:** Implement move-only RAII wrapper for CURL easy handle with fluent API and exception-based error handling.

**Requirements:** R1, R2, R3, R4, R5, R9, R10, R11

**Dependencies:** None

**Files:**
- Create: `include/CurlEasyHandle.h`
- Test: `tests/test_curl_easy_handle.cpp`

**Approach:**
- Class with `CURL* curl_` member variable
- Constructor calls `curl_easy_init()`, throws `std::runtime_error` if null
- Destructor calls `curl_easy_cleanup()`
- Delete copy constructor and copy assignment
- Implement move constructor and move assignment (transfers ownership, sets source to nullptr)
- Fluent methods: `setUrl()`, `setProxy()`, `setTimeoutMs()`, `setTimeoutSec()`, `setNoBody()`, `setFollowLocation()`, `setSslVerifyPeer()`, `setSslVerifyHost()`, `setWriteCallback()`
- Each fluent method calls `curl_easy_setopt()` and throws on error
- `perform()` method calls `curl_easy_perform()` and throws on error
- Info getters: `getTotalTime()`, `getResponseCode()`
- Accessor: `get()` returns `CURL*` (like `Database::get()`)
- Static method: `writeCallback()` to unify the duplicate write callbacks

**Patterns to follow:**
- `include/DatabaseHelper.h` — RAII pattern, `get()` accessor, no-copy semantics
- `memory/project_knowledge.md` — Naming conventions (PascalCase class, camelCase methods, snake_case_ members)

**Test scenarios:**
- Happy path: Create handle, set options, perform HEAD request (mock or real URL)
- Error path: Handle `curl_easy_init()` failure (if possible to simulate)
- Error path: `setopt()` throws on invalid option
- Error path: `perform()` throws on invalid URL
- Edge case: Move constructor transfers ownership correctly (no double-free)
- Edge case: Move assignment transfers ownership correctly
- Happy path: `getTotalTime()` and `getResponseCode()` return valid values after `perform()`
- Happy path: `setWriteCallback()` collects response data correctly

**Verification:**
- `CurlEasyHandle` compiles without errors on MinGW-w64 with C++17
- All fluent methods return `*this` for chaining
- Exception thrown on CURL errors
- Move semantics work correctly (no double-free, proper ownership transfer)

---

- U2. **Add global CURL initialization and cleanup to main.cpp**

**Goal:** Ensure `curl_global_init()` is called at startup and `curl_global_cleanup()` at exit.

**Requirements:** R11

**Dependencies:** None (can be done in parallel with U1)

**Files:**
- Modify: `src/main.cpp`

**Approach:**
- Add `curl_global_init(CURL_GLOBAL_ALL)` near the start of `main()` (after config validation)
- Add `curl_global_cleanup()` in cleanup path (before `Logger::close()`)
- Use existing cleanup pattern: check if already initialized, avoid double-cleanup

**Patterns to follow:**
- Existing resource cleanup pattern in `src/main.cpp` (lines 176-196 of original)
- Use `std::atexit()` or explicit call before `Logger::close()`

**Test scenarios:**
- Happy path: Program starts and initializes CURL globally without error
- Happy path: Program exits and cleans up CURL globally without error

**Verification:**
- Application starts without CURL global init errors
- Application exits without CURL global cleanup errors
- No memory leaks reported by static analysis

---

- U3. **Refactor ProxyTester.cpp to use CurlEasyHandle**

**Goal:** Replace raw CURL usage in `ProxyTester::test()` with CurlEasyHandle wrapper.

**Requirements:** R1, R3, R4, R5, R6, R7

**Dependencies:** U1 (CurlEasyHandle class must exist)

**Files:**
- Modify: `src/ProxyTester.cpp`
- Modify: `src/ProxyTester.h` (if needed for exception handling)

**Approach:**
- Replace `CURL* curl = curl_easy_init()` with `CurlEasyHandle curl;`
- Chain fluent methods: `curl.setUrl(testUrl_).setProxy(proxyUrl).setTimeoutMs(timeoutMs_).setNoBody(true).setFollowLocation(true).perform();`
- Replace `curl_easy_getinfo()` calls with `curl.getTotalTime()` and `curl.getResponseCode()`
- Wrap in try-catch block, populate `TestResult.errorMsg` with `e.what()`
- Remove manual `curl_easy_cleanup()` call (now in destructor)

**Patterns to follow:**
- Existing `TestResult` struct pattern in `ProxyTester.h`
- Exception handling pattern (try-catch, populate error message)

**Test scenarios:**
- Happy path: Successful HEAD request returns correct latency and response code
- Error path: Invalid proxy URL throws exception, caught and recorded in `TestResult.errorMsg`
- Error path: Timeout throws exception, caught and recorded

**Verification:**
- `ProxyTester::test()` works identically to before (same behavior)
- No resource leaks (handle cleaned up in destructor)
- Exception on CURL error propagates correctly to caller

---

- U4. **Refactor ProxyFinder.cpp to use CurlEasyHandle**

**Goal:** Replace raw CURL usage in `ProxyFinder::testProxyConnectivity()` with CurlEasyHandle wrapper.

**Requirements:** R1, R3, R4, R5, R6, R7

**Dependencies:** U1, U3 (can reuse patterns from U3)

**Files:**
- Modify: `src/ProxyFinder.cpp`

**Approach:**
- Similar to U3: Replace raw CURL with `CurlEasyHandle`
- Use fluent API for setting options
- Use `getTotalTime()` and `getResponseCode()` for info
- Wrap in try-catch block
- Remove manual `curl_easy_cleanup()`

**Patterns to follow:**
- U3 (ProxyTester.cpp refactoring pattern)
- Existing error handling in `ProxyFinder.cpp`

**Test scenarios:**
- Happy path: Successful HEAD request via proxy
- Error path: Invalid proxy or URL throws exception

**Verification:**
- `ProxyFinder::testProxyConnectivity()` works identically to before
- No resource leaks

---

- U5. **Unify WriteCallback and refactor SubitemUpdaterV2.cpp**

**Goal:** Unify the duplicate `WriteCallback` function and refactor both CURL usage sites in SubitemUpdaterV2.cpp.

**Requirements:** R1, R3, R4, R5, R6, R8

**Dependencies:** U1 (CurlEasyHandle with `setWriteCallback()` and static `writeCallback()`)

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp`
- Modify: `src/SubitemUpdaterV2.h` (if needed)

**Approach:**
- Remove anonymous namespace `WriteCallback` in `SubitemUpdaterV2.cpp`
- Use `CurlEasyHandle::writeCallback` (static method) instead
- Refactor `fetchUrl()`: Replace raw CURL with `CurlEasyHandle`, use fluent API
- Refactor `fetchUrlViaProxy()`: Same as above
- Both methods: `curl.setUrl(url).setWriteCallback(CurlEasyHandle::writeCallback, &response).setFollowLocation(true).setTimeoutSec(30).setSslVerifyPeer(false).setSslVerifyHost(false).perform();`
- Wrap in try-catch if needed (current code doesn't check CURL error, just logs)

**Patterns to follow:**
- U3, U4 (CURL refactoring pattern)
- Existing `fetchUrl()`/`fetchUrlViaProxy()` logic

**Test scenarios:**
- Happy path: `fetchUrl()` successfully fetches content via GET
- Happy path: `fetchUrlViaProxy()` successfully fetches content via proxy
- Error path: Invalid URL throws exception

**Verification:**
- Both methods in SubitemUpdaterV2.cpp work identically to before
- No duplicate `WriteCallback` functions
- No resource leaks

---

- U6. **Refactor UrlFetcher.cpp to use CurlEasyHandle**

**Goal:** Refactor both CURL usage sites in UrlFetcher.cpp to use CurlEasyHandle wrapper.

**Requirements:** R1, R3, R4, R5, R6, R8

**Dependencies:** U1, U5 (can reuse unified `CurlEasyHandle::writeCallback`)

**Files:**
- Modify: `src/UrlFetcher.cpp`
- Modify: `src/UrlFetcher.h` (if needed)

**Approach:**
- Remove static `writeCallback` method in `UrlFetcher`
- Use `CurlEasyHandle::writeCallback` instead
- Refactor `fetch()`: Replace raw CURL with `CurlEasyHandle`
- Refactor `fetchViaProxy()`: Same as above
- Use fluent API with `setWriteCallback()`
- Note: `UrlFetcher::fetchViaProxy()` currently uses `ProxyTester` to test first; keep this logic, refactor only the CURL part

**Patterns to follow:**
- U5 (SubitemUpdaterV2.cpp refactoring pattern)
- Existing `UrlFetcher` class structure

**Test scenarios:**
- Happy path: `fetch()` successfully fetches content
- Happy path: `fetchViaProxy()` successfully fetches content via proxy
- Error path: Exception on CURL error

**Verification:**
- Both methods in UrlFetcher.cpp work identically to before
- No resource leaks

---

- U7. **Add tests for CurlEasyHandle wrapper**

**Goal:** Create comprehensive tests for the CurlEasyHandle wrapper class.

**Requirements:** R1-R11 (all requirements need test coverage)

**Dependencies:** U1 (CurlEasyHandle class must exist)

**Files:**
- Create: `tests/test_curl_easy_handle.cpp`
- Modify: `CMakeLists.txt` (add test target)

**Approach:**
- Use Google Test framework (already present in `tests/test_model.cpp`)
- Test all fluent methods
- Test move semantics
- Test exception throwing on errors
- Test `getTotalTime()` and `getResponseCode()` (may need mock or real server)
- Test `setWriteCallback()` with a simple GET request
- Note: Integration tests may need a real HTTP server or mock; unit tests can test API contracts

**Patterns to follow:**
- `tests/test_model.cpp` — Google Test usage pattern
- Existing CMake test configuration (may need to add `enable_testing()` and test target)

**Test scenarios:**
- (Covered in U1 test scenarios, plus:)
- Happy path: Chained fluent calls work correctly
- Edge case: Calling `perform()` without required options (e.g., no URL) throws
- Integration: Real HTTP request returns expected data

**Verification:**
- All tests pass
- Test coverage for CurlEasyHandle is adequate (>80%)
- No false positives/negatives in tests

---

- U8. **Update CMakeLists.txt with test target**

**Goal:** Add test target for CurlEasyHandle tests.

**Requirements:** R11

**Dependencies:** U7 (test file must exist)

**Files:**
- Modify: `CMakeLists.txt`

**Approach:**
- Add `enable_testing()` if not present
- Add test executable: `add_executable(test_curl_easy_handle tests/test_curl_easy_handle.cpp)`
- Link libraries: `target_link_libraries(test_curl_easy_handle PRIVATE CURL::libcurl gtest gtest_main)`
- Add test: `add_test(NAME CurlEasyHandleTest COMMAND test_curl_easy_handle)`

**Patterns to follow:**
- Existing executable target in CMakeLists.txt (`add_executable(validproxy ...)`)
- Library linking pattern (`target_link_libraries(validproxy PRIVATE ...)`)

**Test scenarios:**
- Happy path: CMake configures without errors
- Happy path: Test executable builds without errors
- Happy path: Tests run and pass

**Verification:**
- `cmake --build . --parallel 8` succeeds (including test target)
- `ctest` or manual test run passes all tests

---

## System-Wide Impact

- **Interaction graph:** 
  - `ProxyTester::test()` — called by `ProxyBatchTester` during proxy testing
  - `ProxyFinder::testProxyConnectivity()` — called during proxy discovery
  - `SubitemUpdaterV2::fetchUrl()`/`fetchUrlViaProxy()` — called during subscription updates
  - `UrlFetcher::fetch()`/`fetchViaProxy()` — called by `ProxyFinder` for URL fetching
  
- **Error propagation:** CURL errors now throw `std::runtime_error` instead of returning error codes/messages. Callers must handle exceptions (or let them propagate).

- **State lifecycle risks:** 
  - Move semantics must be implemented correctly to avoid double-free
  - Destructor must handle null `CURL*` (if moved-from)
  
- **Unchanged invariants:**
  - External behavior of proxy testing, URL fetching, subscription updates remains the same
  - `TestResult` struct and other data structures unchanged
  - `ProxyTester`, `ProxyFinder`, `SubitemUpdaterV2`, `UrlFetcher` class interfaces unchanged (internal implementation changes only)

---

## Risks & Dependencies

| Risk | Mitigation |
|------|------------|
| Breaking existing error handling logic | Test each migration site individually; keep old code in comments initially; wrap in try-catch |
| CURL global init/cleanup conflicts | Add `curl_global_init()`/`curl_global_cleanup()` only in `main.cpp`, ensure no other calls elsewhere |
| Exception safety in callers | Update all callers to wrap in try-catch, or let exceptions propagate up to a handler |
| Move semantics bugs (double-free) | Test move constructor and move assignment thoroughly; ensure moved-from state is valid (nullptr) |
| Test infrastructure incomplete | `CMakeLists.txt` may need `enable_testing()` and test target; Google Test is already partially set up |

---

## Documentation / Operational Notes

- No user-facing documentation changes needed (internal refactor)
- Consider adding a brief comment in `CurlEasyHandle.h` about usage pattern
- Update `memory/project_knowledge.md` with new class information (optional, can be done later)

---

## Sources & References

- **Origin document:** [docs/superpowers/brainstorm/2026-05-07-curl-raii-wrapper-requirements.md](docs/superpowers/brainstorm/2026-05-07-curl-raii-wrapper-requirements.md)
- Related code: `include/DatabaseHelper.h`, `include/XrayInstance.h` (RAII patterns)
- Related code: `src/ProxyTester.cpp`, `src/ProxyFinder.cpp`, `src/SubitemUpdaterV2.cpp`, `src/UrlFetcher.cpp` (usage sites)
- External docs: https://curl.se/libcurl/c/libcurl-easy.html
