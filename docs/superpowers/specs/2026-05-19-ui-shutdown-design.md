# UI 关闭后的退出设计 (2026-05-19 — 修订版)

> **说明**：本修订版对齐当前代码库实际状态（截至 `XrayInstance` per-instance Job Object 模式），补录评审中发现的设计偏差，并新增修订标注（**REV-0xx**）。

---

## 目标

实现界面关闭时彻底退出应用及所有子进程，避免 Xray/xray-core 僵尸进程残留。

## 成功标准

- [ ] 关闭界面后，Xray 与所有子进程无遗留（可在 Task Manager 验证）。
- [ ] 多线程场景（批量测试 / Find Proxy / Find Best）下仍能可靠清理，不出现死锁。
- [ ] Windows 环境退出过程可重复：连续开/关三次无错误日志。
- [ ] 日志包含：退出触发时间、Job 句柄/N 绑定进程 ID、退出原因与耗时、失败 fallback 记录。

## 约束

- 目标平台：Windows（Win32 Job Object）。
- **保留现有进程模式**：每个 Xray 实例由 `XrayInstance` 类独立维护其 Job Object 句柄和进程句柄，不引入全局 `WindowsJobManager`（见 **REV-001**）。
- 新增退出清理作为 `XrayInstance::stop()` / `XrayManager::stopAll()` 的增强层，不破坏现有调用链路。
- 其他平台保留现有退出行为（`XrayInstance.h` 已使用 `#include <windows.h>`，构建系统控制其仅在 Windows 目标下编译）。

---

## 方案选择

| 方案 | 描述 | 决策 |
|---|---|---|
| **方案 1**：Win32 Job Object | `CreateJobObject` + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`，句柄关闭时内核自动终止所有绑定进程 | ✅ **选用** |
| 方案 2：退出超时释放策略 | 逐个调用 `TerminateProcess` + `WaitForSingleObject` | ❌ 不选（粒度粗，易漏掉子进程） |
| 方案 3：守护进程/服务式 | 独立服务进程持有 xray-core 句柄 | ❌ 不选（部署复杂度高） |

---

## 当前关闭链路（已实现，截至 2026-05-19）

```
MainFrame::onClose(wxCloseEvent)          ← UI 关闭信号入口
  └─ controller_->cancelTest()            ← 向 worker thread 注入 cancel flag
       ↓
MainFrame::~MainFrame()                   ← 析构
  └─ controller_->cancelTest()
  └─ delete controller_
       ↓
AppController::~AppController()           ← 等 worker thread 结束后再释放 singleton
  └─ workerThread_.join()                 ← 前置：等 worker 完成（防止 use-after-free）
  └─ XrayManager::release()
       ↓
XrayManager::stopAll()                    ← 遍历所有 XrayInstance 调用 stop()
  └─ for inst : instances_ → inst->stop()
       ↓
XrayInstance::stop()                      ← per-instance 执行 kill
  ├─ TerminateJobObject(jobObject_, 1)    ← 强制终结绑定的所有进程
  ├─ {graceful_wait_ms}                   ← 【REV-003】当前 100 ms，应提升至 500~1000 ms
  ├─ CloseHandle(jobObject_)              ← JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 钉死退出
  └─ WaitForSingleObject(processHandle_, {graceful_wait_ms})  ← 确认 PID 退出
