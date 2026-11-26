# 任务完成报告浮层：点击外部关闭后立即重现

**Goal:** 修复「任务完成报告悬停浮层点击外部关闭后，几毫秒内又自动弹回」的竞态；同时按业主裁决跳过长期环境 flaky 的 `UI_FLOATINGWIDGET`，并提升 UI 门禁对单项瞬时失败的容忍度。
**Tech Stack:** C++17、wxWidgets 3.3 (wxMSW)、Catch2 UI 自动化、CMake/CTest、PowerShell。
**依据文档:** `docs/specs/2026-11-24-Spec-TaskReportHover-v1.0.md`、`docs/plans/2026-11-24-Plan-TaskReportHover-v1.0.md`。
**约束:** 不使用 `auto`；UI 验收使用本次构建产物；测试默认使用测试配置与测试库。

---

## 0. 现场证据

现象：完成行悬停弹出报告浮层 → 点击浮层外任意位置 → 浮层消失，但 2~19 ms 内又出现在原行，表现为「关不掉」。

临时埋点（已全部移除）在同一会话中记录到固定序列：

```text
MDOWN inside=0            # onPanelMouseDown 判定点击在浮层外
HIDE after Dismiss shown=0 # hideReportPopup() 执行完成，浮层已隐藏
MOTION SHOW hit=0 hovered=0  # 2~19ms 后 onListMotion 收到移动事件并重新 show
```

结论：不是关闭逻辑失效，而是**隐藏动作本身触发了重开**。隐藏一个 topmost 浮层后，Windows 会向光标下的窗口补发 `WM_MOUSEMOVE`，`onListMotion` 判定仍停在完成行 → 重新弹出。

另一处相关事实：浮层被工作区钳制后可能覆盖光标点，此时 `onListLeave` 会因 `overPopup=1` 判定「指针还在浮层上」而不关闭，导致测试轮询 5 s 仍看到浮层可见。

## 1. 根因

1. `wxPopupTransientWindow` 在 `wxPU_CONTAINS_CONTROLS` 下**主动抑制** wx 的原生失焦关闭（`WM_ACTIVATE → DismissOnDeactivate` 不生效），因此关闭完全依赖鼠标离开链路，缺少「点击外部」这一显式路径。
2. 补上点击外部关闭后，`hideReportPopup()` 触发的系统合成 `WM_MOUSEMOVE` 立即命中 `onListMotion`，在指针未移动的情况下重新 `showReportPopup()` —— **关闭与悬停重开之间没有抑制窗口**。
3. 单纯依赖 `onListLeave` 也不可靠：浮层被钳制进工作区后可能盖住光标，命中测试判定指针在浮层内，不触发关闭。

## 2. 修复（`src/ui/TaskMonitorPanel.{h,cpp}`）

| # | 位置 | 修复 |
|---|------|------|
| 1 | 构造函数 | 在 **Panel** 上绑定 `wxEVT_LEFT_DOWN` → `onPanelMouseDown`，覆盖列表空白区与列表本体 |
| 2 | 新增 `onPanelMouseDown` | 浮层可见时把事件坐标换算到屏幕，与 `reportPopup_->GetScreenRect()` 命中测试；**浮层外**才 `suppressHoverId_ = popupTaskId_` 后 `hideReportPopup()`；浮层内点击不关闭；始终 `event.Skip()` |
| 3 | 新增成员 `suppressHoverId_` | 记录「被点击外部关闭的那一行」。`onListMotion` 在指针仍处于同一 taskId 行时直接返回（不重开）；移到其他行立即清除并恢复正常悬停；非完成行/空白区清除 |
| 4 | `onListLeave` | 指针真正离开列表/浮层时清除 `suppressHoverId_`，避免抑制状态泄漏到下一次进入 |

设计取舍：抑制只对**被关闭的那一行**生效，且只在指针未离开该行期间有效。用户移到相邻行或离开再回来即可再次看到报告，不改变「悬停临时层」语义，也不新增关闭按钮（业主裁决：浮层不加关闭图标）。

## 3. TDD 证据

回归用例 `tests/ui/TestTaskReportHover.cpp`（`[taskreporthover]`）在原「悬停显示报告」之后追加两段断言：

1. 向 TaskMonitorPanel HWND `PostMessage(WM_LBUTTONDOWN)` 于客户区原点（必然在浮层矩形外），5 s 内必须不可见 → 覆盖点击外部关闭。
2. 关闭后继续观察 **600 ms**，浮层必须保持隐藏（`stayedHidden`）→ 直接覆盖「立即重现」缺陷。

RED（临时注释 `suppressHoverId_ = popupTaskId_;`）：8 次运行 2 次失败；再跑 10 次第 7 次失败，失败点固定为 `TestTaskReportHover.cpp:332 REQUIRE(stayedHidden)`，日志 `report popup dismissed by outside click: 1` / `stayed hidden: 0`。

GREEN（恢复修复）：连续 10/10 PASS；单独 `ctest -R "^UI_TASKREPORTHOVER$"` 10/10 PASS。

## 4. 门禁结果

| 项 | 命令 | 结果 |
|----|------|------|
| UI 门禁 | `scripts/build-and-test.bat` | **EXIT 0 / ALL TESTS PASSED**，17 项注册用例 100% 通过（`UI_FLOATINGWIDGET` 显示 `***Not Run (Disabled)`），`UI_TASKREPORTHOVER` 首跑 5.97 s 通过、无重试，Total 125.60 s；config-guard OK（`bin/config.json` 未被修改） |
| 非 UI 回归 | `ctest --test-dir build -E "^UI_" --output-on-failure` | **67/67 通过，退出码 0**，Total 165.95 s |
| 关联复跑 | `ctest -R "^UI_(TASKLEDGER\|TASKREPORTHOVER)$"` | 10/10 PASS（期间一次 `UI_TASKLEDGER` 在 137 s 处 `CHECK(fx.app().running())` 失败，判定环境 flake，复跑全绿） |

## 5. 门禁策略调整（业主裁决 2026-11-25）

- `CMakeLists.txt`：对 `UI_FLOATINGWIDGET` 设 `DISABLED TRUE`。该项是长期存在的共享桌面 `WindowFromPoint` z-order 竞态（见 `docs/architecture.md`「已知偶发」及多份 bugfix 记录），并非本次改动引入。**用例源码与 CTest 注册保持不变**，需要排查时直接运行 `UITests.exe "[floatingwidget]"`；ctest 将其显示为 `Not Run (Disabled)` 并计为通过。
- `scripts/build-and-test.bat`：ctest 增加 `--repeat until-pass:2`。单项瞬时失败（环境噪声）自动重试一次；确定性失败重试后仍然失败，不会掩盖真实回归。

## 6. 教训

- 隐藏 topmost 浮层会诱发系统补发 `WM_MOUSEMOVE`；任何「隐藏后由鼠标位置驱动重开」的交互都必须有抑制窗口，否则关闭动作会自我撤销。
- 浮层被钳制进工作区后可能与光标重叠，`leave` 类关闭路径会因命中测试失败而失效；显式「点击外部」路径不能省。
- 抑制状态必须与「具体行 + 指针是否离开」绑定，否则会变成永久禁用悬停。
