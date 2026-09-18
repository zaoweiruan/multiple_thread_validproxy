# ProxyProcessMonitor 设计文档

> **版本**: v1.1  
> **日期**: 2026-08-19  
> **状态**: 待评审  
> **变更**: v1.1 修正状态栏布局（对齐实际代码），新增存活进程数量显示

---

## 1. 概述

### 1.1 背景
当前系统在启动时一次性扫描并接管残留的 xray/sing-box standalone 代理进程（`adoptDanglingStandaloneProxies()`），但运行期间不会主动监控：
- 新出现的悬挂进程无法被自动接管
- 已接管的代理进程异常退出时无法及时感知

### 1.2 目标
新增"监控代理进程"功能，提供可配置的周期性监控：
1. 定期扫描并接管新出现的悬挂 standalone 代理进程
2. 监控已接管代理进程的存活状态
3. 在状态栏实时显示监控状态 + 存活进程数量
4. 支持运行时启停和配置变更

### 1.3 非目标
- 不改变现有 `NetworkMonitor` 的行为
- 不改变代理进程的启动/停止逻辑
- 不新增代理进程的自动重启功能

---

## 2. 配置设计

### 2.1 配置结构（AppConfig）

```cpp
// 在 include/ConfigReader.h 的 AppConfig 中新增
struct {
    bool enabled{false};           // 启用代理进程监控
    int checkIntervalMs{30000};    // 检测间隔（毫秒），范围 5000-300000
} proxy_process_monitor;
```

### 2.2 配置文件格式（config.json）

```json
{
  "proxy_process_monitor": {
    "enabled": false,
    "check_interval_ms": 30000
  }
}
```

### 2.3 默认值说明
| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `enabled` | `false` | 默认不启用，避免对不需要监控的用户产生开销 |
| `check_interval_ms` | `30000` | 30秒检测一次，平衡实时性与资源消耗 |

---

## 3. 状态栏设计

### 3.1 现有布局（v1.0 代码）

状态栏当前 4 字段：`CreateStatusBar(4)`，宽度 `{250, 250, 120, -1}`

| 字段 0 (250px) | 字段 1 (250px) | 字段 2 (120px) | 字段 3 (-1) |
|----------------|----------------|----------------|-------------|
| 操作状态 | 日志文件名 | 网络状态 (netMonPanel_) | 数据库路径 |

### 3.2 新布局（v1.1 扩展为 5 字段）

状态栏扩展为 5 字段：`CreateStatusBar(5)`，宽度 `{200, 200, 100, 110, -1}`

| 字段 0 (200px) | 字段 1 (200px) | 字段 2 (100px) | 字段 3 (110px) | 字段 4 (-1) |
|----------------|----------------|----------------|----------------|-------------|
| 操作状态 | 日志文件名 | 网络状态 (netMonPanel_) | 代理监控状态+存活数 (proxyMonPanel_) | 数据库路径 |

### 3.3 字段 3：代理监控状态 + 存活进程数

使用自定义绘制面板 `proxyMonPanel_`，显示红绿灯圆点 + 存活进程数量文字。

| 状态 | 图标 | 文字示例 | 圆点颜色 |
|------|------|----------|----------|
| 监控运行中 + N 个存活进程 | ● | "● 3" | 绿色 (0, 180, 0) |
| 监控运行中 + 0 个存活进程 | ● | "● 0" | 绿色 (0, 180, 0) |
| 监控已停止 | ● | "● 0" | 红色 (200, 0, 0) |
| 未初始化 | ○ | "○ 0" | 灰色 (128, 128, 128) |

- 存活数量来源于 `AppController::getRunningStandaloneCount()`（返回 `standaloneProxies_` 中 `running == true` 的条目数）
- 每次定时器回调刷新该计数

### 3.4 视觉实现
- 复用 `netMonPanel_` 的绘制模式（wxBG_STYLE_PAINT + 自定义绘制）
- 状态栏字段宽度：110px（足够显示 "● 99" 格式文字）
- 绘制逻辑：圆点 + 空格 + 数字（如 `● 3`）

---

## 4. 实现机制

### 4.1 定时器架构

```
MainFrame
├── netMonTimer_ (现有) → onNetMonTimer() → 刷新网络状态面板
├── proxyMonTimer_ (新增) → onProxyMonTimer() → 执行代理进程监控
└── historyTimer_ (现有) → 刷新代理列表 Runtime 列
```

### 4.2 定时器回调逻辑

```cpp
void MainFrame::onProxyMonTimer(wxTimerEvent&) {
    if (!controller_) return;
    
    // 1. 扫描并 adopt 悬挂的 standalone 代理进程（后台线程避免阻塞 UI）
    std::thread([this]() {
        controller_->adoptDanglingStandaloneProxies();
    }).detach();
    
    // 2. 获取当前存活进程数量并更新状态栏
    int aliveCount = controller_->getRunningStandaloneCount();
    updateProxyMonStatus(true, aliveCount);
}
```

### 4.3 状态栏面板

