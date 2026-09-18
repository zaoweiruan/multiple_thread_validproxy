# Bugfix: Standalone Periodic Probe Triggers "Operation Busy" Dialog

**日期**: 2026-09-15  
**文件**: `src/ui/AppController.cpp`  
**提交**: `5ce71e4`  
**影响版本**: 1.0.3+（含独立代理周期探活功能）

---

## 1. 问题描述

开启独立代理后，用户会在以下场景弹出 **"Operation Busy"** 对话框：

```
Operation Busy
Another operation is already in progress. Please wait or cancel it first.
[OK]
```

截图：`bin/log/屏幕截图 2026-09-15 171145.png`

**触发条件**：
- `proxy_process_monitor.enabled = true`（进程监控开启）
- `standalone_pool.enabled = true`（独立代理开启）
- 周期 timer 到期触发 silent 探活（`testOnlineProxiesAsync(this, true)`）
- 此时若有其他异步操作正在运行（如批量测试、订阅更新等）

**用户影响**：
- 周期性弹出 busy 对话框，干扰正常使用
- 用户误以为程序卡死或需要手动干预

---

## 2. 根因分析

### 2.1 调用链

```
MainFrame::onProxyMonTimer (timer 到期)
  → AppController::testOnlineProxiesAsync(this, true)  // silent=true
    → AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler}
      → guard 检测到 isRunning_ == true && workerThread_.joinable()
        → wxQueueEvent(handler_, new StatusUpdateEvent(0, "REJECT:Another operation..."))
          → MainFrame::onStatusUpdate
            → wxMessageBox("Another operation is already in progress...", "Operation Busy")
```

### 2.2 根本原因

`AsyncOperationGuard` 的设计初衷是**防止异步操作重入**，对用户手动触发的操作（如「测试在线代理」）弹出 busy 提示是合理的。

但 **silent 周期探活**是后台自动行为：
- 不应打扰用户
- 被拒时应静默跳过，等下一轮 timer 重试
- 原代码传入 `wxHandler`（MainFrame），导致 guard 拒绝时弹出对话框

### 2.3 相关代码

**`include/ui/AsyncOperationGuard.h` L35-45**：
```cpp
if (workerThread_.joinable()) {
    if (isRunning_) {
        if (handler_) {
            wxQueueEvent(handler_, new StatusUpdateEvent(0,
                "REJECT:Another operation is already in progress. "
                "Please wait or cancel it first."));
        }
        return;  // allowed_ stays false
    }
    workerThread_.join();
}
```

**`src/ui/MainFrame.cpp` L175-178**：
```cpp
} else if (payload.StartsWith("REJECT:")) {
    wxMessageBox(payload.Mid(7), "Operation Busy",
                 wxOK | wxICON_INFORMATION, this);
}
```

---

## 3. 修复方案

### 3.1 核心改动

**`src/ui/AppController.cpp` — `testOnlineProxiesAsync`**：

```cpp
void AppController::testOnlineProxiesAsync(wxEvtHandler* wxHandler, bool silent) {
        // For silent periodic probes, pass nullptr as the guard handler so a
        // rejection does NOT pop up the "Operation Busy" dialog — the probe
        // is simply skipped and retried on the next timer tick.  Manual
        // (non-silent) calls keep the original handler so the user sees the
        // busy notification when another operation is in progress.
        wxEvtHandler* guardHandler = silent ? nullptr : wxHandler;
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, guardHandler};
        if (!guard.isAllowed()) {
            // Silent periodic probe rejected by another in-flight operation:
            // leave a DEBUG trace only (no dialog, no REPORT/ERR noise) so the
            // skip is diagnosable.  The probe is retried on the next timer tick.
            if (silent) {
                Logger::write("[OnlineProbe] skipped: another operation in progress",
                              LogLevel::DEBUG);
            }
            return;
        }
    workerThread_ = std::thread(&AppController::doTestOnlineProxies, this, wxHandler, silent);
}
```

### 3.2 修复逻辑

| 场景 | 修复前 | 修复后 |
|------|--------|--------|
| **silent 探活被拒**（有其他操作运行） | 弹出 "Operation Busy" ❌ | 静默跳过（仅 DEBUG 日志），等下一轮 timer ✅ |

> **历史澄清**：首次修复（commit `171b660`）曾尝试「手动测试优先于 silent」（cancel + join 预取消块）。经代码审核（见 §7）确认该块为**死代码**——两个手动调用方（`ProxyListPanel.cpp`、`StandaloneFloatingWidget.cpp`）均在调用前经 `controller_->isRunning()` 预检查拦截并弹「操作进行中」，预取消块不可达。且该块存在「取消任意运行中操作 + UI 线程无界阻塞」的潜伏风险，已在本版移除。真正解决弹窗的是本版（`5ce71e4` 内容 + DEBUG 日志）。

### 3.3 关键设计点

