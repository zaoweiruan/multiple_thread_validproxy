# 悬浮窗弹窗点击代理无法定位到主窗口

**Goal:** 修复悬浮窗弹窗（Panel 模式）中点击单个代理时，主窗口不切换订阅/选中对应代理行的问题。
**Tech Stack:** C++17、wxWidgets 3.2 (wxMSW)。

---

## 0. 现场证据

- 用户报告：悬浮窗弹窗中点击单个代理时，应定位到订阅、代理窗口相关记录，目前失效。
- 诊断日志（`[LocateProxy]`）证明整条链路执行正确：`onItemSelected` 触发 → MainFrame handler invoked → `selectProxyByIndexId` 找到行（findRowByIndexId 返回有效行）→ Select + EnsureVisible 全部执行无错误。
- 根因：悬浮窗是 `wxSTAY_ON_TOP`，主窗口始终在它后面；定位逻辑执行了，但用户无法看到结果（主窗口被遮挡）。

## 1. 修复

`src/ui/MainFrame.cpp` `wxEVT_LOCATE_PROXY` handler：定位完成后：
- `floatingWidget_->collapseToOrb()` — 悬浮窗从 Panel 收缩回 Orb 小球，不再遮挡主窗口
- `Iconize(false)` + `Maximize()` + `Raise()` — 主窗口最大化并置前，用户可见订阅切换与代理选中

## 2. 验证

- `.\scripts\build-and-test.bat` → 12/12 全绿（EXIT=0）。
- 门禁含 `UI_FLOATINGWIDGET` 15.01s 通过。