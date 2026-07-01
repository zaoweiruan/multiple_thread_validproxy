# Bugfix: Sync 目标库新订阅 enabled 默认值为 0

## Problem

数据库同步功能 (`syncDatabases`) 将源库的订阅迁移到目标库时，新插入的 `SubItem` 记录的 `enabled` 字段直接复制源库值。目标库中新增订阅应默认禁用 (`enabled = "0"`)，而非继承源库状态，以匹配导入行为（`Importer.cpp` 已默认 `"0"`）。

## Root Cause

`SubitemUpdaterV2::migrateSubscription()` 从源库读取 `SubItem` 行后直接调用 `insertSubItem()`，未重写 `enabled` 字段：

```cpp
// Before fix — enabled copied from source as-is
subitem = db::models::Subitem::fromStmt(srcStmt);  // source.enabled propagates to target
return insertSubItem(dstDb, subitem);
```

对比：`Importer::insertSubItem()` 在构造 `Subitem` 时显式设置 `subitem.enabled = "0"`。

## Fix

`src/SubitemUpdaterV2.cpp` — 在 `migrateSubscription()` 中，`insertSubItem()` 调用前将 `enabled` 置为 `"0"`：

```cpp
// After fix — override enabled to "0" for target
subitem = db::models::Subitem::fromStmt(srcStmt);
subitem.enabled = "0";
return insertSubItem(dstDb, subitem);
```

## Affected File

| File | Change |
|------|--------|
| `src/SubitemUpdaterV2.cpp` | `migrateSubscription()`: 添加 `subitem.enabled = "0"` 一行 |

## Verification

- **编译**: 3 个目标均成功（`validproxy.exe`, `validproxy-cli.exe`, `test_autotask.exe`）
- **测试**: 18 项全部通过（100%）
- **LSP**: 无错误、无警告

## Scope

- 仅影响同步路径（`syncDatabases` → `migrateSubscription`）
- 导入路径（`Importer`）不受影响，已默认为 `"0"`
- 订阅已存在于目标库时被跳过，不受影响
