---
doc_type: Spec
date: 2026-09-17
module: MainFrame
version: v1.0
status: completed
scope: src/ui/MainFrame.cpp
risk: low
---

# Spec — 修复 onMenuConfig 热应用后 MainFrame::config_ 未同步致「监控代理」按钮误报

## 1. 背景

用户报告：通过 GUI 配置窗口将 `proxy_process_monitor.enabled` 改为 `true` 并保存后，单击「监控代理」按钮弹出提示「监控代理进程未在配置中启用」（附截图 `bin/log/04f63df4-1e74-411b-931e-52b2f8390280.png`）。

该提示由 `MainFrame::onMenuStandaloneMonitor` L1121 的守卫分支 `if (!config_.proxy_process_monitor.enabled)` 触发，说明 `MainFrame::config_.proxy_process_monitor.enabled` 仍为 `false`。

## 2. 根因

`MainFrame::onMenuConfig` L1178 调用 `controller_->saveConfig(cfg)`：

```cpp
bool saveOk = controller_->saveConfig(cfg);
```

`AppController::saveConfig` 只更新 `AppController::config_` 成员并写入磁盘，**不更新 `MainFrame::config_`**。

后续两处逻辑依赖 `MainFrame::config_` 旧值：

1. `onMenuConfig` L1200 `bool oldProxyMonEnabled = config_.proxy_process_monitor.enabled;` — 取的是旧值，比较 `cfg.proxy_process_monitor.enabled != oldProxyMonEnabled` 是**正确的**（旧值 vs 新值）。
2. `onMenuStandaloneMonitor` L1121 `if (!config_.proxy_process_monitor.enabled)` — 取的是旧值（`false`），触发误报提示。

## 3. 目标 / 非目标

### 目标

1. `onMenuConfig` 保存配置后，`MainFrame::config_` 与 `AppController::config_` 保持同步。
2. 同步后 `onMenuStandaloneMonitor` 正确识别 `enabled=true`，不再弹出误报提示。
3. 同步后 `startMonitoring` 若被再次触发也能读到新值。

### 非目标

- 不修改 `AppController::saveConfig` 契约（仍只更新自身 `config_`）。
- 不修改 `onMenuStandaloneMonitor` 守卫逻辑（守卫本身正确，问题在于上游 config 不同步）。
- 不修改 `onMenuConfig` 中 L1200-1210 热应用判断（取旧值比较新值，逻辑正确）。
- 不修改悬浮窗 `floatingWidget_`、状态栏 `proxyMonPanel_` 生命周期。

## 4. 实现方案

在 `onMenuConfig` L1178 `controller_->saveConfig(cfg)` 之后立即同步 `config_ = cfg;`：

```cpp
bool saveOk = controller_->saveConfig(cfg);
if (!saveOk) {
    wxMessageBox(...);
}
// 同步 MainFrame::config_ 与 AppController::config_，
// 避免后续 onMenuStandaloneMonitor 等入口读到旧值误报。
config_ = cfg;
```

放置位置：紧跟在 `saveOk` 检查之后、任何依赖 MainFrame::config_ 的逻辑之前。

调整理由：

- L1173 `std::string oldDbPath = config_.database_path;` 在 saveConfig 之前取，不受影响。
- L1174-1177 旧值捕获在 saveConfig 之前，不受影响。
- L1186-1189 网络监控变更判断用 `cfg` 新值，不受影响。
- L1200-1210 代理监控热应用判断用 `cfg` 新值与 MainFrame::config_ 旧值比较，同步前判断已执行完毕，不受影响。
- 同步之后 L1214-1217 悬浮窗 `applySettings(cfg)` 用 `cfg` 新值，不受影响。
- 同步之后 L1220-1221 日志级别用 `cfg` 新值，不受影响。
- 同步之后 L1228 数据库路径切换用 `cfg.database_path` 新值，不受影响。

## 5. 边界与兼容性

- **`saveOk=false` 路径**：即使保存失败，`config_ = cfg` 仍会执行，使内存中的 MainFrame::config_ 与用户所见一致（下次启动从磁盘读取会回滚到旧值）。可接受——保存失败已有 wxMessageBox 提示。
- **DB 路径切换**：L1228 之后 `config_.database_path` 会被新路径更新，与旧代码一致。
- **`onMenuStandaloneMonitor` 行为**：同步前 `enabled=true` 时不弹窗、懒创建悬浮窗并 toggle；同步后行为不变，只是不再误判。
- **`startMonitoring` 再次触发**（若发生）：读取 `controller_->getConfig()`，与 MainFrame::config_ 无关。
- **C++17 无 `auto`**：`config_ = cfg` 是成员赋值，类型由 `config_` 声明决定，无 auto。

## 6. 验证计划

1. `cmake --build build --parallel 8` 增量编译通过（零 error）。
2. 启动 `bin\validproxy.exe`，打开「配置」窗口，将 `proxy_process_monitor.enabled` 改为 `true` 并保存。
3. 单击工具栏「监控代理」按钮：**不再弹出「监控代理进程未在配置中启用」提示**，悬浮窗正常展开/折叠。
4. 状态栏第 3 字段显示绿色圆点（monitor on 态）。

## 7. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ui/MainFrame.cpp` | `onMenuConfig` L1178 `saveOk` 检查之后追加 `config_ = cfg;` 一行，同步 MainFrame::config_ 与 AppController::config_ |
| `docs/INDEX.md` §8.2 | 新增 2026-09-17 本 spec 条目 |
| `docs/plans/project-plans-tracker.md` | 近期文档引用表新增 2026-09-17 条目 |
| `docs/specs/2026-09-17-Spec-ProxyMonitorMainFrameConfigSync-Fix-v1.0.md` | 本 spec（新建） |
