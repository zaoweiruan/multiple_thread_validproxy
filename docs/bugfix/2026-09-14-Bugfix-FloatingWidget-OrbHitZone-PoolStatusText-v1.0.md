# Bugfix: 悬浮球 Orb 模式命中区被 poolStatusText_ 截获（悬停/单击仅下半球有效）

- **日期**: 2026-09-14
- **模块**: src/ui/StandaloneFloatingWidget.cpp（applyShape）
- **状态**: completed
- **关联**: bugfix #82/#83（池 ID/菜单统一、位置回归）；docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md（统一变更引入 8 号池控件）

## 1. 摘要

用户报告：悬停、单击悬浮球**仅右下二分之一位置有效，其它位置失效**，怀疑整体尺寸计算错误。
实际根因：统一池监控变更新增的 8 个池控件中，**`poolStatusText_`（wxStaticText，"代理池状态: …"）是唯一遗漏 Hide/Show 的控件**——Orb（球）模式下它的 HWND 仍横亘在窗口上部，截获该区域全部鼠标 hit-test，导致球上半 enter/leave/click 全部失效。修复 = applyShape 两个分支各补一行 Hide/Show。

## 2. 现象与证据链

- 用户操作：悬停/单击球上半 → 无反应；球下半 → 正常展开。
- **OS 级命中探针实证**（temp/hittest_probe.ps1，7×7 网格 WindowFromPoint 扫描球窗口）：
  - 球 rect=(1222,335)-(1272,385) 50×50（探针自启实例）。
  - 网格结果：**仅第 2-3 行（y≈346-353，球上部）全部未命中球 hwnd，其余全部命中**。
  - 未命中点的 WindowFromPoint 实际归属：**hwnd class='Static'，title='代理池状态: 未运行 成员数: 0 在线代理: 0'** = poolStatusText_ 子窗口。
- 代码事实（applyShape :1015-1078）：Orb 分支 Hide 7 控件（list_/slider_/startStopBtn_/addBtn_/refreshBtn_/reportChk_/pruneChk_/optimizeChk_），Panel 分支 Show 同 7 个——**均无 poolStatusText_**。
- 历史解释：修复 #83 前位置回归问题（球收回跳右中）掩盖了本缺陷——用户注意力在位置上；#83 修复后球停在用户自由位，上部失效带才被注意到。
- UI 自动化为何未抓住：现有测试经 PostMessage 直接投递到球 hwnd（不走 OS hit-test），因此能展开，无法暴露 hit-test 截获。

## 3. 根因

1. 统一变更（258e40c 前身工作树）向悬浮窗新增 8 个池控件：poolStatusText_ + 3 按钮 + 3 checkbox（+ 已有 list_/slider_）。
2. applyShape 的 Orb/Panel 分支逐一点名 Hide/Show 子控件，遗漏 poolStatusText_。
3. Orb 模式下窗口缩为 50×50 圆球，但 poolStatusText_ HWND 仍按 Panel 布局位置（窗口上部）存在并可见（WS_VISIBLE），其矩形区域截获 WM_NCHITTEST → WindowFromPoint 返回 Static hwnd → 球上半鼠标事件全被吞。
4. 与"整体尺寸计算"无关：球尺寸/ULW 渲染/keepCenter 均正确（#83 探针已证 600×480 展开正常）。

## 4. 修复方案

- applyShape Orb 分支：补 `poolStatusText_->Hide();`（与其他池控件并列，null 检查风格一致：`if (poolStatusText_) poolStatusText_->Hide();`）。
- applyShape Panel 分支：补 `if (poolStatusText_) poolStatusText_->Show();`。
- 最小变更（2 行），无 API 变化；防复发由 TDD 用例防护。

## 5. TDD

新增 UI 用例（tests/ui/TestStandaloneFloatingWidget.cpp）：Orb 模式下 WindowFromPoint 网格点（球上部/中心/四象限）必须返回球自身 hwnd——修复前 RED（上部点返回 Static 子窗口），修复后 GREEN。

## 6. 验证计划

| 验证项 | 方法 | 期望 |
|---|---|---|
| TDD RED | UITests 新用例（探针模式 WindowFromPoint 断言） | 上部点返回 Static（复现） |
| TDD GREEN | 同用例，修复后 | 全部网格点返回球 hwnd |
| 构建 | cmake --build build --parallel 8 | 0 error |
| 非 UI 套件 | ctest --test-dir build -E "^UI_" | 38/38 |
| UI 套件 | scripts\build-and-test.bat | 7/7（UI_FLOATINGWIDGET 含 5 用例） |

## 7. 关联文档

- docs/bugfix/2026-09-11-Bugfix-PoolMemberId-DisplayAndMenuUnify-v1.0.md（#82）
- docs/bugfix/2026-09-14-Bugfix-FloatingWidget-HideReposition-v1.0.md（#83）
