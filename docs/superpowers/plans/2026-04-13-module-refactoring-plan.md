# 模块化重构实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将现有代码重构为分层模块化架构，创建PortManager、XrayInstance、Logger、XrayManager、ProxyTester、UrlFetcher、ProxyBatchTester、SubscriptionUpdater模块

**架构:** 分层设计 - Foundation Layer (PortManager, XrayInstance, Logger) -> Core Layer (XrayManager, ProxyTester) -> Application Layer (UrlFetcher, ProxyBatchTester, SubscriptionUpdater)

**技术栈:** C++17, SQLite3, curl, Windows API

---

## 文件结构

### 新增头文件
- `include/PortManager.h` - 端口分配管理
- `include/XrayInstance.h` - 单个xray实例
- `include/Logger.h` - 日志记录
- `include/XrayManager.h` - xray实例集合管理
- `include/ProxyTester.h` - 代理连通性测试
- `include/UrlFetcher.h` - URL获取
- `include/ProxyBatchTester.h` - 批量代理测试

### 新增源文件
- `src/PortManager.cpp`
- `src/XrayInstance.cpp`
- `src/Logger.cpp`
- `src/XrayManager.cpp`
- `src/ProxyTester.cpp`
- `src/UrlFetcher.cpp`
- `src/ProxyBatchTester.cpp`

### 修改文件
- `src/main.cpp` - 简化为调用各模块
- `CMakeLists.txt` - 添加新模块

---

## Task 1: PortManager 基础模块

**Files:**
- Create: `include/PortManager.h`
- Create: `src/PortManager.cpp`

- [ ] **Step 1: 创建 PortManager.h**

```cpp
#ifndef PORT_MANAGER_H
#define PORT_MANAGER_H

#include <vector>
#include <string>

class PortManager {
public:
    static int findAvailable(int startPort, int maxAttempts = 100);
    static bool isInUse(int port);
    static std::vector<int> allocateRange(int startPort, int count);
};

#endif // PORT_MANAGER_H
```

- [ ] **Step 2: 创建 PortManager.cpp**

```cpp
#include "PortManager.h"
#include <winsock2.h>
#include <ws2tcpip.h>

int PortManager::findAvailable(int startPort, int maxAttempts) {
    for (int i = 0; i < maxAttempts; ++i) {
        int port = startPort + i;
        if (port > 65535) port = 10000 + (port - 10000) % 50000;
        if (!isInUse(port)) return port;
    }
    return -1;
}

bool PortManager::isInUse(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    closesocket(sock);
    return result == SOCKET_ERROR;
}

std::vector<int> PortManager::allocateRange(int startPort, int count) {
    std::vector<int> ports;
    for (int i = 0; i < count; ++i) {
        int port = findAvailable(startPort + i * 2, 100);
        if (port > 0) ports.push_back(port);
    }
    return ports;
}
```

- [ ] **Step 3: 提交**

```bash
git add include/PortManager.h src/PortManager.cpp
git commit -m "feat: add PortManager module"
```

---

## Task 2: Logger 基础模块

**Files:**
- Create: `include/Logger.h`
- Create: `src/Logger.cpp`

- [ ] **Step 1: 创建 Logger.h**

```cpp
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>

class Logger {
public:
    static void init(const std::string& logDir, const std::string& prefix);
    static void write(const std::string& msg);
    static void writeTimestamp(const std::string& msg);
    static void flush();
    static void close();
    static bool isEnabled();

private:
    static std::string logDir_;
    static std::string prefix_;
    static std::ofstream* outFile_;
    static std::mutex mutex_;
    static bool enabled_;
};

#endif // LOGGER_H
```

- [ ] **Step 2: 创建 Logger.cpp**

