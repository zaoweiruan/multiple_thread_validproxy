# NetworkMonitor: Batch Network Abort on Disconnect — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Detect Chinese mainland network disconnection during batch operations (update_all/test_all) and immediately abort them, with visible UI feedback.

**Architecture:** A standalone `NetworkMonitor` class with a background thread that periodically sends cURL HEAD requests to configurable URLs (baidu.com, qq.com, taobao.com). An `std::atomic<bool> connected_` flag is read by `ProxyBatchTester` and `SubitemUpdaterV2` in their worker loops. `MainFrame` polls via `wxTimer` (2s) to display a traffic-light status in the status bar.

**Tech Stack:** C++17, cURL (CurlEasyHandle RAII wrapper), wxWidgets 3.2+, std::thread, std::atomic

**Spec:** `docs/superpowers/specs/2026-06-15-Spec-NetworkMonitor-batch-network-abort-on-disconnect-v1.0.md`

---

### Task 1: Create NetworkMonitor header

**Files:**
- Create: `include/NetworkMonitor.h`

- [ ] **Write the header**

```cpp
#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>

class NetworkMonitor {
public:
    NetworkMonitor();
    ~NetworkMonitor();

    NetworkMonitor(const NetworkMonitor&) = delete;
    NetworkMonitor& operator=(const NetworkMonitor&) = delete;

    bool Start(const std::vector<std::string>& urls,
               int checkIntervalMs,
               int checkTimeoutMs);
    void Stop();

    bool IsConnected() const;

private:
    void ThreadLoop();
    bool CheckURL(const std::string& url, int timeoutMs);

    std::vector<std::string> urls_;
    int checkIntervalMs_{10000};
    int checkTimeoutMs_{5000};
    std::atomic<bool> connected_{true};
    std::atomic<bool> stopRequested_{false};
    std::thread thread_;
};
```

- [ ] **Commit**

```bash
git add include/NetworkMonitor.h
git commit -m "feat: add NetworkMonitor header"
```

---

### Task 2: Implement NetworkMonitor

**Files:**
- Create: `src/NetworkMonitor.cpp`

- [ ] **Write the implementation**

```cpp
#include "NetworkMonitor.h"
#include "CurlEasyHandle.h"
#include "Logger.h"
#include <curl/curl.h>

NetworkMonitor::NetworkMonitor() {}

NetworkMonitor::~NetworkMonitor() {
    Stop();
}

bool NetworkMonitor::Start(const std::vector<std::string>& urls,
                           int checkIntervalMs,
                           int checkTimeoutMs) {
    if (urls.empty()) {
        Logger::write(L"NetworkMonitor: no URLs configured, disabled",
                      LogLevel::WARN);
        return false;
    }
    urls_ = urls;
    checkIntervalMs_ = checkIntervalMs;
    checkTimeoutMs_ = checkTimeoutMs;
    stopRequested_ = false;
    thread_ = std::thread(&NetworkMonitor::ThreadLoop, this);
    Logger::write(L"NetworkMonitor: started with " +
                  std::to_wstring(urls_.size()) + L" URLs",
                  LogLevel::INFO);
    return true;
}

void NetworkMonitor::Stop() {
    stopRequested_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool NetworkMonitor::IsConnected() const {
    return connected_.load();
}

void NetworkMonitor::ThreadLoop() {
    while (!stopRequested_.load()) {
        bool anyOk = false;
        for (const auto& url : urls_) {
            if (stopRequested_.load()) break;
            if (CheckURL(url, checkTimeoutMs_)) {
                anyOk = true;
                break;
            }
        }
        bool prev = connected_.exchange(anyOk);
        if (prev && !anyOk) {
            Logger::write(L"[NetworkMonitor] connection LOST",
                          LogLevel::WARN);
        } else if (!prev && anyOk) {
            Logger::write(L"[NetworkMonitor] connection RESTORED",
                          LogLevel::WARN);
        }

        for (int waited = 0;
             waited < checkIntervalMs_ && !stopRequested_.load();
             waited += 200) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

bool NetworkMonitor::CheckURL(const std::string& url, int timeoutMs) {
    CurlEasyHandle curl;
    if (!curl) return false;

    curl.setopt(CURLOPT_URL, url.c_str());
    curl.setopt(CURLOPT_NOBODY, 1L);
    curl.setopt(CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    curl.setopt(CURLOPT_CONNECTTIMEOUT_MS,
                static_cast<long>(timeoutMs / 2));
    curl.setopt(CURLOPT_FOLLOWLOCATION, 1L);
    curl.setopt(CURLOPT_MAXREDIRS, 3L);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) return false;

    long httpCode = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);
    return (httpCode >= 200 && httpCode < 400);
}
```

