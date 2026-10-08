---
title: "docs: bugfix — 组件链编辑「配置 JSON 预览」框不可见"
type: bugfix
status: completed
date: 2026-10-08
updated: 2026-10-08
version: v1.0
---

# Bugfix：组件链编辑「配置 JSON 预览」框不可见

## 1. 现象

配置对话框 →「模板配置」→「组件链编辑」，点「解析」后：

- 状态行显示「解析成功：2 个组件，0 个冲突」（解析本身正常）
- 「配置 JSON 预览（resolveToConfig 输出）:」标签**可见**
- 标签下方**没有任何文本框**，整片空白

证据截图：`bin/log/8449cccc-1678-404f-885e-58e6f4e01a4a.png`

上一轮修复的「组件类型 unknown / 配置节空 / 策略 -」已正常（截图内显示 `Logging`/`log`/`OVERRIDE`、`DNS`/`dns`/`OVERRIDE`），说明 `ComponentRegistry` 自动注册已生效，本次是独立的布局缺陷。

## 2. 根因

`ConfigDialog` 构造函数末尾硬编码了对话框尺寸：

```cpp
SetMinSize(wxSize(1100, 650));
SetSize(wxSize(1100, 650));
```

650px 高度原本够用，但新增「配置 JSON 预览」需要额外 220px（标签 + 文本框）。`wxSizer` 在空间不足时按 proportion 分配**可扩展空间**，而 `wxTextCtrl` 未设 `SetMinSize` 时其最小尺寸可退化为 0，于是预览框被压成 0 高度——标签仍在（固定高度），文本框消失。

这解释了为什么是「有标签、无内容区」而非「整个预览区消失」。

## 3. 修复

### 3.1 对话框尺寸按屏幕自适应（`src/ui/ConfigDialog.cpp`）

```cpp
int screenW = 0; int screenH = 0;
wxDisplaySize(&screenW, &screenH);
int dialogH = 780;                       // 下限
if (screenH > 0) {
    const int usable = static_cast<int>(screenH * 0.85);
    if (usable > dialogH) dialogH = (usable > 1200) ? 1200 : usable;
}
SetMinSize(wxSize(1100, 780));
SetSize(wxSize(1440, dialogH));
```

高度下限 780（> 650 原值 + 预览区所需），宽度 1440 让「Bundle 列表 | 组件表」与预览区同屏可视。新增 `#include <wx/display.h>`（wx 3.3 的 `wxDisplaySize` 为出参版本 `void wxDisplaySize(int*, int*)`）。

### 3.2 预览框独立 panel + 硬性最小高度（同文件）

预览区从原先与编辑区共用 sizer 改为独立 `wxPanel`，文本框设 `SetMinSize(wxSize(200, 160))`：即使父容器被压缩，预览框也保留 160px 下限，不会再塌陷为 0。

### 3.3 空结果不再刷空白（同文件）

原回调在 `success && config.is_object()` 不成立且 `errorMessage` 为空时调用 `showChainPreview(std::string())`，把预览框清空——用户无法区分「没生成」与「功能坏了」。现改为三段明确反馈：

- `!success` → `// 解析失败：<errorMessage>`
- `success && config.is_null()` → `// 解析成功，但生成的配置为空`
- 否则 → `prettyJson(config, 0)`

## 4. 验证

新增 UI 自动化用例 `tests/ui/TestChainPreview.cpp`（tag `[chainpreview]`，ctest 名 `UI_CHAINPREVIEW`）：

- 经 `WM_COMMAND`（`ID_MENU_CONFIG` = 6000+110）打开配置编辑器，按标题「配置编辑器」PID 过滤定位
- 断言对话框高度 ≥ 760
- `EnumChildWindows` 收集 `Edit`/`RichEdit20W` 子控件，断言存在高度 > 40px 的可见编辑框

**RED 验证**：临时把尺寸改回 `SetSize(1100, 650)` 后测试失败并输出 `dialog height=650`；恢复修复后 PASS。

| 项 | 结果 |
|---|---|
| `UI_CHAINPREVIEW` | PASS（RED→GREEN 已确认） |
| `UI_MAINWINDOW` | PASS |
| `test_config_generator` | 320 tests → 319 PASS + 1 SKIP + 0 FAIL |
| `validproxy` 构建 | 0 error |

`UI_PORTCLOSE` 仍超时，原因是本机 `E:\eclipse_workspace\Xray-core\` 缺 `geoip.dat`，xray 无法启动 → 用例 SKIP 后仍耗满 120s 超时窗口（与本改动无关）。`UI_FLOATINGWIDGET` 在并行跑时偶发失败，单跑 PASS（既有 flake）。

## 5. 附带说明

`UI_CHAINPREVIEW` 只断言布局（对话框高度 + 存在可见 Edit），未断言预览框内的 JSON 文本内容——后者需要 UIA `ValuePattern` 读取只读 Edit 的文本，且依赖数据库里已有组件包（UI 沙箱库默认无模板/bundle），会造成环境依赖。当前 JSON 内容正确性由 `IntegrationTest.ResolveToConfigProducesJson`（断言 `resolveToConfig` 产出非空 object）覆盖。