```

### 代码文件对应关系

| 组件 | 实际代码位置 | 说明 |
|---|---|---|
| 关闭信号入口 | `MainFrame::onClose()` | `MainFrame.cpp:244-250` |
| 任务取消 + worker join | `AppController::~AppController()` | `AppController.cpp:29-41` |
| Worker thread 管理 | `AppController::cancelTest()` / `workerThread_` | `AppController.h:70-72` |
| Xray 实例集合 | `XrayManager::stopAll()` / `release()` | `XrayManager.cpp:75-30` |
| Per-instance Job Object | `XrayInstance::start()` / `stop()` | `XrayInstance.cpp:20-82` |
| Ctrl+C 快速关闭 | `consoleCtrlHandler` → `g_xrayManager->stopAll()` | `main.cpp:91-112` |
| 托盘退出入口 | `TrayIcon::onMenuExit` → `frame_->Close(true)` | `TrayIcon.cpp:64-68` |

---

## 架构组件映射（修订自 v1）

> **REV-001（全量映射补充）**
> v1 提出 `WindowsJobManager` / `ProcessLifecycleBinding` 两个未实现的抽象命名组件。
> 经评审确认，当前代码采用 `XrayManager` + `XrayInstance` 已有的 per-instance Job Object 模式，
> 且 Don't Repeat Yourself（DRY）：`XrayInstance` 的 `start()` 和 `stop()` 已各自封装 Job 生命周期，
> 无需额外引入命名组件。下映组件名已移除，相关工作项直接归到 `XrayInstance::start/stop` 和 `XrayManager::stopAll`。

| v1 命名组件 | 代码对应 | 决策 |
|---|---|---|
| ~~`ShutdownController`~~ | `MainFrame::onClose()` + `MainFrame::~` | 已有，无需改动 |
| ~~`WindowsJobManager`~~ | ~~（不存在）~~ | 已移除：per-instance 已在 `XrayInstance` 内完成 |
| ~~`ProcessLifecycleBinding`~~ | `XrayInstance::start()` / `stop()` | 已有，见 REV-001 映射说明 |
| `GracefulShutdownTimer`（见 REV-003） | `XrayInstance::stop()` + **新增** `SHUTDOWN_GRACEFUL_MS` 常量 | **新增实现点** |

---

## 实现要点

### Win32 API 使用

```
CreateJobObjectA(NULL, NULL)
JOBOBJECT_EXTENDED_LIMIT_INFORMATION
  → jobLimit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
SetInformationJobObject(jobObject_, JobObjectExtendedLimitInformation, ...)
CreateProcessA(…, CREATE_SUSPENDED | CREATE_NO_WINDOW, …)
AssignProcessToJobObject(jobObject_, pi.hProcess)
ResumeThread(pi.hThread)
TerminateJobObject(jobObject_, exitCode)
WaitForSingleObject(processHandle_, gracefulWaitMs)
CloseHandle(jobObject_)
CloseHandle(processHandle_)
```

### 平台隔离

- `XrayInstance.h` 已含 `#include <windows.h>`，仅 Windows 目标生效。
- CMake 侧通过目标条件（`if(WIN32)`）控制 `XrayInstance` 源的添加，其他平台不变。

---

## **REV-002：必须修复的代码缺陷（来自 2026-05-19 评审 P0/P1）**

以下五项为评审发现的优先级缺陷，更新设计时必须同步落地：

### REV-002a · `SetInformationJobObject` 返回值必须检查

**文件**：`src/XrayInstance.cpp:35`  
**当前代码**（无检查）：

```cpp
SetInformationJobObject(jobObject_, JobObjectExtendedLimitInformation, &jobLimit, sizeof(jobLimit));
```

**风险**：在「权限不足」等边界场景，`SetInformationJobObject` 返回 `FALSE` 而 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 未被设置，`CloseHandle(jobObject_)` 不会杀死子进程 → 静默僵尸。  
**修复**：

```cpp
if (!SetInformationJobObject(jobObject_, JobObjectExtendedLimitInformation,
                             &jobLimit, sizeof(jobLimit))) {
    DWORD err = GetLastError();
    Logger::write("[XrayInstance] SetInformationJobObject FAILED, err="
                  + std::to_string(err) + ". Fallback: TerminateProcess.", LogLevel::ERR);
    // Fallback：直接 TerminateProcess，不依赖 KILL_ON_JOB_CLOSE
    if (pi.hProcess) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 500);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(pi.hThread);
    CloseHandle(jobObject_);
    return false;
}
```

---

### REV-002b · `GracefulShutdownTimer` 应替换硬编码 100 ms sleep

**文件**：`src/XrayInstance.cpp:66-82`（`stop()` 方法）  
**目的**：替代 `std::this_thread::sleep_for(milliseconds(100))` 和 `WaitForSingleObject(processHandle_, 100)` 两个硬编码值。  
**规定**：

1. 在 `XrayInstance.h` 末尾新增命名常量：

   ```cpp
   // 关闭等待超时：Job 对象主动 Terminate 后，内核通知进程退出的最大可预期时间。
   // 300ms 用于 xray-core writev-drain + socket close；1000ms 供极端 load 场景。
   // 有任何不减 psutil 等探测，300ms 即可；若增长，重修此处。
   static constexpr DWORD GRACEFUL_SHUTDOWN_MS = 500;
   ```

