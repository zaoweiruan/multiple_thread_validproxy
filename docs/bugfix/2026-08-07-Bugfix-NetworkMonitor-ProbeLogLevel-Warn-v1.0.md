# Bugfix: 网络监控探测错误详情日志级别提升为 WARN

| 项 | 内容 |
| :--- | :--- |
| 日期 | 2026-08-07 |
| 模块 | `NetworkMonitor`（`src/NetworkMonitor.cpp` / `include/NetworkMonitor.h`） |
| 严重程度 | 中 |
| 状态 | 已修复 |

## 1. 问题描述

`NetworkMonitor`（网络监控模块）在探测失败时的**错误详情日志**级别过低：

1. **非 DNS 的 curl 探测失败**（`CURLE_*` 错误，如连接超时、拒绝连接）→ `LogLevel::DEBUG`；
2. **DNS 解析失败**（`CURLE_COULDNT_RESOLVE_HOST/PROXY`）→ `LogLevel::TRACE`；
3. **HTTP 状态码非 200-399** → `LogLevel::DEBUG`。

由于生产配置 `bin/config.json` 为 `"file_level":"ERROR"`、`"console_level":"WARN"`，
上述探测错误详情在日志文件与控制台中**均不可见**，网络质量下降时无法定位是
DNS 问题还是连接问题。

## 2. 影响范围

- 仅 `src/NetworkMonitor.cpp` 的 `CheckURLWithDnsFlag`（L51-92）内 3 处错误详情日志级别。
- **不改动** `ThreadLoop`（L123-179）中的连接状态变化日志：
  `LOST / threshold reached / cancelOnDisconnect / RESTORED / check failed` 保持 `LogLevel::ERR`。
- `NetworkMonitor.h` 无需改动。
- 测试 `tests/test_network_monitor.cpp` 的 `LoggingOnConnectionLost` 仅断言
  `"LOST"` 消息的 `LogLevel::ERR`（不涉及本次修改的探测详情日志），**无需修改测试**。

## 3. 根因

探测错误详情本属于“网络故障信号”，对排障有直接价值；但初始设计将其归入
DEBUG/TRACE 调试级别，与生产日志级别（ERROR/WARN）不匹配，导致故障信息被过滤。

## 4. 修复内容

`src/NetworkMonitor.cpp` `CheckURLWithDnsFlag` 内 3 处日志级别调整：

| 行号 | 原日志消息 | 原级别 | 新级别 |
| :--- | :--- | :--- | :--- |
| L67-70 | `NetworkMonitor: probe failed url=... curl_error=...` | `DEBUG` | `WARN` |
| L72-75 | `NetworkMonitor: DNS error url=... curl_error=...` | `TRACE` | `WARN` |
| L84-86 | `NetworkMonitor: probe url=... http_code=...` | `DEBUG` | `WARN` |

调整后：

```cpp
if (!result.isDnsError) {
    Logger::write("NetworkMonitor: probe failed url=" + url +
                      " curl_error=" + std::to_string(static_cast<int>(res)) +
                      " (" + curl_easy_strerror(res) + ")",
                  LogLevel::WARN);
} else {
    Logger::write("NetworkMonitor: DNS error url=" + url +
                      " curl_error=" + std::to_string(static_cast<int>(res)) +
                      " (" + curl_easy_strerror(res) + ")",
                  LogLevel::WARN);
}
// ...
} else {
    Logger::write("NetworkMonitor: probe url=" + url +
                      " http_code=" + std::to_string(httpCode),
                  LogLevel::WARN);
}
```

### 逻辑说明

- 探测失败的三种情形（curl 错误 / DNS 错误 / 非 2xx-3xx 状态码）均属**网络异常事件**，
  提升至 `WARN` 后可在控制台（`console_level=WARN`）即时可见，并可按需写入日志文件
  （`file_level` 设为 `WARN` 或更低时）。
- `ThreadLoop` 的连接状态机日志（`LOST`/`RESTORED` 等）仍为 `ERR`，属于更高优先级的
  状态切换告警，语义不变。

## 5. 验证

1. 构建：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` + `cmake --build build --parallel 8`。
2. 测试：`cmake --build build --target test --parallel 8` + `ctest -V`（基线 21/21，
   `NetworkMonitorTest` 约 8s）。
3. 功能验证：以 `file_level=WARN`（或 ERROR 触发后观察控制台）运行，断网场景下
   `NetworkMonitor: probe failed / DNS error / probe ... http_code` 出现在
   WARN 级输出中。

## 6. 配置文件结构（参考）

| 字段 | 生产值 | 默认值 | 说明 |
| :--- | :--- | :--- | :--- |
| `log.enabled` | `true` | `true` | 日志总开关 |
| `log.console_level` | `WARN` | `INFO` | 控制台输出最低级别 |
| `log.file_level` | `ERROR` | `DEBUG` | 文件输出最低级别 |

## 7. 后续项

- 无。
