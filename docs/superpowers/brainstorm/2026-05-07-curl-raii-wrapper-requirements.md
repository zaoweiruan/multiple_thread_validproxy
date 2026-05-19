# CURL RAII Wrapper - Requirements Document

**Date**: 2026-05-07  
**Source**: Brainstorm session for improvement idea #1 from `docs/ideation/2026-05-07-improvement-ideas-ideation.md`  
**Status**: Draft

---

## 1. Problem Statement

The codebase has 6+ sites using raw `curl_easy_*` API with repetitive setup/cleanup code and inconsistent error handling. Each usage site manually calls `curl_easy_init()`, sets options, performs the request, and calls `curl_easy_cleanup()` — creating maintenance burden and potential resource leaks.

### Current Usage Sites (6 total)
1. **ProxyTester.cpp** (lines 16-47): HEAD request via proxy, get latency + response code
2. **ProxyFinder.cpp** (line ~150+): Similar HEAD request pattern
3. **SubitemUpdaterV2.cpp** (lines 371-382, 393-404): GET request with write callback, 2 sites
4. **UrlFetcher.cpp** (lines 15-30, 42-58): GET request with write callback, 2 sites

---

## 2. Requirements

### 2.1 Functional Requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| FR-1 | RAII wrapper must call `curl_easy_init()` in ctor, `curl_easy_cleanup()` in dtor | Guarantees no resource leak |
| FR-2 | Wrapper must throw `std::runtime_error` on `curl_easy_init()` failure | Currently returns null and continues |
| FR-3 | `setopt()` calls must throw `std::runtime_error` on CURLerror | Currently silently ignored |
| FR-4 | `perform()` must throw `std::runtime_error` on CURLerror | Currently checks response code manually |
| FR-5 | Support fluent API (method chaining) for setting options | Ergonomic: `handle.setUrl(...).setProxy(...).perform()` |
| FR-6 | Support both HEAD mode (CURLOPT_NOBODY) and GET mode (write callback) | Two distinct usage patterns in codebase |
| FR-7 | Provide `getTotalTime()` and `getResponseCode()` info getters | Needed by ProxyTester, ProxyFinder |
| FR-8 | Provide `setWriteCallback()` for GET requests collecting response data | Needed by SubitemUpdaterV2, UrlFetcher |
| FR-9 | Move-only semantics (no copy) | CURL handle is not copyable |
| FR-10 | No connection reuse — simple RAII lifecycle | User chose "No reuse (simple)" |

### 2.2 Non-Functional Requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| NFR-1 | Must be header-only or minimal `.h` + `.cpp` | Follows project convention (check existing files) |
| NFR-2 | Must compile on Windows/MinGW-w64 with C++17 | Project constraint |
| NFR-3 | Must use existing `Logger::write()` for debug logging (optional) | Consistent with project logging |
| NFR-4 | No new third-party dependencies | CURL is already linked |

---

## 3. Design Decisions

### 3.1 Scope
**Decision**: Cover ALL 6 CURL usage sites (ProxyTester, ProxyFinder, SubitemUpdaterV2, UrlFetcher)  
**Rationale**: User chose "All CURL usage sites (Recommended)" to maximize consistency

### 3.2 Error Handling
**Decision**: Throw `std::runtime_error` on setopt/perform failures  
**Rationale**: User chose "Exception on failure" for clear error propagation  
**Example**: 
```cpp
try {
    CurlEasyHandle curl;
    curl.setUrl("http://example.com").setProxy("socks5://127.0.0.1:12345").perform();
} catch (const std::runtime_error& e) {
    // Handle error
}
```

### 3.3 Connection Reuse
**Decision**: No reuse — simple RAII init/cleanup per request  
**Rationale**: User chose "No reuse (simple)" for simplicity  
**Implication**: No `reset()` method needed

### 3.4 API Style
**Decision**: Fluent builder pattern with method chaining  
**Rationale**: Ergonomic and readable, reduces repetitive code  
**Example**:
```cpp
CurlEasyHandle curl;
curl.setUrl(url)
    .setProxy(proxyUrl)
    .setTimeoutMs(5000)
    .setNoBody(true)
    .perform();
```

---

## 4. Usage Examples (After Migration)

### 4.1 ProxyTester (HEAD request)
**Before** (lines 16-47 in ProxyTester.cpp):
```cpp
CURL* curl = curl_easy_init();
if (!curl) { result.errorMsg = "curl_init_failed"; return result; }
curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
curl_easy_setopt(curl, CURLOPT_URL, testUrl_.c_str());
// ... 5 more setopts
CURLcode res = curl_easy_perform(curl);
curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
curl_easy_cleanup(curl);
// Manual error checking...
```

**After**:
```cpp
try {
    CurlEasyHandle curl;
    curl.setUrl(testUrl_)
        .setProxy(proxyUrl)
        .setTimeoutMs(timeoutMs_)
        .setNoBody(true)
        .setFollowLocation(true)
        .perform();
    
    result.latencyMs = static_cast<long>(curl.getTotalTime() * 1000);
    long responseCode = curl.getResponseCode();
    result.success = (responseCode == 200 || responseCode == 204);
} catch (const std::runtime_error& e) {
    result.errorMsg = e.what();
}
```

### 4.2 SubitemUpdaterV2 (GET request with write callback)
**Before** (lines 371-382 in SubitemUpdaterV2.cpp):
```cpp
CURL* curl = curl_easy_init();
curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
// ... more setopts
CURLcode res = curl_easy_perform(curl);
curl_easy_cleanup(curl);
```

**After**:
```cpp
CurlEasyHandle curl;
curl.setUrl(url)
    .setWriteCallback(WriteCallback, &response)
    .setFollowLocation(true)
    .setTimeoutSec(30)
    .setSslVerifyPeer(false)
    .setSslVerifyHost(false)
    .perform();
```

---

## 5. Acceptance Criteria

- [ ] `CurlEasyHandle` class compiles without errors on MinGW-w64
- [ ] All 6 CURL usage sites migrated to use `CurlEasyHandle`
- [ ] No new resource leaks (verify with static analysis or manual review)
- [ ] Exception thrown on `curl_easy_init()` failure
- [ ] Exception thrown on `setopt()` failure
- [ ] Exception thrown on `perform()` failure
- [ ] `getTotalTime()` and `getResponseCode()` return correct values after `perform()`
- [ ] `setWriteCallback()` works correctly for GET requests
- [ ] Move semantics work correctly (no double-free)
- [ ] Build passes with `cmake --build . --parallel 8`

---

## 6. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Breaking existing error handling logic | Medium | Test each migration site individually, keep old code in comments initially |
| CURL global init/cleanup conflicts | Low | `curl_global_init()`/`curl_global_cleanup()` called elsewhere in codebase, ensure no conflict |
| Exception safety in callers | Medium | Update all callers to wrap in try-catch, or let exceptions propagate up |

---

## 7. Next Steps

1. Run `ce-plan` to create implementation plan
2. Implement `include/CurlEasyHandle.h` and `src/CurlEasyHandle.cpp`
3. Migrate ProxyTester.cpp (simplest, good test case)
4. Migrate ProxyFinder.cpp
5. Migrate SubitemUpdaterV2.cpp (2 sites)
6. Migrate UrlFetcher.cpp (2 sites)
7. Run build + verify no regressions
