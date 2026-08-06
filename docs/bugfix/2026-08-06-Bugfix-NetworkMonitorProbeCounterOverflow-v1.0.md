# Bugfix: NetworkMonitor 探针计数器溢出导致误触发

## 问题描述

在 `NetworkMonitor::ThreadLoop()` 中，当网络监控检测到连接异常时，`consecutiveFailures_` 原子计数器会被无条件地设置为 `maxProbes_`，即使此时 `probeEnabled_` 为 `false`。

这导致在探针功能已禁用的情况下，仅 1 次网络失败就会使 `consecutiveFailures_` 达到阈值，从而错误地触发探针逻辑。

## 影响范围

- 文件：`src/NetworkMonitor.cpp`
- 影响行：约第 139 行（第一分支）、约第 165 行（第三分支）

## 根因分析

`ThreadLoop()` 中有三个分支处理 `consecutiveFailures_` 更新：

| 分支 | 条件 | 修复前行为 |
|------|------|-----------|
| 第一分支 | `prev && !allOk` | 无条件 `consecutiveFailures_.store(maxProbes_)` |
| 第二分支（else） | `prev && !allOk` | ✅ 已有 `if (probeEnabled_ && fails >= maxProbes_)` 保护 |
| 第三分支 | `!allOk` | 无条件 `consecutiveFailures_.store(maxProbes_)` |

第一分支和第三分支缺少 `probeEnabled_` 保护，与第二分支逻辑不一致。

## 修复方案

在两处条件分支中添加 `probeEnabled_` 判断：

```cpp
// 修复前（第一分支 ~line 139）
if (fails >= maxProbes_) {
    consecutiveFailures_.store(maxProbes_);  // 无条件！
}

// 修复后
if (probeEnabled_ && fails >= maxProbes_) {
    consecutiveFailures_.store(maxProbes_);
}
```

```cpp
// 修复前（第三分支 ~line 165）
if (fails >= maxProbes_) {
    consecutiveFailures_.store(maxProbes_);  // 无条件！
}

// 修复后
if (probeEnabled_ && fails >= maxProbes_) {
    consecutiveFailures_.store(maxProbes_);
}
```

## 验证结果

- ✅ 全部 21 项测试通过（NetworkMonitorTest: 11/11）
- ✅ 编译无警告
- ✅ 原子操作线程安全性未受影响

## 提交信息

```
fix: NetworkMonitor 探针计数器条件化写入

仅当 probeEnabled_=true 时将 consecutiveFailures_ 写入 maxProbes_，
避免在探针禁用时因网络失败误触发探针逻辑。
```