- [ ] **Commit**

```bash
git add src/NetworkMonitor.cpp
git commit -m "feat: implement NetworkMonitor background thread"
```

---

### Task 3: Add NetworkMonitorConfig to ConfigReader

**Files:**
- Modify: `include/ConfigReader.h`

- [ ] **Add the config struct and getter**

```cpp
struct NetworkMonitorConfig {
    bool enabled{true};
    std::vector<std::string> checkUrls{
        "https://www.baidu.com",
        "https://www.qq.com",
        "https://www.taobao.com"
    };
    int checkIntervalMs{10000};
    int checkTimeoutMs{5000};
};
```

Add getter to `ConfigReader`:
```cpp
const NetworkMonitorConfig& getNetworkMonitorConfig() const;
```

Add private member:
```cpp
NetworkMonitorConfig networkMonitorConfig_;
```

- [ ] **Commit**

```bash
git add include/ConfigReader.h
git commit -m "feat: add NetworkMonitorConfig struct to ConfigReader"
```

---

### Task 4: Parse network_monitor config section

**Files:**
- Modify: `src/ConfigReader.cpp`

- [ ] **Add parsing logic in loadConfig()**

```cpp
// In ConfigReader::loadConfig(), after parsing existing sections:
if (root["network_monitor"].isObject()) {
    const auto& nm = root["network_monitor"];
    networkMonitorConfig_.enabled =
        nm.get("enabled", true).asBool();
    if (nm["check_urls"].isArray()) {
        networkMonitorConfig_.checkUrls.clear();
        for (const auto& u : nm["check_urls"]) {
            networkMonitorConfig_.checkUrls.push_back(u.asString());
        }
    }
    networkMonitorConfig_.checkIntervalMs =
        nm.get("check_interval_ms", 10000).asInt();
    networkMonitorConfig_.checkTimeoutMs =
        nm.get("check_timeout_ms", 5000).asInt();
}
```

Add a getter implementation:
```cpp
const NetworkMonitorConfig& ConfigReader::getNetworkMonitorConfig() const {
    return networkMonitorConfig_;
}
```

- [ ] **Commit**

```bash
git add src/ConfigReader.cpp
git commit -m "feat: parse network_monitor config section"
```

---

### Task 5: Update test config

**Files:**
- Modify: `bin/test_config.json`

- [ ] **Add network_monitor section**

```json
"network_monitor": {
    "enabled": true,
    "check_urls": ["https://www.baidu.com", "https://www.qq.com", "https://www.taobao.com"],
    "check_interval_ms": 10000,
    "check_timeout_ms": 5000
}
```

- [ ] **Commit**

```bash
git add bin/test_config.json
git commit -m "test: add network_monitor section to test_config.json"
```

---

### Task 6: Integrate NetworkMonitor into ProxyBatchTester

**Files:**
- Modify: `include/ProxyBatchTester.h`
- Modify: `src/ProxyBatchTester.cpp`

- [ ] **Modify ProxyBatchTester header**

Add forward declaration and constructor parameter:
```cpp
class NetworkMonitor;  // forward declare at top

// Change constructor:
ProxyBatchTester(DatabaseHelper& dbHelper, ConfigReader& config,
                 const NetworkMonitor* netMon = nullptr);
```

Add private member:
```cpp
const NetworkMonitor* netMon_{nullptr};
```

- [ ] **Modify ProxyBatchTester::run() constructor body**

```cpp
// In constructor initializer or body:
netMon_ = netMon;
```

- [ ] **Modify ProxyBatchTester::run() worker loop**

