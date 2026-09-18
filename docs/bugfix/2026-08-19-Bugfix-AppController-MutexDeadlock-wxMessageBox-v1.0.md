# Bugfix: startStandaloneProxy wxMessageBox 同线程死锁致程序假死

- 日期: 2026-08-19
- 类型: Bugfix
- 模块: AppController (standalone proxy)
- 版本: v1.0
- 日志: `bin/log/ui_20260819_143216.log`

---

## 1. 问题描述

启动独立代理后，连通性测试失败时应弹出 `wxMessageBox` 询问用户是否关闭进程。但实际表现：

1. 弹窗未出现
2. 程序进入假死状态（标题栏显示"未响应"），无法操作

日志最后一行：

```
[2026-08-19 14:34:53] [WARN] [StandaloneProxy] Connectivity test FAILED for 4415892188933738567 on SOCKS5 :10812
```

之后无任何输出，程序挂死。

---

## 2. 根因分析

### 2.1 调用链

```
用户点击启动代理
  → ProxyListPanel::onStartProxy          (主线程, wxWidgets 事件处理)
    → AppController::startStandaloneProxy  (主线程, 同步调用)
      → lock_guard<mutex> standaloneMutex_ (L614, RAII 持锁整个函数)
      → CreateProcessA / waitForPort / verify (阻塞 ~15s)
      → wxMessageBox(...)                  (L870, 弹出模态对话框)
```

### 2.2 死锁机制

`wxMessageBox` 在 Windows 上调用 `::MessageBox()`，后者运行自己的 Win32 消息循环。该消息循环分发所有 `WM_TIMER` 消息。

```
主线程 (持有 standaloneMutex_)
  │
  ├─ wxMessageBox → ::MessageBox 消息循环
  │    │
  │    ├─ WM_TIMER 到达
  │    │    → wxWidgets 拦截 → wxTimer::Notify
  │    │    → MainFrame::onProxyMonTimer        (同一线程!)
  │    │        ├─ spawn adoptDanglingStandaloneProxies (detach, 无锁问题)
  │    │        └─ getRunningStandaloneCount()
  │    │             → lock_guard<mutex> standaloneMutex_  ← 💀 同线程重复锁定
  │    │
  │    └─ 死锁: std::mutex 非递归, 同线程 lock = undefined behavior (挂死)
  │
  └─ wxMessageBox 永远无法返回
```

### 2.3 关键条件

| 条件 | 状态 |
|------|------|
| `startStandaloneProxy` 在主线程执行 | ✅ `onStartProxy` 直接调用 |
| `standaloneMutex_` 在函数入口获取 | ✅ `lock_guard` L614 |
| `wxMessageBox` 弹出期间分发 `WM_TIMER` | ✅ Win32 `MessageBox` 消息循环 |
| `onProxyMonTimer` 在 `WM_TIMER` 时尝试锁定同一 mutex | ✅ `getRunningStandaloneCount()` L1075 |

---

## 3. 修复方案

### 3.1 核心思路

在调用 `wxMessageBox` 前释放锁，返回后重新获取。避免模态对话框消息循环中的同线程重锁。

### 3.2 变更

| 文件 | 变更 |
|------|------|
| `src/ui/AppController.cpp` L614 | `lock_guard` → `unique_lock` |
| L846-849 | port 不就绪路径: `lock.unlock()` → `wxMessageBox` → `lock.lock()` |
| L876-879 | 连通性失败路径: `lock.unlock()` → `wxMessageBox` → `lock.lock()` |

### 3.3 同会话附带修复 (非本次死锁直接相关)

| 文件 | 变更 | 原因 |
|------|------|------|
| `src/ui/AppController.h` L190 | 新增 `ProfileExItemDAO exDao_{db_}` | spec 补全: startup time DB 写入 |
| `src/ui/AppController.cpp` L893 | 连通性通过后调 `exDao_.updateStartupTime()` | spec §3.4 #3 |
| `src/ui/AppController.cpp` L1210 | 悬垂进程纳管后调 `exDao_.updateStartupTime()` | 同上 |
| `src/ui/AppController.cpp` 3 处 NotifyFn | 新增 `running = false` 写入 | 状态栏计数不更新 |
| `src/ui/AppController.cpp` 2 处 `started=true` | 事件 message 改为 `utils::getCurrentTimestamp()` | message 字段携带启动时间 |

---

## 4. 测试

- 构建通过 (`cmake --build build --parallel 8`, 0 error)
- 30/30 单测通过

---

## 5. 验收

- [x] 连通性测试失败 → `wxMessageBox` 正常弹出
- [x] 点击"是" → 进程终止, 程序正常
- [x] 点击"否/取消" → 进程保留, 程序正常
- [x] 端口未就绪路径同样修复
- [x] 程序不再假死
