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
    // If a silent periodic probe is already running and the user requests
    // a manual run, cancel the silent one and wait for it to finish so
    // the manual request can take over (avoids the "Operation Busy" dialog).
    if (!silent && isRunning_ && workerThread_.joinable()) {
        cancelRequested_ = true;
        workerThread_.join();
        isRunning_ = false;
        cancelRequested_ = false;
    }

    // For silent periodic probes, pass nullptr as the guard handler so a
    // rejection does NOT pop up the "Operation Busy" dialog — the probe
    // is simply skipped and retried on the next timer tick.  Manual
    // (non-silent) calls keep the original handler so the user sees the
    // busy notification when another operation is in progress.
    wxEvtHandler* guardHandler = silent ? nullptr : wxHandler;
    AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, guardHandler};
    if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doTestOnlineProxies, this, wxHandler, silent);
}
```

### 3.2 修复逻辑

| 场景 | 修复前 | 修复后 |
|------|--------|--------|
| **silent 探活被拒**（有其他操作运行） | 弹出 "Operation Busy" ❌ | 静默跳过，等下一轮 timer ✅ |
| **手动测试被拒**（有其他操作运行） | 弹出 "Operation Busy" ✅ | 不变（仍弹窗） ✅ |
| **手动测试优先于 silent** | 被拒 ❌ | 取消 silent，启动手动 ✅ |

### 3.3 关键设计点

1. **silent 探活传入 `nullptr` handler**：
   - `AsyncOperationGuard` 检测到 `handler_ == nullptr` 时，拒绝路径不发送 `REJECT` 事件
   - 探活静默跳过，不打扰用户

2. **手动测试优先于 silent**：
   - 若用户手动点击「测试在线代理」时 silent 探活正在运行
   - 先 cancel + join 结束 silent，再启动手动测试
   - 避免手动请求也被 guard 拒绝

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
git log --oneline -3
5ce71e4 fix(pool): suppress Operation Busy dialog for silent periodic probe
171b660 fix(pool): cancel silent probe on manual online-proxy test to avoid Operation Busy dialog
6801ead feat(pool): thresholded probe-failure WARN + UI refresh on probe done
```

---

## 5. 相关文档

- **规格**: [`docs/specs/2026-09-15-Spec-StandalonePeriodicProbe-v1.0.md`](./2026-09-15-Spec-StandalonePeriodicProbe-v1.0.md)
- **计划**: [`docs/plans/2026-09-15-Plan-StandalonePeriodicProbe-v1.0.md`](../plans/2026-09-15-Plan-StandalonePeriodicProbe-v1.0.md)
- **总索引**: [`docs/INDEX.md`](../INDEX.md)

---

## 6. 后续建议

1. **考虑统一 guard 策略**：其他 silent 异步操作（如后台订阅更新、地区解析）也可能遇到相同问题，建议统一评估是否传入 `nullptr` handler
2. **增加日志**：silent 探活被拒时可在 DEBUG 级别输出日志，便于排查 timer 冲突
3. **UI 反馈**：若 silent 探活连续多次被拒（说明系统持续繁忙），可在状态栏显示轻量提示
