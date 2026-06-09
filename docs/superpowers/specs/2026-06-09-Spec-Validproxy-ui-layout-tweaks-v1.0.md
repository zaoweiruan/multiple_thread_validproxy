---
title: "feat: UI 布局调整 — dbpath-panel 去除 / searchbox 右移 / proxydetail 默认隐藏"
type: feat
status: implemented
date: 2026-06-09
---

# UI 布局调整 — dbpath-panel 去除 / searchbox 右移 / proxydetail 默认隐藏

## 问题描述

MainFrame 工具栏和 AUI 布局存在以下三处需优化项：

### 1. dbpath-panel 冗余

工具栏中存在 `m_dbPathLabel` 组件（`wxStaticText`），用于显示数据库路径。该信息已同时在状态栏第 2 字段显示（`MainFrame::initStatusBar()` → `statusBar_->SetStatusText(wxString(getDbPath()), 2)`），造成信息冗余，且占用工具栏空间。需去除此组件。

> **范围限定：** 仅去除工具栏中的 wxStaticText 组件。状态栏的 dbpath 显示（statusBar field 2）保持不动。

### 2. Searchbox 位置偏移

搜索框在工具栏上的位置偏左，与 UI 整体布局不协调。需要将其整体右移 50px 以获得更好的视觉平衡。

### 3. ProxyDetail 面板启动默认可见

ProxyDetail 面板在启动时默认显示，但多数用户启动后首先操作订阅/代理列表，不需要立即查看详情。默认显示该面板占用屏幕空间，降低代理列表的可视区域。

**对比：** 原有 `detailPaneVisible_` 初始值为 `true` + 无 `.Hide()` 调用 → 启动时 detailPane 始终可见。修改后默认隐藏，用户按需通过工具栏"详情"按钮或菜单打开。

## 范围边界

- **修改文件:**
  - `src/ui/MainFrame.h` — 删除 `m_dbPathLabel` 成员，`detailPaneVisible_` 默认值 `true` → `false`
  - `src/ui/MainFrame.cpp` — 三处布局调整的具体代码实现
- **不修改:**
  - `getDbPath()` 方法（状态栏仍使用）
  - ProxyDetailPanel 类本身
  - 工具栏"详情"按钮逻辑
  - 关闭按钮 → AUI pane 关闭事件 → `detailPaneVisible_ = false` 逻辑

## 设计决策

### 决策 1：dbpath-panel 去除范围

| 方案 | 优点 | 缺点 |
|------|------|------|
| **选中：仅去除工具栏 wxStaticText** | 状态栏路径显示保留；改动最小 | 无 |
| 同时去除状态栏路径 | 彻底去重 | 用户失去 dbpath 的可见性提示 |
| 替换为图标按钮 | 节省空间 | 违背简单性原则 |

**结论：** 仅去除工具栏中的 `m_dbPathLabel`，`getDbPath()` 和状态栏 field 2 保留。数据库路径切换时仅更新状态栏文本，不涉及已删除的 label。

### 决策 2：Searchbox 偏移量

| 方案 | 效果 |
|------|------|
| **选中：AddSpacer(20) → AddSpacer(70)（+50px）** | 搜索框整体右移 50px，视觉上更居中 |
| AddSpacer(20) → AddSpacer(120)（+100px） | 偏移过大，挤压右侧 toggle 按钮空间 |
| 改用 AddStretchSpacer | 搜索框会随窗口缩放持续右移，不可控 |

**结论：** 固定 50px 偏移在保持搜索框位置可控的同时改善视觉平衡。

### 决策 3：ProxyDetail 默认隐藏方式

| 方案 | 优点 | 缺点 |
|------|------|------|
| **选中：`.Hide()` + `detailPaneVisible_ = false`** | 双保险确保隐藏；与现有 AUI 机制兼容 | 需在构造时确保两者一致 |
| 仅 `detailPaneVisible_ = false` | 一处修改 | `wxAuiManager` 可能根据上次状态显示面板 |
| 仅 `.Hide()` | AUI 层面隐藏 | `detailPaneVisible_` 与 AUI 真实状态可能不一致 |

**结论：** 两者同时修改确保启动时 detailPane 始终隐藏。`onClose` 事件处理（line 238-244）仍负责在用户手动关闭时同步 `detailPaneVisible_` 状态，不受此改动影响。

## 详细变更

### C1: 去除 dbpath-panel

**位置:** `src/ui/MainFrame.h` + `src/ui/MainFrame.cpp`

