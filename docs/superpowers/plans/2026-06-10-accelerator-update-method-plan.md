# Accelerator Update Method — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `priority_mode` with `update_methods` (multi-select array: accelerator/proxy/direct) and add accelerator URL config for subscription fetching.

**Architecture:** Add `utils::joinUrl()` for URL concatenation. Extend `config::AppConfig` with `accelerator_url` + `update_methods`, remove `priority_mode`, with backward-compat conversion in `ConfigReader::load()`. Refactor `SubitemUpdaterV2::run()` from 2-phase to N-phase loop over update methods. Update `ConfigDialog` to replace dropdown with text input + checkboxes.

**Tech Stack:** C++17, boost::json, CurlEasyHandle, wxPropertyGrid

---

### Task 1: Utils — add joinUrl()

**Files:**
- Modify: `include/Utils.h`
- Modify: `src/Utils.cpp`

- [ ] **Step 1: Write the failing test**

Add to a new or existing test file. Create `tests/test_utils.cpp`:

```cpp
#include <gtest/gtest.h>
#include "Utils.h"

TEST(JoinUrlTest, BothWithoutSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path", "https://sub.com/v2?t=1"),
              "https://acc.com/path/https://sub.com/v2?t=1");
}

TEST(JoinUrlTest, BaseHasTrailingSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path/", "https://sub.com"),
              "https://acc.com/path/https://sub.com");
}

TEST(JoinUrlTest, SuffixHasLeadingSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path", "/https://sub.com"),
              "https://acc.com/path/https://sub.com");
}

TEST(JoinUrlTest, BothHaveSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path/", "/https://sub.com"),
              "https://acc.com/path/https://sub.com");
}

TEST(JoinUrlTest, EmptyBaseReturnsSuffix) {
    EXPECT_EQ(utils::joinUrl("", "https://sub.com"), "https://sub.com");
}

TEST(JoinUrlTest, EmptySuffixReturnsBase) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path", ""), "https://acc.com/path");
}

TEST(JoinUrlTest, BothEmptyReturnsEmpty) {
    EXPECT_EQ(utils::joinUrl("", ""), "");
}

TEST(JoinUrlTest, BaseAlreadyEndsWithSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/", "sub?t=1"),
              "https://acc.com/sub?t=1");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -I include -c tests/test_utils.cpp -o tests/test_utils.o` (temporary — CMake integration in Task 8)

Expected: Compile error — `joinUrl` not declared.

- [ ] **Step 3: Write minimal implementation**

In `include/Utils.h`, add:
```cpp
std::string joinUrl(const std::string& base, const std::string& suffix);
```

In `src/Utils.cpp`, add:
```cpp
std::string joinUrl(const std::string& base, const std::string& suffix) {
    if (base.empty()) return suffix;
    if (suffix.empty()) return base;
    
    std::string b = base;
    std::string s = suffix;
    
    // Remove trailing '/' from base
    while (!b.empty() && b.back() == '/') {
        b.pop_back();
    }
    // Remove leading '/' from suffix
    while (!s.empty() && s.front() == '/') {
        s.erase(s.begin());
    }
    
    if (b.empty()) return s;
    if (s.empty()) return b;
    
    return b + "/" + s;
}
```

- [ ] **Step 4: Run test to verify it passes**

Integration test via ctest after CMake update (Task 8).

- [ ] **Step 5: Staging (commit later)**

```bash
git add include/Utils.h src/Utils.cpp tests/test_utils.cpp
```

---

### Task 2: ConfigReader.h — add accelerator_url + update_methods, remove priority_mode

**Files:**
- Modify: `include/ConfigReader.h`

- [ ] **Step 1: Modify AppConfig struct**

Replace `std::string priority_mode;` (line 23) with:
```cpp
std::string accelerator_url;
std::vector<std::string> update_methods;
```

- [ ] **Step 2: Verify build still works**

Run: `cmake --build build --parallel 8`
Expected: Build failure — ConfigReader.cpp `priority_mode` references need updating.

---

### Task 3: ConfigReader.cpp — update load() and save()

**Files:**
- Modify: `src/ConfigReader.cpp`

- [ ] **Step 1: Update load() — read new fields**

In the subscription section (around line 207-250), replace priority_mode reading with:

