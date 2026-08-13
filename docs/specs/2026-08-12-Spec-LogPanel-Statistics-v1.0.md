# Spec: LogPanel 清空按钮文案调整 + 异步日志统计弹窗

- 日期: 2026-08-12
- 类型: Spec
- 模块: LogPanel / Logger / Events
- 版本: v1.0
- 状态: ✅ completed（实现已完成并通过验证：构建 348/348、ctest 24/24、LogStatisticsTest 8/8）

---

## 1. 需求

1. 将 LogPanel 工具栏 Clear 按钮文本由 `"Clear"` 改为 **`"清空日志窗口"`**。
2. 新增**日志统计**功能：异步解析当前日志文件，统计各级别
   （TRACE/DEBUG/INFO/REPORT/WARN/ERROR）出现次数并**弹窗**输出；
   对 **WARN / ERROR** 级别额外输出**原因**（日志消息内容）及**统计值**
   （该原因出现次数）。
3. 输出本文档用于评审。

---

## 2. 现状分析（修改前）

### 2.1 日志写盘格式（解析输入契约）

`LoggerInstance::write`（src/LoggerInstance.cpp）每行固定格式，每行写盘后立即
`flush`：

```
[YYYY-MM-DD HH:MM:SS] [LEVEL] message
```

- 级别 token 位于**第二个方括号**内，取值：
  `TRACE` / `DEBUG` / `INFO` / `REPORT` / `WARN` / `ERROR`
  （`LoggerInstance::levelToString`，ERR 序列化为 `ERROR`）。
- 时间戳格式 `%Y-%m-%d %H:%M:%S`。

### 2.2 现状问题

| 问题 | 说明 |
|------|------|
| Clear 文案与 UI 语言不统一 | 界面其余文案为中文（如「打开日志」），Clear 仍为英文 |
| 缺少日志洞察入口 | 排查问题时需手工打开日志文件或用文本工具统计级别分布，无内置统计能力 |
| 阻塞风险 | 若直接在 UI 线程解析大日志文件（实测可达上万行）会卡界面，必须异步 |

---

## 3. 方案设计

### 3.1 分层与异步模型

```
┌──────────────── UI 线程 ────────────────┐
│ LogPanel: [清空日志窗口][日志统计][打开日志]  │
│    onLogStatistics ──启动 std::thread──▶  │
│    onLogStatisticsResult(事件) → wxDialog  │
└───────────────▲──────────────────────────┘
                │ wxQueueEvent(this, LogStatisticsEvent)
┌───────────────┴────── 工作线程 ──────────┐
│ parseLogFile(logPath) 纯解析（无 wx 依赖）  │
└──────────────────────────────────────────┘
```

- 解析器 `parseLogFile` 为**纯标准库**模块（新文件
  `include/LogStatistics.h` + `src/LogStatistics.cpp`），不依赖 wxWidgets，
  可被 Google Test 独立单测（tests/test_log_statistics.cpp）。
- 工作线程解析完成后经 `wxQueueEvent` 投递自定义事件
  `LogStatisticsEvent` 回 UI 线程（复用项目既有 LogMessageEvent 的
  worker→UI 转发模式）。
- **线程生命周期**：`LogPanel` 持有 `std::thread statsThread_` 成员；
  触发统计时若线程仍在运行（可重入）则直接返回不叠加；析构函数
  `join()` 保证无悬垂 `this`（与 wxQueueEvent 内部队列锁互斥安全）。
  解析为毫秒级，join 阻塞可接受。

### 3.2 统计口径与展示

- 每行按「第二个方括号内级别 token」判定级别；无法识别的行
  （空行、截断行、非日志行）**不计入**任何计数。
- WARN / ERROR 的「原因」= 去掉 `[ts] [LEVEL] ` 前缀后的消息原文，
  按原文**聚合计数**（相同原因合并，避免上万条重复错误刷屏）。
- 展示按原因出现次数**降序**（同次数保持字典序）。
- 弹窗使用**自定义 wxDialog + 只读多行 wxTextCtrl**（可滚动、可选中复制），
  而非 `wxMessageBox`（MSW 上长文本会截断，且不可滚动）。

弹窗内容示例：

```
日志文件: bin/worker/log/ui_20260812_*.log
有效日志行: 5,000   （无法识别的行不计）

TRACE: 0    DEBUG: 3,200    INFO: 1,200
REPORT: 0   WARN: 400        ERROR: 200

── WARN 原因（共 400 次 / 5 类）──
1. [150 次] chacha20-poly1305 白名单外 cipher 被忽略: ...
2. [120 次] ...

── ERROR 原因（共 200 次 / 3 类）──
1. [180 次] xray 进程退出码非 0: ...
```

### 3.3 接口定义

`include/LogStatistics.h`（新）：

