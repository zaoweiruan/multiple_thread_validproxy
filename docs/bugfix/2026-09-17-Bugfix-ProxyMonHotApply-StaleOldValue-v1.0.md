# Bugfix — `proxy_process_monitor.enabled`→false 后状态栏代理进程监控不置灰（热应用检测失效回归）

- 日期: 2026-09-17
- 类型: Bugfix（MainFrame 配置热应用回归）
- 模块: `src/ui/MainFrame.cpp`（`onMenuConfig` 保存回调）
- 版本: v1.0
- 相关 Spec: `docs/specs/2026-09-17-Spec-ProxyMonitorMainFrameConfigSync-Fix-v1.0.md`（**回归来源**）、`docs/specs/2026-09-17-Spec-ProxyMonitorStatusBarRefresh-Fix-v1.0.md`（已正确实施）

> **触发**：用户报告「`proxy_process_monitor.enabled` → `false` 时，状态栏代理进程监控应该置灰」。

---

## 1. 现象

通过配置窗口将 `proxy_process_monitor.enabled` 从 `true` 改为 `false` 并保存后：

- 状态栏第 3 字段（代理进程监控）仍显示**绿色圆点**（monitor on 态），未按预期置灰；
- 代理监控 timer 仍在运行（`onProxyMonTimer` 持续触发，silent 探活/悬垂纳管/`updateProxyMonStatus(true, aliveCount)` 每 tick 执行）。

## 2. 根因（RCA）

### 2.1 缺陷链路

`MainFrame::onMenuConfig` 保存回调中（修改前代码）：

```cpp
// L1178            saveConfig 更新 AppController::config_
bool saveOk = controller_->saveConfig(cfg);
...
// L1188            【Spec-ConfigSync 实施点】同步 MainFrame::config_
config_ = cfg;
...
// L1204-1206       【缺陷】旧值捕获在读新值之后
bool oldProxyMonEnabled = config_.proxy_process_monitor.enabled;   // 恒 == cfg 新值
int oldProxyMonInterval = config_.proxy_process_monitor.checkIntervalMs; // 恒 == 新值
bool proxyMonEnabledChanged = (cfg.proxy_process_monitor.enabled != oldProxyMonEnabled);  // 恒 false
bool proxyMonIntervalChanged = (cfg.proxy_process_monitor.checkIntervalMs != oldProxyMonInterval); // 恒 false
...
// L1209-1215       热应用分支【永不执行】
if (proxyMonEnabledChanged || proxyMonIntervalChanged) {
    if (cfg.proxy_process_monitor.enabled) { startProxyMonitor(...); }
    else { stopProxyMonitor(); }   // ← 从不触发 → timer 不停、圆点不灰
}
```

### 2.2 回归来源

`2026-09-17-Spec-ProxyMonitorMainFrameConfigSync-Fix-v1.0.md` 将 `config_ = cfg;` 插在 `saveConfig` 之后（当前 L1188）。其「调整理由」第 3 条声称：

> L1200-1210 代理监控热应用判断用 `cfg` 新值与 MainFrame::config_ 旧值比较，**同步前判断已执行完毕，不受影响**。

该前提与实际代码顺序不符：热应用检测块（L1204-1215）位于 `config_ = cfg`（L1188）**之后**，因此 `oldProxyMonEnabled`/`oldProxyMonInterval` 读取的是**已被覆盖的新值**，比较恒为 false——热应用分支成为死代码。改动 `enabled` 或 `check_interval_ms` 均不再生效（monitor 启动/停止/改间隔全部失灵）。

### 2.3 同根因邻近缺陷（顺带修复）

L1191-1194 网络监控变更检测中 `cfg.network_monitor.checkUrls != config_.network_monitor.checkUrls` 同样在覆盖后比较 `config_`，**checkUrls 变更检测恒 false**（enabled/interval/timeout 三项在 L1175-1177 覆盖前捕获，正常）。属同一「覆盖后读旧值」根因类。

## 3. 修复

### 3.1 覆盖前捕获旧值（3 处）

在 `src/ui/MainFrame.cpp` `saveConfig` 之前（L1177 后）插入：

