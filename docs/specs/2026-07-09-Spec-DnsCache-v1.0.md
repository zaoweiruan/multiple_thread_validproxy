# DnsCache DNS 缓存解析模块技术方案 (Spec)

> **版本:** v1.0
> **日期:** 2026-07-09
> **状态:** ✅ completed
> **关联:** RegionDetector, ProxyTester, ConfigGenerator

---

## 1. 问题陈述

项目核心业务涉及大量域名解析：
- **ProxyTester**：测试代理连通性时，需要通过 SOCKS5/Xray 代理建立连接，目标域名需解析为 IP
- **RegionDetector**：Phase 2 中 DNS 解析作为地域检测的第三种策略（Strategy 3）
- **ConfigGenerator**：生成 Xray outbound 配置时涉及域名转换

在原始代码中，域名解析通过 `getaddrinfo()` 直接调用，缺乏缓存机制。同样的域名（如 `google.com`）可能在短时间内被重复解析数十次，导致：
1. **性能浪费**：重复 DNS 查询增加延迟
2. **DNS 服务器压力**：对同一域名的频繁查询可能触发限流
3. **解析失败风暴**：当 DNS 临时故障时，每次查询都重复失败，浪费 CPU

需要引入一个轻量级、线程安全的 DNS 缓存模块。

## 2. 现状分析

### 2.1 DNS 解析调用点

项目中的 DNS 解析调用点分布：

| 位置 | 用途 | 频率 |
|------|------|------|
| `RegionDetector::detect()` Strategy 3 | 解析域名获取 IP 进行地域匹配 | 按需，每个代理一次 |
| `ProxyTester.cpp` | 代理可用性测试中的域名解析 | 每个代理测试一次 |
| `UrlFetcher.cpp` | cURL HTTP 请求中由 libcurl 内部处理 | N/A |

### 2.2 现有工具链

- **系统 API**: `getaddrinfo()` (WinSock2)
- **第三方库**: 无 DNS 相关依赖
- **线程安全**: 现有的 URL Fetcher 和 ProxyTester 在多线程环境下运行，DNS 缓存必须支持并发访问

### 2.3 性能数据

典型使用场景：500 个代理批量测试，每个代理测试涉及 1-3 个域名解析，其中大量域名重复（如各节点指向同一个 CDN 域名），重复率约 60-80%。

---

## 3. 设计

### 3.1 类接口

```cpp
// include/utils/DnsCache.h
namespace utils {

class DnsCache {
public:
    /**
     * Resolve a hostname to an IPv4 address string.
     * Returns the first resolved IPv4 address, or empty string on failure.
     * Failed resolutions are also cached (empty string) to avoid retry storms.
     *
     * @param hostname The domain name to resolve (e.g. "example.com")
     * @return IP address string (e.g. "93.184.216.34") or empty on failure
     */
    static std::string resolve(const std::string& hostname);

private:
    struct CacheEntry {
        std::string ip;
    };

    static std::unordered_map<std::string, CacheEntry>& getCache();
    static std::mutex& getMutex();
};

} // namespace utils
```

### 3.2 核心设计决策

#### 决策 1：全静态类（无实例化）

**选择原因**：
- DnsCache 本质上是全局共享资源，所有模块共享同一个缓存
- 无状态设计：不需要配置、初始化或销毁
- 简化调用：`DnsCache::resolve("example.com")` 随处可调

**代价**：不可替换实现（如果将来需要 Mock，需额外接口层）。

#### 决策 2：Meyers' Singleton (函数级 static)

缓存在首次调用 `getCache()`/`getMutex()` 时惰性初始化：

```cpp
std::unordered_map<std::string, CacheEntry>& DnsCache::getCache() {
    static std::unordered_map<std::string, CacheEntry> cache;
    return cache;
}
```

**C++11 线程安全保证**：函数级 `static` 变量初始化由标准库保证线程安全。

#### 决策 3：无 TTL 过期（永不过期）

**理由**：
1. 项目运行的代理验证会话通常持续数分钟到数小时，DNS 记录在这段时间内几乎不可能变化
2. TTL 机制增加复杂度（定时清理/引用计数），收益极低
3. 应用重启即清空缓存（进程级缓存）

**例外处理**：如果将来需要 TTL，可在 `CacheEntry` 中添加 `timestamp` 字段和清理线程。

#### 决策 4：缓存失败结果（负缓存）

