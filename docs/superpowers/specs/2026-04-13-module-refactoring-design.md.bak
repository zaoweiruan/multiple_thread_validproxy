# validproxy 模块化重构设计

## 1. 概述

### 目标
将现有代码重构为分层模块化架构，提升代码可维护性，便于后续扩展新功能。

### 现有问题
- main.cpp 包含所有功能（687行），职责混杂
- xray启动、代理测试、订阅更新都在主函数中
- 缺乏清晰的分层和模块边界

---

## 2. 架构设计

### 2.1 分层结构

```
┌──────────────────────────────────────────────────────────────────┐
│  Presentation Layer (展示层)                                     │
│  main.cpp                                                         │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│  Application Layer (应用层)                                      │
│  ProxyBatchTester  │  SubscriptionUpdater                         │
└──────────────────────────────────────────────────────────────────┘
          │                               │
          ▼                               ▼
┌─────────────────────┐         ┌─────────────────┐
│   UrlFetcher        │         │   UrlFetcher    │
│   ProxyTester       │         │   ProxyTester   │
│   XrayManager       │         │   XrayManager   │
│   ConfigReader      │         │   ConfigReader  │
└─────────────────────┘         └─────────────────┘
          │                               │
          └───────────────────────────────┘
                                     ▼
┌──────────────────────────────────────────────────────────────────┐
│  Foundation Layer (基础层)                                       │
│  PortManager  │  XrayInstance  │  Logger  │  XrayApi           │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 模块列表

| 层级 | 模块名 | 职责 |
|------|--------|------|
| **Foundation** | PortManager | 端口分配与管理 |
| **Foundation** | XrayInstance | 单个xray实例生命周期 |
| **Foundation** | Logger | 日志写入 |
| **Foundation** | XrayApi | xray API调用 |
| **Core** | XrayManager | 多xray实例协调管理 |
| **Core** | ProxyTester | 代理连通性测试 |
| **Core** | ConfigReader | 读取config.json |
| **Application** | ProxyBatchTester | 批量代理测试 |
| **Application** | UrlFetcher | URL获取（直连/代理） |
| **Application** | SubscriptionUpdater | 订阅更新 |

---

## 3. 详细设计

### 3.1 Foundation Layer

#### PortManager

```cpp
class PortManager {
public:
    static int findAvailable(int startPort, int maxAttempts = 100);
    static bool isInUse(int port);
    static std::vector<int> allocateRange(int startPort, int count);
};
```

#### XrayInstance

```cpp
class XrayInstance {
public:
    XrayInstance(const std::string& xrayPath, int socksPort, int apiPort);
    bool start();
    void stop();
    bool isRunning() const;
    int getSocksPort() const;
    int getApiPort() const;

private:
    std::string xrayPath_;
    int socksPort_;
    int apiPort_;
    HANDLE processHandle_;
    HANDLE jobObject_;
};
```

#### Logger

```cpp
class Logger {
public:
    static void init(const std::string& logDir);
    static void write(const std::string& msg);
    static void writeTimestamp(const std::string& msg);
};
```

### 3.2 Core Layer

#### XrayManager

```cpp
class XrayManager {
public:
    XrayManager(const std::string& xrayPath, const std::string& configDir);
    
    // 启动指定数量的xray实例
    // @param count 实例数量
    // @param startPort 起始端口
    // @param apiPort 起始API端口
    // @return 实际启动的实例数量
    int start(int count, int startPort, int apiPort);
    
    void stopAll();
    XrayInstance* getInstance(int index);
    int getInstanceCount() const;
    std::vector<std::pair<int, int>> getPortPairs(); // {socks, api}

private:
    std::vector<std::unique_ptr<XrayInstance>> instances_;
    std::string xrayPath_;
    std::string configDir_;
};
```

#### ProxyTester

```cpp
class ProxyTester {
public:
    ProxyTester(XrayManager* manager, const std::string& testUrl, int timeoutMs);
    