**MainFrame.h 变更：**
```cpp
// 删除以下成员（不存在于当前头文件中归因于已删除）
// wxStaticText* m_dbPathLabel{nullptr};
```

**MainFrame.cpp 变更：**
```cpp
// initToolBar() — 删除以下代码段（原有的 m_dbPathLabel 创建 + 工具栏添加）
// m_dbPathLabel = new wxStaticText(m_toolbar, wxID_ANY, ...);
// m_toolbar->AddControl(m_dbPathLabel);

// onMenuConfig() — 删除 config 变更后的 text 更新
// if (m_dbPathLabel) {
//     m_dbPathLabel->SetLabelText(...);
// }
```

`getDbPath()` 保留（用于 statusBar field 2 显示）。dbpath 切换时（`onMenuConfig` 中 `cfg.database_path != oldDbPath` 分支）仅更新 statusBar，无需操作已删除的 label。

### C2: Searchbox 右移 50px

**位置:** `src/ui/MainFrame.cpp:393`

```cpp
// 修改前
m_toolbar->AddSpacer(20);  // small gap after tools

// 修改后
m_toolbar->AddSpacer(70);  // small gap after tools, then search
```

增加 50px spacer，搜索框整体右移。右侧的 stretch spacer 和 toggle 按钮位置保持不变。

### C3: ProxyDetail 默认隐藏

**位置:** `src/ui/MainFrame.h:106` + `src/ui/MainFrame.cpp:464-476`

**MainFrame.h：**
```cpp
// 修改前
bool detailPaneVisible_{true};

// 修改后
bool detailPaneVisible_{false};
```

**MainFrame.cpp（initPanels 中的 detailPane AUI 注册）：**
```cpp
// 修改前
auiManager_->AddPane(detailPanel_, wxAuiPaneInfo()
    .Name("detailPane")
    .Caption("Proxy Details")
    .Right()
    .Layer(0).Position(0)
    .BestSize(320, -1)
    .MinSize(250, 400)
    .CloseButton(true)
    .PinButton(true)
    .Resizable(true)
    .Floatable(true)
    // 无 .Hide()
);

// 修改后
auiManager_->AddPane(detailPanel_, wxAuiPaneInfo()
    .Name("detailPane")
    .Caption("Proxy Details")
    .Right()
    .Layer(0).Position(0)
    .BestSize(320, -1)
    .MinSize(250, 400)
    .CloseButton(true)
    .PinButton(true)
    .Resizable(true)
    .Floatable(true)
    .Hide()   // <-- 新增：启动时隐藏
);
```

**关键设计点：**
- `.Hide()` 和 `detailPaneVisible_ = false` 确保启动时一致隐藏
- `onToggleDetailPane`（工具栏"详情"按钮）的 Show/Hide 切换逻辑不变
- AUI pane 关闭事件绑定（`wxEVT_AUI_PANE_CLOSE` → `detailPaneVisible_ = false`）不受影响
- 用户启动后按需点击"详情"按钮即可显示面板

## 兼容性分析

| 场景 | 修改前行为 | 修改后行为 | 影响 |
|------|------------|------------|------|
| 工具栏 dbpath 显示 | 显示完整路径文本 | 不显示 | 信息已移至状态栏，无功能影响 |
| 状态栏 dbpath 显示 | field 2 显示路径 | 同左 | 无变化 |
| Searchbox 位置 | 距工具栏左侧 20px | 距工具栏左侧 70px | 视觉上更居中 |
| 启动时 detailPane | 默认可见 | 默认隐藏 | 用户需手动打开 |
| 工具栏"详情"按钮 | 点击 toggle detailPane | 同左 | 无变化 |
| 手动关闭 detailPane | detailPaneVisible_ = false | 同左 | 无变化 |
| 数据库路径切换 | 更新 toolbar label + statusBar | 仅更新 statusBar | 信息不丢失 |

## 测试覆盖

| 测试 | 验证点 |
|------|--------|
| 构建验证（3/3 targets） | MainFrame 编译无未定义引用错误 |
| 全量测试（7/7 suites） | 所有单元测试通过，无回归 |

- **构建：** `cmake --build build --parallel 8` — 3 个目标全部通过
- **测试：** `ctest -V` — 7/7 测试套件全部通过
- **UI 验证：** 手动启动 GUI 确认 toolbar 无 dbpath label、searchbox 位置、detailPane 启动隐藏