失败（空字符串）也被缓存，原因：
1. **防重试风暴**：DNS 临时故障时，大量线程同时查询同一域名会引发连锁失败
2. **快速失败**：第一次解析失败后，后续调用立即返回空，不发起网络请求
3. **适用场景**：代理测试循环中，当一个域名不可解析时，不需要反复确认

**行为对比**：

| 状态 | 无缓存 | 有缓存（仅成功） | 有缓存（含失败） |
|------|--------|------------------|------------------|
| 域名成功解析 | 每次查询 | 缓存命中，快 | 缓存命中，快 |
| 域名解析失败 | 每次都尝试 | 每次都尝试 | 第一次失败后立即返回空 |

### 3.3 解析流程

```
resolve(hostname)
│
├─ 1. hostname 为空？→ 返回 ""
│
├─ 2. 加锁，检查缓存
│   ├─ 命中 → 解锁，返回缓存 IP
│   └─ 未命中 → 解锁，继续
│
├─ 3. 惰性初始化 WinSock (WSAStartup)，仅第一次调用
│
├─ 4. getaddrinfo(AF_INET) 解析 IPv4
│   ├─ 成功 → inet_ntop 转换 → ip 字符串
│   └─ 失败 → 日志 TRACE, ip = ""
│
└─ 5. 加锁，写入缓存，解锁
    └─ 返回 ip
```

### 3.4 线程安全模型

```
               ┌─────────────┐
               │  Thread 1   │
               │ resolve("a")│────┐
               └─────────────┘    │
                                  ▼
              ┌─────────────────────────────────┐
              │         mutex lock              │
              │  unordered_map cache            │
              │  { "a" → "1.1.1.1"             │
              │    "b" → "" }                   │
              │         mutex unlock            │
              └─────────────────────────────────┘
                                  ▲
               ┌─────────────┐    │
               │  Thread 2   │    │
               │ resolve("b")│────┘
               └─────────────┘
```

- 读/写同一 cache 时通过 `std::mutex` 互斥
- 锁范围限定在 cache 访问期间：DNS 解析本身在锁外执行，避免阻塞其他线程
- 不含嵌套锁或递归锁，消除死锁风险

### 3.5 WinSock 初始化

在第一次 `getaddrinfo()` 调用前调用 `WSAStartup()`：

```cpp
static bool wsaStarted = false;
if (!wsaStarted) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        // 日志 ERR，返回空字符串
    }
    wsaStarted = true;
}
```

- **可重入安全**：`WSAStartup()` 内部维护引用计数，多次调用无害
- **进程生命周期**：不调用 `WSACleanup()` — 进程退出时 OS 自动回收

---

## 4. 文件清单

| 文件 | 说明 | 行数 |
|------|------|------|
| `include/utils/DnsCache.h` | 头文件 — 类声明 | 41 |
| `src/utils/DnsCache.cpp` | 实现 — getaddrinfo + 缓存管理 | 73 |
| `include/utils/RegionDetector.h` | 调用方 — Strategy 3 引用 | 已存在 |
| `src/utils/RegionDetector.cpp` | 调用方 — Strategy 3 实现 | 已存在 |

---

## 5. 测试策略

### 5.1 单元测试

| 测试用例 | 输入 | 预期输出 | 验证点 |
|---------|------|---------|--------|
| 空字符串 | `""` | `""` | 边界保护 |
| 有效域名 | `"example.com"` | `"93.184.216.34"` | 正常解析 |
| 无效域名 | `"invalid.example.nonexistent"` | `""` | 失败处理 |
| 重复解析 | 同一域名两次 | 第二次快于第一次 | 缓存命中 |
| 多线程并发 | 4 线程同时解析不同域名 | 无竞争、全部成功 | 线程安全 |

### 5.2 集成测试点

- RegionDetector Strategy 3 中 DnsCache 调用是否正确
- ProxyTester 在大量并发测试中 DNS 缓存是否生效

---

## 6. 实施记录

| 步骤 | 内容 | 状态 |
|------|------|------|
| 1 | 创建 `include/utils/DnsCache.h` | ✅ completed |
| 2 | 实现 `src/utils/DnsCache.cpp` | ✅ completed |
| 3 | 集成至 `RegionDetector::detect()` Strategy 3 | ✅ completed |
| 4 | 编写单元测试 `tests/test_dnscache.cpp` | ✅ completed |
| 5 | 构建验证 + 18/18 ctest 通过 | ✅ completed |
| 6 | 撰写技术方案文档（本文档） | ✅ completed |