```cpp
#include "Logger.h"
#include <filesystem>
#include <chrono>
#include <iomanip>

std::string Logger::logDir_;
std::string Logger::prefix_;
std::ofstream* Logger::outFile_ = nullptr;
std::mutex Logger::mutex_;
bool Logger::enabled_ = false;

void Logger::init(const std::string& logDir, const std::string& prefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    logDir_ = logDir;
    prefix_ = prefix;
    
    std::filesystem::path dir(logDir);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directory(dir);
    }
    
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&t));
    
    std::string filename = logDir + "/" + prefix + "_" + timestamp + ".log";
    outFile_ = new std::ofstream(filename, std::ios::out | std::ios::trunc);
    enabled_ = outFile_->is_open();
}

void Logger::write(const std::string& msg) {
    if (!enabled_ || !outFile_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    *outFile_ << msg << std::endl;
    outFile_->flush();
}

void Logger::writeTimestamp(const std::string& msg) {
    if (!enabled_ || !outFile_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&t));
    
    *outFile_ << "[" << timestamp << "] " << msg << std::endl;
    outFile_->flush();
}

void Logger::flush() {
    if (outFile_ && outFile_->is_open()) {
        std::lock_guard<std::mutex> lock(mutex_);
        outFile_->flush();
    }
}

void Logger::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (outFile_ && outFile_->is_open()) {
        outFile_->close();
    }
    enabled_ = false;
}

bool Logger::isEnabled() {
    return enabled_;
}
```

- [ ] **Step 3: 提交**

```bash
git add include/Logger.h src/Logger.cpp
git commit -m "feat: add Logger module"
```

---

## Task 3: XrayInstance 基础模块

**Files:**
- Create: `include/XrayInstance.h`
- Create: `src/XrayInstance.cpp`

- [ ] **Step 1: 创建 XrayInstance.h**

```cpp
#ifndef XRAY_INSTANCE_H
#define XRAY_INSTANCE_H

#include <string>
#include <windows.h>

class XrayInstance {
public:
    XrayInstance(const std::string& xrayPath, int socksPort, int apiPort, const std::string& configDir);
    ~XrayInstance();
    
    bool start();
    void stop();
    bool isRunning() const;
    int getSocksPort() const;
    int getApiPort() const;
    std::string getConfigPath() const;

private:
    std::string xrayPath_;
    int socksPort_;
    int apiPort_;
    std::string configPath_;
    HANDLE processHandle_;
    HANDLE jobObject_;
    bool running_;
    
    bool createConfigFile();
};

#endif // XRAY_INSTANCE_H
```

- [ ] **Step 2: 创建 XrayInstance.cpp**

```cpp
#include "XrayInstance.h"
#include <fstream>
#include <thread>
#include <chrono>

XrayInstance::XrayInstance(const std::string& xrayPath, int socksPort, int apiPort, const std::string& configDir)
    : xrayPath_(xrayPath), socksPort_(socksPort), apiPort_(apiPort), running_(false) {
    
    configPath_ = configDir + "/xray_config_" + std::to_string(socksPort) + ".json";
    processHandle_ = nullptr;
    jobObject_ = nullptr;
}

XrayInstance::~XrayInstance() {
    stop();
}

bool XrayInstance::start() {
    if (!createConfigFile()) return false;
    
    jobObject_ = CreateJobObjectA(NULL, NULL);
    if (!jobObject_) return false;
    
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimit = {};
    jobLimit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(jobObject_, JobObjectExtendedLimitInformation, &jobLimit, sizeof(jobLimit));
    
    std::string cmd = "\"" + xrayPath_ + "\" run -c \"" + configPath_ + "\"";
    
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    
    if (!CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
        return false;
    }
    
    AssignProcessToJobObject(jobObject_, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    processHandle_ = pi.hProcess;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    running_ = true;
    return true;
}

void XrayInstance::stop() {
    if (jobObject_) {
        TerminateJobObject(jobObject_, 0);
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
    }
    running_ = false;
}

bool XrayInstance::isRunning() const {
    return running_;
}

int XrayInstance::getSocksPort() const {
    return socksPort_;
}

int XrayInstance::getApiPort() const {
    return apiPort_;
}

std::string XrayInstance::getConfigPath() const {
    return configPath_;
}

bool XrayInstance::createConfigFile() {
    std::string content = R"({
        "log": {"loglevel": "warning"},
        "api": {
            "tag": "api",
            "services": ["HandlerService", "LoggerService", "StatsService"]
        },
        "stats": {},
        "policy": {
            "levels": {"0": {"statsUserUplink": true, "statsUserDownlink": true}},
            "system": {"statsInboundUplink": true, "statsInboundDownlink": true, "statsOutboundUplink": true, "statsOutboundDownlink": true}
        },
        "inbounds": [
            {"tag": "api", "listen": "127.0.0.1", "port": )" + std::to_string(apiPort_) + R"(, "protocol": "dokodemo-door", "settings": {"address": "127.0.0.1"}},
            {"tag": "socks-in", "listen": "127.0.0.1", "port": )" + std::to_string(socksPort_) + R"(, "protocol": "mixed", "settings": {"auth": "noauth", "udp": true}}
        ],
        "outbounds": [{"tag": "direct", "protocol": "freedom"}],
        "routing": {
            "domainStrategy": "AsIs",
            "rules": [
                {"type": "field", "inboundTag": ["api"], "outboundTag": "api"},
                {"type": "field", "outboundTag": "proxy", "network": "tcp"}
            ]
        }
    })";
    
    std::ofstream out(configPath_);
    if (!out.is_open()) return false;
    out << content;
    out.close();
    return true;
}
```