```cpp
if (sub.contains("accelerator_url") && sub["accelerator_url"].is_string()) {
    config.accelerator_url = sub["accelerator_url"].as_string().c_str();
} else if (sub.contains("accelerator_url")) {
    warnWrongType("subscription", "accelerator_url", "string");
}

if (sub.contains("update_methods") && sub["update_methods"].is_array()) {
    for (const auto& m : sub["update_methods"].as_array()) {
        if (m.is_string()) {
            std::string val = m.as_string().c_str();
            if (val == "accelerator" || val == "proxy" || val == "direct") {
                config.update_methods.push_back(val);
            }
        }
    }
} else if (sub.contains("update_methods")) {
    warnWrongType("subscription", "update_methods", "array");
}

// Backward compatibility: priority_mode → update_methods
if (sub.contains("priority_mode") && sub["priority_mode"].is_string()
    && !sub.contains("update_methods")) {
    std::string pm = sub["priority_mode"].as_string().c_str();
    if (pm == "direct_first") {
        config.update_methods = {"direct", "proxy"};
    } else if (pm == "proxy_first") {
        config.update_methods = {"proxy"};
    } else if (pm == "direct_only") {
        config.update_methods = {"direct"};
    }
}

// Default: if update_methods is empty after all parsing
if (config.update_methods.empty()) {
    config.update_methods = {"accelerator"};
}
```

Also remove the old `priority_mode` default (line 246: `config.priority_mode = "direct_first";`). The `update_methods` default will be set to `{"accelerator"}` in the above logic.

- [ ] **Step 2: Update save() — write new fields, remove priority_mode**

In the subscription section (around line 472-477), replace:
```cpp
subObj["priority_mode"] = config.priority_mode;
```
with:
```cpp
if (!config.accelerator_url.empty()) {
    subObj["accelerator_url"] = config.accelerator_url;
}
boost::json::array methodsArr;
for (const auto& m : config.update_methods) {
    methodsArr.emplace_back(m);
}
subObj["update_methods"] = methodsArr;
```

- [ ] **Step 3: Build + fix any compilation errors**

