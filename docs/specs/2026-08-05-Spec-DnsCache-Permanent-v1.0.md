---
title: "DNS 解析结果永久缓存（直到程序退出）"
type: spec
status: in-progress
date: 2026-08-05
---

# DNS 解析结果永久缓存（直到程序退出）技术方案 (Spec)

## 1. 需求

> 用户原文（2026-08-05）：**"调整功能：1.所有解析目标域名后缓存，并不再过期，直到程序退出"**

用户确认范围（**"两者都要"**）：

1. **全局 curl DNS 解析缓存**：所有经 `CurlEasyHandle` 的 curl 请求（NetworkMonitor 探测、UrlFetcher、ProxyTester 等）共享同一份 DNS 解析缓存，解析结果**永久有效**，直到程序退出。
2. **NetworkMonitor 第二层缓存整体删除**：`dnsCache_`（host→成功时间戳）为只写不读死代码，2026-08-05 用户决策"去除第二层dns缓存"后**整体删除**；DNS 去重统一由改动 1 的程序级 `DnsShareCache` 承担（单一缓存）。

## 2. 现状分析

### 2.1 现有 DNS 缓存三处并存

| 位置 | 缓存内容 | 生命周期 | 现状 |
|------|---------|---------|------|
| `src/utils/DnsCache.cpp` | 域名→IP（getaddrinfo 结果） | **永久**（函数级 static，进程退出回收） | ✅ 已是"不过期" |
| libcurl 内部 DNS 缓存 | 域名→IP（curl 自动解析） | 默认 **60 秒**（`CURLOPT_DNS_CACHE_TIMEOUT`） | ❌ 每次探测新建 handle，缓存随 handle 销毁 |
| `NetworkMonitor::dnsCache_` | host→成功解析时间戳 | **30 秒 TTL**（`kDnsCacheTtlMs_`），命中即**跳过整个探测** | ❌ 30s 节流 + 跳过探测 |

### 2.2 核心矛盾

`NetworkMonitor::ThreadLoop`（`src/NetworkMonitor.cpp:139-141`）在 `hasValidDnsCache(host)` 命中时 **`continue` 跳过整个 HTTP 探测**。若仅将 TTL 改为永久，缓存命中后将**永久跳过探测** → **断网检测永久失效**（NetworkMonitor 的核心职责是检测断网并触发批量测试取消）。

因此"不再过期"不能简单理解为"缓存命中就永久跳过探测"，而应理解为：**DNS 解析结果永久缓存（避免重复解析），但连通性探测本身仍每轮执行**。

### 2.3 libcurl 共享缓存机制

- `CURLOPT_DNS_CACHE_TIMEOUT = -1`：单 handle 内 DNS 缓存永久有效。
- `CURLOPT_SHARE` + `curl_share_setopt(CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS)`：**跨 handle 共享 DNS 缓存**（libcurl 官方共享机制）。
- 共享缓存多线程访问**必须**提供 `CURLSHOPT_LOCKFUNC`/`CURLSHOPT_UNLOCKFUNC` 回调（libcurl 文档要求）。

## 3. 设计

### 3.1 改动 1：`CurlEasyHandle.h` 程序级共享 DNS 缓存

**新增 `DnsShareCache` 类**（同文件，header-only）：

```cpp
// 程序级共享 DNS 缓存：生命周期 = 程序，直到进程退出（进程退出由 OS 回收，
// 与 DnsCache.cpp 中 WSAStartup 不 WSACleanup 的既有模式一致，零析构顺序风险）
class DnsShareCache {
public:
    static CURLSH* get();           // 首次调用惰性创建，后续复用
private:
    static CURLSH* createShare();   // curl_share_init + 共享 DNS 数据
    static std::mutex& mutex();     // 共享锁（CURLSHOPT_USERDATA 指向它）
    static void lockCallback(CURL*, curl_lock_data, curl_lock_access, void* userptr);
    static void unlockCallback(CURL*, curl_lock_data, void* userptr);
};
```

- `createShare()`：`curl_share_init()` → `CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS` → `CURLSHOPT_LOCKFUNC/UNLOCKFUNC` 绑定到 `static std::mutex` 的 lock/unlock（多线程安全必需）→ `CURLSHOPT_USERDATA` 指向该 mutex。
- 共享句柄为函数级 `static`，**不注册析构**（进程退出 OS 回收）——与项目既有 `DnsCache` 模式一致。
- 所有 `CurlEasyHandle` 共享同一 DNS 缓存 → "所有解析目标域名后缓存，直到程序退出"。

**`CurlEasyHandle` 构造函数追加**：

