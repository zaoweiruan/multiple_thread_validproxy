# 悬浮球中心镂空区无法悬停/点击/拖动（ULW alpha=0 命中穿透）

**Goal:** 修复悬浮球（StandaloneFloatingWidget, Orb 模式）中心区域鼠标事件穿透到下层窗口的问题；并同步固化 UI 门禁 `UI_FLOATINGWIDGET` 用例的确定性。
**Tech Stack:** C++17、wxWidgets 3.3 (wxMSW)、Win32 UpdateLayeredWindow、Catch2 UI 自动化。
**依据文档:** `docs/DEV-PROCESS.md`。
**约束:** 不使用 `auto`；悬浮球保持非 topmost 产品语义不变。

---

## 0. 现场证据

- UI 门禁 `52-UI_FLOATINGWIDGET` 用例 A（`Orb hit-test is not intercepted by child controls`）
  5x5 探测稳定 20/25：失分点恒为球心菱形五点 (2,3)/(3,2)/(3,3)/(3,4)/(4,3)，
  `WindowFromPoint` 返回球下方窗口（曾为 `ConsoleWindowClass`、`SysListView32` 桌面），
  `childMisses==0` 恒成立 —— 遮挡与子控件两个假设均被证伪（球已 TOPMOST 提升后失分点不变）。
- 失分点形状与球图标 `float_monitor_process_1.png` 中心镂空区精确吻合。

## 1. 根因

Win32 对 `UpdateLayeredWindow` + 逐像素 alpha 的窗口做命中测试时，**alpha=0 的像素视为窗口外**，
鼠标消息直接穿透到下层窗口（`WM_NCHITTEST` 根本不会被询问，`MSWWindowProc` 中
「让整颗悬浮球均可命中测试」的 `HTCLIENT` 覆写因此形同虚设）。
球图标中心镂空 + 数字字形内腔均为 alpha=0 → 球心十字区不可悬停、不可点击、不可拖动，
违背产品既定意图（`StandaloneFloatingWidget.cpp` MSWWindowProc 注释）。

## 2. 修复（`src/ui/StandaloneFloatingWidget.cpp`）

`onPaint` Orb 分支在 `updateLayeredWindow(canvas)` 之前，对**圆形可视半径内** alpha==0
的像素钳制为 1：

| 要点 | 说明 |
|------|------|
| 视觉 | alpha=1/255 不可见，球外观不变 |
| 命中 | alpha≠0 的像素属于窗口 → 整球恢复可命中（`WindowFromPoint` 实测 25/25） |
| 边界 | 圆外四角保持 alpha=0，球外点击照常穿透（球视觉为圆形，不应拦截角落） |
| 性能 | 50x50 像素级布尔判断，仅 Orb 重绘时执行，可忽略 |

## 3. UI 用例确定性加固（`tests/ui/TestStandaloneFloatingWidget.cpp`）

门禁失败点曾随环境漂移（hit-test 20/25、收起超时、位置漂移 215px/59px）。
`[FWTrace]` 轨迹证明产品位置数学精确（`keep=(33,360) → pos=(8,335)` 分毫不差），
失败均为测试环境干扰，逐项在测试端修复：

| 干扰源 | 对策 |
|--------|------|
| 主窗 wxDataView（1280x720 下延伸至 x=1276）遮挡球坞/面板 | `parkMainWindowAway` 将主窗停到左上角小矩形 |
| 桌面/控制台等异物窗口压制分层球 | `setOrbTopmost` 临时 TOPMOST（用毕恢复 NOTOPMOST，产品 z-order 不变） |
| hideTimer（ONE_SHOT 1500ms）到期时物理光标恰好回面板内 → 单发被消费 | `driveCollapse` 每周期 ENTER+LEAVE 重新布防并 100ms 钉光标 |
| `applyShape` SetSize→Move 两调用间被瞬时采样读到中间态矩形 | `waitForStableRect` 连续 4 样一致才读数 |
| 物理鼠标抖动使点击/拖拽落点漂移（轨迹 `LDown pos=(25,25)` ≠ 目标） | `snapCursorVerified` 每条鼠标消息前钉住并校验光标 |

## 4. 验证

- `[floatingwidget]` 独立套件连跑 4 次全绿（6 用例 / 54 断言，含删净诊断后的复跑）。
- 全量门禁 `.\scripts\build-and-test.bat`：结果见 `docs/plans/project-plans-tracker.md` 当日记录。

## 5. 教训

- ULW 逐像素 alpha 窗口的命中测试遵循 alpha 通道：镂空像素=窗口外。需要「形状透明但整窗可点」时，
  给可见形状内的透明像素保留最小非零 alpha，而不是依赖 `WM_NCHITTEST` 覆写。
- UI 自动化断言失败先取证后归因：`[FWTrace]` 状态轨迹能在 10 分钟内区分「产品算错位置」
  与「事件未到达/读数撞中间态/物理鼠标干扰」，避免误改产品代码。
- 断言读取窗口位置必须等稳定态（连续多次采样一致），两 Win32 调用之间的中间态真实可被外部观察到。
