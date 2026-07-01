# 网络探测逻辑及流程分析报告

**日期**: 2026-06-26  
**类型**: 架构分析  
**模块**: NetworkMonitor + ProxyBatchTester  

---

## 1. 架构概览

本项目采用**生产者-消费者双线程模型**实现网络可用性探测：

| 角色 | 组件 | 职责 |
|------|------|------|
| **生产者** | `NetworkMonitor` | 后台线程周期性 ping 公网 URL，判断本机网络可用性 |
| **消费者** | `ProxyBatchTester::Worker` | 测试代理时轮询网络状态，断开则暂停等待恢复 |

两者通过**共享指针 + 原子标志**通信，无直接数据竞争。

---

## 2. 核心组件

### 2.1 NetworkMonitor

| 文件 | 行数 | 说明 |
|------|------|------|
| `include/NetworkMonitor.h` | ~60 | 头文件，声明所有成员 |
| `src/NetworkMonitor.cpp` | ~146 | 实现文件，含 ThreadLoop 和 CheckURL |

**关键成员变量**:
- `std::vector<std::string> urls_` — 探测 URL 列表
- `std::atomic<bool> connected_` — 当前连接状态
- `std::atomic<bool> cancelRequested_` — 取消请求标志
- `std::atomic<bool> cancelOnDisconnect_` — 断开时取消测试标志
- `int consecutiveFailures_` — 连续失败计数
- `int maxProbes_` — 触发取消的最大连续失败次数

### 2.2 默认配置

来源: `include/ConfigReader.h` (AppConfig::NetworkMonitorConfig)

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `checkIntervalMs` | 10000 | 探测间隔 (毫秒) |
| `checkTimeoutMs` | 5000 | 单次请求超时 (毫秒) |
| `maxProbes` | 3 | 连续失败阈值 |
| `checkUrls` | `[baidu.com, qq.com, taobao.com]` | 探测 URL 列表 |

---

## 3. 探测流程详解

### 3.1 ThreadLoop 主循环

位置: `src/NetworkMonitor.cpp:65-145`

```
┌─────────────────────────────────────────────────┐
│              ThreadLoop 主循环                    │
│  while (!stopRequested_) {                       │
│                                                   │
│   1. 遍历 urls_ 依次调用 CheckURL()               │
│      ├─ baidu.com   → CURLE_OK + HTTP 2xx-3xx ✓  │
│      ├─ qq.com      → CURLE_OK + HTTP 2xx-3xx ✓  │
│      └─ taobao.com  → 失败 ✗                      │
│      任一失败 → allOk = false                     │
│                                                   │
│   2. atomic 交换状态:                              │
│      bool prev = connected_.exchange(allOk)       │
│                                                   │
│   3. 状态转换处理:                                 │
│      if (prev && !allOk) {                        │
│         // 已连接 → 断开                           │
│         consecutiveFailures_++                    │
│         if (consecutiveFailures_ >= maxProbes_) { │
│            cancelOnDisconnect_ = true             │
│            cancelRequested_ = true                │
│         }                                         │
│      } else if (!prev && allOk) {                 │
│         // 断开 → 恢复                             │
│         consecutiveFailures_ = 0                  │
│      }                                            │
│                                                   │
│   4. 休眠 checkIntervalMs                         │
│  }                                                │
└─────────────────────────────────────────────────┘
```

### 3.2 CheckURL 方法

位置: `src/NetworkMonitor.cpp`

```
CheckURL(url):
  1. 创建 CurlEasyHandle (RAII 封装)
  2. 设置 HEAD 请求 + 超时时间
  3. 执行 Perform()
  4. 获取响应码
  5. 判定:
     - CURLE_OK 且 HTTP 2xx/3xx → 成功
     - 其他 → 失败
```

---

## 4. Worker 线程检查点

位置: `src/ProxyBatchTester.cpp`

Worker 在以下检查点调用 `netMon_->IsConnected()`：