- [ ] **Step 3: 提交**

```bash
git add include/XrayInstance.h src/XrayInstance.cpp
git commit -m "feat: add XrayInstance module"
```

---

## Task 4: XrayManager 核心模块

**Files:**
- Create: `include/XrayManager.h`
- Create: `src/XrayManager.cpp`

- [ ] **Step 1: 创建 XrayManager.h**

```cpp
#ifndef XRAY_MANAGER_H
#define XRAY_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include "XrayInstance.h"

class XrayManager {
public:
    XrayManager(const std::string& xrayPath, const std::string& configDir);
    ~XrayManager();
    
    int start(int count, int startPort, int apiPort);
    void stopAll();
    XrayInstance* getInstance(int index);
    int getInstanceCount() const;
    std::vector<std::pair<int, int>> getPortPairs();

private:
    std::vector<std::unique_ptr<XrayInstance>> instances_;
    std::string xrayPath_;
    std::string configDir_;
};

#endif // XRAY_MANAGER_H
```

- [ ] **Step 2: 创建 XrayManager.cpp**

```cpp
#include "XrayManager.h"
#include "PortManager.h"
#include <algorithm>

XrayManager::XrayManager(const std::string& xrayPath, const std::string& configDir)
    : xrayPath_(xrayPath), configDir_(configDir) {}

XrayManager::~XrayManager() {
    stopAll();
}

int XrayManager::start(int count, int startPort, int apiPort) {
    std::vector<int> usedPorts;
    int actualCount = 0;
    
    for (int i = 0; i < count; ++i) {
        int socksPort = PortManager::findAvailable(startPort + i * 2, 1000, usedPorts);
        int apiPortAddr = PortManager::findAvailable(apiPort + i * 2, 1000, usedPorts);
        
        if (socksPort <= 0 || apiPortAddr <= 0) break;
        
        usedPorts.push_back(socksPort);
        usedPorts.push_back(apiPortAddr);
        
        auto instance = std::make_unique<XrayInstance>(xrayPath_, socksPort, apiPortAddr, configDir_);
        if (instance->start()) {
            instances_.push_back(std::move(instance));
            actualCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    return actualCount;
}

void XrayManager::stopAll() {
    for (auto& inst : instances_) {
        inst->stop();
    }
    instances_.clear();
}

XrayInstance* XrayManager::getInstance(int index) {
    if (index >= 0 && index < static_cast<int>(instances_.size())) {
        return instances_[index].get();
    }
    return nullptr;
}

int XrayManager::getInstanceCount() const {
    return static_cast<int>(instances_.size());
}

std::vector<std::pair<int, int>> XrayManager::getPortPairs() {
    std::vector<std::pair<int, int>> pairs;
    for (auto& inst : instances_) {
        pairs.push_back({inst->getSocksPort(), inst->getApiPort()});
    }
    return pairs;
}
```

