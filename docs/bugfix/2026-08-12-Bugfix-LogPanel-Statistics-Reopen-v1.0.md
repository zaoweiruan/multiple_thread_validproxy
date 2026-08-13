# Bugfix: 日志统计按钮关闭弹窗后无法再次打开

- 日期: 2026-08-12
- 类型: Bugfix
- 模块: `src/ui/LogPanel.cpp`（LogPanel 日志统计功能）
- 版本: v1.0
- 状态: ✅ completed

---

## 1. 问题现象

LogPanel 工具栏「日志统计」按钮**只能成功打开一次**：第一次点击异步解析并弹窗
正常；关闭统计弹窗后再次点击，按钮无任何反应，统计窗口无法再弹出。

## 2. 根因分析

`LogPanel::onLogStatistics` 使用防叠加守卫：

```cpp
void LogPanel::onLogStatistics(wxCommandEvent&) {
    if (statsThread_.joinable()) {
        return; // a statistics parse is already running; ignore repeated clicks
    }
    ...
    statsThread_ = std::thread([this, logPath]() {
        LogStatisticsResult result = parseLogFile(logPath);
        wxQueueEvent(this, new LogStatisticsEvent(result));
    });
}
```

C++ 标准规定：`std::thread::joinable()` 在线程**执行完毕之后仍返回 `true`**，
直到对该线程对象显式调用 `join()` 或 `detach()`。

实际执行序列：

1. 第一次点击：`statsThread_` 为默认构造（joinable = false）→ 启动后台解析线程。
2. 后台线程解析完成，`wxQueueEvent` 投递 `LogStatisticsEvent` 后线程函数返回、
   线程终止——但 `statsThread_` **从未被 `join()`**，joinable 恒为 `true`。
3. UI 线程收到事件，`onLogStatisticsResult` 弹窗展示统计结果——该回调
   **没有回收（join）线程对象**。
4. 用户关闭弹窗后再次点击：「日志统计」守卫 `statsThread_.joinable() == true`
   → 直接 `return`，统计永远不会再次启动。

**结论**：防叠加守卫语义正确，但缺少"结果回调后回收线程"的闭环，导致
线程对象永久处于 joinable 状态，一次性耗尽。

## 3. 修复

在 `LogPanel::onLogStatisticsResult` 开头回收已结束的后台解析线程：

```cpp
void LogPanel::onLogStatisticsResult(LogStatisticsEvent& event) {
    // 回收已结束的后台解析线程：std::thread 执行完毕后 joinable() 仍为 true，
    // 若不在此 join，下次点击 onLogStatistics 会被防叠加守卫拦截而无法再次统计。
    if (statsThread_.joinable()) {
        statsThread_.join();
    }
    const LogStatisticsResult& result = event.getResult();
    ...
}
```

修复后状态机：

- **点击时**：若上次解析仍在进行（事件未到达），joinable 为 true → 防叠加 return
  （预期行为）；若上次解析已完成，joinable 为 false → 可再次启动。
- **结果回调时**：join 回收线程对象，joinable 复位为 false，为下一次点击放行。

### 3.1 线程安全说明

- `statsThread_` 的赋值（`onLogStatistics`）与回收（`onLogStatisticsResult`）
  均发生在 **UI 线程**（事件处理上下文），工作线程仅读取捕获的 `logPath` 与
  `this`（用于 `wxQueueEvent`），不触碰 `statsThread_` 成员——无数据竞争。
- 事件到达 UI 线程时，工作线程已执行完 `wxQueueEvent` 即将退出，`join()`
  仅等待线程函数返回（微秒级），不会造成可感知的 UI 阻塞。
- 析构路径 `~LogPanel` 原有 `joinable() → join()` 兜底保持不变（此时通常
  已被结果回调 join，joinable 为 false，兜底分支跳过）。

## 4. 验证

- 全量构建：`cmake --build build --parallel 8` → 347/347 编译链接成功
  （仅既有 Utils.cpp PROCESSENTRY32W missing-field-initializers 噪音警告）。
- 全量回归：`ctest --test-dir build -V` → 23/24 通过；唯一失败项
  CurlEasyHandleTest 为**网络相关偶发失败**（与本次改动无关，LogPanel.cpp
  不参与其链接），单独重跑 `ctest --test-dir build -R CurlEasyHandleTest`
  → 1.80s Passed，恢复 24/24 全绿。
- 手工验证路径：连续多次点击「日志统计」→ 每次均弹窗；解析进行中重复点击
  无叠加（防叠加保留）。

## 5. 范围与影响

- 改动仅 `src/ui/LogPanel.cpp`（`onLogStatisticsResult` 增加 5 行线程回收）。
- 解析器 `LogStatistics.h/.cpp`、事件 `LogStatisticsEvent`、单测
  `test_log_statistics.cpp`（8 用例）均不受影响。
- 防叠加语义完整保留：仅修复"一次耗尽"缺陷。