In `run()` where workers process items, add after `stopRequested_` check:
```cpp
if (netMon_ && !netMon_->IsConnected()) {
    stopRequested_ = true;
    Logger::write(L"[ProxyBatchTester] network disconnected — "
                  L"aborting batch test", LogLevel::WARN);
    break;
}
```

- [ ] **Commit**

```bash
git add include/ProxyBatchTester.h src/ProxyBatchTester.cpp
git commit -m "feat: ProxyBatchTester checks NetworkMonitor for disconnect"
```

---

### Task 7: Integrate NetworkMonitor into SubitemUpdaterV2

**Files:**
- Modify: `include/SubitemUpdaterV2.h`
- Modify: `src/SubitemUpdaterV2.cpp`

- [ ] **Modify SubitemUpdaterV2 header**

```cpp
class NetworkMonitor;  // forward declare

// Add to constructor:
SubitemUpdaterV2(DatabaseHelper& dbHelper, ConfigReader& config,
                  const NetworkMonitor* netMon = nullptr);

// Add private member:
const NetworkMonitor* netMon_{nullptr};
```

- [ ] **Modify SubitemUpdaterV2::run() update loop**

In the loop that iterates over subitems to update, add:
```cpp
if (netMon_ && !netMon_->IsConnected()) {
    Logger::write(L"[SubitemUpdaterV2] network disconnected — "
                  L"aborting update", LogLevel::WARN);
    return false;
}
```

- [ ] **Commit**

```bash
git add include/SubitemUpdaterV2.h src/SubitemUpdaterV2.cpp
git commit -m "feat: SubitemUpdaterV2 checks NetworkMonitor for disconnect"
```

---

### Task 8: AppController owns NetworkMonitor

**Files:**
- Modify: `include/ui/AppController.h`
- Modify: `src/ui/AppController.cpp`

`AppController` owns the `NetworkMonitor` so `MainFrame` can read the flag anytime via `IsNetworkConnected()`. `AutoTaskManager` receives a pointer and manages Start/Stop.

- [ ] **Add NetworkMonitor member and methods to AppController header**

```cpp
#include "NetworkMonitor.h"
#include "NetworkMonitorConfig.h"  // or forward-declare

class AppController {
    // ...
public:
    bool IsNetworkConnected() const;
    NetworkMonitor* GetNetworkMonitor();  // for AutoTaskManager

private:
    NetworkMonitor networkMonitor_;
};
```

- [ ] **Implement in AppController.cpp**

```cpp
bool AppController::IsNetworkConnected() const {
    return networkMonitor_.IsConnected();
}

NetworkMonitor* AppController::GetNetworkMonitor() {
    return &networkMonitor_;
}
```

- [ ] **Commit**

```bash
git add include/ui/AppController.h src/ui/AppController.cpp
git commit -m "feat: AppController owns NetworkMonitor instance"
```

---

### Task 9: AutoTaskManager receives NetworkMonitor pointer

**Files:**
- Modify: `include/AutoTaskManager.h`
- Modify: `src/AutoTaskManager.cpp`

The approach: AppController owns the NetworkMonitor, AutoTaskManager receives a pointer and manages Start/Stop during run().

- [ ] **Modify AutoTaskManager header**

Add forward declaration:
```cpp
class NetworkMonitor;
```

Add member and setter:
```cpp
public:
    void SetNetworkMonitor(NetworkMonitor* netMon);

private:
    NetworkMonitor* networkMonitor_{nullptr};
```

- [ ] **Modify AutoTaskManager::run()**

At the start, start the monitor:
```cpp
if (networkMonitor_) {
    const auto& nmConfig = config_.getNetworkMonitorConfig();
    if (nmConfig.enabled) {
        networkMonitor_->Start(nmConfig.checkUrls,
                               nmConfig.checkIntervalMs,
                               nmConfig.checkTimeoutMs);
    }
}
```

After each step in the pipeline, check:
```cpp
if (networkMonitor_ && !networkMonitor_->IsConnected()) {
    Logger::write(L"[AutoTask] network disconnected — "
                  L"pipeline aborted", LogLevel::ERR);
    break;
}
```

At the end, stop:
```cpp
if (networkMonitor_) {
    networkMonitor_->Stop();
}
```

