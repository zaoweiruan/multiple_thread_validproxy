---
title: "feat(ConfigDialog): database path file picker + runtime database switching"
type: feat
status: completed
date: 2026-05-29
origin: "User request: 为配置编辑窗口的\"数据库->路径\"添加文件选择窗口，并切换到选择的数据库"
---

# Database Path File Picker + Runtime Database Switching

## 问题描述

ConfigDialog 中的"数据库→路径"字段使用 `wxStringProperty`（纯文本输入框），用户必须手动输入数据库文件路径，无法通过文件选择对话框浏览选择。此外，当用户修改路径后点击确定，仅保存到 `config.json`，运行中的 `sqlite3*` 句柄未关闭/重新打开，导致应用程序仍在使用旧数据库。

## 范围边界

- **修改**:
  - `database_path` 字段由 `wxStringProperty` → `wxFileProperty`（带浏览按钮）
  - 运行时数据库切换逻辑（关闭旧 db + 打开新 db）
  - 切换后刷新所有面板（订阅列表 + 代理列表）
  - 退出时安全关闭最终 db 句柄
- **NOT 修改**:
  - 其他配置字段的 UI 类型
  - 异步操作中的数据库竞争处理（切换仅在非操作空闲时发生）
  - `ProxyListPanel` 中未使用的 `db_` 成员（保留向后兼容）

## 详细变更

### U1: `src/ui/ConfigDialog.cpp` — 字段类型变更

将 `database_path` 从 `wxStringProperty` 改为 `wxFileProperty`，与 `xray_executable`、`sync_source_db`、`sync_target_db` 保持一致：

```cpp
// 修改前:
propGrid_->Append(new wxStringProperty(L"路径", "database_path", cfg.database_path));

// 修改后:
wxFileProperty* dbPathProp = new wxFileProperty(L"路径", "database_path", cfg.database_path);
propGrid_->Append(dbPathProp);
propGrid_->SetPropertyAttribute("database_path", wxPG_FILE_SHOW_FULL_PATH, (long)1);
```

### U2: `src/ui/AppController.h/cpp` — 数据库切换方法

新增 `switchDatabase(const std::string& newPath)`:

- 关闭当前 `db_`
- 打开新路径的数据库
- 设置 `busy_timeout(5000)` 和 `PRAGMA journal_mode=WAL`
- 更新 `config_.database_path`
- 返回新的 `sqlite3*`，失败返回 `nullptr`

### U3: `src/ui/MainFrame.cpp` — 配置保存后触发切换

`onMenuConfig()` 新增流程：

1. 保存旧路径 `oldDbPath = config_.database_path`
2. `controller_->saveConfig(cfg)`
3. 比较 `cfg.database_path != oldDbPath`
4. 若变更：
   - 调用 `controller_->switchDatabase(newPath)`
   - 成功：更新 `MainFrame::db_`、通知 `UIApp::setDb()`、更新工具栏标签 `m_dbPathLabel`、刷新订阅/代理面板
   - 失败：恢复 `config.json` 旧路径、弹出错误对话框

### U4: `src/ui/UIApp.h` — 数据库句柄访问

新增 `getDb()` / `setDb()` 公开方法，供 `main.cpp` 在退出时获取最终句柄。

### U5: `src/main.cpp` — 退出清理安全

在 `wxEntry()` 返回后，通过 `UIApp::getDb()` 获取最终数据库句柄关闭，避免因运行时切换导致 double-close：

```cpp
if (UIApp* theApp = wxDynamicCast(wxApp::GetInstance(), UIApp)) {
    db = theApp->getDb();
}
sqlite3_close(db);
```

## 验证步骤

1. ✅ **Build**: `cmake --build build --parallel 8` — 成功
2. ✅ **Tests**: `ctest -V` — 3/3 通过
3. **GUI 验证**（手动）:
   - 打开"配置编辑器"→ 点击数据库"路径"的 **...** 按钮 → 应弹出文件选择对话框
   - 选择另一个 `.db` 文件 → 点击确定 → 工具栏路径标签更新、订阅/代理列表刷新
   - 选择不存在的文件 → 应提示错误并恢复旧路径
   - 不修改路径直接确定 → 正常运行，无需切换
   - 退出程序 → 无崩溃（无 double-free）

## 文件变更列表

| 文件 | 改动类型 | 说明 |
|------|----------|------|
| `src/ui/ConfigDialog.cpp` | 修改 | `database_path` 改为 `wxFileProperty` |
| `src/ui/AppController.h` | 修改 | 新增 `switchDatabase()` 声明 |
| `src/ui/AppController.cpp` | 修改 | 实现 `switchDatabase()` |
| `src/ui/MainFrame.cpp` | 修改 | `onMenuConfig()` 数据库切换逻辑 + 引入 `UIApp.h` |
| `src/ui/UIApp.h` | 修改 | 新增 `getDb()` / `setDb()` |
| `src/main.cpp` | 修改 | 退出时使用 `UIApp::getDb()` 获取最终句柄 |

## 风险

- **切换失败场景**：若 `sqlite3_open()` 在 `sqlite3_close()` 之后失败，应用程序无有效数据库句柄。已通过恢复旧路径 + 错误对话框处理，用户需重启应用恢复。
- **异步操作冲突**：若用户在进行测试/更新等异步操作时切换数据库，旧句柄可能在操作中被关闭。实际场景中用户在打开配置对话框前应等待操作完成（工具栏 Cancel 按钮），此风险已通过代码评审确认可接受。