Run: `cmake --build build --parallel 8`
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add include/ConfigReader.h src/ConfigReader.cpp
git commit -m "feat(config): add accelerator_url + update_methods, remove priority_mode"
```

---

### Task 4: ConfigReader — backward compatibility tests

**Files:**
- Modify: `tests/test_config_reader_load.cpp`

- [ ] **Step 1: Add backward compatibility test**

Add to `test_config_reader_load.cpp`:

```cpp
TEST_F(ConfigReaderLoadTest, PriorityModeBackwardCompat_DirectFirst) {
    writeConfig("pm_df.json", R"({
        "subscription": {
            "priority_mode": "direct_first"
        }
    })");
    auto result = ConfigReader::load(configPath("pm_df.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 2);
    EXPECT_EQ(result->update_methods[0], "direct");
    EXPECT_EQ(result->update_methods[1], "proxy");
}

TEST_F(ConfigReaderLoadTest, PriorityModeBackwardCompat_ProxyFirst) {
    writeConfig("pm_pf.json", R"({
        "subscription": {
            "priority_mode": "proxy_first"
        }
    })");
    auto result = ConfigReader::load(configPath("pm_pf.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 1);
    EXPECT_EQ(result->update_methods[0], "proxy");
}

TEST_F(ConfigReaderLoadTest, PriorityModeBackwardCompat_DirectOnly) {
    writeConfig("pm_do.json", R"({
        "subscription": {
            "priority_mode": "direct_only"
        }
    })");
    auto result = ConfigReader::load(configPath("pm_do.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 1);
    EXPECT_EQ(result->update_methods[0], "direct");
}

TEST_F(ConfigReaderLoadTest, UpdateMethodsTakesPriority) {
    writeConfig("um_priority.json", R"({
        "subscription": {
            "priority_mode": "proxy_first",
            "update_methods": ["accelerator", "direct"]
        }
    })");
    auto result = ConfigReader::load(configPath("um_priority.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 2);
    EXPECT_EQ(result->update_methods[0], "accelerator");
    EXPECT_EQ(result->update_methods[1], "direct");
}

TEST_F(ConfigReaderLoadTest, AcceleratorUrlParsed) {
    writeConfig("acc_url.json", R"({
        "subscription": {
            "accelerator_url": "https://cdn.acc.com/"
        }
    })");
    auto result = ConfigReader::load(configPath("acc_url.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->accelerator_url, "https://cdn.acc.com/");
}

TEST_F(ConfigReaderLoadTest, UpdateMethodsDefaultWhenEmpty) {
    writeConfig("um_empty.json", R"({
        "subscription": {}
    })");
    auto result = ConfigReader::load(configPath("um_empty.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 1);
    EXPECT_EQ(result->update_methods[0], "accelerator");
}

TEST_F(ConfigReaderLoadTest, UpdateMethodsFiltersInvalid) {
    writeConfig("um_invalid.json", R"({
        "subscription": {
            "update_methods": ["accelerator", "invalid", "proxy"]
        }
    })");
    auto result = ConfigReader::load(configPath("um_invalid.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 2);
    EXPECT_EQ(result->update_methods[0], "accelerator");
    EXPECT_EQ(result->update_methods[1], "proxy");
}
```

- [ ] **Step 2: Run backward compat tests**

Run: `ctest -R ConfigReaderTest -V`
Expected: All tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_config_reader_load.cpp include/ConfigReader.h src/ConfigReader.cpp
git commit -m "test(config): backward compat and new field tests for accelerator/update_methods"
```

---

### Task 5: Utils test CMake integration

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add test_utils target to CMakeLists.txt**

Add after the existing test targets (near line 313):
```cmake
# -- Utils unit tests (pure functions, no external deps beyond gtest) --
add_executable(test_utils tests/test_utils.cpp src/Utils.cpp src/Logger.cpp)
target_include_directories(test_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_utils PRIVATE gtest_main gtest)
set_target_properties(test_utils PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME UtilsTest COMMAND test_utils)
```

(Note: Logger.cpp is not needed for joinUrl, but required for linking since Utils.cpp may trigger it. Actually, looking at the CMake pattern, many test targets don't include Logger. But `test_logger` does include it. Let's be safe and include it.)

Actually, let's look at `test_logger` which has `src/Logger.cpp`. `test_utils.cpp` only uses `utils::joinUrl` which doesn't reference Logger. So it shouldn't need Logger. But let's not link it to keep minimal deps.

- [ ] **Step 2: Build and run test**

```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -R UtilsTest -V
```

Expected: Build succeeds, all joinUrl tests pass.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt tests/test_utils.cpp
git commit -m "test(utils): add joinUrl unit tests with CMake target"
```

---

### Task 6: SubitemUpdaterV2.h — add UpdateMethod enum, new methods

**Files:**
- Modify: `include/SubitemUpdaterV2.h`

- [ ] **Step 1: Replace Strategy enum with UpdateMethod enum**

Remove the `Strategy` enum:
```cpp
enum class Strategy {
    DirectFirst,
    ProxyFirst,
    DirectOnly
};
```

Add after the class opening:
```cpp
enum class UpdateMethod {
    Accelerator,
    Proxy,
    Direct
};
```

- [ ] **Step 2: Replace parseStrategy + updateWithStrategy declarations**

Remove:
```cpp
bool updateWithStrategy(const std::string& subUrl, const std::string& subId, Strategy strategy);
Strategy parseStrategy(const std::string& mode);
```

Add:
```cpp
std::vector<UpdateMethod> parseUpdateMethods(const std::vector<std::string>& methods);
std::string fetchUrlViaAccelerator(const std::string& url);
bool updateWithMethods(const std::string& subUrl, const std::string& subId,
                       const std::vector<UpdateMethod>& methods);
```

- [ ] **Step 3: Build + fix**

Run: `cmake --build build --parallel 8`
Expected: Build failure — SubitemUpdaterV2.cpp uses old Strategy symbols.

---

### Task 7: SubitemUpdaterV2.cpp — implement multi-method update

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp`

This is the largest change. The entire `run()`, `runSingle()`, `parseStrategy()`, and `updateWithStrategy()` methods need rewriting.

- [ ] **Step 1: Replace `parseStrategy` with `parseUpdateMethods`**

Replace:
```cpp
Strategy SubitemUpdaterV2::parseStrategy(const std::string& mode) {
    ...
}
```

With:
```cpp
std::vector<SubitemUpdaterV2::UpdateMethod> SubitemUpdaterV2::parseUpdateMethods(
    const std::vector<std::string>& methods)
{
    std::vector<UpdateMethod> result;
    // Fixed order: accelerator → proxy → direct
    for (const auto& m : methods) {
        if (m == "accelerator" && std::find(result.begin(), result.end(), UpdateMethod::Accelerator) == result.end()) {
            result.push_back(UpdateMethod::Accelerator);
        } else if (m == "proxy" && std::find(result.begin(), result.end(), UpdateMethod::Proxy) == result.end()) {
            result.push_back(UpdateMethod::Proxy);
        } else if (m == "direct" && std::find(result.begin(), result.end(), UpdateMethod::Direct) == result.end()) {
            result.push_back(UpdateMethod::Direct);
        }
    }
    if (result.empty()) {
        result.push_back(UpdateMethod::Accelerator);
    }
    return result;
}
```

- [ ] **Step 2: Add `fetchUrlViaAccelerator` method**

```cpp
std::string SubitemUpdaterV2::fetchUrlViaAccelerator(const std::string& url) {
    try {
        std::string fetchUrl = utils::joinUrl(config_.accelerator_url, url);
        Logger::write("INFO: Fetching via accelerator: " + fetchUrl, LogLevel::INFO);
        
        std::string response;
        CurlEasyHandle curl;
        curl.setUrl(fetchUrl)
            .setWriteCallback(CurlEasyHandle::writeCallback, &response)
            .setFollowLocation()
            .setConnectTimeoutMs(config_.subscription_connect_timeout_ms)
            .setTimeoutMs(config_.subscription_timeout_ms)
            .setSslVerifyPeer(false)
            .setSslVerifyHost(false);
        
        curl.perform();
        return response;
    } catch (const std::exception& e) {
        Logger::write("fetchUrlViaAccelerator failed - " + std::string(e.what()), LogLevel::INFO);
        return "";
    }
}
```

- [ ] **Step 3: Replace `updateWithStrategy` with `updateWithMethods`**

```cpp
bool SubitemUpdaterV2::updateWithMethods(const std::string& subUrl, const std::string& subId,
                                          const std::vector<UpdateMethod>& methods)
{
    for (auto method : methods) {
        if (isCancelled()) {
            Logger::write("INFO: Single update cancelled: " + subId, LogLevel::REPORT);
            return false;
        }
        
        std::string content;
        switch (method) {
            case UpdateMethod::Accelerator:
                Logger::write("INFO: Trying accelerator connection...", LogLevel::INFO);
                content = fetchUrlViaAccelerator(subUrl);
                break;
            case UpdateMethod::Proxy: {
                Logger::write("INFO: Trying proxy connection...", LogLevel::INFO);
                auto [socks, api] = getProxyPorts(subUrl);
                (void)api;
                if (socks > 0) {
                    content = fetchUrlViaProxy(subUrl, socks);
                } else {
                    Logger::write("WARN: No proxy available", LogLevel::WARN);
                }
                break;
            }
            case UpdateMethod::Direct:
                Logger::write("INFO: Trying direct connection...", LogLevel::INFO);
                content = fetchUrl(subUrl);
                break;
        }
        
        if (!content.empty()) {
            auto profiles = parseSubscription(content, subId);
            if (!profiles.empty()) {
                Logger::write("INFO: Update successful via method " + std::to_string(static_cast<int>(method)), LogLevel::INFO);
                return updateProfileItems(subId, profiles);
            }
        }
        Logger::write("WARN: " + std::to_string(static_cast<int>(method)) + " failed for " + subId, LogLevel::WARN);
    }
    
    return false;
}
```

- [ ] **Step 4: Rewrite `run()` — multi-phase loop**

Replace the entire `run()` method body (lines 165-348) with the new multi-phase flow:

```cpp
bool SubitemUpdaterV2::run() {
    Logger::write("========================================", LogLevel::INFO);
    Logger::write("INFO: Starting SubitemUpdaterV2", LogLevel::INFO);
    Logger::write("========================================", LogLevel::INFO);

    db::models::SubitemDAO subDao(db_);
    auto enabledSubs = subDao.getEnabledSubscriptions();

    if (enabledSubs.empty()) {
        Logger::write("INFO: No enabled subscriptions found", LogLevel::INFO);
        return false;
    }

    Logger::write("INFO: Found " + std::to_string(enabledSubs.size()) + " enabled subscriptions", LogLevel::INFO);

    std::vector<UpdateMethod> updateMethods = parseUpdateMethods(config_.update_methods);
    
    // Log methods
    {
        std::string methodStr;
        for (auto m : updateMethods) {
            switch (m) {
                case UpdateMethod::Accelerator: methodStr += "accelerator "; break;
                case UpdateMethod::Proxy: methodStr += "proxy "; break;
                case UpdateMethod::Direct: methodStr += "direct "; break;
            }
        }
        Logger::write("INFO: Update methods: " + methodStr, LogLevel::INFO);
    }

    int totalSubs = enabledSubs.size();
    int successCount = 0;

    std::vector<std::tuple<std::string, std::string, std::string>> failedSubs;
    for (const auto& sub : enabledSubs) {
        if (shouldSkipUpdate(sub)) {
            Logger::write("INFO: Skipping sub " + sub.id + " (within update interval)", LogLevel::INFO);
            continue;
        }
        failedSubs.push_back({sub.id, sub.remarks, sub.url});
    }

    int phaseNum = 0;
    for (auto method : updateMethods) {
        if (failedSubs.empty()) break;
        if (isCancelled()) {
            Logger::write("INFO: Update cancelled by user", LogLevel::REPORT);
            break;
        }
        
        phaseNum++;
        std::string methodName;
        switch (method) {
            case UpdateMethod::Accelerator: methodName = "Accelerator"; break;
            case UpdateMethod::Proxy: methodName = "Proxy"; break;
            case UpdateMethod::Direct: methodName = "Direct"; break;
        }
        
        Logger::write("========================================", LogLevel::INFO);
        Logger::write("INFO: Phase " + std::to_string(phaseNum) + "/" + std::to_string(updateMethods.size()) + " - " + methodName, LogLevel::INFO);
        Logger::write("========================================", LogLevel::INFO);

        // Pre-find proxy if proxy phase
        int proxySocksPort = -1;
        int proxyApiPort = -1;
        (void)proxyApiPort;
        if (method == UpdateMethod::Proxy && !failedSubs.empty()) {
            Logger::write("INFO: Pre-finding proxy for proxy phase...", LogLevel::INFO);
            auto result = getProxyPorts(std::get<2>(failedSubs[0]));
            proxySocksPort = result.first;
            proxyApiPort = result.second;
            if (proxySocksPort > 0) {
                Logger::write("INFO: Found working proxy, socks=" + std::to_string(proxySocksPort), LogLevel::INFO);
            } else {
                Logger::write("WARN: Failed to find working proxy", LogLevel::WARN);
            }
        }

        std::vector<std::tuple<std::string, std::string, std::string>> stillFailed;
        
        for (size_t i = 0; i < failedSubs.size(); ++i) {
            if (isCancelled()) {
                Logger::write("INFO: Update cancelled by user during " + methodName + " phase", LogLevel::REPORT);
                break;
            }
            
            const auto& sub = failedSubs[i];
            Logger::write("[" + std::to_string(i + 1) + "/" + std::to_string(failedSubs.size()) + "] Processing via " + methodName + ": " + std::get<2>(sub), LogLevel::REPORT);
            
            std::string content;
            switch (method) {
                case UpdateMethod::Accelerator:
                    content = fetchUrlViaAccelerator(std::get<2>(sub));
                    break;
                case UpdateMethod::Proxy:
                    if (proxySocksPort > 0) {
                        content = fetchUrlViaProxy(std::get<2>(sub), proxySocksPort);
                    }
                    break;
                case UpdateMethod::Direct:
                    content = fetchUrl(std::get<2>(sub));
                    break;
            }
            
            if (isCancelled()) break;
            
            if (!content.empty()) {
                auto profiles = parseSubscription(content, std::get<0>(sub));
                if (!profiles.empty()) {
                    updateProfileItems(std::get<0>(sub), profiles);
                    successCount++;
                    Logger::write("INFO: Updated successfully: " + std::get<0>(sub), LogLevel::INFO);
                    std::string newTime = getCurrentTimestamp();
                    std::string updateSql = "UPDATE SubItem SET UpdateTime = '" + newTime + "' WHERE Id = '" + std::get<0>(sub) + "'";
                    execSql(updateSql, "[SubitemUpdaterV2] SQL exec failed");
                } else {
                    stillFailed.push_back(sub);
                    Logger::write("ERROR: Parse failed: " + std::get<0>(sub), LogLevel::ERR);
                }
            } else {
                stillFailed.push_back(sub);
                Logger::write("WARN: " + methodName + " connection failed for " + std::get<0>(sub), LogLevel::WARN);
            }
        }
        
        failedSubs = stillFailed;
    }

    releaseProxyPorts();

    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Update Summary", LogLevel::REPORT);
    Logger::write("Total subscriptions: " + std::to_string(totalSubs), LogLevel::REPORT);
    Logger::write("Total - Success: " + std::to_string(successCount) + ", Failed: " + std::to_string(totalSubs - successCount), LogLevel::REPORT);
    
    if (!failedSubs.empty()) {
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Failed subscriptions:", LogLevel::REPORT);
        for (const auto& sub : failedSubs) {
            Logger::write("  Id: " + std::get<0>(sub) + ", Remarks: " + std::get<1>(sub) + ", URL: " + std::get<2>(sub), LogLevel::REPORT);
        }
    }
    Logger::write("========================================", LogLevel::REPORT);

    if (config_.dedup_after_update && config_.dedup_enabled) {
        Logger::write("INFO: Running dedup after subscription update...", LogLevel::INFO);
        deduplicate();
    }

    return successCount > 0;
}
```

- [ ] **Step 5: Rewrite `runSingle()` — use updateWithMethods**

Replace the `runSingle()` body (lines 350-392):

```cpp
bool SubitemUpdaterV2::runSingle(const std::string& subId) {
    Logger::write("INFO: runSingle - subId: " + subId, LogLevel::INFO);

    auto optSub = getSubscription(subId);
    if (!optSub) {
        Logger::write("ERROR: Subscription not found: " + subId, LogLevel::ERR);
        return false;
    }
    const auto& sub = *optSub;

    if (sub.enabled != "1") {
        Logger::write("ERROR: Subscription is disabled: " + subId, LogLevel::ERR);
        return false;
    }

    if (shouldSkipUpdate(sub)) {
        Logger::write("INFO: Skipping sub " + sub.id + " (within update interval)", LogLevel::INFO);
        return true;
    }

    std::vector<UpdateMethod> updateMethods = parseUpdateMethods(config_.update_methods);
    
    if (isCancelled()) {
        Logger::write("INFO: Single update cancelled by user: " + subId, LogLevel::REPORT);
        return false;
    }
    
    bool result = updateWithMethods(sub.url, sub.id, updateMethods);

    if (result) {
        std::string newTime = getCurrentTimestamp();
        std::string updateSql = "UPDATE SubItem SET UpdateTime = '" + newTime + "' WHERE Id = '" + sub.id + "'";
        execSql(updateSql, "[SubitemUpdaterV2] SQL exec failed");
    }

    releaseProxyPorts();

    if (config_.dedup_after_update && config_.dedup_enabled) {
        Logger::write("INFO: Running dedup after subscription update...", LogLevel::INFO);
        deduplicate();
    }

    return result;
}
```

- [ ] **Step 6: Remove old `parseStrategy` and `updateWithStrategy` methods**

Delete the old `parseStrategy` and `updateWithStrategy` method bodies entirely.

- [ ] **Step 7: Update the "priority mode" log line in run()**

In the `run()` method (the old one), line 168 had: `Logger::write("INFO: Priority mode: " + config_.priority_mode, LogLevel::INFO);`
This is already replaced in the new `run()` body above. The new code logs the update methods instead.

- [ ] **Step 8: Build + fix compilation**

Run: `cmake --build build --parallel 8`
Expected: Clean build.

- [ ] **Step 9: Commit**

```bash
git add include/SubitemUpdaterV2.h src/SubitemUpdaterV2.cpp
git commit -m "feat(updater): multi-method subscription update with accelerator/proxy/direct"
```

---

### Task 8: ConfigDialog — replace priority_mode with accelerator_url + update_methods

**Files:**
- Modify: `src/ui/ConfigDialog.cpp`

- [ ] **Step 1: Remove priority_mode dropdown, add accelerator_url + update_methods**

In the "订阅" category (lines 65-75 of ConfigDialog.cpp), replace the priority_mode block:

Old code:
```cpp
propGrid_->Append(new wxPropertyCategory(L"订阅"));
wxArrayString modeChoices;
modeChoices.Add("proxy_first"); modeChoices.Add("direct_first"); modeChoices.Add("direct_only");
propGrid_->Append(new wxEnumProperty(L"优先级模式", "priority_mode", modeChoices));
```

New code:
```cpp
propGrid_->Append(new wxPropertyCategory(L"订阅"));
propGrid_->Append(new wxStringProperty(L"加速器地址", "accelerator_url", cfg.accelerator_url));
```

Add update_methods multi-select checkboxes after the existing timeout fields (after `subscription_timeout_ms`):

```cpp
// update_methods: multi-select via separate bool properties
// Using wxBoolProperty with "update_method_" prefix for each method
// Order: accelerator, proxy, direct (fixed)
propGrid_->Append(new wxPropertyCategory(L"更新方式"));
bool hasAccel = false, hasProxy = false, hasDirect = false;
for (const auto& m : cfg.update_methods) {
    if (m == "accelerator") hasAccel = true;
    else if (m == "proxy") hasProxy = true;
    else if (m == "direct") hasDirect = true;
}
// Default: all unselected → none checked, treated as ["accelerator"]
propGrid_->Append(new wxBoolProperty(L"加速器", "update_method_accelerator", hasAccel));
propGrid_->Append(new wxBoolProperty(L"代理", "update_method_proxy", hasProxy));
propGrid_->Append(new wxBoolProperty(L"直连", "update_method_direct", hasDirect));
```

- [ ] **Step 2: Update saveConfig() — save new fields**

In `saveConfig()` (line 162-166), replace:
```cpp
editedConfig_.priority_mode = propGrid_->GetPropertyValueAsString("priority_mode").ToStdString();
```
with:
```cpp
editedConfig_.accelerator_url = propGrid_->GetPropertyValueAsString("accelerator_url").ToStdString();
```

Also remove the `priority_mode` from `loadConfig()` (line 137):
```cpp
propGrid_->SetPropertyValue("priority_mode", wxString(cfg.priority_mode));
```

Add update_methods saving after the subscription connect/timeout:
```cpp
// update_methods
editedConfig_.update_methods.clear();
bool useAccel = propGrid_->GetPropertyValueAsBool("update_method_accelerator");
bool useProxy = propGrid_->GetPropertyValueAsBool("update_method_proxy");
bool useDirect = propGrid_->GetPropertyValueAsBool("update_method_direct");
// Fixed order: accelerator → proxy → direct
if (useAccel) editedConfig_.update_methods.push_back("accelerator");
if (useProxy) editedConfig_.update_methods.push_back("proxy");
if (useDirect) editedConfig_.update_methods.push_back("direct");
// If nothing selected, defaults to ["accelerator"] in ConfigReader::save() but we should
// still store an empty array; the default is applied on load
```

- [ ] **Step 3: Build + verify**

Run: `cmake --build build --parallel 8`
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add src/ui/ConfigDialog.cpp include/ConfigReader.h
git commit -m "feat(ui): replace priority_mode dropdown with accelerator_url + update_methods checkboxes"
```

---

### Task 9: Fix any references to config_.priority_mode outside SubitemUpdaterV2/ConfigReader

- [ ] **Step 1: Search for remaining priority_mode references**

Run: `rg "priority_mode" --include "*.cpp" --include "*.h"`
Expected: Only hits in `include/ConfigReader.h` (removed), `src/ConfigReader.cpp` (backward compat), and `src/ui/ConfigDialog.cpp` in the old code that was replaced.

- [ ] **Step 2: Full build + test**

```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V
```

Expected: All tests pass (including ConfigReaderTest with new backward compat tests).

- [ ] **Step 3: Commit final changeset**

```bash
git add -A
git commit -m "feat: complete accelerator update method implementation"
```

---

## Post-Implementation Verification

1. **CLI mode**: Run `.\bin\validproxy-cli -U <subid>` with new config containing `accelerator_url` + `update_methods` — verify accelerator URL is used for fetching.
2. **ConfigDialog**: Open GUI, go to Config — verify priority_mode is gone, accelerator_url text field + update_methods checkboxes are present, save/load round-trips correctly.
3. **Old config backward compat**: Load old `config.json` with `priority_mode` — verify it auto-converts to `update_methods` and config is saved back.