```cpp
// 新增成员变量（MainFrame.h）
wxTimer* proxyMonTimer_{nullptr};
wxPanel* proxyMonPanel_{nullptr};
bool proxyMonEnabled_{false};   // 监控是否启用（来自配置）
int proxyAliveCount_{0};        // 当前存活进程数量

// 面板创建（在 startMonitoring 中）
proxyMonPanel_ = new wxPanel(statusBar_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
proxyMonPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
proxyMonPanel_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
    wxPaintDC dc(proxyMonPanel_);
    wxSize sz = proxyMonPanel_->GetClientSize();
    if (sz.x < 4 || sz.y < 4) return;
    
    // 背景
    wxColour face = wxSystemSettings::GetColour(wxSYS_COLOUR_MENUBAR);
    dc.SetBrush(wxBrush(face));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, sz.x, sz.y);
    
    // 边框
    wxColour shadow = wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW);
    wxColour highlight = wxSystemSettings::GetColour(wxSYS_COLOUR_3DHIGHLIGHT);
    dc.SetPen(wxPen(shadow));
    dc.DrawLine(0, 0, sz.x - 1, 0);
    dc.DrawLine(0, 0, 0, sz.y - 1);
    dc.SetPen(wxPen(highlight));
    dc.DrawLine(0, sz.y - 1, sz.x - 1, sz.y - 1);
    dc.DrawLine(sz.x - 1, 0, sz.x - 1, sz.y - 1);
    
    // 文字：存活进程数
    wxString label = wxString::Format("%d", proxyAliveCount_);
    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    wxSize textExt = dc.GetTextExtent(label);
    int textH = textExt.y;
    int r = (textH - 2) / 2;
    if (r < 2) r = 2;
    int cx = r + 3;
    int cy = sz.y / 2;
    
    // 圆点颜色
    wxColour dotColor;
    if (!proxyMonEnabled_) {
        dotColor = wxColour(128, 128, 128);  // 灰色：未启用
    } else {
        dotColor = wxColour(0, 180, 0);      // 绿色：监控运行中
    }
    dc.SetBrush(wxBrush(dotColor));
    dc.SetPen(wxPen(dotColor));
    dc.DrawCircle(cx, cy, r);
    
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
    dc.DrawText(label, cx + r + 4, cy - textH / 2);
});

// 状态更新
void MainFrame::updateProxyMonStatus(bool enabled, int aliveCount) {
    if (!proxyMonPanel_) return;
    proxyMonEnabled_ = enabled;
    proxyAliveCount_ = aliveCount;
    proxyMonPanel_->Refresh();
}
```

### 4.4 位置调整

```cpp
// 新增 repositionProxyMonPanel（与 repositionNetMonPanel 类似）
void MainFrame::repositionProxyMonPanel() {
    if (!statusBar_ || !proxyMonPanel_) return;
    wxRect fieldRect;
    statusBar_->GetFieldRect(3, fieldRect);  // 字段 3
    proxyMonPanel_->SetSize(fieldRect);
    proxyMonPanel_->Refresh();
}

// 在 statusBar_ 的 EVT_SIZE 绑定中追加
statusBar_->Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
    evt.Skip();
    repositionNetMonPanel();      // 现有
    repositionProxyMonPanel();    // 新增
});
```

### 4.5 启停控制

```cpp
// MainFrame 中新增方法
void MainFrame::startProxyMonitor(int intervalMs) {
    if (proxyMonTimer_) {
        proxyMonTimer_->Stop();
        delete proxyMonTimer_;
    }
    proxyMonTimer_ = new wxTimer(this);
    Bind(wxEVT_TIMER, &MainFrame::onProxyMonTimer, this);
    proxyMonTimer_->Start(intervalMs);
    updateProxyMonStatus(true, 0);  // 初始 0，下次回调更新
}

void MainFrame::stopProxyMonitor() {
    if (proxyMonTimer_) {
        proxyMonTimer_->Stop();
        delete proxyMonTimer_;
        proxyMonTimer_ = nullptr;
    }
    updateProxyMonStatus(false, 0);
}
```

### 4.6 配置变更处理

在 `MainFrame::onMenuConfig()` 中，保存配置后检测变更：

```cpp
// 检测 proxy_process_monitor 配置变更
bool proxyMonEnabledChanged = (cfg.proxy_process_monitor.enabled != oldProxyMonEnabled);
bool proxyMonIntervalChanged = (cfg.proxy_process_monitor.checkIntervalMs != oldProxyMonInterval);

if (proxyMonEnabledChanged || proxyMonIntervalChanged) {
    if (cfg.proxy_process_monitor.enabled) {
        startProxyMonitor(cfg.proxy_process_monitor.checkIntervalMs);
    } else {
        stopProxyMonitor();
    }
}
```

### 4.7 启动时初始化

在 `onFirstShow()` 的 `startMonitoring()` 中，根据配置决定是否启动：

```cpp
void MainFrame::startMonitoring() {
    // ... 现有代码 ...
    
    // 代理进程监控（根据配置决定是否启动）
    if (controller_ && controller_->getConfig().proxy_process_monitor.enabled) {
        startProxyMonitor(controller_->getConfig().proxy_process_monitor.checkIntervalMs);
    } else {
        updateProxyMonStatus(false, 0);  // 显示 "○ 0"
    }
}
```