```cpp
        // Capture old proxy-process-monitor + checkUrls values BEFORE config_
        // is overwritten by cfg (bugfix 2026-09-17: reading them after
        // config_ = cfg made the comparisons always-false, so hot-apply of the
        // proxy monitor switch / interval and netmon checkUrls never ran —
        // the status-bar dot stayed green after disabling the monitor).
        bool oldProxyMonEnabled = config_.proxy_process_monitor.enabled;
        int oldProxyMonInterval = config_.proxy_process_monitor.checkIntervalMs;
        std::vector<std::string> oldNetMonCheckUrls = config_.network_monitor.checkUrls;
```

### 3.2 检测块改用覆盖前捕获值（2 处）

- L1191-1194：`config_.network_monitor.checkUrls` → `oldNetMonCheckUrls`
- L1205-1206：删除原 `bool oldProxyMonEnabled = config_.proxy_process_monitor.enabled;` / `int oldProxyMonInterval = config_.proxy_process_monitor.checkIntervalMs;`（改为使用 3.1 捕获的变量，避免与 3.1 重声明冲突）

### 3.3 修复后行为

- 保存 `enabled=false`：`stopProxyMonitor()` 真正执行 → `updateProxyMonStatus(false, 0)` → **灰点**；timer 停止，探活/纳管随之停止。
- 保存 `enabled=true`：`startProxyMonitor(interval)` 真正执行 → 绿点 + timer 按新间隔启动。
- `check_interval_ms` 修改：interval 变更检测恢复生效。
- `network_monitor.checkUrls` 修改：网络监控重启路径恢复生效。

## 4. 验证

- 构建：`cmake --build build --parallel 8` → **0 error**（validproxy.exe / validproxy-cli.exe 均链接成功）。
- 回归：`ctest -R "UI_MAINWINDOW|UI_SEARCH|UI_FLOATINGWIDGET|ConfigReaderTest" --test-dir build`
  - UI_SEARCH ✅（2.20s）/ UI_FLOATINGWIDGET ✅（12.00s，53 断言 6 用例）/ ConfigReaderTest ✅
  - UI_MAINWINDOW 并行首跑 32s 超时 `win.valid()=false`（既有窗口枚举时序 flake），**单独重跑 0.76s 通过**——非本次回归（本次改动仅涉保存回调，不影响启动）。
- 手工验证清单：
  1. 启动 `bin\validproxy.exe`（**非 `bin/worker/` 陈旧副本**），打开「配置」→ 取消勾选「监控悬浮窗」→ 保存 → 状态栏第 3 字段变**灰点**，`bin/log/ui_*.log` 不再出现 `onProxyMonTimer` 的 probe 日志。
  2. 再次勾选「监控悬浮窗」→ 保存 → 状态栏变**绿点**，`[MainFrame] startProxyMonitor: intervalMs=...` 出现在日志。
  3. 修改「检测间隔(毫秒)」→ 保存 → 日志确认新的 intervalMs。

## 5. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ui/MainFrame.cpp` | `onMenuConfig`：saveConfig 前捕获 `oldProxyMonEnabled`/`oldProxyMonInterval`/`oldNetMonCheckUrls`；netmon checkUrls 比较改用捕获值；proxy mon 检测块删除重复声明改用捕获值 |
| `docs/bugfix/2026-09-17-Bugfix-ProxyMonHotApply-StaleOldValue-v1.0.md` | 本 bugfix（新建） |
| `docs/INDEX.md` §8.2 | 新增本 bugfix 条目 |
| `docs/CONTEXT.md` §三 | 记录本会话修复条目 |

## 6. 关联文档

- `docs/specs/2026-09-17-Spec-ProxyMonitorMainFrameConfigSync-Fix-v1.0.md` — 回归来源（建议后续修订其「调整理由」第 3 条，标注热应用检测须在覆盖前捕获旧值）
- `docs/specs/2026-09-17-Spec-ProxyMonitorStatusBarRefresh-Fix-v1.0.md` — 启动绘制顺序修复（已正确，本次未触碰）
- `docs/plans/2026-09-17-Plan-HealthMonitorUnify-v1.0.md` — 未来 Task 3 重构热改门控时，本修复的捕获变量可直接复用