1. **silent 探活传入 `nullptr` handler**：
   - `AsyncOperationGuard` 检测到 `handler_ == nullptr` 时，拒绝路径不发送 `REJECT` 事件
   - 探活静默跳过，不打扰用户（仅 DEBUG 日志留痕，便于排查）
2. **手动测试行为保持原语义**：
   - 手动调用方（ProxyListPanel / StandaloneFloatingWidget）已有 `isRunning_` 预检查，运行中操作时弹「操作进行中」
   - 其余手动路径（如 ken 无预检查的调用）被 guard 拒绝时仍弹 "Operation Busy"（传递原始 handler）
   - **不引入跨操作取消**——避免影响批量测试/订阅更新/自动任务等共享 worker 线程的操作

---

## 4. 验证

### 4.1 构建验证

```powershell
cmake --build build --target validproxy --parallel 8
```

**结果**：
```
[4/4] Linking CXX executable E:\eclipse_workspace\multiple_thread_validproxy\bin\validproxy.exe
```

**状态**：0 error

### 4.2 提交记录

```powershell
git log --oneline -4
<NEW_COMMIT> fix(pool): remove unreachable pre-cancel block, keep silent-probe quiet (review)
c5040be docs(pool): add bugfix doc for silent probe Operation Busy dialog
5ce71e4 fix(pool): suppress Operation Busy dialog for silent periodic probe
171b660 fix(pool): cancel silent probe on manual online-proxy test to avoid Operation Busy dialog
```

> `171b660` 的预取消块经审核确认不可达且具跨操作取消风险，已在本版移除（见 §7）。

---

## 5. 相关文档

- **规格**: [`docs/specs/2026-09-15-Spec-StandalonePeriodicProbe-v1.0.md`](./2026-09-15-Spec-StandalonePeriodicProbe-v1.0.md)
- **计划**: [`docs/plans/2026-09-15-Plan-StandalonePeriodicProbe-v1.0.md`](../plans/2026-09-15-Plan-StandalonePeriodicProbe-v1.0.md)
- **总索引**: [`docs/INDEX.md`](../INDEX.md)

---

## 6. 后续建议

1. **考虑统一 guard 策略**：其他 silent 异步操作（如后台订阅更新、地区解析）也可能遇到相同问题，建议统一评估是否传入 `nullptr` handler
2. **增加日志** ⏳ 已在本版实现：silent 探活被拒时输出 DEBUG 级 `[OnlineProbe] skipped: another operation in progress`
3. **UI 反馈**：若 silent 探活连续多次被拒（说明系统持续繁忙），可在状态栏显示轻量提示

---

## 7. 代码审核记录（2026-09-16）

对提交 `171b660` + `5ce71e4` 进行正确性审查（ce-correctness-reviewer），结论与处置如下：

| ID | 严重度 | 发现 | 处置 |
|----|--------|------|------|
| C1 | Critical | `171b660` 预取消块 `if (!silent && isRunning_ && joinable())` **不区分操作类型**，会 cancel+join 中断任何运行中的异步操作（批量测试/订阅更新/自动任务/数据库同步/地区解析），破坏共享单 worker 线程的互斥语义 | **已移除**该块 |
| I1 | Important | `workerThread_.join()` 在 UI 线程**无界阻塞**，最长可达被取消操作的网络超时（订阅抓取 30s、单代理地区解析整次 HTTP） | 随 C1 一并移除 |
| I2 | Important | 预取消块**不可达（死代码）**：两个手动调用方（`ProxyListPanel.cpp:481`、`StandaloneFloatingWidget.cpp:848`）均先 `controller_->isRunning()` 预检查并弹「操作进行中」；因此 `171b660` 对弹窗问题零贡献，真正修复是 `5ce71e4` | 移除后行为不变（修复效果保留） |
| M1 | Minor | silent 被拒完全无痕，不可观测 | 已补 DEBUG 日志 |
| M2 | Minor | join 后手动 `isRunning_=false` 冗余（worker ScopeGuard 已复位，join happens-before） | 随块移除 |
| M3 | Minor | 注释声称「silent 探活运行时」与代码（仅查 isRunning_）不符 | 随块移除 |
| M4 | Minor | `cancelRequested_=false` 复位可能覆盖 NetworkMonitor 断连信号（既有模式，非本次引入） | 记录备忘，不处理 |

### 7.1 最终修复形态

- **保留** `5ce71e4`：silent 探活传 `nullptr` 给 guard → 被拒不弹 "Operation Busy"
- **新增**：silent 被拒时 DEBUG 日志（M1）
- **删除** `171b660` 预取消块（C1/I1/I2/M2/M3）
- 手动调用方行为不变：「操作进行中」预检查照旧

### 7.2 残余风险

1. 共享单 `workerThread_` 架构下，任何未来新增的调用方若未做 `isRunning_` 预检查，手动触发被拒时会弹 "Operation Busy"（恢复原始行为，语义正确，仅 UX 提醒）
2. M4 网络断连取消信号覆盖为既有模式，若需区分「用户取消」与「断连取消」应拆分标志——超出本次范围
