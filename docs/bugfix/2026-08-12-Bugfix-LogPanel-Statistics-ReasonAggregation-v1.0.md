# Bugfix: LogPanel 日志统计原因归一化聚合（去具体 host）

- 日期: 2026-08-12
- 类型: Bugfix
- 模块: LogStatistics / LogPanel
- 版本: v1.0
- 状态: ✅ completed

---

## 1. 现象

日志统计弹窗中 WARN/ERROR 原因明细按**含具体 host 的消息原文**拆分为大量类别
（实际日志中 WARN 原因一度达 **521 类**）。同一原因（如 `invalid UUID format`）因
host 不同被拆成多类：

```
===== WARN 原因明细 (521 类) =====
1. [13 次] SKIP: 140.248.186.45:443 - invalid UUID format
2. [ 8 次] SKIP: www.speedtest.net:443 - invalid UUID format
3. [ 5 次] SKIP: 188.114.97.6:443 - invalid UUID format
```

统计值碎片化，无法快速定位真实问题类别。

## 2. 根因

`src/LogStatistics.cpp` 的 `parseLogFile` 按日志消息**原文**聚合
（`++warnAgg[message];` / `++errorAgg[message];`），而 SKIP 消息格式为
`SKIP: <host>[:port] - <reason>`，包含易变的具体 host；host 不同即被拆成不同类别。

## 3. 修复

### 3.1 归一化函数 `extractReason`（src/LogStatistics.cpp 匿名命名空间新增）

```cpp
// 从日志消息提取归一化原因（聚合键），去除具体 host 等易变前缀，
// 使同一原因（不同 host）聚合为一类。
// 规则：消息以 "SKIP: " 开头且含 " - " 分隔符时，取分隔符之后部分
//   "SKIP: <host>[:port] - <reason>"  ->  "<reason>"
//   （如 "SKIP: 140.248.186.45:443 - invalid UUID format" -> "invalid UUID format"）；
// 其余消息保留原文（如 "Failed to parse vmess: not JSON"、"proxy timeout"）。
std::string extractReason(const std::string& message) {
    const std::string prefix = "SKIP: ";
    if (message.compare(0, prefix.size(), prefix) != 0) {
        return message;
    }
    std::size_t sep = message.find(" - ");
    if (sep == std::string::npos) {
        return message;
    }
    std::size_t reasonStart = sep + 3;
    while (reasonStart < message.size() && message[reasonStart] == ' ') {
        ++reasonStart;
    }
    std::size_t reasonEnd = message.size();
    while (reasonEnd > reasonStart &&
           (message[reasonEnd - 1] == ' ' || message[reasonEnd - 1] == '\r')) {
        --reasonEnd;
    }
    return message.substr(reasonStart, reasonEnd - reasonStart);
}
```

### 3.2 聚合键归一化

`parseLogFile` 的 WARN / ERROR 分支聚合键改为：

```cpp
++warnAgg[extractReason(message)];   // 原 ++warnAgg[message];
++errorAgg[extractReason(message)];  // 原 ++errorAgg[message];
```

### 3.3 注释同步

`include/LogStatistics.h` 的 `ReasonCount.reason` 注释更新为
"归一化原因（去时间戳/级别前缀，并去除 `SKIP: <host> - ` 等具体 host 前缀，
使同一原因的不同 host 聚合为一类）"。

## 4. 验证

- `tests/test_log_statistics.cpp` 新增 2 用例（共 10 用例）：
  - `SkipReasonNormalization`：3 行不同 host 的
    `SKIP: <host>:443 - invalid UUID format` 聚合为一类，reason 无 host、count==3；
  - `ReasonNormalizationBoundaries`：无 ` - ` 分隔符的 SKIP 消息与非 SKIP 消息保留原文；
    ERROR 侧不同 host 的 `SKIP: ... - unsupported SS cipher: 'aes-256-cfb'` 聚合为一类。
- 构建：`cmake --build build --parallel 8` → 347/347 编译链接成功
  （仅既有 Utils.cpp PROCESSENTRY32W 噪音警告）。
- 回归：`ctest --test-dir build -V` → 24/24 全部通过（LogStatisticsTest 10/10）。

## 5. 范围与影响

- 仅影响统计聚合键与 `ReasonCount` 注释；`LogPanel::buildStatisticsText` 标题行格式
  （`===== WARN 原因明细 (N 类) =====` + `i+1. [count 次] reason`）已具备，无需改动。
- 既有 8 个 LogStatisticsTest 用例不含 `SKIP: ` 前缀消息，结果不受影响。
