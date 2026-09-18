# Bugfix: 悬浮窗单击展开后收回时位置回归屏幕右侧中部（保留自由落点）

- **日期**: 2026-09-14
- **类型**: Bugfix
- **模块**: StandaloneFloatingWidget（UI 悬浮监控组件）
- **版本**: v1.0
- **状态**: completed

## 1. 摘要

用户报告（m1593）："单击可以弹窗，但悬浮窗位置会自动回归到屏幕右侧中间，不是单击时位置"。
单击悬浮球展开面板本身已正常工作（探针实验 temp/click_probe2.ps1 实测：50×50 → 600×480，
WS_EX_LAYERED 正确清零，位置 keepCenter 保位）。缺陷在于**收回（Panel→Orb）时窗口被强制
重新停靠到 dockEdge_ 对应的屏幕边缘中央**，丢弃了用户拖拽后的自由落点。同样影响悬停展开路径。

## 2. 现象与证据链

- 单击展开后，鼠标离开面板 → hideTimer（默认 1500ms）到期 → `onTimer` hideTimer 分支
  （StandaloneFloatingWidget.cpp:431-437）：`setMode(Mode::Orb); positionForCurrentEdge();`
  → 球跳回屏幕右中（dockPosition 全屏居中停靠）。
- 面板失活路径同样回归：`onActivate`（:895-901）失活时 `setMode(Mode::Orb);
  positionForCurrentEdge();`——用户点别处面板收回，球跳回右中。**两条路径均触发**。
- 用户既有预期：free-drop 位置保留语义（已有回归用例 "Floating widget keeps free-drop
  position after re-show" 覆盖 Hide/Show 路径，TestStandaloneFloatingWidget.cpp）。
- positionForCurrentEdge（:1083-1094）= dockPosition(dockEdge_) 全屏居中停靠，仅首次
  （placed_==false，Show 路径 :349-352）与拖拽收尾（onLeftUp nearestEdge 记 dockEdge_）合理。

## 3. 根因

`onTimer` hideTimer 分支与 `onActivate` 失活分支在收回球时**无条件调用
positionForCurrentEdge()**，将窗口重置到边缘中央，覆盖了 setMode→applyShape 内部的
keepCenter 保位逻辑（setMode 记录 oldCenter 并在 applyShape 尾部 keepCenter 移动）。
即：setMode 已保位成功，紧随的 positionForCurrentEdge 又破坏了它。

## 4. 修复方案

1. `onTimer` hideTimer 分支（:436）：删除 `positionForCurrentEdge();` 调用（保留
   setMode(Mode::Orb)——applyShape oldCenter keepCenter 已保位，Orb 50×50 以球心居中）。
2. `onActivate` 失活分支（:899）：同样删除 `positionForCurrentEdge();`。
3. Show 首次定位（:342/:351 placed_ 守卫）与拖拽收尾停靠语义不变。
4. 移除 3 处 TODO(debug-click) ClickDiag 诊断日志（:482-486/:492-495/:516-523）+
   Logger.h include（诊断任务 b37-b39 已完成使命）。
5. TDD：新增 UI 用例 "Click-expand then auto-hide keeps free-drop position"——拖球到
   自由位 → 单击展开（PostMessage DOWN/UP 同点）→ 等面板尺寸 → 移走光标等 hideTimer
   （默认 1500ms + 余量）→ 断言球收回后 rect 中心 == 展开前球心（keepCenter 保位），
   而非右侧中央停靠位。

## 5. 风险与边界

- 保留自由落点后，球可能停在屏幕中部——clampToScreen 仍防越界；拖拽收尾 nearestEdge
  仍更新 dockEdge_（未来如需"靠边"语义可再议）。
- onActivate 分支删除停靠后，失活收回 = 原地缩回球（与 hideTimer 路径行为一致化）。

## 6. 验证记录（实测，2026-09-14）

| 验证项 | 方法 | 结果 |
|---|---|---|
| TDD RED（修复前） | 新用例单跑 | 位置断言 FAILED：球心偏移 275px/215px（跳回右侧中央停靠位）——精确复现用户缺陷 |
| TDD GREEN（修复后） | `tests\UITests.exe "[floatingwidget]"` | **All tests passed（4 用例 34 断言）**，新用例保位断言 ≤10px 全过 |
| 构建 | `cmake --build build --parallel 8`（temp/build_verify.ps1） | **[7/7] EXIT=0**（bin/validproxy.exe 重链；仅预先存在的 BLENDFUNCTION 告警） |
| 非 UI 套件 | `ctest --test-dir build -E "^UI_"` | **38/38 PASSED（233.66s）** |
| UI 自动化套件 | `.\scripts\build-and-test.bat`（temp/run_ui.bat） | **7/7 ALL TESTS PASSED（29.90s）**：UI_FLOATINGWIDGET 6.41s（含新回归用例）等七项分时长全绿 |
| 手动 | 交付用户验证 | 单击展开→离开收回→球应回到单击时位置（非右侧中央） |

## 7. 关联文档

- docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.1.md（悬浮窗接管）
- docs/bugfix/2026-09-11-Bugfix-FloatingWidget-CloseDoubleDelete-v1.0.md（关闭崩溃修复）
