# Spec: LogPanel 工具栏新增「打开日志」按钮并移除状态栏双击打开

- 日期: 2026-08-12
- 类型: Spec
- 模块: LogPanel / MainFrame
- 版本: v1.0
- 状态: ✅ completed

---

## 1. 需求

在 LogPanel 工具栏的 Clear 按钮后新增「打开日志」按钮，一键以系统默认程序打开当前
日志文件；同时**移除整条状态栏的双击打开日志绑定**，消除误触发缺陷。状态栏 Field1
的日志文件名显示**保留**。

用户决策（原话）：

> "方案 A 2. 更简方案：只加按钮，移除整条状态栏的双击绑定。按钮文本'打开日志'"

---

## 2. 现状分析（修改前）

### 2.1 日志打开入口（唯一）

`MainFrame::onStatusBarDClick` 绑定整条状态栏 `wxEVT_LEFT_DCLICK`
（构造函数内 `statusBar_->Bind(...)`，非事件表），双击后经
`ShellExecuteA` / `wxLaunchDefaultApplication` 打开 `logFilePath_`。

### 2.2 现状问题

| 问题 | 说明 |
|------|------|
| 可发现性差 | 打开日志的唯一入口是"双击状态栏"，无任何文字/按钮提示，用户无法察觉 |
| 误触发缺陷 | 双击未限定 field1，**双击 field2（网络状态）/ field3（DB 路径）同样打开日志**，与用户意图不符 |
| 语义割裂 | 日志文件的"清空"在 LogPanel 工具栏，而"打开"在状态栏，功能入口不聚合 |

---

## 3. 方案对比（评估结论）

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| A. LogPanel 工具栏加按钮 | 可发现性强；与 Clear 语义闭环；实现成本低 | 无 | **采纳** |
| B. 保留双击（限定 field1 再触发） | 保留快捷路径 | 可发现性不变；需维护 statusBar Bind + field 坐标判断，复杂度高 | 弃 |
| C. 保留双击 + 加按钮 | 两者兼得 | 双入口维护成本；双击误触发虽限定仍存在 | 弃（用户选更简方案） |

---

## 4. 设计实现

### 4.1 LogPanel 工具栏

`src/ui/LogPanel.h`：

- 事件 ID 枚举改为显式：`{ ID_LOG_CLEAR = wxID_HIGHEST + 200, ID_LOG_FILTER, ID_LOG_OPEN }`。
- 事件表由 `EVT_BUTTON(wxID_ANY, ...)` 改为具体 ID
  （`EVT_BUTTON(ID_LOG_CLEAR, ...)` / `EVT_BUTTON(ID_LOG_OPEN, ...)` /
  `EVT_CHOICE(ID_LOG_FILTER, ...)`）——**消除新增第二个按钮后 Clear 被重复触发的陷阱**。
- 新增成员 `wxButton* openBtn_;` 与处理器 `void onOpenLog(wxCommandEvent& event);`。

`src/ui/LogPanel.cpp`：

```cpp
openBtn_ = new wxButton(this, ID_LOG_OPEN, "打开日志");
toolSizer->Add(openBtn_, 0, wxLEFT, 8);

void LogPanel::onOpenLog(wxCommandEvent&) {
    std::string logPath = Logger::getFilePath();
    if (logPath.empty()) { return; }
#ifdef __WXMSW__
    ShellExecuteA(NULL, "open", logPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    wxLaunchDefaultApplication(logPath);
#endif
}
```

日志路径经 `Logger::getFilePath()` 实时获取（与 MainFrame 旧 `logFilePath_`
同源），不新增缓存成员。

### 4.2 MainFrame 双击绑定移除

`src/ui/MainFrame.h`：删除 `void onStatusBarDClick(wxMouseEvent& event);` 声明
与 `std::string logFilePath_;` 成员（仅被 setLogFileLabel 与 onStatusBarDClick
使用，双击移除后为死成员）。

`src/ui/MainFrame.cpp`：

- 构造函数删除 `statusBar_->Bind(wxEVT_LEFT_DCLICK, ...)` 绑定块。
- `setLogFileLabel` 保留 basename → field1 显示，删除 `logFilePath_ = filePath;`。
- 文件末尾 `onStatusBarDClick` 实现整体删除。

### 4.3 变更点

| 文件 | 变更 |
|------|------|
| `src/ui/LogPanel.h` | 事件 ID 枚举 + `onOpenLog` 声明 + `openBtn_` 成员 |
| `src/ui/LogPanel.cpp` | 事件表改具体 ID；工具栏加按钮；`onOpenLog` 实现；`#include <shellapi.h>` |
| `src/ui/MainFrame.h` | 删除 `onStatusBarDClick` 声明与 `logFilePath_` 成员 |
| `src/ui/MainFrame.cpp` | 删除 statusBar 双击 Bind；`setLogFileLabel` 去掉成员存储；删除 `onStatusBarDClick` 实现 |

---

## 5. 验证

- `cmake --build build --parallel 8` → **341/341 编译链接成功**（含
  `bin\validproxy.exe` 与全部测试 exe），仅剩既有 Utils.cpp
  PROCESSENTRY32W missing-field-initializers 噪音警告（项目允许）。
- grep `onStatusBarDClick|logFilePath_|EVT_LEFT_DCLICK` 于 `src/ui` →
  **零残留**。
- 手工验证路径（GUI 运行）：LogPanel 工具栏可见「打开日志」按钮，点击以默认
  编辑器打开当前日志文件；双击状态栏任意字段不再触发打开。

---

## 6. 范围与约束

- 状态栏 Field1 日志文件名显示**保留不变**（2026-08-06
  Bugfix-MainFrame-StatusBar-LogFile 的显示语义不受影响，仅移除双击行为）。
- 历史文档 `docs/bugfix/2026-08-06-Bugfix-MainFrame-StatusBar-LogFile-v1.0.md`
  记载的"双击 Bind"已在本版移除，属预期演进。
- 全栈禁止 `auto`，显式类型。
