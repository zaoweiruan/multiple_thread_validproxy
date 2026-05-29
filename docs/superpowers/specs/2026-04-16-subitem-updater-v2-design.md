# 2026-04-16-subitem-updater-v2-design.md

## 1. 概述

新建 `SubitemUpdaterV2` 类，复用 `ProxyFinder` 模块的代理查找功能，消除原有代码中的重复逻辑。保持与原 `SubitemUpdater` 兼容的接口，逐步替代原有功能。

## 2. 目标

- 复用 `ProxyFinder::findFirstWorkingProxy()` 获取可用代理端口
- 保持与原 `SubitemUpdater` 完全兼容的接口
- 支持配置中的 `priority_mode` 策略 (direct_first/proxy_first/direct_only)
- 先新建再替换，避免大规模修改导致编译失败

## 3. 设计

### 3.1 类结构

```cpp
// include/SubitemUpdaterV2.h
#ifndef SUBITEM_UPDATER_V2_H
#define SUBITEM_UPDATER_V2_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include <fstream>
#include <iostream>
#include <optional>

#include "Subitem.h"
#include "Profileitem.h"
#include "XrayManager.h"
#include "ProxyFinder.h"
#include "ConfigReader.h"

namespace update {

class SubitemUpdaterV2 {
public:
    SubitemUpdaterV2(sqlite3* db,
                    const std::string& xrayPath,
                    const config::AppConfig& config,
                    std::ofstream* logOut = nullptr,
                    const std::string& baseDir = "");

    ~SubitemUpdaterV2() {
        cleanupXray();
    }

    // 与原 SubitemUpdater 兼容的接口
    bool run();
    bool runSingle(const std::string& subId);
    bool runSingleWithProxy(const std::string& subId, int socksPort);

private:
    enum class Strategy {
        DirectFirst,
        ProxyFirst,
        DirectOnly
    };

    bool updateWithStrategy(const std::string& subUrl, const std::string& subId, Strategy strategy);
    std::string fetchUrl(const std::string& url);
    std::string fetchUrlViaProxy(const std::string& url, int socksPort);

    std::vector<db::models::Profileitem> parseSubscription(const std::string& content, const std::string& subid);
    bool updateProfileItems(const std::string& subid, const std::vector<db::models::Profileitem>& profiles);

    std::pair<int, int> getProxyPorts();
    void releaseProxyPorts();

    bool startXray(const std::string& indexId, int socksPort, int apiPort);
    void cleanupXray();

    Strategy parseStrategy(const std::string& mode);
    std::string getCurrentTimestamp();
    void log(const std::string& msg);
    std::string decodeBase64(const std::string& input);
    std::string urlDecode(const std::string& input);

    sqlite3* db_;
    std::string xrayPath_;
    config::AppConfig config_;
    std::ofstream* logOut_;
    std::string baseDir_;

    XrayManager* xrayMgr_;
    ProxyFinder* proxyFinder_;

    int xrayProcessId_;
    HANDLE xrayJob_;
};

} // namespace update

#endif // SUBITEM_UPDATER_V2_H
```

### 3.2 核心流程

#### 策略模式更新

```cpp
bool SubitemUpdaterV2::updateWithStrategy(const std::string& subUrl, const std::string& subId, Strategy strategy) {
    bool tryDirect = (strategy != Strategy::ProxyFirst);
    bool tryProxy = (strategy != Strategy::DirectOnly);

    // 1. 优先直连
    if (tryDirect) {
        log("INFO: Trying direct connection...");
        std::string content = fetchUrl(subUrl);
        if (!content.empty()) {
            log("INFO: Direct connection successful");
            auto profiles = parseSubscription(content, subId);
            return updateProfileItems(subId, profiles);
        }
        log("WARN: Direct connection failed");
    }

    // 2. 失败后尝试代理
    if (tryProxy && !tryDirect) {
        log("INFO: Trying proxy connection...");
        auto [socksPort, apiPort] = getProxyPorts();
        if (socksPort > 0) {
            std::string content = fetchUrlViaProxy(subUrl, socksPort);
            if (!content.empty()) {
                log("INFO: Proxy connection successful");
                auto profiles = parseSubscription(content, subId);
                releaseProxyPorts();
                return updateProfileItems(subId, profiles);
            }
        }
    }

    return false;
}
```

#### 复用 ProxyFinder

```cpp
std::pair<int, int> SubitemUpdaterV2::getProxyPorts() {
    xrayMgr_ = XrayManager::getInstance(xrayPath_, baseDir_,
                                         config_.xray_workers,
                                         config_.xray_start_port,
                                         config_.xray_api_port);
    xrayMgr_->start(1, config_.xray_start_port, config_.xray_api_port);

    proxyFinder_ = new ProxyFinder(db_, xrayMgr_, xrayPath_,
                                     config_.test_url,
                                     config_.test_timeout_ms,
                                     logOut_);

    return proxyFinder_->findFirstWorkingProxy();
}

void SubitemUpdaterV2::releaseProxyPorts() {
    if (proxyFinder_) {
        proxyFinder_->release();
        delete proxyFinder_;
        proxyFinder_ = nullptr;
    }
    if (xrayMgr_) {
        xrayMgr_->stopAll();
        XrayManager::release();
        xrayMgr_ = nullptr;
    }
}
```

### 3.3 数据流

```
run()
  ├── SubitemDAO.getEnabledSubscriptions()
  ├── for each subscription
  │     ├── parseStrategy(priority_mode) → Strategy
  │     ├── updateWithStrategy(url, subId, strategy)
  │     │     ├── fetchUrl(url) ← direct
  │     │     └── getProxyPorts() → ProxyFinder::findFirstWorkingProxy()
  │     │           └── fetchUrlViaProxy(socksPort)
  │     └── updateProfileItems() → DB
  └── releaseProxyPorts()
```

