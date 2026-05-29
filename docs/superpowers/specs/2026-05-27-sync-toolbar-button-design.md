# 同步工具栏按钮 — 设计文档

**创建日期**: 2026-05-27
**状态**: 待实施

## 1. 目标

在工具栏添加一个"同步"按钮，触发数据库同步操作（将配置的 `source_db` 中的有效代理同步到 `target_db`）。

## 2. 改动范围

涉及 4 个源文件 + 1 个 icon 文件拷贝：

| 文件 | 改动 |
|------|------|
| `src/ui/MainFrame.h` | 新增 `onToolSync` 方法声明 |
| `src/ui/MainFrame.cpp` | 新增 `ID_TOOL_SYNC`、事件表条目、工具栏按钮、handler |
| `src/ui/AppController.h` | 新增 `syncDatabasesAsync` / `doSyncDatabases` 声明 |
| `src/ui/AppController.cpp` | 新增 async 实现，遵循现有 worker 线程模式 |
| `bin/icons/tool_synchronize.png` | 从 `docs/design/ui/icon/png/tool_synchronize.png` 拷贝 |

## 3. 详细设计

### 3.1 ID 定义 (MainFrame.cpp enum)

```cpp
ID_TOOL_SYNC = wxID_HIGHEST + 209,
```

### 3.2 事件表

```cpp
EVT_MENU(ID_TOOL_SYNC, MainFrame::onToolSync)
```

### 3.3 工具栏按钮

在现有按钮序列中，放在 `ID_TOOL_FIND` 之前（同步按功能分组靠近"更新"）：

```cpp
tb->AddTool(ID_TOOL_SYNC, wxEmptyString,
    ToolbarIcons::load("tool_synchronize"), "同步");
```

现有按钮顺序：更新 → 测试 → 取消测试 → **同步（新增）** → 查找 → 去重 → 导入 → 配置

### 3.4 AppController async 模式

遵循与 `updateAllSubscriptionsAsync` / `doUpdateAllSubscriptions` 相同的模式：

```cpp
// 公开方法
void AppController::syncDatabasesAsync(wxEvtHandler* wxHandler) {
    if (joinable()) {
        if (isRunning_) {
            // REJECT: 已有操作在进行
            wxQueueEvent(wxHandler, new StatusUpdateEvent("REJECT:另一个操作正在进行中"));
            return;
        }
        workerThread_.join();
    }
    isRunning_ = true;
    workerThread_ = std::thread([this, wxHandler] {
        doSyncDatabases(wxHandler);
    });
}

// 私有 worker
void AppController::doSyncDatabases(wxEvtHandler* wxHandler) {
    StatusUpdateEvent* evt = nullptr;
    bool ok = syncDatabases();  // 使用配置的 source_db / target_db
    if (ok) {
        wxQueueEvent(wxHandler, new StatusUpdateEvent("数据库同步完成"));
    } else {
        wxQueueEvent(wxHandler, new StatusUpdateEvent("ERR:数据库同步失败，请查看日志"));
    }
    isRunning_ = false;
}
```

### 3.5 按钮处理器 (MainFrame)

```cpp
void MainFrame::onToolSync(wxCommandEvent&) {
    setStatusText(0, "同步中…");
    controller_->syncDatabasesAsync(this);
}
```

### 3.6 状态反馈

同步结果通过 `StatusUpdateEvent` 传递，经由 `wxEVT_STATUS_UPDATE` 绑定处理：
- 成功：状态栏显示"数据库同步完成"
- 失败：弹出错误对话框显示错误信息
- REJECT：弹出提示"另一个操作正在进行中"

## 4. 依赖关系

- 需要 `config_.sync.source_db` 和 `config_.sync.target_db` 已配置
- 依赖现有 `AppController::syncDatabases()` 实现（无改动）
- 依赖现有的 worker 线程 re-entry guard（`isRunning_` / `joinable()`）

## 5. 风险与注意事项

- Re-entry 保护：与其他 async 操作共享同一个 `isRunning_` 标志，同步进行时不允许启动其他操作
- Icon 拷贝：需要手动将 PNG 从设计目录拷贝到 `bin/icons/`
- 同步是本地 SQLite 操作（非网络 I/O），但涉及批量插入，可能耗时数百毫秒到数秒
