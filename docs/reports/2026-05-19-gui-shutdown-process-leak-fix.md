# 2026-05-19 GUI 退出子进程残留修复报告

## 问题描述

`validproxy -ui` 关闭界面后，xray.exe 子进程未被终止，留在 Task Manager 中成为僵尸进程。

## 根因分析

### 主要根因：`AppController::~AppController()` 析构顺序错误

**文件**：`src/ui/AppController.cpp:29-41`（修复前）

```cpp
AppController::~AppController() {
    cancelRequested_ = true;
    
    // ❌ WRONG ORDER: 先销毁 XrayManager
    XrayManager::release();
    
    // ❌ DETACH: detach 后线程仍可能在访问已销毁的 XrayManager 引用,
    //        从而持有对 xray 实例的悬垂指针，阻止进程退出
    workerThread_.detach();
}
```

**数据流**：
```
用户关闭窗口
  → MainFrame::onClose() { cancelTest(); event.Skip(); }
  → wxWidgets 析构 MainFrame
    → ~MainFrame() { delete controller_; }
      → ~AppController() {
            cancelRequested_ = true;    // 注入 cancel flag
            XrayManager::release();     // 立刻销毁单例
            workerThread_.detach();     // 工作线程被"剥离"
        }
          → 工作线程在 detach 后仍持有已销毁单例的 xray 实例的引用
          → CloseHandle(xray process handle) 被提前关闭
          → xray.exe 成僵尸 (Task Manager 可见)
```

> **detach 的危害**：`detach()` 不会阻塞或同步任何操作——线程继续运行（可能数分钟直到工作完成），并且持有对 `XrayManager` singleton（已被 `release()` 销毁）的空悬引用。延后到 Operator 手工结束。

### 次要问题：`XrayManager::release()` 双重 `stopAll()` 冗余日志

**文件**：`src/XrayManager.cpp:23-33` + `~XrayManager()`

```cpp
void XrayManager::release() {
    instance_->stopAll();    // 第一次调用：正常终止所有 xray
    delete instance_;        // 触发 ~XrayManager()
    ...
}

XrayManager::~XrayManager() {
    stopAll();               // 第二次调用：instances_ 已清空，仅打 0 条日志
}
```

问题：`release()` 内部先 `stopAll()` 再 `delete instance_`；触发 `~XrayManager()` 再次 `stopAll()` → 空 vector → `cleared 0 instance(s)` 日志。

## 修复

### FIX-1：反转析构顺序（`join()` 替换 `detach()`）

**修改文件**：`src/ui/AppController.cpp:29-43`

```cpp
AppController::~AppController() {
    cancelRequested_ = true;            // 注入 cancel flag

    // ✅ CORRECT: Wait for worker to finish before deallocating XrayManager
    if (workerThread_.joinable()) {
        workerThread_.join();           // 阻塞直到 worker 完全退出
    }

    // XrayManager 释放操作此时是安全的：无活跃线程持有引用
    XrayManager::release();
}
```

**原理验证**：
- `cancelRequested_ = true` → worker 下次检查 flag 时立即开始退出
- `workerThread_.join()` → 阻塞主线程直至 worker 的 stack frame 完全 unwind
- worker 在 `doFindBestProxy / doTestSubscription / ...` 结束返回后，worker thread 析构完毕
- `XrayManager::release()` 此时才是线程安全的（无其他线程持有 singleton）

### FIX-2：`XrayManager` 幂等化 — 消除双重 `stopAll()` 冗余日志

**修改文件**：
- `include/XrayManager.h` — 新增 `bool stopped_{false};` 成员
- `src/XrayManager.cpp:23-33` — `release()` 设 `stopped_ = true`
- `src/XrayManager.cpp:85-87` — `~XrayManager()` 加标志检查

```cpp
XrayManager::~XrayManager() {
    if (!stopped_) {        // release() 已执行时跳过
        stopAll();
    }
}
```

## 验证结果

| 维度 | 结果 |
|---|---|
| Build | ✅ 成功（Ninja, Debug） |
| Tests | ✅ 3/3 Passed |
| CLI `-FMIN` | ✅ `XrayManager::release()` → xray PID 消失，零残留 |
| 设计覆盖率 | ✅ `stopAll()` 幂等性修复；AppController 析构顺序修复 |

## 涉及文件汇总

| 文件 | 改动 |
|---|---|
| `src/ui/AppController.cpp` | `~AppController` 析构顺序：join→release，替换 detach |
| `include/XrayManager.h` | 新增 `bool stopped_{false};` |
| `src/XrayManager.cpp` | `release()` 置 `stopped_ = true`；`~XrayManager()` 布尔守卫 |
| `include/XrayInstance.h` | (上次) `GRACEFUL_SHUTDOWN_MS` 命名常量 |
| `src/XrayInstance.cpp` | (上次) `SetInformationJobObject` 检查 + `stop()` 全量日志 |
| `docs/specs/2026-05-19-ui-shutdown-design.md` | (上次) 设计文档修订版 |

## 遗留说明

- `Detach 后僵尸问题` 的**真实复现**需手动在 GUI 中点击"Find Best Proxy"再立刻关闭窗口。
  代码顺序修复在静态逻辑上确保退出链路唯一——worker 必须先退出，manager 才能销毁。
- 如需在模拟环境中完整复现 GUI 关闭后 xray 残留的修复效果，可通过
  `validproxy.exe -ui` → 点 "Find Best Proxy" → 点关闭窗口 → Task Manager 验证，
  观察到 xray.exe 在关闭后数秒内消失而非持续残留。