```cpp
struct LogLevelCounts {
    std::size_t trace = 0;
    std::size_t debug = 0;
    std::size_t info = 0;
    std::size_t report = 0;
    std::size_t warn = 0;
    std::size_t error = 0;
    std::size_t total = 0;   // 有效日志行数（级别可识别）
};

struct ReasonCount {
    std::string reason;      // 原因 = 消息原文（去时间戳/级别前缀）
    std::size_t count = 0;   // 统计值 = 该原因出现次数
};

struct LogStatisticsResult {
    LogLevelCounts counts;
    std::vector<ReasonCount> warnReasons;   // 降序
    std::vector<ReasonCount> errorReasons;  // 降序
    bool fileOpened = false;                // 文件是否成功打开
};

LogStatisticsResult parseLogFile(const std::string& filePath);
```

实现要点（src/LogStatistics.cpp）：

- 逐行 `std::getline`；行首须为 `[`，取第一个 `]` 与第二个 `[`/`]` 之间
  的 token 作为级别，`]` 后跳过空格为消息。
- 内部用 `std::map<std::string, std::size_t>` 聚合原因，转
  `std::vector<ReasonCount>` 后 `std::stable_sort` 按 count 降序
  （稳定排序保留字典序，消除排序抖动）。
- 全栈**禁止 `auto`**，显式类型。

`Events.h/.cpp`（新增 `LogStatisticsEvent`，携带 `LogStatisticsResult`
值拷贝，随 `wxDEFINE_EVENT(wxEVT_LOG_STATISTICS, ...)` 注册）。

### 3.4 LogPanel 工具栏变更

| 项 | 变更 |
|----|------|
| 按钮文本 | `"Clear"` → `"清空日志窗口"` |
| 新按钮 | `statsBtn_`（文本「日志统计」，ID `ID_LOG_STATISTICS`），置于 Clear 之后、打开日志之前 |
| 事件表 | 追加 `EVT_BUTTON(ID_LOG_STATISTICS, LogPanel::onLogStatistics)`（沿用既有具体 ID 绑定，无 wxID_ANY 陷阱） |
| 线程成员 | `std::thread statsThread_;` 析构 `join()` |

按钮顺序语义：清空窗口 → 统计当前文件 → 打开文件（三个操作同组闭环）。

---

## 4. 变更文件清单

| 文件 | 变更 |
|------|------|
| `include/LogStatistics.h` | 新增：结构体 + `parseLogFile` 声明 |
| `src/LogStatistics.cpp` | 新增：解析与聚合实现（纯 std） |
| `src/ui/Events.h` | 新增 `LogStatisticsEvent` 声明 + `wxEVT_LOG_STATISTICS` |
| `src/ui/Events.cpp` | 新增 `wxDEFINE_EVENT(wxEVT_LOG_STATISTICS, LogStatisticsEvent)` |
| `src/ui/LogPanel.h` | enum 增 `ID_LOG_STATISTICS`；`onLogStatistics`/`onLogStatisticsResult` 声明；`statsBtn_`、`statsThread_` 成员 |
| `src/ui/LogPanel.cpp` | Clear 文本改中文；新增统计按钮；事件表追加；异步线程实现；结果弹窗 wxDialog 实现；析构 join |
| `CMakeLists.txt` | CORE_SOURCES 追加 `src/LogStatistics.cpp`；新增 `test_log_statistics` 目标 |
| `tests/test_log_statistics.cpp` | 新增：8 组单测（见 §5） |

---

## 5. 验证方案

- 单元测试 `test_log_statistics`（Google Test）：
  1. 各级别计数正确（含 total）；
  2. WARN 同原因聚合计数；
  3. ERROR 同原因聚合计数；
  4. 畸形行（空行/无级别 token/截断最后一行）不计入；
  5. 原因按次数降序（同次数字典序）；
  6. 消息内容含 `[ERROR]` 文本不误判（只解析前两个括号）；
  7. 文件不存在 → `fileOpened == false`；
  8. 空文件 → 打开成功、total == 0。
  临时日志文件写入 `std::filesystem::temp_directory_path()`，测试自清理。
- `cmake --build build --parallel 8` 全量编译；`ctest -V` 全量回归
  （既有 8 套件 + 新增 1 套件全部通过）。
- 手工验证路径（GUI）：点击「日志统计」界面不卡顿（异步）；弹窗显示
  各级别计数；WARN/ERROR 按原因列出次数；长文本可滚动复制；快速连点
  不叠加线程；关闭窗口期间统计完成不崩溃。

---

## 6. 范围与约束

- 只统计 `Logger::getFilePath()` 指向的**当前日志文件**（快照语义：统计
  的是解析时刻已写盘的内容，统计期间新增日志不保证纳入，文档说明即可）。
- 不修改 Logger 写盘格式；不影响既有「打开日志」「清空日志窗口」行为。
- 全栈禁止 `auto`；C++17；构建仅允许既有噪音警告。
- 历史文档引用：LogPanel 工具栏演进记录见
  `2026-08-06-Bugfix-MainFrame-StatusBar-LogFile-v1.0.md` 与
  `2026-08-12-Spec-LogPanel-OpenLogButton-v1.0.md`。
