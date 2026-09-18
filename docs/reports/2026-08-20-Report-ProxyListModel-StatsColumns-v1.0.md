# Report: ProxyListPanel Starts / Runtime / Health 三列计算逻辑提取

- 日期: 2026-08-20
- 类型: Report（代码逻辑提取与现状记录）
- 模块: ProxyListModel / ProxyListPanel (UI 代理列表面板)
- 版本: v1.1（修订：纳入 RuntimeHistory PID 匹配 + 心跳 baseline 会话的语义影响）
- 代码基线: `src/ui/ProxyListModel.cpp` / `src/ui/ProxyListModel.h`

> **修订记录（v1.0 → v1.1, 2026-08-20）**：本报告提取的三列数据源受
> `docs/specs/2026-08-20-Spec-RuntimeHistory-PidMatching-v1.0.md` 会话影响：
> `proxy_runtime_history` 新增 `pid` 列、`insertStart`/`findInProgressHistory` 改为
> `(indexId, pid, started_at)` 三因子、`started_at` 改为系统进程创建时间、
> 心跳 `duration_ms` 含纳管前 baseline。三列计算逻辑本身未变，但**数据语义已升级**，
> 详见 §2.2 / §3.2 / §7 标注。

---

## 1. 概述

`ProxyListPanel` 的 **Starts**（`COL_START_COUNT`）、**Runtime**（`COL_TOTAL_RUNTIME_MS`）、**Health**（`COL_HEALTH`）三列展示值均来自 `ProxyListModel` 内部缓存的三个 map：

| 列 | 显示列枚举 | 缓存 map | 显示格式 |
|----|-----------|----------|----------|
| Starts | `COL_START_COUNT` (10) | `startCountMap_` | `std::to_string(int)` |
| Runtime | `COL_TOTAL_RUNTIME_MS` (11) | `runtimeMap_` + `runningDurations_` | `%lld:%02lld`（分:秒，如 `1:23`） |
| Health | `COL_HEALTH` (12) | `healthMap_` | `%.3f`（3 位小数，如 `0.857`） |

**数据源**：`ProfileExItem` 三个聚合列 —— `start_count`（累计启动次数）、`crash_count`（累计崩溃次数）、`total_runtime_ms`（累计运行毫秒）。聚合列由 `ProfileExItemDAO::insertStart` / `finalizeStop` 在会话结束回写时累加。

**刷新时机**：
1. `rebuildMaps()` — 全量重建（DB 加载 / refreshResults 后）
2. `setRunningDurations()` — 每 3s 心跳快照（后台线程查 `proxy_runtime_history` 中 `ended_at IS NULL` 行）

---

## 2. Starts（启动次数）列

### 2.1 计算逻辑

```
startCountMap_[indexid] = ex.start_count
```

- **写入点**：`rebuildMaps()` L68
- **读取/显示**：`GetValueByRow` L256-260 → `std::to_string(map[idx])`，缺省 `"0"`
- **Getter**：`getStartCount()` L509-513 → map 查不到返回 `0`
- **排序**：`Compare` L391-399 → 数值比较 `(sA > sB) - (sA < sB)`

### 2.2 语义

数据库已持久化的启动总次数，**不含**当前正在运行的会话（运行中会话结束 `finalizeStop` 后才 `+1` 回写）。因此正在运行代理的 Starts 列不会实时增长。

**v1.1 补充（PID 匹配会话后）**：`start_count` 的 +1 时机受
`insertStart` 调用点影响。本会话将悬垂纳管/正常启动/R6 接管统一改为
"系统进程创建时间 + pid" 三因子匹配后：

- 外部新启动进程（无匹配 in-progress 会话）→ `insertStart` → starts +1 ✅；
- GUI 崩溃重启纳管同一进程（同 pid + 同 started_at）→ `findInProgressHistory`
  复用会话 → 不 +1 ✅；
- 同 indexId 崩溃重启后新实例（新 pid）→ 不复用旧会话 → +1 ✅。

修复前仅按 `index_id` 匹配时，崩溃重启后的新进程会误复用旧 in-progress 会话
（starts 不 +1），本会话已消除该偏差。

