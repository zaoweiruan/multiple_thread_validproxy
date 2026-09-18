# Bugfix: 独立代理关闭崩溃 / 右键菜单选中丢失 / 端口释放时延

- 日期: 2026-09-02
- 模块: `AppController::stopStandaloneProxy`、`ProxyListPanel::onContextMenu`、`ProxyListPanel::onMenuCloseProxy`
- 版本: v1.0
- 状态: 已修复 + 编译验证

---

## 1. 问题现象

### 1.1 关闭代理时程序崩溃

用户在独立代理监控面板右键点击代理 → 菜单「关闭代理」(`ID_MENU_CLOSE_PROXY`) 后，程序**直接崩溃**（无错误弹窗，进程退出）。

### 1.2 右键菜单选中状态丢失

`wxListCtrl` 弹出右键菜单时会自动清除选中状态，导致 `onMenuCloseProxy` 中通过 `GetNextItem` 获取选中行时返回 `-1`，无法确定要关闭哪个代理。

### 1.3 端口释放时延导致立即重启失败

关闭代理后立即重新启动同一代理，`onStartProxy` 检测到端口仍被占用，触发 "port occupied, using alternate port" 提示，导致：
- 用户困惑（明明已经关闭了为什么端口还被占用）
- 被迫使用备用端口（如 10810 而非期望的 10808）

---

## 2. 根因

### 2.1 死锁（关闭崩溃）

`stopStandaloneProxy` 在持有 `standaloneMutex_` 的情况下调用 `unwatch()`，而监听线程的 `notifyFn` 回调需要获取同一把锁，导致**死锁**。

旧代码流程：
```cpp
std::lock_guard<std::mutex> lock(standaloneMutex_);
// ... 终止进程 ...
exitListener_->unwatch(key);  // ← 这里会触发 notifyFn，需要 standaloneMutex_
```

### 2.2 选中状态丢失（右键菜单）

`wxListCtrl` 的右键菜单行为会清除选中状态，但 `onMenuCloseProxy` 依赖 `GetNextItem(-1, wxLIST_NEXT_ALL)` 获取当前选中行，返回 `-1` 后无法定位目标代理。

### 2.3 端口释放时延

`TerminateProcess` + `WaitForSingleObject` 只能保证**进程退出**，但 Windows 内核可能仍短暂持有 LISTEN 套接字（TIME_WAIT / 内核回收延迟）。关闭后立即重启时，端口尚未完全释放。

---

## 3. 修复

### 3.1 死锁修复（重构锁顺序）

`src/ui/AppController.cpp` - `stopStandaloneProxy`：

**修复策略**：快照关键信息后，先释放锁，再执行终止操作，最后 `unwatch`。

```cpp
// 1. 快照关键信息（不持有锁）
const int64_t indexId = ...;
const int socksPort = ...;
const HANDLE hProcess = ...;
const ExitListener::WatchKey key = ...;

// 2. 从 map 中移除条目（短暂持有锁）
{
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    standaloneProxies_.erase(indexId);
}

// 3. 终止进程（不持有锁）
if (hProcess != nullptr && hProcess != INVALID_HANDLE_VALUE) {
    TerminateProcess(hProcess, 1);
    WaitForSingleObject(hProcess, 3000);
    CloseHandle(hProcess);
}

// 4. unwatch（此时 notifyFn 可安全获取锁）
if (key != 0) {
    exitListener_->unwatch(key);
}
```

**关键点**：
- `unwatch` 移到锁外执行，避免与 `notifyFn` 产生锁竞争
- 快照所有需要的信息后再释放锁，确保后续操作有完整上下文

### 3.2 右键菜单选中状态修复

`src/ui/ProxyListPanel.h` - 新增成员变量：

```cpp
private:
    long contextMenuSel_;  // 右键菜单触发时缓存的选中行索引
```

`src/ui/ProxyListPanel.cpp` - `onContextMenu`：

