---
title: "fix(UI): 关闭窗口时取消批量测试和查找代理操作"
type: fix
status: draft
date: 2026-05-26
origin: "Investigation of GUI close behavior leaving xray processes running after batch test / find proxy"
---

# Fix Plan: 关闭窗口时终止批量测试/查找代理的后台进程

## 问题描述 (Problem Summary)

当用户在批量测试或查找代理操作进行中关闭 GUI 窗口时，xray.exe 进程无法被终止，因为取消信号无法传播到实际执行工作的子对象。

### 当前问题

| 问题 | 位置 | 表现 |
|------|------|------|
| C1 | `AppController::doTestSubscription()` | 局部 `ProxyBatchTester` 不接收 `AppController` 的取消信号 |
| C2 | `AppController::doTestSingleProxy()` | 同上 - 局部 `ProxyBatchTester` |
| C3 | `AppController::doFindFirstProxy()` | 局部 `ProxyFinder` 无取消机制，循环中不检查取消 |
| C4 | `AppController::doFindBestProxy()` | 同上 - 局部 `ProxyFinder` |

### 根因分析

**双重取消标志：**
- `AppController` 有 `cancelRequested_` (原子 bool)
- `ProxyBatchTester` 有**自己的** `cancelRequested_` (原子 bool)
- 关闭窗口 → `AppController::cancelTest()` 设 `cancelRequested_=true`
- `ProxyBatchTester` 的 `cancelRequested_` 仍然为 `false`
- 工作线程只检查 `ProxyBatchTester::cancelRequested_`，所以永远看不到取消

**测试过程中关闭流程：**
1. 用户点 X → `MainFrame::onClose()` → `controller_->cancelTest()` → 设 `AppController::cancelRequested_ = true`
2. `~MainFrame()` → `delete controller_` → `~AppController()` → 设标志(多余) → `workerThread_.join()` 5秒超时 → `workerThread_.detach()`
3. 工作线程正在 `doTestSubscription()` 中运行，内部有**局部** `ProxyBatchTester tester` 
4. `tester.runWithSubId()` → `testProxiesMultiThreaded()` → 创建 N 个工作线程 → 只检查 `tester.cancelRequested_`（永远为 false）
5. 工作线程在 `proxyTester_->test(socksPort)` 处阻塞（curl，每个代理最长 10 秒）
6. `testProxiesMultiThreaded()` join 所有工作线程后才返回 → 然后才 `xrayManager_->stopAll()`

**查找过程中关闭流程：**
- `doFindFirstProxy()` 创建局部 `ProxyFinder finder`，循环中无取消检查
- `doFindBestProxy()` 同

## 范围边界

### 修改 (In Scope)
- `src/ProxyBatchTester.h` — 添加外部取消标志指针
- `src/ProxyBatchTester.cpp` — `isCancelled()` 检查外部标志
- `src/ProxyFinder.h` — 添加外部取消标志指针
- `src/ProxyFinder.cpp` — 循环中添加取消检查
- `src/ui/AppController.cpp` — 传入 `&cancelRequested_`

### 不改 (Out of Scope)
- `MainFrame` 关闭逻辑 — 已有正确调 `cancelTest()`
- `XrayManager` / `XrayInstance` — 不需要改动
- 测试框架 — 不存在，跳过
- `ProxyBatchTester::cancel()` 方法 — 保留不动（可能其他调用者仍使用）

## 文件结构

| 文件 | 变更类型 | 职责 |
|------|---------|------|
| `src/ProxyBatchTester.h` | 修改 | 添加外部取消标志指针成员 + 构造函数重载 |
| `src/ProxyBatchTester.cpp` | 修改 | 统一 `isCancelled()` 检查内外标志 |
| `src/ProxyFinder.h` | 修改 | 添加外部取消标志指针成员 + 构造函数重载 |
| `src/ProxyFinder.cpp` | 修改 | 查找循环中添加取消检查 |
| `src/ui/AppController.cpp` | 修改 | 传入 `&cancelRequested_` 给所有子对象 |

## 实施方案

### Step 1: ProxyBatchTester — 添加外部取消指针