    // 测试代理连通性
    // @param socksPort SOCKS端口
    // @return {success, latencyMs, errorMsg}
    TestResult test(int socksPort);
    
    // 获取可用实例
    XrayInstance* getAvailableInstance();

private:
    XrayManager* manager_;
    std::string testUrl_;
    int timeoutMs_;
};
```

#### ConfigReader

（已存在，无需修改）

### 3.3 Application Layer

#### ProxyBatchTester

```cpp
class ProxyBatchTester {
public:
    ProxyBatchTester(sqlite3* db, const AppConfig& config);
    
    // 执行批量测试
    bool run();
    
    // 测试指定订阅的代理
    // @param subId 订阅ID，将替换sql中的{subid}占位符
    bool runWithSubId(const std::string& subId);

private:
    // 1. 获取代理列表
    std::vector<Profileitem> loadProxies(const std::string& subId = "");
    
    // 2. 确定xray实例数量（不超过config.xray_workers）
    int calculateXrayInstanceCount(int proxyCount);
    
    // 3. 启动xray实例
    bool startXrayInstances(int count);
    
    // 4. 多线程测试
    void testProxiesMultiThreaded();
    
    // 5. 输出统计
    void printSummary();
    
    sqlite3* db_;
    AppConfig config_;
    XrayManager* xrayManager_;
    int totalProxies_;
    int successCount_;
    int failedCount_;
};
```

#### UrlFetcher

```cpp
class UrlFetcher {
public:
    UrlFetcher(ProxyTester* tester, const std::string& baseDir);
    
    // 直连获取
    std::string fetch(const std::string& url);
    
    // 通过代理获取（先验证代理可用）
    std::string fetchViaProxy(const std::string& url, int socksPort);

private:
    ProxyTester* tester_;
    std::string baseDir_;
};
```

#### SubscriptionUpdater

```cpp
struct UpdateRecord {
    std::string subId;
    bool success;
    Phase phase;           // Phase::Direct 或 Phase::Proxy
    int latencyMs;
    std::string errorMsg;
};

class SubscriptionUpdater {
public:
    SubscriptionUpdater(sqlite3* db, const AppConfig& config, const std::string& baseDir);
    
    // 执行两阶段更新
    bool run();
    
    // 更新单个订阅
    bool runSingle(const std::string& subId);

private:
    // 解析订阅内容为代理列表（去重）
    // 去重依据：Address + Port + Network 唯一
    std::vector<Profileitem> parseSubscription(const std::string& content, const std::string& subId);
    
    // 写入代理列表（去重后）
    // 已存在则更新，不存在则插入
    bool updateProfileItems(const std::string& subid, const std::vector<Profileitem>& profiles);
    
    // 第一阶段：批量直连更新
    std::vector<std::string> phase1DirectUpdate();
    
    // 第二阶段：批量代理更新（仅对失败列表）
    void phase2ProxyUpdate(const std::vector<std::string>& failedSubIds);
    
    // 选取可用代理（delay > 0，按delay升序）
    std::vector<Profileitem> selectAvailableProxies(int maxCount);
    
    // 输出统计
    void writeSummary();
    