Pass to child modules:
```cpp
SubitemUpdaterV2 updater(dbHelper_, config_, networkMonitor_);
// ...
ProxyBatchTester tester(dbHelper_, config_, networkMonitor_);
```

Implement setter:
```cpp
void AutoTaskManager::SetNetworkMonitor(NetworkMonitor* netMon) {
    networkMonitor_ = netMon;
}
```

- [ ] **Commit**

```bash
git add include/AutoTaskManager.h src/AutoTaskManager.cpp
git commit -m "feat: AutoTaskManager manages NetworkMonitor lifecycle via pointer"
```

---

### Task 10: Add network status to MainFrame status bar

**Files:**
- Modify: `include/ui/MainFrame.h`
- Modify: `src/ui/MainFrame.cpp`

- [ ] **Add wxTimer member to header**

```cpp
#include <wx/timer.h>

// Private members:
wxTimer networkStatusTimer_;
```

- [ ] **Initialize timer in MainFrame constructor**

```cpp
// In MainFrame::MainFrame():
networkStatusTimer_.Bind(wxEVT_TIMER, &MainFrame::OnNetworkStatusTimer, this);
networkStatusTimer_.Start(2000);  // every 2 seconds
```

- [ ] **Add timer handler declaration**

```cpp
void OnNetworkStatusTimer(wxTimerEvent& event);
```

- [ ] **Implement timer handler**

```cpp
void MainFrame::OnNetworkStatusTimer(wxTimerEvent&) {
    bool connected = appController_->IsNetworkConnected();
    if (connected) {
        SetStatusText(L" ● Network OK", 1);
    } else {
        SetStatusText(L" ✕ Network Down", 1);
    }
}
```

- [ ] **Commit**

```bash
git add include/ui/MainFrame.h src/ui/MainFrame.cpp
git commit -m "feat: add network status indicator to MainFrame status bar"
```

---

### Task 11: Write unit tests for NetworkMonitor

**Files:**
- Create: `tests/NetworkMonitorTest.cpp`

- [ ] **Write the test suite**

```cpp
#include <gtest/gtest.h>
#include "NetworkMonitor.h"

TEST(NetworkMonitorTest, StartWithInvalidUrls) {
    NetworkMonitor nm;
    std::vector<std::string> badUrls = {
        "https://nonexistent.invalid.example.com"
    };
    bool started = nm.Start(badUrls, 500, 1000);
    EXPECT_TRUE(started);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_FALSE(nm.IsConnected());
    nm.Stop();
}

TEST(NetworkMonitorTest, StartWithEmptyUrls) {
    NetworkMonitor nm;
    bool started = nm.Start({}, 500, 1000);
    EXPECT_FALSE(started);
    nm.Stop();
}

TEST(NetworkMonitorTest, StartStopNoCrash) {
    NetworkMonitor nm;
    EXPECT_TRUE(nm.Start({"https://www.baidu.com"}, 500, 1000));
    nm.Stop();
    nm.Stop();  // double stop = no crash
}

TEST(NetworkMonitorTest, IsConnectedDefaultsTrue) {
    NetworkMonitor nm;
    EXPECT_TRUE(nm.IsConnected());
}

TEST(NetworkMonitorTest, StartStopMultipleCycles) {
    NetworkMonitor nm;
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(nm.Start({"https://www.baidu.com"}, 500, 1000));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        nm.Stop();
    }
}
```

- [ ] **Ensure test is registered in CMakeLists.txt**

Add to `tests/CMakeLists.txt`:
```cmake
target_sources(test_suite PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/NetworkMonitorTest.cpp
)
```

- [ ] **Build and run the new tests**

```bash
cmake --build build --parallel 8
ctest -V
```
Expected: All tests pass.

- [ ] **Commit**

```bash
git add tests/NetworkMonitorTest.cpp tests/CMakeLists.txt
git commit -m "test: add NetworkMonitor unit tests"
```

---

### Task 12: Build and verify full system

- [ ] **Full build**

```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
```
Expected: No compile errors.

- [ ] **Run all tests**

```bash
ctest -V
```
Expected: All tests pass (9+ existing + 4 new = 13+ tests).

- [ ] **Final commit**

```bash
git add -A
git commit -m "feat: integrate NetworkMonitor across batch pipeline and UI"
```
