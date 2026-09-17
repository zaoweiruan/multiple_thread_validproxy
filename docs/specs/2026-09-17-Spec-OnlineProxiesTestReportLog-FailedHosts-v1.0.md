---
title: "Spec: Online Proxies Test Report Log — 失败 host:port 明细"
type: spec
status: completed
date: 2026-09-17
version: v1.0
updated: 2026-09-17
module: AppController
scope: src/ui/AppController.cpp
risk: low
---

# Spec: Online Proxies Test Report Log — 失败 host:port 明细

## 1. 背景

`AppController::doTestOnlineProxies(wxEvtHandler*, bool silent)` 完成一次独立代理周期/手动连通性测试后，无论成功或失败都写一条汇总日志到 `REPORT` 级别：

```
Online proxies test finished: total=2, success=1, failed=1
```

在 44 条订阅源 / 数十条独立代理的场景下，仅有 `failed=N` 计数不足定位是哪个 `indexId` 的哪个 `host:port` 失败。用户明确要求：`failed > 0` 时在同一行 REPORT 中附上失败的 host 与监听端口。

## 2. 目标与非目标

**目标**

- 当 `failed > 0` 时，REPORT 日志尾部追加 `failed_hosts=` 字段，逐条列出 `host:port`，元素间用 ` | ` 分隔。
- `failed == 0` 的两条分支（silent DEBUG 与常规 REPORT）不追加该字段，保持既有日志简洁性。
- 复用 `StandaloneMonitorRow.host` / `socksPort` 已有字段，不新增 DAO 查询、不新增配置项。

**非目标**

- 不改日志级别划分（`silent` 全失败路径仍是 DEBUG；只有 `silent==false` 或 `silent==true && failed>0` 才 REPORT）。
- 不改动 `failedIndexIds` 的既有语义与写入位置（该 vector 仍未被日志消费，保留供后续调试路径）。
- 不改动 `ProfileExItemDAO::updateTestResult` 的调用契约。

## 3. 数据可用性论证

`doTestOnlineProxies` 主循环遍历 `std::vector<StandaloneMonitorRow> monitors = getWatchedStandaloneMonitors()`，`StandaloneMonitorRow` 已含目标字段（`src/ui/AppController.h` L51-59）：

```cpp
struct StandaloneMonitorRow {
    std::string indexId;
    std::string host;        // ProfileItem.Address; empty when profile missing
    std::string startedAt;
    int64_t durationMs = 0;
    int socksPort = 0;       // 0 = port unknown (config parse failed)
    int64_t pid = -1;
    long long lastDelayMs = -1;
};
```

两个失败分支均能拿到 `mon.host` 与 `mon.socksPort`：

- 分支 A（`socksPort <= 0`，config 解析失败）：`mon.socksPort` 为 0，展示为 `host:0` 与错误消息 `socks port unknown` 语义一致。
- 分支 B（`!r.success`，连通性失败）：`mon.socksPort` 有效，展示为 `host:<真实端口>`。

`ProfileExItem` 无 host/port 字段（仅 delay/speed/sort/message/counter），故不采用它；直接用 `mon` 上已有字段最经济。

## 4. 实现方案

### 4.1 累加器

在既有 `std::vector<std::string> failedIndexIds;`（L2256）之后新增一个同作用域、同类型的累加器，格式化为最终日志字段字符串：

```cpp
std::string failedHostsStr;
```

选择 `std::string` 而非 `std::vector<std::string>` 的原因：REPORT 日志最终拼接为单个 `std::string`，用 vector 反而需要在写日志时再做一次 join；直接累积字符串少一次循环，且元素间分隔符 ` | ` 在写入时确定，后续拼接无歧义。

### 4.2 写入点

在两个失败分支追加 host:port 到 `failedHostsStr`：

- **分支 A**（L2281 附近，`failedIndexIds.push_back(mon.indexId);` 之后）：
  ```cpp
  failedHostsStr += (failedHostsStr.empty() ? std::string() : " | ") + mon.host + ":" + std::to_string(mon.socksPort);
  ```
- **分支 B**（L2293 附近，`failedIndexIds.push_back(mon.indexId);` 之后）：同上。

采用 `host + ":" + std::to_string(socksPort)` 与 `src/ui/UnifiedMonitorRows.h` 内既有 `host + ":" + std::to_string(socksPort)` 拼多元素列表的范式对齐（分隔符 ` | ` 亦沿用该范式）。

### 4.3 REPORT 日志拼接

修改 L2327-2329 的 REPORT 分支（`failed > 0` 路径）：

```cpp
if (failed > 0) {
    Logger::write(
        std::string("Online proxies test finished: total=") + std::to_string(total)
        + ", success=" + std::to_string(success)
        + ", failed=" + std::to_string(failed)
        + ", failed_hosts=" + failedHostsStr,
        LogLevel::REPORT);
}
```

其它两条分支（`else if (silent)` DEBUG / `else` REPORT 全成功）保持不动，`failedHostsStr` 恒为空、不参与拼接。

## 5. 边界与约束

- **空 host**：若 profile 缺失导致 `mon.host` 为空，日志呈现为 `:port`；符合 `StandaloneMonitorRow` 结构注释「empty when profile missing」的语义，读者可结合 indexId 定位。
- **端口未知**：`socksPort==0` 展示为 `host:0`，与既有 `exDao_.updateTestResult(mon.indexId, -1, false, "socks port unknown")` 的错误文案在语义上呼应（`delay=-1`、`port=0`）。
- **单条超长日志**：REPORT 单次日志长度上限由 Logger 内部处理，不做截断；监控条目量级为个位数（`getWatchedStandaloneMonitors` 语义即"正在被监控"的独立代理，通常 0-10 条），不会导致日志过大。
- **编码**：host 可能含 IPv6/字母数字；不引入转义。与既有 `ProxyListPanel` host 列显示风格一致。
- **`auto` 禁用**：所有变量显式指定类型，遵循 AGENTS.md §1 C++17 全栈禁 `auto` 约束。

## 6. 验证计划

1. `cmake --build build --parallel 8` 增量编译，0 error / 0 warning 回归。
2. 手工验证（可选）：启动 GUI，制造至少一条独立代理失败场景（例如启动后断开网络），检查 `bin/validproxy.log` 中 REPORT 行是否含 `failed_hosts=host:port`。
3. 已有单测不受影响：本次改动位于 UI 层 `doTestOnlineProxies`，无对应 GTest 覆盖，且不影响任何公共 API 契约。

## 7. 变更清单

| 文件 | 变更 |
|------|------|
| `src/ui/AppController.cpp` | `doTestOnlineProxies` 内新增 `std::string failedHostsStr;` 累加器（L2257 附近）；两个失败分支 push（L2281、L2293 附近）；REPORT 日志追加 `failed_hosts=` 字段（L2327-2329） |
| `docs/specs/2026-09-17-Spec-OnlineProxiesTestReportLog-FailedHosts-v1.0.md` | 本文件 |
| `docs/INDEX.md` §7.5 | 登记 |
| `docs/plans/project-plans-tracker.md` 近期文档引用表 | 登记 |
