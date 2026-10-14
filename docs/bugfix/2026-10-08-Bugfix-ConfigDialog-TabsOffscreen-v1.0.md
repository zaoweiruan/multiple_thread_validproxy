---
title: "docs: bugfix — 配置对话框无法切换 Tab（Tab 栏被顶出屏幕）"
type: bugfix
status: completed
date: 2026-10-08
updated: 2026-10-08
version: v1.0
---

# Bugfix：配置对话框无法切换 Tab

## 1. 现象

打开配置对话框后看不到 notebook 的 Tab 栏（「应用配置」/「模板配置」），画面只有属性网格内容铺满，表现为**无法切换 Tab**。

## 2. 根因（三重叠加）

### 2.1 `SetSizeHints` 把内容理想尺寸写成硬下限

```cpp
topSizer->Fit(this);
topSizer->SetSizeHints(this);   // ← 元凶
...
SetSize(wxSize(1440, dialogH));  // wx 拒绝：不能缩到 size hint 之下
```

「应用配置」Tab 的 `wxPropertyGrid` 有几十个属性行，`SetSizeHints` 把这份「内容理想尺寸」写进对话框的 size hint，实测高度达 **1170**。随后 `SetSize(1440, 918)` 被 wx 静默拒绝，对话框被撑到 **1950×1170**。

### 2.2 `CentreOnScreen()` 把超屏对话框的顶部顶到可视区外

1950×1170 放到 1920×1080 屏幕上居中 → 实测 `rect = L=-30 T=-90 R=1920 B=1080`。**顶部 90px 在屏幕外**，notebook 的 Tab 栏正好位于顶部 → 完全不可见 → 用户看到的就是「无法切换 Tab」。

### 2.3 `SetMinSize(1100, 780)` 与低分辨率屏冲突

`SetMinSize` 设的高度下限（780）在 1280×720 屏幕上高于可用高度，即便做了上限计算，最终 `wantH` 仍会被 `minH` 拉回去。UI 测试沙箱实测：屏幕 1280×720、对话框 1240×**740**，超 20px。

## 3. 修复（`src/ui/ui/ConfigDialog.cpp`）

### 3.1 移除会撑大窗口的两处调用

- 删除 `topSizer->Fit(this)` —— 它按内容设窗口尺寸
- 删除 `topSizer->SetSizeHints(this)` —— 它把内容尺寸变成硬下限
- 保留 `SetSizeHints(0, 0)` 显式清空已有 hint

### 3.2 用 `SetMaxSize` 卡死上限

```cpp
HMONITOR mon = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
MONITORINFO mi{}; mi.cbSize = sizeof(mi);
if (mon && ::GetMonitorInfoW(mon, &mi)) {
    maxW = mi.rcWork.right - mi.rcWork.left;   // 工作区（不含任务栏）
    maxH = mi.rcWork.bottom - mi.rcWork.top;
}
maxW -= 20; maxH -= 20;
SetMaxSize(wxSize(maxW, maxH));
```

不用 `wxDisplaySize()`：它返回的是**默认主显示器**尺寸，多显示器或低分辨率主屏时会算出比当前屏幕更大的上限（UI 沙箱 1280×720 上就因此得到过 740 > 720）。对话框此时尚未显示，`GetHwnd()` 不可用，故用 `MonitorFromPoint` 取主显示器（与 `CentreOnScreen()` 的落点一致）。

### 3.3 `SetMinSize` 不得大于 `SetMaxSize`

```cpp
const int effMinW = (minW < maxW) ? minW : maxW;
const int effMinH = (minH < maxH) ? minH : maxH;
```

否则 wx 会为了满足 min 高度把窗口撑到超出屏幕 —— 低分辨率屏必现。

## 4. 验证

### 4.1 生产环境（1920×1080）

```
dialog rect: L=15 T=15 R=1905 B=1065  size=1890x1050
dialog top vs screen: T=15 → visible
```

完全在屏幕内，Tab 栏可见。

### 4.2 UI 测试断言改为真正的回归条件

原断言 `REQUIRE(dlgH >= 760)` 只是「预览框有空间」的代理检查，**抓不到本次 bug**（超屏的对话框只会更大，不会更小）。改为直接验证「对话框必须完整落在屏幕内」：

```cpp
const int scrW = ::GetSystemMetrics(SM_CXSCREEN);
const int scrH = ::GetSystemMetrics(SM_CYSCREEN);
REQUIRE(dlgH <= scrH);
REQUIRE(dlgW <= scrW);
REQUIRE(dlgRect.top >= 0);      // 顶部出屏 → Tab 栏消失
REQUIRE(dlgRect.left >= 0);
```

### 4.3 结果

| 项 | 结果 |
|---|---|
| UI 沙箱（1280×720）3 用例 | **21 断言全过** |
| UI_MAINWINDOW/SEARCH/CLEAR/POOL/EXITASSERT | 5/5 PASS |
| `test_config_generator` | 320 → 319 PASS + 1 SKIP + 0 FAIL |
| validproxy + UITests 构建 | 0 error |

## 5. 附注

修复过程中确认了两处 wxWidgets 3.3 的 API 事实：

- `wxDisplay::Get()` 在 3.3 中**不存在**（只有 `wxDisplay::GetFromWindow(hwnd)`）
- `wxWindow::GetClientArea()` 也**不存在**

最终用 Win32 `MonitorFromPoint` + `GetMonitorInfoW` 直接取工作区，语义最明确且不依赖 wx 版本差异。新增 `#include <windows.h>`。