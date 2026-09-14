# Bugfix: 悬浮球边缘展开后收回位置漂移至屏幕中部（savedOrbCenter_）

- **日期**: 2026-09-14
- **模块**: src/ui/StandaloneFloatingWidget.cpp / .h（setMode / applyShape）
- **状态**: completed
- **关联**: bugfix #83（keepCenter 收回）、#84（Orb 命中区截获）；docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md

## 1. 摘要

用户报告：悬浮窗（球）在**屏幕边缘附近**时，展开面板再关闭（收回 Orb），位置跳到**屏幕靠近中部位置**，不是原始边缘位置。
根因：展开时 600×480 面板被 `clampToScreen` 钳制进屏幕（边缘球中心→面板左上角可能为负→钳到 margin=8），**面板中心 ≠ 球中心**；收回时 `applyShape` 以当前（被钳制后）面板中心为 keepCenter，球便落在面板中心（屏幕中部）。修复 = 展开前保存球中心，收回时用保存值。

## 2. 现象与证据链

- 用户操作：球拖动/停靠到屏幕边缘附近 → 悬停/单击展开面板 → 关闭（超时 hideTimer / 失焦 onActivate）→ 球跳到屏幕中部，而非原始边缘位置。
- 代码事实：
  - `setMode(:1001-1008)`：`oldCenter = GetScreenPosition() + GetSize() / 2; mode_ = m; applyShape(oldCenter);`
  - `applyShape` 尾部（:1064-1070）：`rx = keepCenter.x - disp.x - w / 2; ry = keepCenter.y - disp.y - h / 2; clampToScreen(rx, ry, w, h, anchor); Move(...)`
  - `FloatingWidgetPolicy::clampToScreen`：越界即钳到 margin=8。
- 机制推演（左缘示例，屏幕 1280×720）：
  1. 球中心 x≈33（左缘）。
  2. 展开：rx = 33 - 600/2 = -267 → clamp → rx=8 → 面板 x=8，面板中心 x = 308（屏幕中部附近）。
  3. 收回：oldCenter = 面板中心 x≈308 → rx = 308 - 25 = 283 → 球 x≈283（屏幕中部）。❌ 原始边缘位置丢失。

## 3. 根因

`applyShape` 的 keepCenter 语义是"旧中心保持不动"，但**展开时若目标尺寸过大被 clampToScreen 钳制，面板中心必然偏离旧中心**；收回 Orb（小尺寸 50×50 不触发 clamp）时再以面板中心为基准，球便继承该漂移。根本问题：收回应回**展开前球中心**，而非"当前窗口中心"。

## 4. 修复方案

- 头文件加 `std::optional<wxPoint> savedOrbCenter_;`（#include <optional>）。
- `setMode` 改造：
  1. Orb→Panel 展开：保存 `savedOrbCenter_ = oldCenter`（展开前球中心）。
  2. Panel→Orb 收回：`keep = savedOrbCenter_`（若有效）而非 oldCenter（面板中心）。
  3. 收回后 `savedOrbCenter_.reset()`（用后清除，避免陈旧值）。
- 行为验证：
  - ① 边缘球展开→收回：回原始边缘位置（#85 修复目标）。
  - ② 屏中自由位置展开→收回：回原位置（与 #83 一致，未钳制时 savedOrbCenter_==面板中心）。
  - ③ 连续展开：用后清除，第二次正常。

## 5. TDD

新增 UI 用例（tests/ui/TestStandaloneFloatingWidget.cpp）：拖球到左边缘 → 单击展开（记面板中心 x2）→ 移开鼠标等 hideTimer 收回 → 断言球中心 x3 ≈ 展开前 x1（≤10px）且 x3 与 x2 显著不同。
- 修复前：x3 == x2（球落面板中心，屏幕中部）→ RED。
- 修复后：x3 ≈ x1（回左缘原位）→ GREEN。

## 6. 验证计划

| 验证项 | 方法 | 期望 |
|---|---|---|
| TDD RED | UITests 新用例 | x3≈x2 失败（漂移复现） |
| TDD GREEN | 同用例，修复后 | x3≈x1 通过 |
| 构建 | cmake --build build --parallel 8 | 0 error |
| 非 UI 套件 | ctest --test-dir build -E "^UI_" | 38/38 |
| UI 套件 | ctest --test-dir build -R "^UI_" | 7/7（UI_FLOATINGWIDGET 6 用例） |

## 7. 关联文档

- docs/bugfix/2026-09-14-Bugfix-FloatingWidget-HideReposition-v1.0.md（#83，keepCenter 收回）
- docs/bugfix/2026-09-14-Bugfix-FloatingWidget-OrbHitZone-PoolStatusText-v1.0.md（#84）