---

## 3. Runtime（运行时长）列

### 3.1 计算逻辑

```
显示值 = runtimeMap_[indexid]（历史累计） + runningDurations_[indexid]（当前会话心跳时长）
```

- **历史部分 `runtimeMap_`**：`rebuildMaps()` L72-77
  ```cpp
  if (ex.total_runtime_ms > 0) {
      runtimeMap_[ex.indexid] = ex.total_runtime_ms;        // 正常回写
  } else if (runtimeMap_.find(ex.indexid) == runtimeMap_.end()) {
      runtimeMap_[ex.indexid] = 0;                           // 新行占位 0
  }
  // else: 保留上一轮非零值 —— 运行中会话未回写 DB（仍 0）时不覆盖历史基数
  ```
- **实时部分 `runningDurations_`**：`setRunningDurations()` L97-98 **整体替换**（幂等，非累加）
  ```cpp
  bool changed = (runningDurations_ != runningMs);
  runningDurations_ = runningMs;
  ```
- **合并显示**：`GetValueByRow` L261-283 → `totalMs = base + running`，`%lld:%02lld`（分:秒）
- **Getter**：`getRuntime()` L485-499 → `runtimeMap_[id] + runningDurations_[id]`（缺省 0）
- **排序**：`Compare` L400-408 → **仅比较 `runtimeMap_`（历史部分）**，见 §6

### 3.2 幂等性保证

`setRunningDurations()` 采用**整表替换**而非 `+=` 累加：心跳快照本身是"当前会话绝对时长"，若累加则每 3s 刷新 Runtime 列翻倍膨胀（5s→15s→30s）。`changed` 返回值供调用方跳过无变化时的 UI 重绘（2026-08-18 修复）。

**v1.1 补充（心跳 baseline 修正后）**：后台查询的 `duration_ms` 现在
= **纳管前 baseline（进程真实启动至纳管时刻） + 监控增量（纳管时刻至当前）**，
即从进程系统启动时间起算。对正常启动路径 baseline≈0（进程刚创建），
对悬垂纳管/R6 接管路径 baseline = 纳管前已运行时长。因此 `runningDurations_`
（及最终 `finalizeStop` 累加的 `total_runtime_ms`）**包含纳管前已运行时间**，
Runtime 列展示与"系统启动时间"目标一致（修复前会话时长从纳管时刻起算，丢失纳管前时间）。

---

## 4. Health（健康分）列

### 4.1 基础分（贝叶斯平滑）

```
stable = max(start_count - crash_count, 0)
base   = (stable + 1) / (start_count + 2)          // 仅当 start_count > 0
       = 0.0                                       // 当 start_count == 0（冷启动）
```

- **写入点**：`rebuildMaps()` L79-86
- **冷启动特判**（2026-08-20 修复，v1.0.3）：`start_count == 0` → `healthMap_[id] = 0.0`，不再产出贝叶斯先验 `0.5`，与 `getHealth()` fallback 及 `ProxyScorer::compute` 冷启动 `history_score=0.0` 语义对齐

### 4.2 运行加成（30 分钟斜坡）

`setRunningDurations()` L107-133，**仅当 `start_count > 0`**：

```
RAMP_MS = 1800000   // 30 分钟达到最大加成
WEIGHT  = 0.3       // 最大加成 0.3

if running > 0:
    ramp  = min(running / RAMP_MS, 1.0)
    bonus = ramp * WEIGHT

h = base + bonus
if h > 1.0: h = 1.0        // 封顶 1.0
```

- **冷启动代理（`start_count == 0`）运行中不授予 bonus**（2026-08-20 修复）
- **显示**：`GetValueByRow` L285-290 → `wxString::Format("%.3f", h)`（3 位小数）
- **Getter**：`getHealth()` L502-506 → map 查不到返回 `0.0`
- **排序**：`Compare` L409-417 → 按 `healthMap_` 数值比较（已含运行加成）

### 4.3 语义示例

