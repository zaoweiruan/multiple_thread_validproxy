# 任务报告横向布局与订阅更新统计修正 v1.0

- 日期：2026-11-26
- 类型：Spec（行为调整）
- 关联：`docs/specs/2026-11-24-Spec-TaskReportHover-v1.0.md`、`docs/specs/2026-11-25-Spec-TaskReportFormat-v1.0.md`
- 分支：`feat/task-report-format`

## 1. 目标

1. 任务完成报告改为**横向字段布局**，减少纵向行数，便于悬停浮层一次看全。
2. 悬停浮层尺寸**按内容自适应**（受工作区上限约束），不再固定 560×420。
3. 更新订阅类任务的报告列出**失败订阅的名称（Remarks）与 ID**。
4. 修正统计口径：处于自动更新间隔内而被跳过的订阅计入 `skipped`，不再计入 `failed`。

## 2. 输入

- `task::TaskHandle`：taskId、type、status、createdAt/startedAt/completedAt、targetId、error、progress、parentTaskId。
- `task::TaskStats`：total/success/failed/skipped，以及新增的 `failures`（名称 + ID 列表）。
- `SubitemUpdaterV2::run/runSingle` 的订阅集合（`Remarks` 作为订阅名称）。

## 3. 输出

### 3.1 报告文本（`report_<taskId>.txt` 与内存索引内容）

```text
taskId: task-21473000749800-0
type: 测试订阅 (TEST_SUBSCRIPTION) targetId: 5544178410297751350
status: COMPLETED durationMinutes: 0
createdAt: 2026-11-25 14:22:07 startedAt: 2026-11-25 14:22:07 completedAt: 2026-11-25 14:22:27
---- statistics ----
total: 22 | success: 1 | failed: 21 | skipped: 0
```

- 第 2 行：`type` 与 `targetId` 同行；targetId 为空时只保留 `type`。
- 第 3 行：`status` 与 `durationMinutes` 同行；durationMinutes 无效（-1）时只保留 `status`。
- 第 4 行：`createdAt`/`startedAt`/`completedAt` 同行，仅输出有效字段，空格分隔。
- 统计行：`total: N | success: N | failed: N | skipped: N`。
- `error`、`progressCurrent/progressTotal`、`parentTaskId`、`updateMethods` 保持独立行，位置在统计节之前。
- 失败订阅列表（仅当 `stats.failures` 非空）置于统计节之后：

```text
---- failed subscriptions ----
订阅名称 (sub-id)
```

名称（Remarks）为空时只输出 ID。

### 3.2 浮层尺寸

- 依据报告文本的行数与最长行宽度计算内容尺寸，加内边距后设为浮层尺寸。
- 上限为当前显示器工作区宽 90%、高 80%；下限 360×120；超限由文本控件滚动。
- 浮层位置仍按触发点 + 8px 偏移并钳制进工作区。

### 3.3 统计口径

- `run()`：`skipped` = 因 `shouldSkipUpdate()` 跳过的订阅数；`failed` = `total - success - skipped`。
- `runSingle()`：间隔内跳过 → `total=1, skipped=1, failed=0`。
- 失败列表：`run()` 取最终 `updateSteps_` 中 `FAILED` 的订阅行（排除清洗行），`runSingle()` 失败时取该订阅。

## 4. 前置条件

- 报告文本为 UTF-8；类型标签沿用 `taskTypeToString()`（已含中文 + 枚举名）。
- 悬停浮层只在完成行、且报告已发布时显示（沿用既有语义）。
- 统计不变式 `total == success + failed + skipped` 对所有任务保持成立。

## 5. 边界条件

- 三个时间字段全部无效：第 4 行整行省略。
- `targetId` 为空：第 2 行不输出 `targetId:`。
- `durationMinutes` 无效：第 3 行不输出 `durationMinutes:`。
- 无失败订阅：不输出 `---- failed subscriptions ----` 节。
- 报告内容超长：浮层尺寸受工作区上限约束，文本控件出现滚动条。
- 多显示器：按浮层锚点所在显示器的工作区计算上限。

## 6. 验收标准

1. `TaskReportServiceTest` 新增横向布局断言与失败列表断言，全部通过。
2. `TaskStatsExposureTest` 新增「间隔内跳过计 skipped 不计 failed」（run + runSingle）与「失败订阅名称/ID 进入 stats」用例，全部通过。
3. `[taskreporthover]` UI 用例新增浮层高度小于旧固定值（420）的断言，并在报告文本中断言横向统计行。
4. 非 UI 回归 `ctest -E "^UI_"` 全绿；UI 门禁 `scripts/build-and-test.bat` EXIT 0。
5. `docs/INDEX.md` 与本 Spec 登记；工程进度写入 `docs/plans/project-plans-tracker.md`。