- [ ] **Step 3: 提交**

```bash
git add include/XrayManager.h src/XrayManager.cpp
git commit -m "feat: add XrayManager module"
```

---

## Task 5: ProxyTester 核心模块

**Files:**
- Create: `include/ProxyTester.h`
- Create: `src/ProxyTester.cpp`

- [ ] **Step 1: 创建 ProxyTester.h**

```cpp
#ifndef PROXY_TESTER_H
#define PROXY_TESTER_H

#include <string>
#include <curl/curl.h>

class XrayManager;

struct TestResult {
    bool success;
    long latencyMs;
    std::string errorMsg;
};

class ProxyTester {
public:
    ProxyTester(XrayManager* manager, const std::string& testUrl, int timeoutMs);
    ~ProxyTester();
    
    TestResult test(int socksPort);
    XrayManager* getManager() const;

private:
    XrayManager* manager_;
    std::string testUrl_;
    int timeoutMs_;
};

#endif // PROXY_TESTER_H
```

- [ ] **Step 2: 创建 ProxyTester.cpp**

```cpp
#include "ProxyTester.h"
#include "XrayManager.h"
#include "XrayApi.h"
#include "ConfigGenerator.h"
#include <chrono>

ProxyTester::ProxyTester(XrayManager* manager, const std::string& testUrl, int timeoutMs)
    : manager_(manager), testUrl_(testUrl), timeoutMs_(timeoutMs) {}

ProxyTester::~ProxyTester() {}

TestResult ProxyTester::test(int socksPort) {
    TestResult result;
    result.success = false;
    result.latencyMs = -1;
    result.errorMsg = "";
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.errorMsg = "curl_init_failed";
        return result;
    }
    
    std::string proxyUrl = "http://127.0.0.1:" + std::to_string(socksPort);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, testUrl_.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs_);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    auto startTime = std::chrono::high_resolution_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto endTime = std::chrono::high_resolution_clock::now();
    
    double totalTime = 0;
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
    result.latencyMs = static_cast<long>(totalTime * 1000);
    
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        result.errorMsg = curl_easy_strerror(res);
    } else if (responseCode != 200 && responseCode != 204) {
        result.errorMsg = "http_" + std::to_string(responseCode);
    }
    
    result.success = (res == CURLE_OK && (responseCode == 200 || responseCode == 204));
    return result;
}

XrayManager* ProxyTester::getManager() const {
    return manager_;
}
```

- [ ] **Step 3: 提交**

```bash
git add include/ProxyTester.h src/ProxyTester.cpp
git commit -m "feat: add ProxyTester module"
```

---

## Task 6: UrlFetcher 应用模块

**Files:**
- Create: `include/UrlFetcher.h`
- Create: `src/UrlFetcher.cpp`

- [ ] **Step 1: 创建 UrlFetcher.h**

```cpp
#ifndef URL_FETCHER_H
#define URL_FETCHER_H

#include <string>

class ProxyTester;

class UrlFetcher {
public:
    UrlFetcher(ProxyTester* tester);
    
    std::string fetch(const std::string& url);
    std::string fetchViaProxy(const std::string& url, int socksPort);

private:
    ProxyTester* tester_;
    
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

#endif // URL_FETCHER_H
```

- [ ] **Step 2: 创建 UrlFetcher.cpp**

```cpp
#include "UrlFetcher.h"
#include "ProxyTester.h"
#include <curl/curl.h>
#include <sstream>

UrlFetcher::UrlFetcher(ProxyTester* tester) : tester_(tester) {}

size_t UrlFetcher::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string UrlFetcher::fetch(const std::string& url) {
    std::string result;
    
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    return result;
}

std::string UrlFetcher::fetchViaProxy(const std::string& url, int socksPort) {
    if (tester_) {
        auto result = tester_->test(socksPort);
        if (!result.success) {
            return "";
        }
    }
    
    std::string result;
    
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    
    std::string proxyUrl = "socks5://127.0.0.1:" + std::to_string(socksPort);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    return result;
}
```

