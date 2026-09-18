---
doc_type: Spec
date: 2026-09-17
module: MainFrame
version: v1.0
status: completed
scope: src/ui/MainFrame.cpp
risk: low
---

# Spec — 修复 proxy_process_monitor.enabled=true 时状态栏代理进程监控不刷新（仍显示灰色）

## 1. 背景

用户报告：当 `bin/config.json` 中 `proxy_process_monitor.enabled=true` 时，主窗口状态栏第 3 字段（代理进程监控）应显示**绿色圆点**（monitor 激活态），但实际仍为**灰色**。

`onProxyMonTimer` 每 5 秒会调用 `updateProxyMonStatus(true, aliveCount)`，理论上会强制 `Refresh()` 触发 paint，paint handler 内 `proxyMonEnabled_=true` 分支应画绿色圆点 `(0,180,0)`。当前实现未能稳定显示绿点。

## 2. 目标 / 非目标

### 目标

1. 当 `proxy_process_monitor.enabled=true` 启动时，状态栏第 3 字段稳定显示**绿色圆点**（monitor on 态）。
2. 关键节点写入 DEBUG 日志，便于事后从 `bin/log/ui_*.log` 精确定位（panel client size、`proxyMonEnabled_`、`fieldRect` 实际值）。
3. 不改变 `enabled=false` 时灰点显示行为；不改变 `onProxyMonTimer` 与 `onMenuConfig` 热应用逻辑。

### 非目标

- 不修改 `ProxyProcessMonitorConfigParser`、`AppController::saveConfig`、`onMenuConfig` 热应用逻辑（已确认为正确）。
- 不修改 `onProxyMonTimer` 的 adoption / probe 流程。
- 不修改按钮「监控代理」`onMenuStandaloneMonitor` 行为（只切换悬浮窗，不触碰 timer）。
- 不新增 GTest 覆盖（UI 层状态栏绘制无对应可编译单测入口，且不影响公共 API）。

## 3. 根因分析

代码路径（`src/ui/MainFrame.cpp` `startMonitoring`）当前实现：

```cpp
// L537-543
if (controller_ && controller_->getConfig().proxy_process_monitor.enabled) {
    startProxyMonitor(interval);      // 内部 updateProxyMonStatus(true, 0) → Refresh()
} else {
    updateProxyMonStatus(false, 0);
}
repositionProxyMonPanel();            // SetSize(GetFieldRect(3)) + Refresh()
```

`startProxyMonitor`（L967-976）在末尾 `updateProxyMonStatus(true, 0)` 触发 paint，但此时 `proxyMonPanel_` 尚未 reposition 到最终位置与尺寸：

- 创建时 parent=statusBar_，size 为 `wxDefaultSize`（-1,-1 → best-fit 120×24 或 0×0）。
- 若 best-fit 尺寸 < 4×4，paint handler L499 早退（`if (sz.x < 4 || sz.y < 4) return;`），不画任何内容，用户看到默认状态栏背景色（灰）。
- 即使尺寸 > 4×4，此时绘制位置也不在最终 fieldRect 位置，视觉上不直观。

随后 `repositionProxyMonPanel` 调 `SetSize(fieldRect) + Refresh()`，理论上二次 paint 应显示绿点。但 wxWidgets 在 panel 尺寸从 best-fit 切到 fieldRect 时，可能因 paint 事件合并、DC 复用等时序问题导致最终 paint 使用了旧 client size（早退）。

同时三处关键节点（paint handler / startProxyMonitor / repositionProxyMonPanel）**均无日志**，无法从 `bin/log/ui_*.log` 验证实际值。

## 4. 实现方案

### 4.1 调整 `startMonitoring` 中顺序（防御性修复）

将 `repositionProxyMonPanel()` 移到 `startProxyMonitor` **之前**：

```cpp
repositionProxyMonPanel();                              // 先定位到 fieldRect
if (controller_ && controller_->getConfig().proxy_process_monitor.enabled) {
    startProxyMonitor(interval);                        // 内部 updateProxyMonStatus(true,0) → Refresh
} else {
    updateProxyMonStatus(false, 0);
}
// 兜底：reposition 之后强制二次刷新，确保 panel 拿到正确 size 后重画
if (proxyMonPanel_) proxyMonPanel_->Refresh();
```

调整理由：

1. reposition 时 `proxyMonEnabled_` 仍为初始 `false`，paint 画灰点（短暂、无害）。
2. 紧接着 `startProxyMonitor` 内 `updateProxyMonStatus(true, 0)` 触发二次 paint，此时 panel 已在最终位置与尺寸，画绿点。
3. 末尾追加 `Refresh()` 作为兜底，规避 wxWidgets paint 事件合并导致的最终 paint 早退。

### 4.2 关键节点加 DEBUG 日志

- `paint handler` L496：打印 `sz.x×sz.y` 与 `proxyMonEnabled_`，诊断早退路径。
- `startProxyMonitor` L967：打印 `intervalMs`，确认被调用。
- `repositionProxyMonPanel` L916：打印 `fieldRect`，确认 `GetFieldRect(3)` 返回值。

### 4.3 输出日志格式

```
[MainFrame] startProxyMonitor: intervalMs=5000
[MainFrame] repositionProxyMonPanel: fieldRect=(x,y,w,h)
[MainFrame] proxyMonPaint: sz=WxH, enabled=1, aliveCount=0
```

级别：`LogLevel::DEBUG`（`bin/config.json` `log.file_level` 需设为 `DEBUG` 或以上）。

## 5. 边界与兼容性

- **`enabled=false` 路径**：`updateProxyMonStatus(false, 0)` 保持灰点绘制，行为不变。
- **`repositionProxyMonPanel` 早退**（`statusBar_` 或 `proxyMonPanel_` 为空）：现有守卫保留，日志仅在通过守卫后打印。
- **paint handler 早退**（sz<4）：仍早退，但日志已打印 sz，便于诊断。
- **`proxyAliveCount_`**：日志字段追加，不影响绘制逻辑（绘制仅依赖 `proxyMonEnabled_`）。
- **C++17 无 `auto`**：所有变量显式类型；日志拼接使用 `std::string + std::to_string`。

## 6. 验证计划

1. `cmake --build build --parallel 8` 增量编译通过（零 error）。
2. 运行 `bin\validproxy.exe`（最新构建产物，非 `bin/worker/` 陈旧副本）。
3. 确认 `bin/log/ui_*.log` 中出现：
   - `[MainFrame] startProxyMonitor: intervalMs=5000`
   - `[MainFrame] repositionProxyMonPanel: fieldRect=...`
   - `[MainFrame] proxyMonPaint: sz=WxH, enabled=1, aliveCount=0`（enabled 应为 1）
4. 观察状态栏第 3 字段：显示绿色圆点 + alive count 数字。
5. 5 秒后 `onProxyMonTimer` 触发，状态栏刷新为最新 alive count（绿色圆点保持）。

## 7. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ui/MainFrame.cpp` | 调整 L537-543 顺序（reposition 先于 start）；末尾追加 `Refresh()` 兜底；L496 paint handler、L967 startProxyMonitor、L916 repositionProxyMonPanel 各追加一行 DEBUG 日志 |
| `docs/INDEX.md` §8.2 | 新增 2026-09-17 本 spec 条目 |
| `docs/plans/project-plans-tracker.md` | 近期文档引用表新增 2026-09-17 条目 |
| `docs/specs/2026-09-17-Spec-ProxyMonitorStatusBarRefresh-Fix-v1.0.md` | 本 spec（新建） |
