# Bugfix: ProxyListPanel Health 列冷启动初始值 0.5 → 0.0

- 日期: 2026-08-20
- 类型: Bugfix（行为调整）
- 模块: ProxyListModel / ProxyListPanel (UI 代理列表面板)
- 版本: v1.0
- 日志: N/A（UI 展示值调整，无运行时日志）

---

## 1. 问题描述

`ProxyListPanel` 的 **Health**（健康分）列对**从未测试过**的代理（`start_count == 0`、`crash_count == 0`）显示初始值 `0.5`，而非 `0.0`。

用户预期：无任何历史数据的新代理，健康分应为 `0.0`（中性起点），而非贝叶斯先验的 `0.5`。同时，未测试代理即使正在运行，也不应获得运行时长加成（无历史记录可证明其稳定性）。

---

## 2. 根因分析

### 2.1 计算位置

`src/ui/ProxyListModel.cpp` 两处计算 health：

**① `rebuildMaps()`（L79-86，初始构建）：**

```cpp
int stable = ex.start_count - ex.crash_count;
if (stable < 0) stable = 0;
healthMap_[ex.indexid] = static_cast<double>(stable + 1) /
                         static_cast<double>(ex.start_count + 2);
```

当 `start_count == 0` 时：`health = (0 + 1) / (0 + 2) = 0.5`。

**② `setRunningDurations()`（L117-131，运行期心跳刷新）：**

```cpp
double base = static_cast<double>(stable + 1) /
              static_cast<double>(ex.start_count + 2);
double bonus = ...;  // running > 0 时 min(running/30min,1)*0.3
```

冷启动代理（`start_count == 0`）在运行中会获得 `base=0.5 + bonus`，使健康分虚高。

### 2.2 语义分析

原公式 `(stable + 1) / (start_count + 2)` 为**贝叶斯平滑**（+1/+2 拉普拉斯先验），本意是避免 0 次启动时出现 `0/0` 并给出中性起点。但副作用是：

- 未测试代理（无任何历史）获得 `0.5` 的"假健康"，与其他有真实历史的代理无法区分；
- UI 语义上，用户期望"无数据 = 0 分"，与 `getHealth()` 对缺失索引的 fallback（`0.0`）保持一致。

---

## 3. 修复方案

### 3.1 核心思路

对 `start_count == 0` 的代理，health 强制置 `0.0`；**仅**当 `start_count > 0` 时才启用贝叶斯平滑与运行时长加成。

### 3.2 变更

**文件：** `src/ui/ProxyListModel.cpp`

**① `rebuildMaps()`（L79-86）：**

```cpp
int stable = ex.start_count - ex.crash_count;
if (stable < 0) stable = 0;
if (ex.start_count == 0) {
    healthMap_[ex.indexid] = 0.0;
} else {
    healthMap_[ex.indexid] = static_cast<double>(stable + 1) /
                             static_cast<double>(ex.start_count + 2);
}
```

**② `setRunningDurations()`（L117-131）：** base 与 bonus 均纳入 `start_count > 0` 条件：

```cpp
double base = 0.0;
double bonus = 0.0;
if (ex.start_count > 0) {
    base = static_cast<double>(stable + 1) /
           static_cast<double>(ex.start_count + 2);
    if (running > 0) {
        double ramp = static_cast<double>(running) /
                      static_cast<double>(RAMP_MS);
        if (ramp > 1.0) ramp = 1.0;
        bonus = ramp * WEIGHT;
    }
}
double h = base + bonus;
```

### 3.3 修复后语义

| 场景 | 公式 | 值 |
|------|------|-----|
| 未测试（`start_count == 0`） | `0.0`（不授予运行加成） | `0.0` |
| 有历史（`start_count > 0`） | `(stable+1)/(start_count+2)` | 例：5 启 0 崩 → `6/7 ≈ 0.857` |
| 有历史 + 运行中 | 基础分 + `min(running/30min,1)×0.3`，上限 1.0 | 运行 2 分钟 → `0.857 + 0.02` |
| map 无此索引（fallback） | `0.0` | `0.0` |

---

## 4. 测试

### 4.1 新增回归测试

**文件：** `tests/test_proxy_list_model.cpp` — `ColdStartProxyHasZeroHealth`

- 场景 1：`makeEx("A", 0, 0, 0)` → `rebuildMaps()` 后 `getHealth("A") == 0.0`
- 场景 2：同一冷启动代理注入运行会话（120000ms）→ `setRunningDurations()` 后 `getHealth("A")` 仍为 `0.0`（不授予 bonus）

### 4.2 验证

- 构建通过：`cmake --build build --parallel 8`，0 error
- 单元测试：`ctest 30/30` passed（100%），无回归
  - `ProxyListModelTest` 8/8（含新增用例）
  - 既有用例 `BaselineWithoutRunningSessions` / `RunningSessionsMergeRuntimeAndHealthBonus` / `LongRunningHealthCappedAtOne` 期望值不变（均使用 `start_count ≥ 1`）

---

## 5. 验收

- [x] 未测试代理 Health 列显示 `0.000`
- [x] 冷启动代理运行中 Health 仍为 `0.000`（无运行加成）
- [x] 有历史代理的 Health 计算不受影响（贝叶斯平滑保留）
- [x] 构建 0 error，ctest 30/30 passed

---

## 6. 影响范围

- **UI 层**：`ProxyListPanel` Health 列显示值（仅影响 `start_count == 0` 的代理）
- **数据层**：无（不修改 DB 聚合列，仅内存 map 计算）
- **排序**：Health 列排序（`Compare` COL_HEALTH）自动跟随新值，冷启动代理排后
- **ProxyScorer**：不受影响——`scoring::compute` 冷启动分支早已返回 `history_score = 0.0`，本次修改使 UI 展示与评分引擎语义对齐
- **风险**：低。修改范围限定在两处 health 计算，且新增回归测试守护