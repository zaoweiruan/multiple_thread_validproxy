---
title: "feat: UI 自动化测试能力增强 —— 窗口感知 / 布局自调 / 菜单点击 / 弹窗错误提取"
type: plan
status: completed
date: 2026-10-15
version: v1.0
---

# 计划：UI 自动化测试能力增强

## 1. 目标（用户原始诉求）

> 优化ui测试，生成技能：实现UI模式下功能自行测试、页面布局自动调整，能点击UI页面按钮、菜单，能截图并分析页面布局，能读取窗口信息，能感知窗口类型，对错误弹窗能提取错误信息，实现自动化测试交付

拆解为 7 项可验收能力 + 1 项交付形态：

| # | 能力 | 落地载体 | ctest 条目 |
| :-- | :--- | :-------- | :--------- |
| 1 | UI 模式功能自行测试 | `framework/Fixtures.h`（既有）+ 新用例 | `[window]` |
| 2 | 页面布局自动调整 | `framework/Layout.h`（新） | `[layout]` |
| 3 | 点击按钮 / 菜单 | `UIElement::click()`（既有）+ `framework/Menu.h`（新） | `[menu]` |
| 4 | 截图并分析页面布局 | `framework/Screenshot.h` + `Diagnostics.h`（既有）+ `Layout.h` | `[layout]` |
| 5 | 读取窗口信息 | `framework/WindowInfo.h`（新） | `[window]` |
| 6 | 感知窗口类型 | `WindowInfo.h::classifyWindow()`（新） | `[window]` |
| 7 | 错误弹窗提取错误信息 | `framework/Dialog.h`（新） | `[popup]` |
| — | 自动化测试交付 | 技能 `ui-automation-test`（全局 skills 目录） | — |

## 2. 现状基线

已有 `tests/ui/framework/` 9 个文件提供：COM RAII、PID 过滤查找、`Invoke`/`Value`
pattern、`dumpTree`、GDI+ 截图、失败产物自动落盘（`ArtifactsListener` + `AppFixture`
析构）。

能力缺口（本次需补齐）：

- **窗口类型感知**：`className()` 只是取回来，没有显式归类。
- **菜单点击**：无任何菜单 helper。
- **错误弹窗提取**：有 `dumpTree` 但无 dialog 断言封装。
- **布局校验**：无控件 `rect` 采集、无重叠 / 越界校验、无 `SetWindowPos` 调整。
- **布局截图分析**：无把 PNG 交由多模态模型分析的流程。

## 3. 实施内容

### 3.1 框架层（`tests/ui/framework/`，全部 header-only + 全 `inline`）

| 文件 | 行数 | 职责 |
| :--- | ---: | :--- |
| `WindowInfo.h` | 205 | `WindowKind` 9 值枚举 + `classifyWindow()`（7 步顺序判定）+ `WindowInfo` 结构 + `topLevelWindows()` / `describeTopLevelWindows()` / `findWindowOfKind()` / `windowInfoLine()` |
| `Dialog.h` | 428 | `DialogSeverity` 分级 + `DialogReport` + `inspectDialog()` / `findPopup()` / `waitForErrorDialog()` / `dialogSummary()` / `extractErrorCode()` + `clickDialogButton()` 三级兜底 + `dismissAllPopups()` |
| `Menu.h` | 376 | `MenuEntry` + `readNativeMenu()`（`GetMenu` 递归）+ `clickMenu()` 双通道（UIA ExpandCollapse/Invoke → `PostMessageW(WM_COMMAND)`）+ `clickMenuViaKeyboard()` |
| `Layout.h` | ~580 | RECT helpers + `collectRects()`（BFS + 节点预算 + 网格剪枝）+ `rectReportText()` + `LayoutValidator` 四类规则 + `resizeWindow()` / `ensureMinSize()` / `WindowStateGuard` |
| `Text.h` | 43 | `toUtf8()` / `toWide()`（MinGW 下 `INFO << std::wstring` 不编译） |