---

## 5. 配置对话框

### 5.1 新增分类

在 ConfigDialog 中新增"监控代理进程"分类：

```cpp
// 新增配置分类
propGrid_->Append(new wxPropertyCategory(L"监控代理进程"));
propGrid_->Append(new wxBoolProperty(L"启用", "proxy_process_monitor_enabled", 
                   cfg.proxy_process_monitor.enabled));
propGrid_->Append(new wxIntProperty(L"检测间隔(毫秒)", "proxy_process_monitor_check_interval_ms", 
                   cfg.proxy_process_monitor.checkIntervalMs));
propGrid_->SetPropertyAttribute("proxy_process_monitor_check_interval_ms", 
                   wxPG_ATTR_MIN, (long)5000);
propGrid_->SetPropertyAttribute("proxy_process_monitor_check_interval_ms", 
                   wxPG_ATTR_MAX, (long)300000);
```

### 5.2 属性映射

在 `ConfigDialog::saveConfig()` 中添加映射：

```cpp
// proxy_process_monitor
editedConfig_.proxy_process_monitor.enabled = 
    propGrid_->GetValueAsString("proxy_process_monitor_enabled") == "true";
editedConfig_.proxy_process_monitor.checkIntervalMs = 
    propGrid_->GetValueAsLong("proxy_process_monitor_check_interval_ms");
```

---

## 6. 文件修改清单

| 文件 | 修改类型 | 修改内容 |
|------|----------|----------|
| `include/ConfigReader.h` | 修改 | 新增 `proxy_process_monitor` 结构体 |
| `src/ConfigReader.cpp` | 修改 | 解析/保存新配置项 |
| `src/ui/ConfigDialog.cpp` | 修改 | 新增配置 UI 分类和属性 |
| `src/ui/AppController.h` | 修改 | 新增 `getRunningStandaloneCount()` 方法声明 |
| `src/ui/AppController.cpp` | 修改 | 实现 `getRunningStandaloneCount()` |
| `src/ui/MainFrame.h` | 修改 | 新增 `proxyMonTimer_`、`proxyMonPanel_`、`proxyMonEnabled_`、`proxyAliveCount_` 成员；新增 `startProxyMonitor()`、`stopProxyMonitor()`、`onProxyMonTimer()`、`updateProxyMonStatus()`、`repositionProxyMonPanel()` 方法声明 |
| `src/ui/MainFrame.cpp` | 修改 | 状态栏 4→5 字段、实现定时器逻辑、代理监控面板绘制与定位、配置变更处理、启动初始化、EVT_SIZE 追加 reposition |

---

## 7. 测试计划

### 7.1 功能测试
1. **配置持久化**：修改配置后重启，确认配置生效
2. **启停控制**：在配置对话框中启用/禁用，确认定时器启停
3. **间隔调整**：修改检测间隔，确认新间隔生效
4. **状态栏显示**：确认红绿灯状态正确反映监控状态
5. **存活进程计数**：启动 standalone 代理后，确认计数实时更新

### 7.2 集成测试
1. **悬挂进程 adopt**：启动外部 xray standalone 进程，确认被自动 adopt 且计数 +1
2. **配置变更热更新**：运行时修改配置，确认无需重启即可生效
3. **进程退出计数**：手动终止 adopted 进程，确认计数 -1

### 7.3 边界测试
1. **最小间隔**：设置 5000ms，确认不会过于频繁
2. **最大间隔**：设置 300000ms，确认不会过于稀疏
3. **快速切换**：快速启用/禁用多次，确认无内存泄漏

---

## 8. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 定时器回调中创建 detached thread 可能导致资源泄漏 | 低 | adoptDanglingStandaloneProxies 本身是轻量操作，且有去重逻辑 |
| 配置变更时定时器启停时序问题 | 中 | 先停止旧定时器，再启动新定时器，确保原子性 |
| 状态栏字段宽度不足导致文字截断 | 低 | 使用固定宽度 110px，足够显示 "● 99" |
| 状态栏从 4 扩展到 5 字段影响其他字段 | 低 | 均匀缩减各固定宽度字段，-1 可变宽度字段自动适配 |

---

## 9. 附录

### 9.1 相关代码参考
- `NetworkMonitor` 实现：`include/NetworkMonitor.h`、`src/NetworkMonitor.cpp`
- `ProcessExitListener` 实现：`include/ProcessExitListener.h`、`src/ProcessExitListener.cpp`
- `standaloneProxies_` 数据结构：`src/ui/AppController.h` (L179-180)
- `getRunningStandaloneIds()`：`src/ui/AppController.cpp` (L1046) — 遍历 `standaloneProxies_` 返回 `running == true` 的 ID 列表
- 状态栏绘制参考：`MainFrame.cpp` 中 `netMonPanel_` 的绘制逻辑 (L364-396)
- 状态栏字段宽度：`MainFrame.cpp` `initStatusBar()` (L644) `{250, 250, 120, -1}`

### 9.2 配置文件示例
```json
{
  "proxy_process_monitor": {
    "enabled": true,
    "check_interval_ms": 30000
  }
}
```
