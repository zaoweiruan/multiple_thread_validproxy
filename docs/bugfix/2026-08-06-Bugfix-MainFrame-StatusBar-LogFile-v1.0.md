# Bugfix: MainFrame 状态栏日志文件名不可见

- **日期**: 2026-08-06
- **模块**: `src/ui/MainFrame.cpp` / `src/ui/MainFrame.h`
- **版本**: v1.0
- **状态**: 已修复并验证

## 1. 问题现象

将日志文件名从 LogPanel 工具栏移到 MainFrame 状态栏 Field1 后，用户运行 GUI，
状态栏中**始终看不到日志文件名**（Field1 显示为空）。

## 2. 根因分析

`netMonPanel_`（网络状态指示面板）是状态栏的子窗口，创建于状态栏之上，使用
`wxBG_STYLE_PAINT` 自绘 "Network OK" / "Disconnected" 文字，**不透明**。

`MainFrame::repositionNetMonPanel()` 原实现：

```cpp
void MainFrame::repositionNetMonPanel() {
    if (!statusBar_ || !netMonPanel_) return;
    wxRect fieldRect;
    statusBar_->GetFieldRect(1, fieldRect);   // ← 取 Field1 区域
    netMonPanel_->SetSize(fieldRect);          // ← 面板铺满 Field1
    netMonPanel_->Refresh();
}
```

`setLogFileLabel()` → `SetStatusText(1, basename)` 本身设置成功，但**不透明的
netMonPanel_ 覆盖在 Field1 上**，日志文件名被完全遮挡，视觉上"没有显示"。

### 诊断过程要点

1. 运行 GUI 后用 UI Automation（COM，跨进程安全）读取状态栏子元素：
   - 状态栏 class=`msctls_statusbar32`，UIA 类型为 Pane（非 StatusBar）
   - 子元素含 `name='panel'`（netMonPanel_）、`name='Ready'`（Field0）、
     `name=''`（Field1 被遮挡区域）等
2. 由此确认文本实际已写入控件，只是被子窗口面板遮挡。

### 重要教训：跨进程 SendMessage 是崩溃源

- 对本机其他进程的窗口句柄发送 `SB_GETPARTS` / `SB_GETTEXT` /
  `SB_GETTEXTLENGTH`（`0x0406`/`0x040C`/`0x040D`）且携带指针型 lParam，
  会导致 GUI 进程按 PowerShell 进程的指针访问内存 → COMCTL32 访问违规 →
  `0xC0000005` / `0xC000041D` 崩溃（WER 记录错误模块 COMCTL32.dll 偏移 0x292c）。
- 早期"part 宽度为 0"（SB_GETPARTS rightEdges=[0,0,0]）的结论即由此类
  跨进程读取得到**不可靠数据**，应废弃。
- **验证 GUI 控件内容必须使用 UIA（COM 跨进程安全），禁止跨进程
  SendMessage 携带指针参数。**

## 3. 修复方案

状态栏由 3 字段扩展为 4 字段，将网络面板从 Field1 移开，字段规划：

| 字段 | 内容 | 写入位置 |
| :--- | :--- | :--- |
| Field0 | 状态消息（Ready / 进度等） | `setStatusText(0, ...)` |
| Field1 | **日志文件名** | `setLogFileLabel()` |
| Field2 | 网络状态（netMonPanel_ 绘制） | `repositionNetMonPanel()` |
| Field3 | 数据库路径 | `initStatusBar()` / `onMenuConfig` |

### 代码修改清单（MainFrame.cpp）

1. `initStatusBar()`: `CreateStatusBar(3)` → `CreateStatusBar(4)`；
   新增 `SetStatusWidths(4, {250,250,120,-1})`（末字段 -1 = 可变宽度，
   Field3 自动拉伸铺满状态栏剩余宽度）；数据库路径
   `SetStatusText(getDbPath(), 2)` → `(..., 3)`。
2. `repositionNetMonPanel()`: `GetFieldRect(1)` → `GetFieldRect(2)`。
3. `onMenuConfig`: `SetStatusText(cfg.database_path, 2)` → `(..., 3)`。
4. 构造函数 SetMenuBar / AUI Update 之后新增
   `statusBar_->SendSizeEvent()` 强制状态栏重算 part 宽度。
5. 事件表删除 `EVT_LEFT_DCLICK(MainFrame::onStatusBarDClick)`，改为构造函数中
   `statusBar_->Bind(wxEVT_LEFT_DCLICK, &MainFrame::onStatusBarDClick, this);`
   （双击事件限定在状态栏自身，避免全局误触发）。

## 4. 验证

- 重新构建：`cmake --build build --parallel 8` 成功。
- 运行 GUI（PID 12108）后 UIA 读取状态栏子元素：
  - Field0 = `Ready`
  - **Field1 = `ui_20260806_171358.log`（日志文件名正常显示）**
  - Field3 = `E:\eclipse_workspace\multiple_thread_validproxy\bin\...`（数据库路径）
- GUI 稳定运行，无 0xC000041D 崩溃。

## 5. 补充修复：状态栏未铺满底部行（2026-08-06 追加）

### 问题现象

修复日志文件名显示后，状态栏右侧出现一段空白，视觉上"状态栏没有铺满底部行"。

### 根因分析

`SetStatusWidths(4, {250,250,120,300})` 固定宽度总和 = 920 逻辑 px，小于状态栏
逻辑宽度 1280 px（150% DPI 下物理 1920 px）。wxStatusBar 的固定宽度字段不会
自动拉伸，导致 SB_SETPARTS 的最后右边界停在 920 处，右侧约 360 逻辑 px 为
无字段空白背景。

### 修复方案

末字段改为 -1（wxStatusBar 可变宽度字段，占用剩余空间）：

```cpp
int widths[] = { 250, 250, 120, -1 };
statusBar_->SetStatusWidths(4, widths);
```

Field3（数据库路径）自动拉伸至状态栏右缘，铺满整行。

### 验证（UIA，PID 1984，逻辑坐标）

- Field3 数据库路径 rect = (652,1045)-(1280,1068)，**右缘 = 1280 = 状态栏逻辑右缘** ✓
- grip 尺寸手柄 (1896,1056)-(1920,1080)；状态栏控件 (0,1043)-(1920,1081)
  物理宽 1920 = 逻辑 1280，精确铺满
- 修复前 Field3 停在 920 逻辑 px 处，右侧空白约 360 逻辑 px

## 6. 相关文件

- `src/ui/MainFrame.cpp`：上述 5 处修改
- `src/ui/MainFrame.h`：`setLogFileLabel` / `onStatusBarDClick` / `logFilePath_`
  （此前会话已添加）
- `src/ui/LogPanel.cpp` / `LogPanel.h`：移除 logFileLabel_ / logFileBtn_ 等
  （此前会话已移除）