`UIElement.h/.cpp` 扩展：新增 `controlType()` / `boundingRect()` / `parent()` /
`hasPattern()` / `findAll()` / `clone()`；`click()` / `setText()` / `getText()` 改为
`const`。

### 3.2 用例层（`tests/ui/`）

| 文件 | tag | 行数 | 覆盖 |
| :--- | :---- | ---: | :--- |
| `TestWindowSensing.cpp` | `[window]` | 97 | 窗口信息读取 + 类型分类 + Z 序枚举 |
| `TestLayoutSensing.cpp` | `[layout]` | 174 | RECT 单元测试 + `collectRects` + `LayoutValidator` + 尺寸调整 |
| `TestMenuAction.cpp` | `[menu]` | 97 | 原生菜单枚举 + 条目查找 + 双通道点击 + 分隔符 id |
| `TestPopupGuard.cpp` | `[popup]` | 101 | 弹窗发现 + 严重级分级 + 错误码提取 + 关闭 |

`CMakeLists.txt` 双重注册（`UI_TEST_SOURCES` + `add_test`），并补
`target_include_directories(UITests PRIVATE .../tests .../tests/ui)` —— 此前只给
`tests/ui`，导致 `#include "framework/XXX.h"` 解析失败静默落到 vcpkg 的
`UIAutomation.h`，报 `IUIAutomationCondition was not declared in this scope`。

### 3.3 技能层

`ui-automation-test` 技能（全局 skills 目录，683 行）：Overview 能力对照表、
5 条 Iron Laws、7 个 Capability 分节（含可编译示例）、17 条 UIA/MinGW 陷阱、
Debugging playbook（含 gdb attach / Timeout 误判 / 阻塞网络预算）、Cleanup
discipline、新用例编写 7 步、Known baseline failures、Anti-patterns。

## 4. 过程中修复的两个真实缺陷

均选择「修代码」而非「加白名单」。

### 4.1 `xrayCanStart()` 阻塞探测

- **文件**：`tests/ui/TestStandalonePortClose.cpp`（+97）、`TestStandaloneProxyPool.cpp`（+129）
- **症状**：`UI_PORTCLOSE` 被 ctest 判 `*** FAILED (Timeout) ***`，实际测试输出是
  `1 passed | 3 skipped` + `assertions: 14 | 14 passed`，只是耗时 161.9s 超过 120s 预算。
- **根因**：阻塞 socket `connect()` + 25 次重试无任何超时控制。本机装有 HIPS/ESET
  （`HipsDaemon.exe` / `esif_uf.exe`）静默 DROP 环回 SYN，单次 `connect()` 在内核阻塞
  ~2.16s → `25 × 2.16s × 3 调用点 ≈ 160s`。
- **证据**：gdb attach 栈停在 `TestStandalonePortClose.cpp:189 connect()`，3 秒 CPU 增量
  0.000s（纯阻塞，非算力瓶颈）。
- **修复**：非阻塞 socket + `select()` 限时 120ms + 2s 总预算 +
  `static const bool cached` 记忆化（3 处调用变 1 次）；探测配置改写入沙盒
  `test/ui-sandbox/xray-probe.json`（原指向并不存在的 `bin/config/probe2.json`）。
- **效果**：`[portclose]` 161.9s → **2.2s**；`[pooldialog]` 58.06s → **6.9s**。

### 4.2 `TestStandaloneFloatingWidget.cpp` 折叠阶段

- **症状**：`UI_FLOATINGWIDGET` 确定性失败（基线对照：stash 后原始 main 代码失败断言与数值
  **完全相同**，证明与本次改动无因果关系）。
- **根因 (a)**：硬编码 `::SetCursorPos(10, 10)`。orb 被 `dragWindow(hwnd, -1500, 0)` 拖到
  屏幕左边缘（margin=8，orb 中心 x=33），展开后面板被 clamp 进屏幕占据约 `x=[8, 608]`，
  于是 `(10, 10)` **落在面板矩形内部** → 不产生 `WM_MOUSELEAVE` → 永不 auto-hide。
  数值铁证：`orbCx2 - orbCx1 = 275`，`orbCx1 = 33` → `orbCx2 = 308` = 源码注释里的展开态
  面板中心 `8 + 300`。