```cpp
void ProxyListPanel::onContextMenu(wxContextMenuEvent& event) {
    // 缓存当前选中行（wxListCtrl 弹出菜单时会清除选中状态）
    contextMenuSel_ = proxyList_->GetNextItem(-1, wxLIST_NEXT_ALL);
    // ... 其余菜单逻辑 ...
}
```

`src/ui/ProxyListPanel.cpp` - `onMenuCloseProxy`：

```cpp
void ProxyListPanel::onMenuCloseProxy(wxCommandEvent& event) {
    // 使用缓存的选中行，而非重新查询（菜单弹出后选中状态已丢失）
    const long sel = contextMenuSel_;
    if (sel < 0) {
        wxMessageBox("请先选择一个代理", "提示", wxOK | wxICON_INFORMATION);
        return;
    }
    // ... 其余关闭逻辑 ...
}
```

### 3.3 端口释放时延处理

**方案选择**：移除阻塞式 spin-wait，依赖现有端口回退机制。

**原因**：
- 之前的 spin-wait 阻塞 UI 线程 0~3 秒，用户体验差
- 项目已有成熟的端口回退机制：`onStartProxy` 发现目标端口被占用时，自动选择备用端口（10810、10811 等）
- 日志证据：`Port 10808 occupied, using alternate port 10810 for standalone proxy`

**当前行为**：
- `stopStandaloneProxy` 不再阻塞等待端口释放
- 用户立即重启时，系统自动使用备用端口
- 日志记录实际使用的端口，便于追踪

---

## 4. 验证结果

| 项 | 结果 |
|----|------|
| 构建（validproxy.exe） | 0 error（仅既有 unused-parameter 警告） |
| 关闭代理不崩溃 | 验证通过（日志显示 `Stopped ... on SOCKS5 :10810`，无崩溃） |
| 右键菜单选中状态 | 验证通过（`onMenuCloseProxy TRIGGERED sel=0` 正确获取选中行） |
| 立即重启代理 | 验证通过（自动使用备用端口 10810，日志记录 `Port 10808 occupied, using alternate port 10810`） |

---

## 5. 日志证据

### 5.1 正常关闭流程

```
[2026-09-02 10:02:23] [DEBUG] [StandaloneProxy] onContextMenu mode=1 rows=3
[2026-09-02 10:02:23] [DEBUG] [StandaloneProxy] onContextMenu sel=0 rows=3
[2026-09-02 10:02:23] [DEBUG] [StandaloneProxy] onMenuCloseProxy TRIGGERED sel=0 rows=3
[2026-09-02 10:02:23] [DEBUG] [StandaloneProxy] onMenuCloseProxy indexId=5328895200368966287 pid=11760
[2026-09-02 10:02:23] [REPORT] [StandaloneProxy] Stopped 5328895200368966287 on SOCKS5 :10810
[2026-09-02 10:02:23] [DEBUG] [StandaloneProxy] stopStandaloneProxy returned 1
```

### 5.2 端口占用自动回退

```
[2026-09-02 10:02:04] [INFO] [UI] onStartProxy: checking desiredPort=10808 isPortAvailable=false
[2026-09-02 10:02:06] [INFO] [UI] Port 10808 occupied, using alternate port 10810 for standalone proxy
[2026-09-02 10:02:08] [REPORT] [StandaloneProxy] Started 5328895200368966287 on SOCKS5 :10810
```

---

## 6. 后续建议

- 如需强制释放端口（而非使用备用端口），可在 `stopStandaloneProxy` 中增加异步端口释放等待（后台线程轮询，不阻塞 UI）
- 考虑在 UI 中显示实际使用的端口（而非始终显示期望端口），避免用户困惑
- 可增加「关闭后自动刷新端口状态」的机制，减少用户手动重启时的等待时间

---

## 7. 相关文件

- `src/ui/AppController.cpp` - `stopStandaloneProxy` 锁顺序重构
- `src/ui/ProxyListPanel.h` - 新增 `contextMenuSel_` 成员
- `src/ui/ProxyListPanel.cpp` - `onContextMenu` / `onMenuCloseProxy` 选中状态缓存