- [ ] **Step 3: 提交**

```bash
git add include/UrlFetcher.h src/UrlFetcher.cpp
git commit -m "feat: add UrlFetcher module"
```

---

## Task 7: ProxyBatchTester 应用模块

**Files:**
- Create: `include/ProxyBatchTester.h`
- Create: `src/ProxyBatchTester.cpp`

- [ ] **Step 1: 创建 ProxyBatchTester.h**

```cpp
#ifndef PROXY_BATCH_TESTER_H
#define PROXY_BATCH_TESTER_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include "Profileitem.h"
#include "Profileexitem.h"
#include "ConfigReader.h"
#include "XrayManager.h"
#include "ProxyTester.h"

class ProxyBatchTester {
public:
    ProxyBatchTester(sqlite3* db, const config::AppConfig& config);
    ~ProxyBatchTester();
    
    bool run();
    bool runWithSubId(const std::string& subId);

private:
    std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
    int calculateXrayInstanceCount(int proxyCount);
    bool startXrayInstances(int count);
    void testProxiesMultiThreaded();
    void printSummary();
    
    sqlite3* db_;
    config::AppConfig config_;
    XrayManager* xrayManager_;
    ProxyTester* proxyTester_;
    int totalProxies_;
    int successCount_;
    int failedCount_;
};

#endif // PROXY_BATCH_TESTER_H
```

- [ ] **Step 2: 创建 ProxyBatchTester.cpp** (简化版，后续完善)

```cpp
#include "ProxyBatchTester.h"
#include "ConfigGenerator.h"
#include "Logger.h"
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>

ProxyBatchTester::ProxyBatchTester(sqlite3* db, const config::AppConfig& config)
    : db_(db), config_(config), totalProxies_(0), successCount_(0), failedCount_(0) {
    
    std::string baseDir = std::filesystem::current_path().string();
    std::string configDir = baseDir + "/config";
    
    xrayManager_ = new XrayManager(config_.xray_executable, configDir);
    proxyTester_ = new ProxyTester(xrayManager_, config_.test_url, config_.test_timeout_ms);
}

ProxyBatchTester::~ProxyBatchTester() {
    delete proxyTester_;
    delete xrayManager_;
}

std::vector<db::models::Profileitem> ProxyBatchTester::loadProxies(const std::string& subId) {
    config::ConfigGenerator configGen(db_);
    std::string sql = config_.sql_query;
    
    if (!subId.empty()) {
        size_t pos = sql.find("{subid}");
        if (pos != std::string::npos) {
            sql.replace(pos, 7, subId);
        }
    }
    
    return configGen.loadProfiles(sql);
}

int ProxyBatchTester::calculateXrayInstanceCount(int proxyCount) {
    int maxWorkers = config_.xray_workers;
    return std::min(proxyCount, maxWorkers);
}

bool ProxyBatchTester::startXrayInstances(int count) {
    int actual = xrayManager_->start(count, config_.xray_start_port, config_.xray_api_port);
    return actual > 0;
}

void ProxyBatchTester::testProxiesMultiThreaded() {
    // 实现多线程测试逻辑
    // 与现有main.cpp中的workerThreadFunc类似
}

void ProxyBatchTester::printSummary() {
    std::cout << "Total: " << totalProxies_ << ", Success: " << successCount_ << ", Failed: " << failedCount_ << std::endl;
}

bool ProxyBatchTester::run() {
    auto proxies = loadProxies();
    totalProxies_ = proxies.size();
    
    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        std::cerr << "Failed to start xray instances" << std::endl;
        return false;
    }
    
    testProxiesMultiThreaded();
    printSummary();
    
    xrayManager_->stopAll();
    return true;
}

bool ProxyBatchTester::runWithSubId(const std::string& subId) {
    auto proxies = loadProxies(subId);
    totalProxies_ = proxies.size();
    
    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        std::cerr << "Failed to start xray instances" << std::endl;
        return false;
    }
    
    testProxiesMultiThreaded();
    printSummary();
    
    xrayManager_->stopAll();
    return true;
}
```