- **根因 (b)**：折叠预算 `GetTickCount() - t0 >= 20000` 的 `t0` 在**展开前**设定，展开已消耗
  ~8s，折叠实际只剩 ~12s。
- **修复**：新增 `pickPointOutsideRect(RECT, int margin)` 按面板矩形 + 虚拟屏边界计算一个
  保证在面板外的光标落点（两处 collapse 块同步修改，`expected_replacements: 2`）；折叠改用
  独立计时起点 `tCollapse`；并在回位断言前加**稳定性门禁**（连续 4 次 / 6s 采样满足折叠态）
  —— 展开循环每 800ms 重发 click，在途消息可能在 auto-hide 之后才投递把面板重新打开。
- **效果**：32.1s / 3 failed → **11.4s / 全绿**，连跑 3 次稳定。

## 5. 验收结果

```
UI_MAINWINDOW 3.7s / UI_SEARCH 3.5s / UI_CLEAR 3.1s
UI_FLOATINGWIDGET ~11s / UI_POOL 6.97s / UI_PORTCLOSE 2.10s
UI_EXITASSERT 0.75s / UI_CHAINPREVIEW 25.12s
UI_WINDOWSENSING 13.35s / UI_LAYOUTSENSING 5.38s
UI_MENUACTION 11.05s / UI_POPUPGUARD 7.97s
100% tests passed, 0 tests failed out of 12
ALL TESTS PASSED
```

总耗时 **253.41s → 87.08s（-65.6%）**。满足 `docs/DEV-PROCESS.md` §「UI 自动化测试
规范」铁律②（交付前必须 ALL TESTS PASSED exit 0）。

## 6. 沉淀的通用教训（已写入技能 pitfalls 章节）

1. **UIA 遍历中禁止中途 `Release()`** —— UIA 客户端会回收 element proxy，已释放指针可能在
   下一次 walker 调用中被再次交回 → 双重释放 + 悬垂 → SIGSEGV。正确模式：所有 AddRef 指针
   存入 `owned` 向量，遍历后统一释放（slot 0 是 root，归调用方 ComPtr，永不释放）。
   修复后 `[layout]` 从「400+ 秒卡死」变为 5.3s。
2. **`GetWindowThreadProcessId` 返回线程 ID**，进程 ID 要走出参；两者数值范围相同，bug 极具
   迷惑性（表现为「窗口属于别的进程」）。
3. **stdout 重定向 / 管道全缓冲**，进程崩溃会丢弃整个缓冲区 → 「日志突然变短」= 崩溃而非 hang。
4. **最大化窗口静默忽略 `SetWindowPos`**（症状与「resize 无效」完全一样）。
5. **`wxDataView` 必须「只测量不深入」** —— 每个可见单元格就是一个 UIA 节点。
6. **任何碰 socket 的循环都要 `GetTickCount()` 截止线；任何被多个用例调用的助手都要记忆化。**
7. **轮询式交互会在途消息未清** —— 断言前加「连续 N 次采样一致」的稳定性门禁。
8. **白名单会掩盖真 bug** —— 在测试确实坏掉时写的白名单条目会永远放过同一个缺陷。

## 7. 关联文档

- 规范：[`docs/DEV-PROCESS.md`](../DEV-PROCESS.md) §「UI 自动化测试规范」
- 前序计划：[`2026-08-24-Plan-UITestFramework-v1.0.md`](./2026-08-24-Plan-UITestFramework-v1.0.md)
- 前序报告：[`2026-08-24-Report-UITestFramework-v1.0.md`](../reports/2026-08-24-Report-UITestFramework-v1.0.md)
- 一键验收：`scripts/build-and-test.bat`
- 定位常量唯一源：`tests/ui/UIIds.h`