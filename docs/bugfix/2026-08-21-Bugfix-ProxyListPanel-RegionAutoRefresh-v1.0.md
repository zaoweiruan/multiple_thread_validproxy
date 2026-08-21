# Bugfix: 解析/批量解析完成后 ProxyListPanel 自动刷新

- **日期**: 2026-08-21
- **模块**: `src/ui/ProxyListPanel` / `src/ui/AppController` / `src/ui/MainFrame`
- **类型**: UI 数据刷新缺陷
- **关联**: `2026-08-21-Bugfix-RegionBatchResolver-IpWhoIs-Accuracy-v1.0.md`

---

## 1. 问题描述

用户右键单代理解析地区、或批量解析地区完成后，状态栏提示成功，但 **ProxyListPanel 的 Region 列不更新**，必须手动右键"刷新列表"才能看到新解析的地区值。

## 2. 根因分析

Region 存储于 `ProfileItem.Region`，属于 `ProxyListPanel::proxies_`（由 `loadProxies()` 加载）。

解析完成事件链路：

```
AppController::doResolveRegionsBatch / doResolveSingleProxyRegion
  → wxQueueEvent(ProxyTestProgressEvent(..., isCompleted=true))
    → MainFrame progress handler
      → proxyPanel_->refreshResults()
```

而 `refreshResults()` 的语义是**只重载 `exItems_`**（ProfileExItem：Delay/Failures/Starts/Runtime/Health 列），注释明确 "Proxies list and user selection are preserved"，从不重载 `proxies_` —— 因此 Region 列永远显示旧值。

## 3. 修复方案

遵循项目既有的 StatusUpdateEvent 字符串命令模式（先例：`DEDUP_OK`、`RESOLVE_REGION_START`），新增完成命令 `REGION_RESOLVE_DONE`：

| # | 文件 | 改动 |
|---|------|------|
| 1 | `src/ui/ProxyListPanel.h` | 新增公有方法声明 `void reloadFromDatabase();` |
| 2 | `src/ui/ProxyListPanel.cpp` | 实现 `reloadFromDatabase()`：经 `controller_->loadProxiesAsync(currentSubId_, this)` 异步全量重载（后台线程读库 + 预构建 maps，与 DEDUP_OK 刷新路径一致，不阻塞 UI） |
| 3 | `src/ui/AppController.cpp` `doResolveRegionsBatch` | 批量解析完成时向顶层窗口追加广播 `StatusUpdateEvent(0, "REGION_RESOLVE_DONE")` |
| 4 | `src/ui/AppController.cpp` `doResolveSingleProxyRegion` | 单代理解析成功时向顶层窗口发送同一命令（custom wxEvent 不向上传播，须显式定位 topLevel） |
| 5 | `src/ui/MainFrame.cpp` `onStatusUpdate` | 新增 `REGION_RESOLVE_DONE` 分支 → `proxyPanel_->reloadFromDatabase()` |

### 设计要点

- **不改 `refreshResults()`**：它在每次测试完成、standalone 启停等高频路径被调用，附加全量 ProfileItem 重载（5 万行级）开销不可接受且改变既有语义。
- **不匹配中文完成消息前缀**：以消息文本做协议脆弱；字符串命令常量与 `DEDUP_OK` 同级管理。
- **事件顺序安全**：批量路径中 final ProgressEvent 先入队（恢复 UI 状态 + refreshResults），`REGION_RESOLVE_DONE` 后入队触发全量重载，最终状态正确。
- **取消路径不刷新**：用户主动取消时不发命令（保持既有行为）；注：RegionBatchResolver 析构仍会 flush 已缓冲结果，如需取消后刷新可后续扩展。

## 4. 验证

- 构建：`cmake --build build --parallel 8` ✅ 0 error（仅预存 unused-parameter warning）
- 回归：`ctest --parallel 8` ✅ **31/31 全部通过**

UI 行为验证（人工）：批量解析完成后 Region 列自动出现新值；单代理解析完成后该行 Region 立即更新。

## 5. 已知限制

- AutoTaskManager 流水线内的 region 步骤完成后无此刷新（其进度回调仅上报步骤名），如需覆盖需在 AutoTask 完成事件链路单独接入。