- [ ] **Step 3: 提交**

```bash
git add include/ProxyBatchTester.h src/ProxyBatchTester.cpp
git commit -m "feat: add ProxyBatchTester module"
```

---

## Task 8: SubscriptionUpdater 应用模块（两阶段更新+去重）

**Files:**
- Modify: `include/SubitemUpdater.h`
- Modify: `src/SubitemUpdater.cpp`

- [ ] **Step 1: 更新 SubitemUpdater.h 添加去重方法**

在 `private:` 部分添加:
```cpp
// 解析订阅内容为代理列表（去重）
// 去重依据：Address + Port + Network 唯一
std::vector<Profileitem> parseSubscription(const std::string& content, const std::string& subId);

// 写入代理列表（去重后）
// 已存在则更新，不存在则插入
bool updateProfileItems(const std::string& subid, const std::vector<Profileitem>& profiles);
```

- [ ] **Step 2: 实现去重逻辑**

```cpp
std::vector<Profileitem> SubitemUpdater::parseSubscription(const std::string& content, const std::string& subId) {
    auto profiles = decodeSubscriptionContent(content, subId);
    
    // 去重: Address + Port + Network
    std::map<std::tuple<std::string, int, std::string>, Profileitem> uniqueMap;
    for (auto& p : profiles) {
        std::string network = p.network.empty() ? "tcp" : p.network;
        auto key = std::make_tuple(p.address, p.port, network);
        uniqueMap[key] = p;
    }
    
    std::vector<Profileitem> result;
    for (auto& pair : uniqueMap) {
        result.push_back(pair.second);
    }
    return result;
}

bool SubitemUpdater::updateProfileItems(const std::string& subId, const std::vector<Profileitem>& profiles) {
    ProfileitemDAO dao(db_);
    for (const auto& p : profiles) {
        // 查询是否存在
        auto existing = dao.findByAddressPortNetwork(p.address, p.port, p.network);
        if (existing.has_value()) {
            dao.update(existing.value().indexid, p);
        } else {
            dao.insert(p);
        }
    }
    return true;
}
```

- [ ] **Step 3: 实现两阶段更新**

```cpp
std::vector<std::string> SubitemUpdater::phase1DirectUpdate() {
    std::vector<std::string> failed;
    
    auto subs = getAllSubscriptions();
    for (const auto& sub : subs) {
        std::string content = fetchUrl(sub.url);
        if (content.empty()) {
            failed.push_back(sub.id);
            continue;
        }
        
        auto profiles = parseSubscription(content, sub.id);
        updateProfileItems(sub.id, profiles);
        directSuccess_.push_back({sub.id, true, Phase::Direct, 0, ""});
    }
    return failed;
}

void SubitemUpdater::phase2ProxyUpdate(const std::vector<std::string>& failedSubIds) {
    auto proxies = selectAvailableProxies(3); // 最多3个代理
    
    for (const auto& subId : failedSubIds) {
        bool foundWorking = false;
        
        for (const auto& proxy : proxies) {
            // 启动xray实例使用该代理
            int socksPort = startXrayWithProxy(proxy);
            if (socksPort <= 0) continue;
            
            // 测试连通性
            long latency;
            std::string error;
            if (testProxyConnectivity(socksPort, latency, error)) {
                // 通过代理获取订阅
                std::string content = fetchUrlViaProxy(getSubscriptionUrl(subId), socksPort);
                if (!content.empty()) {
                    auto profiles = parseSubscription(content, subId);
                    updateProfileItems(subId, profiles);
                    proxySuccess_.push_back({subId, true, Phase::Proxy, latency, ""});
                    foundWorking = true;
                }
            }
            
            stopXray();
            if (foundWorking) break;
        }
        
        if (!foundWorking) {
            failed_.push_back({subId, false, Phase::Proxy, 0, "no_working_proxy"});
        }
    }
}
```

