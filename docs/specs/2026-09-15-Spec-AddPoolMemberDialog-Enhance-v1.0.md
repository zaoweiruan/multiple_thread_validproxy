---
title: "Spec: 代理池添加代理弹窗增强（有效过滤 + 扩展列 + 排序 + 多选）（v1.0）"
module: src/ui（AddPoolMemberDialog / AppController）+ tests
status: 待实施
date: 2026-09-15
supersedes: （无 — 新增规格；范围内增强 AddPoolMemberDialog 既有责权）
---

# 规格说明：代理池添加代理弹窗增强（v1.0）

## 1. 目标

| 目标 | 说明 | 验收标准 |
| --- | --- | --- |
| G1 有效过滤 | 代理池添加代理弹窗（`AddPoolMemberDialog`）候选代理只显示有效代理 | 候选列表仅含 `ProfileExItem.delay > 0` 的代理；提示文案候选数同步更新 |
| G2 扩展列 | 候选列表新增「时延 / Region / 健康度 / Message」显示项；原「类型」列改为代理协议名称 | 列表 8 列：IndexId / 协议 / 地址 / 时延 / Region / 健康度 / Message / 备注；协议列显示 `utils::getProtocolName(configtype)` 结果（如 VMess/VLESS/SOCKS）而非原始数字 |
| G3 单击排序 | 支持单击列头排序（升/降切换） | 单击任意列头按该列排序，再次单击同列切换方向；排序后选中行与 IndexId 映射保持正确 |
| G4 多选 | 支持 Shift+单击范围多选 | wxListCtrl 保持多选模式（不设 `wxLC_SINGLE_SEL`），Shift+单击选中连续区间，Ctrl+单击切换单项；「加入」收集全部选中 IndexId |
| G5 文档交付 | 本规格作为实施前设计文档，登记至 `docs/INDEX.md` | 本文件写入 `docs/specs/`，`docs/INDEX.md` §8.2 新增登记行，`docs/plans/project-plans-tracker.md` 新增引用行 |

## 2. 现状与差距

### 2.1 现状

**2.1.1 `AddPoolMemberDialog`（`src/ui/AddPoolMemberDialog.h/.cpp`）**

- wxDialog，标题「选择代理」，尺寸 580×440。
- 数据源：构造时 `controller_->getPoolCandidateProfiles(500)` → `std::vector<db::models::Profileitem>`。
- 列表 `wxListCtrl`（`wxLC_REPORT | wxLC_HRULES | wxLC_VRULES`，无 `wxLC_SINGLE_SEL`）4 列：IndexId(220) / 类型(80) / 地址(130) / 备注(140)。
- 「类型」列显示 `configtype` 原始数字字符串（如 "4" / "10"），非协议名称。
- 支持文本搜索过滤（IndexId / 地址 / 备注，大小写不敏感）。
- `onOK` 遍历 `wxLIST_STATE_SELECTED` 收集选中 IndexId → `EndModal(wxID_OK)`；调用方（`StandaloneFloatingWidget::onAdd` 等）遍历 `injectProxyToPool(idx)`。
- 无列排序。

**2.1.2 `AppController::getPoolCandidateProfiles`（`src/ui/AppController.cpp:342`）**

- 签名：`std::vector<db::models::Profileitem> getPoolCandidateProfiles(int limit = 300)`。
- 实现：`SELECT IndexId FROM ProfileItem LIMIT n` 后逐个 `dao.getByIndexId(idx)` 组装；**不 join `ProfileExItem`**（注释明确：picker 只需基础行，测试库无扩展行也能工作）。
- 仅被 `AddPoolMemberDialog` 一处调用。

**2.1.3 数据模型**

- `db::models::Profileitem`（`include/Profileitem.h`）：`indexid / configtype / address / port / remarks / region(第35列) / ...`。
- `db::models::ProfileExItem`（`include/ProfileExItem.h`）：`indexid / delay / speed / sort / message / consecutive_failures / start_count / total_runtime_ms / crash_count`。
- `Profileitem::fromStmt` 只读前 36 列（Region 在第 35 列，`sqlite3_column_count(stmt) > 35` 守卫），`SELECT p.*` 后追加列位于 36+ 位置被安全忽略。

### 2.2 差距

| # | 差距 | 影响 |
| --- | --- | --- |
| D1 | 候选列表包含无效代理（delay ≤ 0 或从未测试） | 用户需在大量无效代理中筛选，添加后成员立即失败 |
| D2 | 无时延/Region/健康度/Message 列 | 用户无法按质量指标选择候选 |
| D3 | 「类型」列显示 configtype 数字 | 不直观，用户无法识别协议 |
| D4 | 无列排序 | 大候选集（上限 500）无法按指标排序浏览 |
| D5 | 多选行为未显式验证 | Shift+单击范围多选依赖 wxListCtrl 默认行为，无测试保障 |

## 3. 设计

### 3.1 数据层：`PoolCandidateItem` + `getPoolCandidateProfiles` 改造

**新增结构 `PoolCandidateItem`**（定义于 `src/ui/AppController.h`，与 `getPoolCandidateProfiles` 同处）：

```cpp
struct PoolCandidateItem {
    db::models::Profileitem profile;  // 基础字段（indexid/configtype/address/remarks/region...）
    std::string delay;                // ProfileExItem.delay（有效过滤后恒 > 0）
    std::string message;              // ProfileExItem.message（"测试时间+启动时间"）
    int start_count = 0;              // ProfileExItem.start_count
    int crash_count = 0;              // ProfileExItem.crash_count
};
```

