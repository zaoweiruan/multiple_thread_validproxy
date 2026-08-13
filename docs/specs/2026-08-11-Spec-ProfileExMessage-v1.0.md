# Spec: ProfileExItem message 字段双时间戳（测试时间 + 启动时间）

- 日期: 2026-08-11
- 类型: Spec
- 模块: ProfileExItemDAO / AppController / SubitemUpdaterV2
- 版本: v1.0
- 状态: ✅ completed

---

## 1. 需求

对 `ProfileExItem.message`（代理列表中显示的 Message 列）只写入两种信息：

1. **测试时间** — 代理被测试为有效（成功连通）的时刻，格式 `yyyy-MM-dd HH:mm:ss`。
2. **启动时间** — 代理作为独立代理启动（standalone proxy 启动成功）的时刻，同一格式。

用户明确约束（原话）：

> "message显示为测试时间+启动时间，测试时间始终在前，'+'不能省略"

> "始终保留 + 号"

**恒定格式**：`<测试时间>+<启动时间>`，二者之间恒有一个 `+`：

| 场景 | message 值 |
|------|-----------|
| 仅测试过（未启动过） | `2026-08-11 16:00:00+` |
| 仅启动过（未测试成功过） | `+2026-08-11 16:05:00` |
| 两者都有 | `2026-08-11 16:00:00+2026-08-11 16:05:00` |

**变更保留规则**：当一类信息发生变更时，**保留另一类信息**。

---

## 2. 现状分析（修改前）

### 2.1 现有 message 写入路径

| 路径 | 位置 | 现状 |
|------|------|------|
| 单代理测试成功 | `src/ProfileExItemDAO.cpp` `updateTestResult()` L52 | 写 `"OK"` |
| 单代理测试失败 | 同上 | 写 `curlMsg` 或 `"FAILED"` |
| 批量测试 | `updateTestResultBatch()` L103 | 同上（每项） |
| 订阅导入 | `src/SubitemUpdaterV2.cpp` L715 | 写 `"NOT_TESTED"` |
| 数据库同步 | `SubitemUpdaterV2.cpp` L1070/L1100 | 原样复制源库值 |
| 配置导入 | `src/config/ProfileConfigRepository.cpp` L70 | 原样复制 |

### 2.2 现存问题

- message 被测试结果字符串（OK/FAILED/curlMsg/NOT_TESTED）覆盖，丢失历史信息。
- 无法区分"测试通过时间"与"启动时间"。

---

## 3. 设计方案

### 3.1 message 语法

```
message := testPart ('+' startupPart)?
```

- 以第一个 `+` 为分隔符。
- `+` 之前 = 测试时间，`+` 之后 = 启动时间。
- 任一侧为空（不存在）时 `+` 仍保留（位置占位）。
- 两侧时间戳均为 `yyyy-MM-dd HH:mm:ss`（19 字符，严格模式校验：`\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}`）。

### 3.2 旧值兼容

- 旧的 `"OK"` / `"FAILED"` / curlMsg / `"NOT_TESTED"` / 空串均**不含** `+`，视为两侧均无效 → 整体被新值替换。

### 3.3 合并规则（仅替换自身一侧，保留另一侧）

- **测试成功写入**：解析现有 message 的启动侧（`+` 之后）；若为合法时间戳则保留，否则丢弃 → 结果 `新测试时间+[原启动时间]`。
- **启动写入**：解析现有 message 的测试侧（`+` 之前）；若为合法时间戳则保留，否则丢弃 → 结果 `[原测试时间]+新启动时间`。
- **测试失败**：不再写任何 message（两种信息之外的信息不再写入）；保留现有 message 原值（若行不存在则置空）。

### 3.4 变更点

| # | 文件 | 变更 |
|---|------|------|
| 1 | `include/Profileexitem.h` | 新增静态辅助：`formatTestMessage` / `formatStartupMessage` / `currentTimeString` / `isMessageTimestamp`（私有）；新增方法 `updateStartupTime` |
| 2 | `src/ProfileExItemDAO.cpp` | `updateTestResult` / `updateTestResultBatch` 改为新逻辑（SELECT 扩展取 Message）；实现新方法 |
| 3 | `src/ui/AppController.cpp` | `startStandaloneProxy` 启动成功后调用 `updateStartupTime(indexId)` |
| 4 | `src/SubitemUpdaterV2.cpp` | 导入初始 message `"NOT_TESTED"` → `""`（空） |

### 3.5 不做变更（记录原因）

- 数据库同步 / 配置导入路径仅**原样复制**现有值，不产生新语义，保持原样。
- UI（ProxyListModel / ProxyListPanel）仅展示字符串，无需改动。
- `updateTestResult` 签名（含 `curlMsg` 参数）保持不变以兼容调用方（ProxyFinder / ProxyBatchTester / ProxyTestResultSink）；`curlMsg` 不再写入 message。

### 3.6 时间戳生成

使用本地时间 `yyyy-MM-dd HH:mm:ss`（`localtime_s` + `strftime`，Windows），保证 UI 列可读。不使用 `utils::getCurrentTimestamp()`（返回 epoch 秒数字符串）。

---

## 4. 测试计划

新增 `tests/test_profile_ex_item_dao.cpp`（Google Test，内存库，DDL 与生产 `ProfileExItem` 一致）：

1. `formatTestMessage` 单元用例（保留启动侧 / 替换测试侧 / 旧值整体替换 / `+` 恒在）。
2. `formatStartupMessage` 单元用例（保留测试侧 / 替换启动侧 / 旧值整体替换）。
3. `updateTestResult` 成功 → `"<时间>+"`；已有 `<旧测试>+<启动>` → 保留启动侧；失败 → 保留现有 message。
4. `updateTestResultBatch` 同规则。
5. `updateStartupTime` → `"+<时间>"`；已有测试侧 → 保留测试侧。

CMake 注册：`test_profile_ex_item_dao`，链接 `gtest_main gtest libsqlite3.a`，源含 `src/ProfileExItemDAO.cpp`（`ProfileExItemDAO` 构造函数会执行 `migrateTable`，对内存库的 ALTER 无害）。

---

## 5. 验收标准

- [x] 测试成功 → message 写入 `"<测试时间>+"`，保留原启动时间。
- [x] 代理启动成功 → message 写入 `"+<启动时间>"`（或合并进已有测试时间）。
- [x] 测试失败 → 不覆盖 message。
- [x] 导入新代理 → message 为空，不再出现 `"NOT_TESTED"`。
- [x] `+` 分隔符恒存在。
- [x] 全仓无新增 `auto`。
- [x] 构建通过 + ctest 全绿。
