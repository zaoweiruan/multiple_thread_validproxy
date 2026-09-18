---
title: "Plan: 代理池添加代理弹窗增强（v1.0）"
module: src/ui（AddPoolMemberDialog / AppController）+ tests
status: 待实施
date: 2026-09-15
supersedes: （无）
---

# 实施计划：代理池添加代理弹窗增强（v1.0）

规格：`docs/specs/2026-09-15-Spec-AddPoolMemberDialog-Enhance-v1.0.md`

## 任务清单

### Task 1：数据层 — `PoolCandidateItem` + `getPoolCandidateProfiles` 改造

**位置**：`src/ui/AppController.h`、`src/ui/AppController.cpp:342`

**变更**：
1. `AppController.h` 新增结构 `PoolCandidateItem`（profile / delay / message / start_count / crash_count）。
2. `getPoolCandidateProfiles` 签名改为 `std::vector<PoolCandidateItem> getPoolCandidateProfiles(int limit = 300)`。
3. 实现改为 join SQL：`SELECT p.*, e.Delay, e.Message, e.start_count, e.crash_count FROM ProfileItem p INNER JOIN ProfileExItem e ON p.IndexId = e.IndexId WHERE CAST(e.delay AS INTEGER) > 0 LIMIT ?`；`Profileitem::fromStmt` 解析前 36 列 + 手动读 36-39 列组装。

**验收**：仅返回 delay>0；结构含 delay/message/start_count/crash_count；`AddPoolMemberDialog` 编译适配。

### Task 2：UI 列布局改造

**位置**：`src/ui/AddPoolMemberDialog.cpp`（构造 + `buildList`）、`src/ui/AddPoolMemberDialog.h`

**变更**：
1. 窗口尺寸 580×440 → 900×440。
2. 列改为 8 列：IndexId(170) / 协议(80) / 地址(120) / 时延(70) / Region(80) / 健康度(70) / Message(130) / 备注(120)。
3. `candidates_` 类型改为 `std::vector<PoolCandidateItem>`。
4. `buildList` 填充：协议列 `utils::getProtocolName(configtype)`；时延列 delay 原值（空显示 "-"）；健康度列 `"%.3f"`（贝叶斯公式）；Message 列 message 原值。
5. 提示文案候选数改用 `candidates_.size()`（过滤后数量）。

**验收**：8 列齐全；协议列显示协议名称；候选数文案正确。

### Task 3：单击列头排序

**位置**：`src/ui/AddPoolMemberDialog.cpp`、`src/ui/AddPoolMemberDialog.h`

**变更**：
1. 新增 `CandidateSortKey` 枚举 + 静态比较器 `compareCandidates(a, b, key, ascending)`（可测试纯函数）。
2. 绑定 `wxEVT_LIST_COL_CLICK` → `onColumnClick`：记录 `(sortColumn_, ascending_)`，同列切换方向，异列重置升序。
3. `buildList` 内对 `candidates_` 副本按当前排序键排序后重建行；`rowIndexIds_` 与行序同步。

**验收**：单击列头排序生效；升/降切换正确；排序后「加入」收集 IndexId 与选中行一致。

### Task 4：Shift+单击多选验证

**位置**：`src/ui/AddPoolMemberDialog.cpp`

**变更**：确认 `wxListCtrl` 不设 `wxLC_SINGLE_SEL`（现状已满足）；`onOK` 遍历 `wxLIST_STATE_SELECTED` 逻辑不变。

**验收**：构建验证多选行为；无代码变更则记录验证结论。

### Task 5：GTest 单测

**位置**：`tests/test_pool_candidate_sort.cpp`（新文件）+ `CMakeLists.txt` 注册

**变更**：
1. 排序比较器：8 列 × 升/降；字符串大小写不敏感；时延无效值排后；健康度数值序。
2. 健康度公式：start_count==0 → 0.0；稳定代理 → (stable+1)/(start_count+2)；crash 超 start 钳制 0。
3. 有效过滤：`getPoolCandidateProfiles` 仅返回 delay>0（复用 `test/guindb.db`）。

**验收**：新单测全绿；`ctest` 注册。

### Task 6：构建验证 + 文档登记

**位置**：构建产物 + `docs/INDEX.md` + `docs/plans/project-plans-tracker.md`

**变更**：
1. `cmake --build build --parallel 8` 0 error。
2. `ctest` 全量回归无新增失败。
3. `docs/INDEX.md` §8.2 登记本计划；`docs/plans/project-plans-tracker.md` 新增引用行。

**验收**：构建 0 error；ctest 全绿；文档登记完成。

## 依赖关系

- Task 1 → Task 2（数据源先行）
- Task 2 → Task 3（列布局先行，排序依赖列索引）
- Task 5 可与 Task 2/3 并行（比较器/健康度为纯函数，不依赖 UI）
- Task 6 最后执行

## 风险与对策

| 风险 | 对策 |
| --- | --- |
| `Profileitem::fromStmt` 列序假设（Region 第 35 列） | join SQL 用 `p.*` 保持前 36 列序不变，追加列在 36+ 位置 |
| 测试库 `test/guindb.db` 无 ProfileExItem 行 | 有效过滤后候选可能为空；单测用已知含 delay>0 的库或构造数据 |
| 排序后选中状态丢失 | 排序重建行前记录选中 IndexId，重建后恢复（或接受重建清空选中，文档注明） |