# Spec: ProfileExItem message 字段排序（最近活跃时间）

- 日期: 2026-08-12
- 类型: Spec
- 模块: ProfileExItemDAO / ProxyListModel
- 版本: v1.0
- 状态: ✅ completed

---

## 1. 需求

为代理列表的 Message 列增加列头排序。message 格式为 `<测试时间>+<启动时间>`
（恒含一个 `+`，任一侧可缺失，如 `2026-08-11 16:00:00+` / `+2026-08-11 16:05:00`，
或为空字符串）。

用户明确约束（原话）：

> "最近活跃时间（取两者较新）"

**排序语义**：排序键 = 测试时间与启动时间中**较新**的一个（`messageActiveTime`）；
两侧均无合法时间戳（空串 / 遗留值 OK/FAILED/curlMsg/NOT_TESTED）视为空，**排最后**。

| 场景 | message | 排序键 |
|------|---------|--------|
| 仅测试过 | `2026-08-11 16:00:00+` | `2026-08-11 16:00:00` |
| 仅启动过 | `+2026-08-11 16:05:00` | `2026-08-11 16:05:00` |
| 两者都有（测试旧） | `2026-08-11 16:00:00+2026-08-11 16:05:00` | `2026-08-11 16:05:00` |
| 两者都有（启动旧） | `2026-08-11 16:05:00+2026-08-11 16:00:00` | `2026-08-11 16:05:00` |
| 空 / 遗留值 | `""` / `OK` | 无（排最后） |

---

## 2. 现状分析（修改前）

### 2.1 现有 Message 列排序

`src/ui/ProxyListModel.cpp` `Compare()` 的 `COL_MESSAGE` 分支：

```cpp
cmp = mA.compare(mB);   // 纯字典序
```

### 2.2 现存问题

- `+2026-08-11 16:05:00`（仅启动）按字典序 `'+' < '2'` 排到所有
  `2026-...`（仅测试）项**之前**，与实际时间无关。
- 两侧时间戳中**较新的一侧从不参与比较**（例如
  `2026-08-11 08:00:00+2026-08-11 16:05:00` 只按测试侧 08:00 参与排序，
  与另一条 `2026-08-11 09:00:00+` 比较时本应按 16:05 胜出却按 08:00 落后）。

---

## 3. 设计方案

### 3.1 排序键提取（DAO 静态方法）

在 `ProfileExItemDAO` 新增两个 public static 方法（复用私有
`isMessageTimestamp` 严格校验，**不触碰数据库**）：

- `static std::string messageActiveTime(const std::string& message)`
  - 按第一个 `+` 拆分：`+` 前 = 测试侧，`+` 后 = 启动侧。
  - 无 `+` 时整串视为单侧（遗留值校验失败 → 空）。
  - 两侧各自 `isMessageTimestamp` 校验；仅保留合法侧。
  - 返回两侧中较新的一个（`yyyy-MM-dd HH:mm:ss` 字典序 == 时间序）；
    均无效/空 → 返回空串。
- `static int compareMessage(const std::string& lhs, const std::string& rhs)`
  - `tA = messageActiveTime(lhs)`，`tB = messageActiveTime(rhs)`。
  - 两侧均空 → `0`；空侧排后（`tA` 空返回 `1`，`tB` 空返回 `-1`）；
  - 否则 `tA.compare(tB)` 归一为 `-1/0/1`。

### 3.2 模型接入

`ProxyListModel::Compare()` 的 `COL_MESSAGE` 分支：

```cpp
cmp = db::models::ProfileExItemDAO::compareMessage(mA, mB);
```

外层 `return ascending ? cmp : -cmp;` 保持不变（升/降序翻转由既有机制处理）。

### 3.3 变更点

| 文件 | 变更 |
|------|------|
| `include/Profileexitem.h` | 新增 `messageActiveTime` / `compareMessage` 声明 |
| `src/ProfileExItemDAO.cpp` | 新增两方法实现（`formatStartupMessage` 之后） |
| `src/ui/ProxyListModel.cpp` | `Compare()` COL_MESSAGE 改用 `compareMessage` |
| `src/ui/ProxyListPanel.cpp` | Message 列标题增加 `↕` 指示符（与其他可排序列一致） |
| `tests/test_profile_ex_item_dao.cpp` | 追加排序相关单测 |

---

## 4. 测试

在 `tests/test_profile_ex_item_dao.cpp`（已链接 `ProfileExItemDAO.cpp` + gtest）
追加用例，全部为静态调用、无需 DB：

- `messageActiveTime`：空/遗留值 → 空；仅一侧 → 该侧；两侧取新；
  无效侧忽略。
- `compareMessage`：合法 vs 空 → 合法在前（-1/1）；相同活跃时间 → 0；
  按活跃时间先后定序（含仅启动侧参与比较的场景）。

---

## 5. 范围与约束

- 不改动 message 写入语义（Spec 2026-08-11 保持有效）。
- 排序为**纯展示层**行为：`Compare()` 排序结果不写回数据库、不影响
  `ProfileExItem` 表 `Sort` 字段。
- 全栈禁止 `auto`，显式类型。