**修改 `getPoolCandidateProfiles`**：

- 签名：`std::vector<PoolCandidateItem> getPoolCandidateProfiles(int limit = 300)`。
- SQL（与项目既有 join 写法一致，参考 `ProfileitemDAO.cpp:57` / `AppController.cpp:531`）：

```sql
SELECT p.*, e.Delay, e.Message, e.start_count, e.crash_count
FROM ProfileItem p
INNER JOIN ProfileExItem e ON p.IndexId = e.IndexId
WHERE CAST(e.delay AS INTEGER) > 0
LIMIT ?;
```

- 实现：`sqlite3_prepare_v2` → step 循环 → `Profileitem::fromStmt(stmt)` 解析前 36 列 → 手动读第 36/37/38/39 列（Delay/Message/start_count/crash_count）组装 `PoolCandidateItem`。
- 有效过滤语义：`CAST(e.delay AS INTEGER) > 0`（项目既有「有效」定义，与 `exportShareLinks` / `countValidBySubId` / `RegionBatchResolver` 一致）。

### 3.2 UI 层：`AddPoolMemberDialog` 改造

**列布局**（窗口 580 → 900）：

| 列 | 宽度 | 内容 | 排序键 |
| --- | --- | --- | --- |
| IndexId | 170 | `profile.indexid` | 字符串 |
| 协议 | 80 | `utils::getProtocolName(profile.configtype)` | 字符串 |
| 地址 | 120 | `profile.address` | 字符串 |
| 时延 | 70 | `delay` 原始值；空显示 "-" | 数值（atoi，无效=-1） |
| Region | 80 | `profile.region` | 字符串 |
| 健康度 | 70 | `"%.3f"`（贝叶斯公式） | 数值 |
| Message | 130 | `message` 原始字符串 | 字符串 |
| 备注 | 120 | `profile.remarks` | 字符串 |

**健康度公式**（与 `ProxyListModel::rebuildMaps` 一致，`src/ui/ProxyListModel.cpp:97-104`）：

```
stable = max(start_count - crash_count, 0)
start_count == 0 → 0.0
否则 health = (stable + 1) / (start_count + 2)
```

（弹窗场景无运行时数据，不应用 `setRunningDurations` 的 +0.3 运行加成。）

**排序**：

- 绑定 `wxEVT_LIST_COL_CLICK` → `onColumnClick`：记录 `(sortColumn, ascending)`，同列再击切换方向，异列重置为升序。
- 排序比较器提取为**可测试纯函数**（`AddPoolMemberDialog` 静态方法或自由函数）：

```cpp
enum class CandidateSortKey { IndexId, Protocol, Address, Delay, Region, Health, Message, Remarks };
// 返回 true 表示 a 应排在 b 前（按 key + ascending）
bool compareCandidates(const PoolCandidateItem& a, const PoolCandidateItem& b,
                       CandidateSortKey key, bool ascending);
```

- 字符串列：大小写不敏感比较（`wxStricmp` 语义）；数值列：时延按 `atoi`（无效=-1）、健康度按 double。
- 排序在 `buildList` 内对 `candidates_` 副本排序后重建行，`rowIndexIds_` 与行序同步。

**多选**：

- 保持 `wxListCtrl` 默认多选（不设 `wxLC_SINGLE_SEL`），Shift+单击范围多选 / Ctrl+单击切换由 wxMSW 原生支持。
- `onOK` 现有遍历 `wxLIST_STATE_SELECTED` 逻辑不变，天然收集多选。

### 3.3 测试策略（TDD）

| 测试 | 位置 | 覆盖 |
| --- | --- | --- |
| 排序比较器 | `tests/test_pool_candidate_sort.cpp`（GTest） | 8 列 × 升/降；字符串大小写不敏感；时延无效值排后；健康度数值序 |
| 健康度公式 | 同上 | start_count==0 → 0.0；稳定代理 → (stable+1)/(start_count+2)；crash 超 start 钳制 0 |
| 有效过滤 | `tests/`（复用 `test/guindb.db`） | `getPoolCandidateProfiles` 仅返回 delay>0；返回结构含 delay/message/start_count/crash_count |
| 多选/排序 UI 行为 | 构建验证 + 现有 UI 测试回归 | 不新增 UI 自动化用例（弹窗交互依赖真实 GUI，成本高）；以纯逻辑单测 + 构建验证保障 |

## 4. 验收标准

1. 构建 0 error（`cmake --build build --parallel 8`）。
2. 新增 GTest 单测全绿；`ctest` 全量回归无新增失败。
3. 弹窗候选列表仅含 delay>0 代理；8 列齐全；协议列显示协议名称。
4. 单击列头排序生效，升/降切换正确；排序后「加入」收集的 IndexId 与选中行一致。
5. Shift+单击多选可用（wxListCtrl 默认行为，构建验证）。
6. `docs/INDEX.md` §8.2 与 `docs/plans/project-plans-tracker.md` 登记本规格与实施计划。

## 5. 相关文档

- `docs/plans/2026-09-15-Plan-AddPoolMemberDialog-Enhance-v1.0.md`（实施计划）
- `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`（统一池监控，AddPoolMemberDialog 调用方上下文）
- `docs/specs/2026-08-12-Spec-ProfileExMessage-v1.0.md`（message 双时间戳格式）