# Bugfix: StandaloneMonitor 数据列异常（时长=0 / 监控端口为空）

| 项目 | 内容 |
| :--- | :--- |
| 日期 | 2026-08-21 |
| 模块 | `AppController`（纳管与监控数据装配）、`Utils`（时间戳工具） |
| 关联 Spec | `docs/specs/2026-08-13-Spec-ProxyScoring-History-v1.0.md`、`docs/specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.0.md` |
| 前置修复 | `docs/bugfix/2026-08-21-Bugfix-StandaloneMonitorDialog-v1.0.md`（断言崩溃，已验证通过） |

## 1. 现象

StandaloneMonitorDialog 打开正常后，列表中两条纳管记录显示：

1. **运行时长(分) 恒为 0.0**（实际进程已运行约 90 分钟）；
2. **监控端口 显示 "-"**（socksPort=0），日志出现 WARN：
   `Could not resolve inbound port from standalone_<indexId>-xray.json`。

## 2. 根因分析

### Bug B：时长恒为 0 —— 时间戳语义混用

`utils::getCurrentTimestamp()` 返回的是 **Unix 纪元秒十进制字符串**（如 `"1755762158"`），
并非日期时间格式。而 `AppController::getWatchedStandaloneMonitors()` 将其当作
`"yyyy-MM-dd HH:mm:ss"` 使用：

```cpp
const std::string now = utils::getCurrentTimestamp();          // "1755762158"
row.durationMs = durationMsBetween(row.startedAt, now);        // startedAt 来自 DB，格式正确
```

`ProcessInspector::durationMsBetween()` 内部以 `sscanf("%d-%d-%d %d:%d:%d")`
解析，纯数字串解析失败返回 -1 → 函数整体返回 **0**。

同一误用在 `AppController.cpp` 共 **7 处**：

| 位置 | 用途 | 后果 |
| :--- | :--- | :--- |
| `getWatchedStandaloneMonitors` ~L1175 | 对话框 now 基准 | 时长恒 0（本 bug 直接症状） |
| `adoptDanglingStandaloneProxies` ~L1297 | PEB 读取失败时的 procStartedAt 回退 | 会向 DB 写入非法格式 started_at |
| 同上 ~L1348 | 纳管 baselineElapsedMs | 心跳基线恒 0 |
| 正常启动 ~L922 | safeStartedAt 回退 | 同 procStartedAt 回退 |
| 正常启动 ~L937 | baselineMs | 心跳基线恒 0 |
| 接管路径 ~L1005 | newStartedAt 回退 | 同 procStartedAt 回退 |
| 接管路径 ~L1027 | takeoverBaselineMs | 心跳基线恒 0 |

另：正常启动事件消息载荷（~L911）亦传纪元秒串，但 MainFrame 处理器仅消费
isStarted/indexId、不读该消息，按最小改动原则保留不动。

### Bug A：端口为空 —— 纳管配置路径单一

悬垂进程由 **worker 实例**（`bin\worker\validproxy.exe`）启动，其独立配置写在
`bin\worker\config\standalone_*.json`；主实例纳管时仅按自身目录拼接：

```cpp
const std::string adoptedConfigPath =
    utils::getExecutableDir() + "\\config\\" + configFileName;   // bin\config\ ✗
```

文件不存在 → `readStandaloneInboundPort` 返回 0 → 列显示 "-"。
（磁盘取证：目标文件实际位于 `bin\worker\config\standalone_4204447522477277469-xray.json` 等。）

## 3. 修复方案

### Fix B：新增规范格式时间戳工具（最小侵入，不改全局语义）

`getCurrentTimestamp()` 的纪元秒语义被既有代码广泛依赖（排序键等），**不做全局修改**；
新增专用函数：

```cpp
// Utils.h / Utils.cpp
std::string getCurrentTimestampFormatted();   // "%Y-%m-%d %H:%M:%S" 本地时间
```

并将上述 3 处误用点全部替换为该函数。

### Fix A：纳管配置路径候选列表

按已知两种部署布局依次探测，首个命中即用：

```text
<exeDir>\config\<configFileName>          （主实例自启进程）
<exeDir>\worker\config\<configFileName>   （worker 实例自启进程）
```

## 4. 测试（TDD）

`tests/test_utils.cpp` 新增 `GetCurrentTimestampFormattedTest`：

- `MatchesDatetimeFormat`：输出可被 `%d-%d-%d %d:%d:%d` 完整解析且长度=19；
- `CompatibleWithDurationMsBetween`：同串相减为 0（保证与解析器兼容）。

## 5. 验证清单

- [x] 单测：`ctest -R UtilsTest -V` 通过（2 个新用例 GREEN）
- [x] 全量回归：`ctest -V` 30/31 通过（NetworkMonitorTest 环境抖动，复跑通过——2026-08-18 已知偶发同款）
- [ ] GUI 回归：重启后 Ctrl+M，时长随刷新递增、端口列显示真实 SOCKS 端口