```cpp
CurlEasyHandle() : curl_(curl_easy_init()) {
    // ... 既有空指针检查 ...
    // 接入程序级共享 DNS 缓存（永久缓存，直到进程退出）
    CURLSH* share = DnsShareCache::get();
    if (share != nullptr) {
        curl_easy_setopt(curl_, CURLOPT_SHARE, share);
    }
    curl_easy_setopt(curl_, CURLOPT_DNS_CACHE_TIMEOUT, -1L);
}
```

- setopt 失败保守忽略（退化默认行为，不抛异常，不影响既有调用方）。
- 效果：任意线程创建的任意 `CurlEasyHandle` 共享同一永久 DNS 缓存。

### 3.2 改动 2：`NetworkMonitor` dnsCache_ 整体删除（2026-08-05 追加决策）

> 原方案为"永久化保留纯记录"；经调查确认 `dnsCache_` 全项目**只写不读**（唯一写入点 `NetworkMonitor.cpp:104`，零读取方），为死代码。用户决策：**去除第二层dns缓存** → 由"保留纯记录"升级为"整体删除"。

| 位置 | 改动 |
|------|------|
| `include/NetworkMonitor.h:69-74` | 删除 `dnsCache_`/`dnsCacheMutex_` 成员及注释（只写不读死代码） |
| `include/NetworkMonitor.h:5,8,9` | 删除 `<unordered_map>`/`<chrono>`/`<mutex>` include（仅 dnsCache_ 使用） |
| `src/NetworkMonitor.cpp:44-56` | 删除 `getHostFromUrl` 静态函数（唯一调用方为 dnsCache_ 写入块） |
| `src/NetworkMonitor.cpp:97-105` | 删除 dnsCache_ 写入块（`result.success = true` 保留） |
| `src/NetworkMonitor.cpp:7` | 删除 `#include <chrono>`（仅写入块使用） |
| `src/NetworkMonitor.cpp:164-166` | RESTORED 注释更新（不再引用已删除的 dnsCache_，指向 DnsShareCache） |

**保留**：`dnsFailures_`/`getDnsFailures()`（ThreadLoop 129-134 行诊断计数用，外部接口）；`dnsFailures_.store(0)` 重置逻辑。

**行为变化**：无——dnsCache_ 删除不影响探测行为（每轮仍执行 HTTP 探测）；DNS 去重完全由改动 1 承担（curl 共享缓存永久解析结果，不重复 DNS 查询）。

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `include/CurlEasyHandle.h` | +`DnsShareCache` 类；构造函数接入 `CURLOPT_SHARE` + `CURLOPT_DNS_CACHE_TIMEOUT=-1`（✅ 已实施） |
| `include/NetworkMonitor.h` | 删 dnsCache_/dnsCacheMutex_ 成员及注释；删 `<unordered_map>`/`<chrono>`/`<mutex>` include |
| `src/NetworkMonitor.cpp` | 删 getHostFromUrl 函数/dnsCache_ 写入块/`<chrono>` include；更新 RESTORED 注释 |
| `tests/test_curl_easy_handle.cpp` | +DnsShareCache 测试（get() 非空、幂等同指针）（✅ 已实施） |

## 5. 测试策略

### 5.1 新增测试（tests/test_curl_easy_handle.cpp，非 gtest）

| 用例 | 断言 |
|------|------|
| `DnsShareCache::get()` 非空 | 共享句柄可用 |
| 两次调用返回同一指针 | 幂等，单实例 |

### 5.2 既有测试回归

- `tests/test_network_monitor.cpp` 11 个 gtest：无断言依赖 TTL/continue 跳过逻辑，应全部继续通过。
- `CurlEasyHandleTest`（真实请求 example.com）：构造函数改动不改变行为，应继续通过。

### 5.3 验证命令

```powershell
cmake --build build --parallel 8
ctest --test-dir build -R "CurlEasyHandleTest|NetworkMonitorTest" --output-on-failure
```

## 6. 实施记录

| 步骤 | 内容 | 状态 |
|------|------|------|
| 1 | 撰写本技术方案文档 | ✅ completed |
| 2 | 实现 `DnsShareCache` + CurlEasyHandle 构造函数接入 | ✅ completed |
| 3 | NetworkMonitor dnsCache_ 整体删除（成员/include/getHostFromUrl/写入块/注释） | ✅ completed |
| 4 | 补充测试（DnsShareCache get() 非空/幂等 + NetworkMonitor 11 用例回归） | ✅ completed |
| 5 | 构建 + ctest 验证（NetworkMonitorTest 11 用例通过，8.22s） | ✅ completed |
| 6 | 登记 docs/INDEX.md（#22）+ 更新 tracker | ✅ completed |