**`src/ProxyBatchTester.h`**:
- 添加新成员: `std::atomic<bool>* externalCancel_{nullptr};`
- 添加新构造函数或参数:
  ```cpp
  ProxyBatchTester(sqlite3* db, const AppConfig& config, const std::string& baseDir,
                   const std::string& settingsDir = "",
                   const std::string& subId = "",
                   std::atomic<bool>* externalCancel = nullptr);
  ```
  或使用默认参数方式保持向后兼容。

**`src/ProxyBatchTester.cpp`**:
- 构造函数初始化 `externalCancel_`
- 修改 `isCancelled()` 方法:
  ```cpp
  bool isCancelled() const {
      if (externalCancel_ && externalCancel_->load())
          return true;
      return cancelRequested_.load();
  }
  ```
  或者更简洁：
  ```cpp
  bool isCancelled() const {
      return cancelRequested_.load() || (externalCancel_ && externalCancel_->load());
  }
  ```

**现有调用** `isCancelled()` 的位置自动生效（`workerThreadFunc()` 中多处检查）。

### Step 2: ProxyFinder — 添加取消机制

**`src/ProxyFinder.h`**:
- 添加成员: `std::atomic<bool>* cancelRequested_{nullptr};`
- 构造函数重载或默认参数:
  ```cpp
  ProxyFinder(sqlite3* db, XrayManager* xrayMgr,
              const std::string& xrayPath,
              const std::string& testUrl,
              const std::string& targetUrl,
              int timeoutMs,
              std::atomic<bool>* cancelFlag = nullptr);
  ```

**`src/ProxyFinder.cpp`**:
- 构造函数初始化 `cancelRequested_`
- `findFirstWorkingProxy()` 循环中每个代理测试前检查:
  ```cpp
  for (size_t i = 0; i < validProxies.size(); ++i) {
      if (cancelRequested_ && cancelRequested_->load())
          return nullptr;  // 或空字符串
      ...
  }
  ```
- `findWorkingProxy()` 同样:
  ```cpp
  for (size_t i = 0; i < validProxies.size(); ++i) {
      if (cancelRequested_ && cancelRequested_->load())
          return {};
      ...
  }
  ```

### Step 3: AppController — 传递取消标志

**`src/ui/AppController.cpp`**:

- `doTestSubscription()` (line ~330): 
  ```cpp
  // Before: ProxyBatchTester tester(db_, config_, "");
  // After:
  ProxyBatchTester tester(db_, config_, "", "", "", &cancelRequested_);
  ```

- `doTestSingleProxy()` (line ~358):
  ```cpp
  // Before: ProxyBatchTester tester(db_, config_, "");
  // After:
  ProxyBatchTester tester(db_, config_, "", "", "", &cancelRequested_);
  ```

- `doFindFirstProxy()` (line ~392):
  ```cpp
  // Before: ProxyFinder finder(...)
  // After:
  ProxyFinder finder(db_, m_xrayManager.get(), ..., &cancelRequested_);
  ```

- `doFindBestProxy()` (line ~442):
  ```cpp
  // Before: ProxyFinder finder(...)
  // After:
  ProxyFinder finder(db_, m_xrayManager.get(), ..., &cancelRequested_);
  ```

### Step 4: 验证

1. CMake Build — `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8`
2. CTest — `ctest -V`
3. GUI 验证:
   - 启动批量测试 → 点击关闭 → 确认测试立即停止 → 无残留 xray 进程
   - 启动 Find First Proxy → 点击关闭 → 确认立即停止
   - 启动 Find Best Proxy → 点击关闭 → 确认立即停止

## 风险与注意事项

- `externalCancel_` 指针指向 `AppController::cancelRequested_`，生命周期必须保证：只要 `ProxyBatchTester`/`ProxyFinder` 在运行，`AppController` 必须存活。
  - ✅ 实际 `ProxyBatchTester`/`ProxyFinder` 是 `AppController::workerThread_` 内的局部变量，`AppController destructor` 在 `join/detach` 后才完成，所以指针有效。
- `ProxyFinder` 之前完全没有取消机制，这是添加新功能的最小侵入方式。
- 不影响已有使用 `ProxyBatchTester::cancel()` 的代码路径（如有其他地方直接调用 `cancel()`）。
