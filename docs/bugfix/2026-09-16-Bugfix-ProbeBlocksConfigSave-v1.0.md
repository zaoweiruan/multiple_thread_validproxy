# Bugfix: 周期探活阻塞配置保存（"操作进行中，无法保存配置"）

**日期**: 2026-09-16  
**类型**: bugfix  
**模块**: 独立代理周期探活 / 配置保存  
**提交**: `(待填)`  
**影响版本**: 1.0.3+（含独立代理周期探活功能）

---

## 1. 问题描述

**症状**：未进行任何手动操作，仅修改配置并保存时，反复弹出：
```
操作进行中，无法保存配置
```

**触发条件**：
- `proxy_process_monitor.enabled = true`（进程监控开启）
- `proxy_process_monitor.check_interval_ms` 较小（本机配置为 **5000ms**）
- 有独立代理在运行（被监控，探活有实际目标）

## 2. 根因分析

### 2.1 证据（日志 `ui_20260916_093044.log`）

```
[09:30:44] [REPORT] [StandaloneProxy] Adopted dangling process pid=1984 ...
[09:30:51] [DEBUG] Online proxies test finished: total=2, success=2, failed=0
[09:30:57] [DEBUG] Online proxies test finished: total=2, success=2, failed=0
[09:31:03] [DEBUG] Online proxies test finished: total=2, success=2, failed=0
[09:31:07] [DEBUG] Online proxies test finished: total=2, success=2, failed=0
[09:31:11] [DEBUG] ...
[09:31:16] [DEBUG] ...
[09:31:22] [DEBUG] ...
[09:31:25] [DEBUG] [AppController] cancelTest() called  ← 用户操作与探活重入
```

- 周期探活 **每 5-6 秒运行一次**（`check_interval_ms = 5000`）
- 每次探活占用共享 `workerThread_ / isRunning_` 约 1-2 秒（total=2 个代理）

### 2.2 调用链

```
MainFrame::onProxyMonTimer (timer 5s)
  → AppController::testOnlineProxiesAsync(this, true)   // silent 周期探活
    → AsyncOperationGuard 允许 → isRunning_ = true
      → doTestOnlineProxies 线程运行（1-2s，测试每个代理）
        → ScopeGuard 复位 isRunning_ = false

MainFrame::onMenuConfig（用户修改配置点确定）
  → controller_->isRunning() == true   ← 探活窗口内！
    → wxMessageBox("操作进行中，无法保存配置")   // 误报
```

### 2.3 根本问题

1. **周期探活是后台自动任务**，但复用共享 `isRunning_` 标志（与用户主动操作相同的语义）
2. **MainFrame::onMenuConfig 无条件检查 `isRunning()`**——无论占用者是用户操作还是后台探活，都拒绝保存配置
3. 探活频率高（5s）+ 每次占用 1-2s → **isRunning_ 的占用比例约 20-40%**，用户保存配置时大概率命中窗口，感知为"一直提示"

## 3. 修复方案

### 3.1 核心思想

区分"**用户主动操作**"（保存配置时拒绝）与"**后台静默探活**"（保存普通配置允许）：

- **用户主动操作**运行中 → 仍拒绝保存（防止 `saveConfig()` 写 `config_` 与 worker 线程读 `config_` 竞争）
- **后台探活**运行中 → 允许保存普通配置：
  - 探活线程**快照**所需 config 字段（锁保护），消除读写竞争
  - 切换数据库路径仍要求完全空闲（`switchDatabase()` 会替换探活正在使用的 `db_`）

### 3.2 代码改动

**`src/ui/AppController.h`**：
```cpp
bool isOnlineProbeRunning() const { return onlineProbeRunning_.load(); }
...
mutable std::mutex configMutex_;              // 保护 config_ 写 vs 探活快照读
std::atomic<bool> onlineProbeRunning_{false}; // silent 探活运行标志
```

**`src/ui/AppController.cpp`**：
```cpp
bool AppController::saveConfig(const config::AppConfig& cfg) {
    std::lock_guard<std::mutex> lock(configMutex_);   // 写 config_ 加锁
    config_ = cfg;
    ...
}

void AppController::testOnlineProxiesAsync(wxEvtHandler* wxHandler, bool silent) {
    ...
    if (!guard.isAllowed()) { ... return; }
    if (silent) {
        onlineProbeRunning_ = true;   // 标记后台探活运行中
    }
    workerThread_ = std::thread(&AppController::doTestOnlineProxies, this, wxHandler, silent);
}

void AppController::doTestOnlineProxies(wxEvtHandler* wxHandler, bool silent) {
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};
    ScopeGuard<std::atomic<bool>> _probeGuard{onlineProbeRunning_};  // 复位探活标志

    // 快照探活所需的 config 字段（锁内拷贝，消除与 saveConfig 的竞争）
    std::string probeTestUrl; int probeTimeoutMs = 0; int probeFailStreak = 0;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        probeTestUrl = config_.test_url;
        probeTimeoutMs = config_.test_timeout_ms;
        probeFailStreak = config_.standalone_pool.evaluate.pruneFailStreak;
    }
    // 循环内使用快照局部变量（原 3 处 config_ 读取替换）
    ...
}
```

**`src/ui/MainFrame.cpp` — `onMenuConfig`**：
```cpp
config::AppConfig cfg = configDialog_->getConfig();
// 用户主动操作运行中 → 拒绝保存（原行为）
if (controller_->isRunning() && !controller_->isOnlineProbeRunning()) {
    wxMessageBox(L"操作进行中，无法保存配置", ...);
    return;
}
// 后台探活运行中 + 切换数据库 → 拒绝（switchDatabase 替换探活使用的 db_）
bool dbPathChanged = !cfg.database_path.empty() && cfg.database_path != config_.database_path;
if (controller_->isRunning() && dbPathChanged) {
    wxMessageBox(L"操作进行中，无法切换数据库", ...);
    return;
}
```

### 3.3 场景对照

| 场景 | 修复前 | 修复后 |
|------|--------|--------|
| 探活运行中，保存普通配置 | "无法保存配置" ❌ | 允许保存 ✅ |
| 探活运行中，切换数据库 | "无法保存配置" ❌ | "无法切换数据库"（等探活结束 1-2s） |
| 用户操作运行中（测试/更新等），保存配置 | "无法保存配置" | 不变（仍拒绝） ✅ |
| 空闲，保存配置 | 允许 | 不变 ✅ |

### 3.4 并发安全

| 竞争对 | 防护 |
|--------|------|
| `saveConfig()` 写 `config_` vs 探活读 `config_` | `configMutex_`（写与快照读互斥） |
| `switchDatabase()` 替换 `db_` vs 探活用 `db_` | `isRunning() && dbPathChanged` 拒绝保存 |
| 探活 vs 手动测试重入 | `AsyncOperationGuard`（既有，不变） |

## 4. 验证

- 构建：`validproxy.exe` 0 error
- 行为验证（逻辑推演）：见 §3.3 场景对照表
- 手动场景：开启独立代理（探活 5s 周期）后直接修改配置保存 → 不再弹"操作进行中"

## 5. 相关文档

- 规格：`docs/specs/2026-09-16-Spec-LogLevelRefinement-v1.0.md`（同期日志优化）
- 规格：`docs/specs/2026-09-15-Spec-StandalonePeriodicProbe-v1.0.md`（周期探活设计）
- 修复：`docs/bugfix/2026-09-15-Bugfix-StandaloneProbe-OperationBusy-v1.0.md`（同期探活弹窗问题）