    sqlite3* db_;
    AppConfig config_;
    std::string baseDir_;
    std::vector<UpdateRecord> directSuccess_;
    std::vector<UpdateRecord> proxySuccess_;
    std::vector<UpdateRecord> failed_;
};
```

### 去重规则

| 字段 | 说明 |
|------|------|
| Address | 代理服务器地址 |
| Port | 代理服务器端口 |
| Network | 传输协议 (tcp/udp/ws) |

- **已存在** (Address+Port+Network 相同): 更新记录
- **不存在**: 插入新记录

---

## 4. 配置扩展

### 4.1 config.json

```json
{
    "database": {
        "path": "E:/v2rayN-windows-64/guiConfigs/guiNDB.db",
        "sql": "SELECT ... FROM ProfileItem WHERE Subid NOT IN (...)",
        "sql_by_subid": "SELECT ... FROM ProfileItem WHERE Subid = '{subid}'"
    },
    "xray": {
        "executable": "E:/v2rayN-windows-64/bin/xray/xray.exe",
        "workers": 4,
        "start_port": 2080,
        "api_port": 11080
    },
    "test": {
        "url": "https://www.google.com/generate_204",
        "timeout_ms": 5000
    },
    "subscription": {
        "update_enabled": true,
        "priority_mode": "direct_first"
    }
}
```

### 4.2 新增配置项

| 配置项 | 可选值 | 默认值 | 说明 |
|--------|--------|--------|------|
| `database.sql_by_subid` | SQL模板 | - | 运行时替换 `{subid}` |
| `subscription.priority_mode` | `direct_first`, `proxy_first`, `direct_only` | `direct_first` | 更新优先级模式 |

---

## 5. 命令行接口

```bash
# 默认批量测试模式（使用config.json中的sql）
validproxy.exe

# 测试指定订阅的代理
validproxy.exe --test-sub '5544178410297751350'

# 更新单个订阅
validproxy.exe --update 'subId'

# 更新所有订阅（两阶段）
validproxy.exe --update-all
```

---

## 6. 调用关系

```
main.cpp
    │
    ├─► ConfigReader ────────────────────────────────────────────┐
    │                                                               │
    ├─► ProxyBatchTester                                          │
    │       │                                                      │
    │       ├─► ConfigReader                                       │
    │       ├─► loadProxies() → ProfileitemDAO                    │
    │       ├─► XrayManager::start()                              │
    │       │       └─► XrayInstance × N                         │
    │       │               ├─► PortManager                        │
    │       │               └─► XrayApi                            │
    │       ├─► testProxiesMultiThreaded()                        │
    │       │       └─► ProxyTester                                │
    │       │               ├─► XrayManager                        │
    │       │               ├─► XrayApi                             │
    │       │               └─► curl                                │
    │       └─► printSummary() → ProfileexitemDAO                │
    │                                                               │
    └─► SubscriptionUpdater                                        │
            │                                                      │
            ├─► phase1DirectUpdate()                              │
            │       └─► UrlFetcher::fetch()                       │
            │               └─► curl                              │
            │                                                      │
            ├─► selectAvailableProxies()                          │
            │       └─► ProfileitemDAO (delay>0)                  │
            │                                                      │
            ├─► phase2ProxyUpdate()                               │
            │       ├─► ProxyTester::test()                        │
            │       │       └─► XrayManager                        │
            │       └─► UrlFetcher::fetchViaProxy()                │
            │               └─► ProxyTester                       │
            │                                                      │
            └─► writeSummary() → Logger
```

---

## 7. 文件规划

### 7.1 新增头文件

| 文件 | 位置 |
|------|------|
| PortManager.h | include/ |
| XrayInstance.h | include/ |
| Logger.h | include/ |
| XrayManager.h | include/ |
| ProxyTester.h | include/ |
| ProxyBatchTester.h | include/ |
| UrlFetcher.h | include/ |

### 7.2 新增源文件

| 文件 | 位置 |
|------|------|
| PortManager.cpp | src/ |
| XrayInstance.cpp | src/ |
| Logger.cpp | src/ |
| XrayManager.cpp | src/ |
| ProxyTester.cpp | src/ |
| ProxyBatchTester.cpp | src/ |
| UrlFetcher.cpp | src/ |

### 7.3 修改文件

| 文件 | 修改内容 |
|------|----------|
| main.cpp | 简化为调用ProxyBatchTester和SubscriptionUpdater |
| CMakeLists.txt | 添加新模块的编译单元 |

---

## 8. 实现顺序

1. **Foundation Layer**: PortManager → XrayInstance → Logger → XrayApi
2. **Core Layer**: XrayManager → ProxyTester → ConfigReader
3. **Application Layer**: UrlFetcher → ProxyBatchTester → SubscriptionUpdater
4. **集成**: 修改main.cpp，添加CMake配置