- [ ] **Step 4: 提交**

```bash
git add include/SubitemUpdater.h src/SubitemUpdater.cpp
git commit -m "feat: add two-phase update and deduplication to SubscriptionUpdater"
```

---

## Task 9: 更新 CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 添加新源文件到构建**

```cmake
set(SOURCES
    src/main.cpp
    src/ConfigReader.cpp
    src/ConfigGenerator.cpp
    src/SubitemUpdater.cpp
    src/XrayApi.cpp
    src/PortManager.cpp      # 新增
    src/Logger.cpp          # 新增
    src/XrayInstance.cpp    # 新增
    src/XrayManager.cpp     # 新增
    src/ProxyTester.cpp     # 新增
    src/UrlFetcher.cpp      # 新增
    src/ProxyBatchTester.cpp # 新增
)
```

- [ ] **Step 2: 提交**

```bash
git add CMakeLists.txt
git commit -m "build: add new module source files to CMakeLists.txt"
```

---

## Task 10: 重构 main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 简化 main.cpp，调用各模块**

```cpp
#include <iostream>
#include <sqlite3.h>
#include <curl/curl.h>
#include "ConfigReader.h"
#include "ProxyBatchTester.h"
#include "SubitemUpdater.h"

int main(int argc, char* argv[]) {
    std::string configPath = "config.json";
    std::string subId;
    std::string commandMode;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) configPath = argv[++i];
        else if (arg == "--test-sub" && i + 1 < argc) { subId = argv[++i]; commandMode = "test-sub"; }
        else if (arg == "--update" && i + 1 < argc) { subId = argv[++i]; commandMode = "update"; }
        else if (arg == "--update-all") { commandMode = "update-all"; }
        else if (arg == "-h") { /* help */ return 0; }
    }
    
    auto appConfig = config::ConfigReader::load(configPath);
    if (!appConfig) {
        std::cerr << "Failed to load config" << std::endl;
        return 1;
    }
    
    sqlite3* db = nullptr;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open database" << std::endl;
        return 1;
    }
    
    bool result = true;
    
    if (commandMode == "test-sub" || commandMode.empty()) {
        ProxyBatchTester tester(db, *appConfig);
        if (!subId.empty()) {
            result = tester.runWithSubId(subId);
        } else {
            result = tester.run();
        }
    } else if (commandMode == "update" || commandMode == "update-all") {
        SubitemUpdater updater(db, *appConfig, std::filesystem::current_path().string());
        if (!subId.empty()) {
            result = updater.runSingle(subId);
        } else {
            result = updater.run();
        }
    }
    
    sqlite3_close(db);
    curl_global_cleanup();
    
    return result ? 0 : 1;
}
```

- [ ] **Step 2: 编译测试**

```bash
cd build
cmake --build . --parallel 8
./validproxy.exe
```

- [ ] **Step 3: 提交**

```bash
git add src/main.cpp
git commit -m "refactor: simplify main.cpp to use new modules"
```

---

## Task 11: 配置 config.json 扩展

**Files:**
- Modify: `config.json`

- [ ] **Step 1: 添加 sql_by_subid 配置**

```json
{
    "database": {
        "path": "E:/v2rayN-windows-64/guiConfigs/guiNDB.db",
        "sql": "SELECT ... FROM ProfileItem WHERE ...",
        "sql_by_subid": "SELECT ... FROM ProfileItem WHERE Subid = '{subid}'"
    },
    "subscription": {
        "update_enabled": true,
        "priority_mode": "direct_first"
    }
}
```

- [ ] **Step 2: 提交**

```bash
git add config.json
git commit -m "config: add sql_by_subid and priority_mode settings"
```