| 检查点 | 行号范围 | 触发时机 |
|--------|----------|----------|
| 1 | ~120 | 每个 Worker 循环开始时 |
| 2 | ~174 | 加载代理列表后 |
| 3 | ~180, 197, 222 | 单个代理测试前 |

如果 `IsConnected()` 返回 `false`，则调用 `waitForNetworkRecovery()`。

### 4.1 waitForNetworkRecovery

位置: `src/ProxyBatchTester.cpp:355-366`

```cpp
bool ProxyBatchTester::waitForNetworkRecovery() {
    while (!isCancelled()) {
        if (!netMon_ || netMon_->IsConnected()) {
            return true; // 网络恢复或被取消
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return false; // 被取消
}
```

---

## 5. 状态机

```
                    ┌──────────────┐
                    │   初始状态    │
                    │ connected_=? │
                    └──────┬───────┘
                           │ 首次 CheckURL
                    ┌──────┴───────┐
              ┌────┤ CONNECTED     │
              │    │ connected_=true│
              │    └──────┬────────┘
              │           │
              │    ┌──────┴──────────┐
              │    │ DISCONNECTED     │
              │    │ connected_=false │
              │    │ consecFails=0..N │
              │    └──────┬──────────┘
              │           │
    ┌─────────┼───────────┼─────────────┐
    │         │           │             │
    ▼         ▼           ▼             ▼
  恢复     fails<max   fails≥max    持续失败
  consec=0  probes      cancel     仅日志
            内重试      标志置位    防泛滥
```

---

## 6. 通信机制

| 机制 | 方向 | 类型 | 数据安全 |
|------|------|------|----------|
| 共享指针 `netMon_` | AppController → ProxyBatchTester | 智能指针 | 读共享 |
| `connected_.exchange()` | NetworkMonitor 写入 | std::atomic | 无竞争 |
| `IsConnected()` | Worker 读取 | std::atomic load | 无竞争 |
| `cancelRequested_` | NetworkMonitor 设置 | std::atomic | 无竞争 |
| `cancelOnDisconnect_` | NetworkMonitor 设置 | std::atomic | 无竞争 |

**关键保证**: 所有跨线程通信均使用 `std::atomic`，无数据竞争。

---

## 7. 关键文件索引

| 文件 | 作用 |
|------|------|
| `src/NetworkMonitor.cpp` | 探测逻辑实现 (ThreadLoop, CheckURL) |
| `include/NetworkMonitor.h` | 类声明与成员变量 |
| `src/ProxyBatchTester.cpp` | Worker 检查点 + waitForNetworkRecovery |
| `include/ProxyBatchTester.h` | netMon_ 成员声明 |
| `src/ui/AppController.cpp` | NetworkMonitor 初始化 (lines 30-43) + 重启 (lines 536+) |
| `include/config/sections/NetworkMonitorConfigParser.h` | 配置解析 |
| `include/ConfigReader.h` | AppConfig::NetworkMonitorConfig 结构体 |
| `tests/test_network_monitor.cpp` | 单元测试 |

---

## 8. 核心设计要点

1. **容错机制**: `maxProbes_` (默认 3) 提供连续失败容忍窗口，避免瞬态断网导致测试中断
2. **防日志泛滥**: 超过阈值后停止计数，仅记录首次超限日志
3. **主动取消**: 达到阈值后设置 `cancelRequested_ = true`，通知所有 Worker 立即停止
4. **优雅恢复**: 网络恢复后 `consecutiveFailures_` 清零，测试自动继续
5. **零数据竞争**: 全栈使用 `std::atomic`，无需 mutex

---

## 9. 结论

NetworkMonitor 作为**生产者**负责探测网络可用性，ProxyBatchTester Worker 作为**消费者**根据状态暂停/恢复/取消测试。两者通过共享指针 + 原子标志通信，probe-on-disconnect 机制提供 3 次连续失败容错窗口，有效平衡了网络波动与测试效率之间的关系。