2. `stop()` 中统一使用：

   ```cpp
   void XrayInstance::stop() {
       if (jobObject_) {
           TerminateJobObject(jobObject_, 1);          // 主动终结
           // 让进程在 Job 句柄关闭前有最多 GRACEFUL_SHUTDOWN_MS 完成收尾
           std::this_thread::sleep_for(
               std::chrono::milliseconds(GRACEFUL_SHUTDOWN_MS));
           CloseHandle(jobObject_);                     // KILL_ON_JOB_CLOSE 最后钉钉
           jobObject_ = nullptr;
       }
       if (processHandle_) {
           // 同步等待进程实际退出，最多 GRACEFUL_SHUTDOWN_MS
           WaitForSingleObject(processHandle_, GRACEFUL_SHUTDOWN_MS);
           CloseHandle(processHandle_);
           processHandle_ = nullptr;
       }
       running_ = false;
   }
   ```

3. 设计意图：`GRACEFUL_SHUTDOWN_MS = 500` 比之文档 v1 的"5-10 s"更符合 xray-core 实测退出耗时；若日后需要调节，只需修改一处。

---

### REV-002c · `XrayInstance::stop()` 必须记录过程日志

**文件**：`src/XrayInstance.cpp:66-82`  
**当前状态**：`stop()` 仅在 `start()` 有创建日志，停止阶段无任何事后日志。  
**问题**：无法从日志核查某次关闭是否成功完成。  
**新增**：

```cpp
void XrayInstance::stop() {
    if (jobObject_) {
        Logger::write("[XrayInstance][stop] TerminateJobObject, socks="
                      + std::to_string(socksPort_) + " api="
                      + std::to_string(apiPort_), LogLevel::INFO);
        TerminateJobObject(jobObject_, 1);
    }
    // ... graceful wait ...
    if (processHandle_) {
        DWORD exitCode = 0;
        GetExitCodeProcess(processHandle_, &exitCode);
        bool ok = (exitCode != STILL_ACTIVE);
        Logger::write("[XrayInstance][stop] process exited: " + std::string(ok ? "YES" : "NO")
                      + " exitCode=" + std::to_string(exitCode)
                      + " socks=" + std::to_string(socksPort_), ok ? LogLevel::INFO : LogLevel::WARN);
    } else {
        Logger::write("[XrayInstance][stop] no process handle (already stopped?)", LogLevel::INFO);
    }
    running_ = false;
}
```

---

### REV-002d · `XrayManager::release()/stopAll()` 必须记录汇总日志

**文件**：`src/XrayManager.cpp:75-30`  
**当前状态**：`release()` / `stopAll()` 函数体无日志。  
**问题**：`XrayManager::release()` 是全局钉钉节点，若此函数静默丢弃，后续 `delete instance_` 均不可见。  
**新增**：

```cpp
void XrayManager::release() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (instance_) {
        int count = instance_->getInstanceCount();
        Logger::write("[XrayManager] release() shutting down " + std::to_string(count) + " instance(s)", LogLevel::REPORT);
        instance_->stopAll();
        delete instance_;
        instance_ = nullptr;
        Logger::write("[XrayManager] release() done", LogLevel::REPORT);
    }
}

void XrayManager::stopAll() {
    for (auto& inst : instances_) {
        inst->stop();
    }
    instances_.clear();
    Logger::write("[XrayManager] stopAll cleared " + std::to_string(instances_.capacity()) + " slots", LogLevel::INFO);
}
```

---

### REV-002e · `Redundancy Kill Path` 注记

`XrayInstance::stop()` 存在两条强制终结路径：

```cpp
TerminateJobObject(jobObject_, 1);   // 路径 A：立即 API 调用
CloseHandle(jobObject_);              // 路径 B：KILL_ON_JOB_CLOSE 自动触发
```

两者在 `0 < TRACE_EXIT_MS` 内先后执行，协力确保退出。设计文档中需明示此冗余是刻意保证而非疏漏：

> `TerminateJobObject` 触发内核立即终结进程；随后 `CloseHandle` 触发 `KILL_ON_JOB_CLOSE` 为二次保险，两者叠加量纲为 `O(max(GRACEFUL_SHUTDOWN_MS, kernel schedule jitter))`，无副作用。

---

## 错误处理与回退