| 场景 | 公式 | 值 |
|------|------|-----|
| 未测试（`start_count=0`） | `0.0`（无 bonus） | `0.000` |
| 5 启 0 崩，未运行 | `(5-0+1)/(5+2)` | `0.857` |
| 5 启 0 崩，运行 2 分钟 | `0.857 + min(120s/1800s,1)×0.3` | `0.877` |
| 5 启 0 崩，运行 30+ 分钟 | `0.857 + 0.3 = 1.157 → 封顶` | `1.000` |

---

## 5. 数据流图

```
DB (ProfileExItem 聚合列)
   start_count / crash_count / total_runtime_ms
   │
   ├── rebuildMaps()（全量重建）
   │     ├── startCountMap_[id] = start_count              → Starts 列
   │     ├── runtimeMap_[id]    = total_runtime_ms (>0 才覆盖)  → Runtime 历史
   │     └── healthMap_[id]     = start_count>0 ? (stable+1)/(start_count+2) : 0.0
   │
DB (proxy_runtime_history 中 ended_at IS NULL 行，后台线程每 3s 查询)
   │     （v1.1: 行含 pid 列；started_at = 系统进程创建时间；duration_ms = baseline + 监控增量）
   │
   └── setRunningDurations(snapshot)（整体替换，幂等）
         ├── runningDurations_[id] = 心跳绝对时长（含纳管前 baseline）  → Runtime 实时
         └── healthMap_[id] 重算 = base + min(running/30min,1)*0.3（start_count>0 时）

显示: GetValueByRow
   Runtime = runtimeMap_[id] + runningDurations_[id]  →  "M:SS"
   Health  = healthMap_[id]                            →  "%.3f"
   Starts  = startCountMap_[id]                        →  "N"
```

---

## 6. 排序行为与已知不一致

| 列 | Compare 逻辑 | 与显示一致性 |
|----|--------------|--------------|
| Starts | `startCountMap_` 数值 | ✅ 一致 |
| Runtime | **仅 `runtimeMap_`（历史部分）**，L400-408 | ⚠️ **不一致**：运行中时长不参与排序；两代理历史相同但一个在运行、一个已停止时排序相同，而显示不同 |
| Health | `healthMap_` 数值（含运行加成） | ✅ 一致 |

> **遗留问题（候选改进）**：`Compare` COL_TOTAL_RUNTIME_MS 建议改用 `getRuntime()`（含 `runningDurations_`），与显示语义对齐。改动影响面小（单 case 内 2 行），需配套排序单测。
>
> **v1.1 状态**：本会话（PID 匹配 + baseline）未改动 `ProxyListModel.cpp`，该不一致**仍然存在**（Compare L400-408 仍仅比较 `runtimeMap_` 历史部分）。建议与 baseline 语义一并后续处理。

---

## 7. 相关历史修复

| 日期 | 关联 | 文档 |
|------|------|------|
| 2026-08-18 | `runningDurations_` 整体替换语义 + `changed` 返回（Runtime 膨胀/空转刷新） | `docs/bugfix/2026-08-18-Bugfix-ProxyListRefresh-IdleRedraw-v1.0.md` |
| 2026-08-18 | 定时刷新异步化（后台线程查 running 时长） | `docs/specs/2026-08-18-Spec-ProxyListRefresh-v2.0.md` |
| 2026-08-20 | 冷启动 health 0.5 → 0.0（本次会话） | `docs/bugfix/2026-08-20-Bugfix-ProxyListModel-HealthColdStartZero-v1.0.md` |
| 2026-08-20 | RuntimeHistory 会话匹配引入 PID 因子 + 系统进程启动时间 + 心跳 baseline（本报告 v1.1 修订依据） | `docs/specs/2026-08-20-Spec-RuntimeHistory-PidMatching-v1.0.md` |

---

## 8. 验证

- 当前代码基线构建通过：`cmake --build build --parallel 8`，0 error
- 全量测试：`ctest --test-dir build -V` → 30/30 passed（含本会话新增 DAO 三因子 6 用例 + heartbeat baseline 1 用例）
- 本报告为逻辑提取记录，不涉及代码变更，无新增测试
- 本会话验证：PID 匹配 Spec 验收项见 `docs/specs/2026-08-20-Spec-RuntimeHistory-PidMatching-v1.0.md` §5