### 3.4 日志输出

- 输出到 logOut_ (日志文件)
- 格式与原 SubitemUpdater 一致

## 4. 文件组织

| 文件 | 说明 |
|------|------|
| include/SubitemUpdaterV2.h | 头文件 |
| src/SubitemUpdaterV2.cpp | 实现 |

## 5. 替换计划

1. Phase 1: 新建 SubitemUpdaterV2，编译通过
2. Phase 2: main.cpp 添加 V2 入口
3. Phase 3: 验证功能
4. Phase 4: 逐步迁移
5. Phase 5: 删除原重复代码

## 6. 验收标准

- [x] 保持与原 SubitemUpdater 兼容接口
- [x] 复用 ProxyFinder 查找代理
- [x] 支持 priority_mode 配置
- [x] 更新完成后关闭代理

## 7. 2026-04-16 更新 (v1.0.4)

### 7.1 IndexId 生成规则修改

为与 v2rayN 兼容，IndexId 生成规则修改为固定 4 或 5 开头的 19 位数字：

```cpp
std::string generateUniqueId() {
    static std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::uniform_int_distribution<int> firstDist(0, 1);  // 0 or 1
    static std::uniform_int_distribution<long long> restDist(0, 999999999999999999);  // 18 digits
    
    int first = 4 + firstDist(rng);  // 4 or 5
    long long rest = restDist(rng);  // 0-999...9
    
    std::ostringstream oss;
    oss << first << std::setw(18) << std::setfill('0') << rest;
    return oss.str();  // e.g., "412345678901234567890"
}
```

### 7.2 批量删除优化

更新订阅时，ProfileExItem 批量删除优化：

```sql
-- 之前：逐个删除 N 个 IndexId
SELECT IndexId FROM ProfileItem WHERE Subid = 'xxx';
-- 循环删除 N 次
DELETE FROM ProfileExItem WHERE IndexId = 'idx1';
DELETE FROM ProfileExItem WHERE IndexId = 'idx2';
...

-- 之后：单条 SQL 批量删除
DELETE FROM ProfileExItem WHERE IndexId IN 
    (SELECT IndexId FROM ProfileItem WHERE Subid = 'xxx');
```

### 7.3 Bug 修复

1. **proxy_first 模式修复**：之前 proxy_first 模式未正确处理所有订阅，现已修复为直接跳过直连阶段，所有订阅走代理
2. **重复释放日志修复**：修复 runSingle() 中 updateWithStrategy() 与析构函数重复调用 releaseProxyPorts() 的问题
3. **添加空指针检查**：releaseProxyPorts() 添加检查避免重复释放

### 7.4 文件变更

| 文件 | 变更 |
|------|------|
| src/SubitemUpdaterV2.cpp | IndexId 生成、批量删除、重复释放修复、proxy_first 修复 |
| src/SubitemUpdater.cpp | IndexId 生成、批量删除 |
| include/SubitemUpdaterV2.h | 保持不变 |

### 7.5 版本记录

- v1.0.4 (2026-04-16): 可用版本 - IndexId 生成优化、批量删除优化、Bug 修复

## 8. 2026-04-17 更新 (v1.0.41)

### 8.1 代码重构

1. **删除 SubitemUpdater**：统一使用 SubitemUpdaterV2
   - 删除 include/SubitemUpdater.h
   - 删除 src/SubitemUpdater.cpp
   - main.cpp 移除 SubitemUpdater 引用

2. **公共模块 Utils 扩展**：
   - 添加 `generateUniqueId()` - 4/5 开头 19 位数字 ID 生成
   - 添加 `getProtocolName()` - ConfigType 转协议名称

3. **目录创建统一**：
   - main.cpp 开头统一创建 config/log 目录
   - 移除各分支重复创建代码

### 8.2 测试日志优化

1. **输出格式**：
   ```
   [Worker-2] [15/100] 1.2.3.4:443 (Shadowsocks) OK 123ms
   [Worker-1] [16/100] 5.6.7.8:443 (Trojan) FAIL Timeout was reached
   ```

2. **Mutex 保护**：添加 coutMutex 保护控制台输出，避免交错

3. **计数器修复**：在锁内读取并递增 processedCount_，避免竞态条件

### 8.3 ConfigType 扩展

1. **getProtocolName() 扩展**：支持 ConfigType 7-12, 16, 17

| ConfigType | 协议 |
|------------|------|
| 1 | VMess |
| 2 | Custom |
| 3 | Shadowsocks |
| 4 | SOCKS |
| 5 | VLESS |
| 6 | Trojan |
| 7 | Hysteria2 |
| 8 | TUIC |
| 9 | WireGuard |
| 10 | HTTP |
| 11 | Anytls |
| 12 | Naive |
| 16 | WireGuard (兼容) |
| 17 | TUIC (兼容) |

2. **ConfigGenerator 补充**：ConfigType 11, 12 添加显式提示

### 8.4 文件变更

| 文件 | 变更 |
|------|------|
| include/Utils.h | 新增 getProtocolName() |
| src/Utils.cpp | 实现 generateUniqueId(), getProtocolName() |
| src/main.cpp | 统一目录创建，移除 SubitemUpdater |
| src/ProxyBatchTester.cpp | 日志格式优化，Mutex 保护，计数器修复 |
| src/ConfigGenerator.cpp | 添加 ConfigType 11,12 提示 |
| src/SubitemUpdaterV2.cpp | 使用 utils 模块 |
| include/SubitemUpdater.h | 删除 |
| src/SubitemUpdater.cpp | 删除 |

### 8.5 版本记录

- v1.0.41 (2026-04-17): 删除旧模块，日志优化，ConfigType 扩展