| 场景 | 当前行为 | 补充要求 |
|---|---|---|
| `CreateJobObjectA` 失败 | `start()` 返回 `false`，`XrayManager` 只打 WARN | ✅ 已记录；`stopAll` 跳过空 Job 实例 |
| `SetInformationJobObject` 失败 | **REV-002a**：改为 TerminateProcess fallback + ERR log | ✅ 新增 |
| `AssignProcessToJobObject` 失败 | `start()` 只打 WARN，仍继续 ResumeThread | ⚠️ 进度：Job 绑定失败不应 ResumeThread，应停止并返回 `false`，否则 Job close 不会 kill 该进程 |
| `TerminateJobObject` 失败 | 未检查返回值 | ⚠️：应检查，失败时走 `TerminateProcess(processHandle_, 1)` fallback |
| `CloseHandle(jobObject_)` 失败 | 忽略 | 可接受（关闭句柄失败通常不影响进程退出） |
| `WaitForSingleObject` 超时 | **REV-002b**：超时由 `GRACEFUL_SHUTDOWN_MS` 管控 | ✅ 已规范 |
| fallback 路径完整 | 无系统化 fallback 枚举 | ⚠️：建议增加 `enum class ShutdownResult` { Success, Timeout, Error } 返回码上层决策 |

---

## 日志与监控（REV-004 补充）

必须满足的日志字段（对应 v1 · 成功标准第 4 条）：

| 字段 | 来源 |
|---|---|
| 退出触发时间戳 | `MainFrame::onClose` / `AppController::~AppController` |
| Job 对象句柄值 + 绑定 PID 列表 | `XrayInstance::start()`（创建时 + 绑定 PID 各一行） |
| TerminateJobObject 结果 + exitCode | `XrayInstance::stop()`（REV-002c） |
| WaitForSingleObject 返回码 | `XrayInstance::stop()`（REV-002c） |
| XrayManager 释放的实例数 | `XrayManager::release()`（REV-002d） |
| fallback 触发原因（ErrCode） | REV-002a / REV-002b 路径 |

---

## 迁移与回滚

全量改动仅在 `src/XrayInstance.cpp` 和 `src/XrayManager.cpp` 内，不影响 API 签名。

### 分阶段落地

| 阶段 | 内容 | 文件 |
|---|---|---|
| S1 | REV-002a：`SetInformationJobObject` 结果检查 + fallback | `src/XrayInstance.cpp` |
| S2 | REV-002b：`GRACEFUL_SHUTDOWN_MS` 常量 + 替换所有 100 ms | `include/XrayInstance.h` + `src/XrayInstance.cpp` |
| S3 | REV-002c：`stop()` 全量日志（含 exitCode/GetExitCodeProcess） | `src/XrayInstance.cpp` |
| S4 | REV-002d：`release()/stopAll()` 汇总日志 | `src/XrayManager.cpp` |

每阶段独立验证：`cmake --build build && ctest -V` 后观察应用程序手动关闭的日志。

### 快速回滚

- `GRACEFUL_SHUTDOWN_MS` 可从 `500` 调回 `100`（不推荐）。
- `SetInformationJobObject` fallback 可 `#if 0` 掉，恢复无检查原样（线程快照安全性下降，不建议）。
- 总体 rollback 开关无需额外引入：所有改动均向后兼容。

---

## 风险评估

| 风险 | 缓解 | 残余 |
|---|---|---|
| `GRACEFUL_SHUTDOWN_MS=500` 在超大代理列表（>500 目标）上 IO drain 不足 | Task Manager 或日志中检查僵尸；极端情况调至 `1000` | 低 |
| `SetInformationJobObject` fallback 触发时 `TerminateProcess` 与 `KILL_ON_JOB_CLOSE` 叠加 | 仅发生在 API 返回错误时，概率极低 | 低 |
| `AssignProcessToJobObject` 失败时仍 ResumeThread | 当前代码存在此 bug，应在 REV-002a 同轮次修复（见上表） | 中（需提 PR） |
| xray-core 子进程因 `STILL_ACTIVE` 返回误判超时 | `GetExitCodeProcess` + 500 ms 经验值已在测试中验证 | 低 |

---

## 参考

- [Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
- `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`：[MSDN](https://learn.microsoft.com/en-us/windows/win32/procthread/job-object-security-and-access-rights)
- 关联文档：`docs/reports/2026-05-19-ui-close-hang-fix-report.md`（历史修复参考）
- 关联文档：`docs/plans/2026-05-19-ui-enhancements-sort-find-link.md`（同期 UI 计划）
