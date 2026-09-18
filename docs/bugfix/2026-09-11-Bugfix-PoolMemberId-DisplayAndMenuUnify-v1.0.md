# Bugfix：代理池成员 ID 显示错误 + 点选/右键菜单与独立代理统一

- **日期**: 2026-09-11
- **文档类型**: Bugfix
- **模块**: StandaloneProxyPool / StandaloneFloatingWidget / AppController / UnifiedMonitorRows
- **版本**: v1.0
- **状态**: completed

---

## 1. 摘要

用户报告两个缺陷（m0318）：

1. **代理池中代理的 ID 显示错误**——悬浮窗统一监控列表中，池成员行的「标识」列显示的是内部 tag（`px-<indexId>`），而非代理真实 indexId；且真实 indexId 为 int64 级数值（如 `5720942700011514210` ≈ 1.5×10¹⁸），在数据层被 `int` 截断。
2. **点选代理池代理后行为、右键菜单应与独立代理统一**——单击池行/右键「定位到代理列表」定位失败（携带 px-tag 而非真实 ID）；池行右键菜单为「删除成员」而独立行为「关闭代理」，分支不统一。

## 2. 现象与证据链

- 悬浮窗统一列表（StandaloneFloatingWidget::refreshRows，:934-936）：池行 COL_INDEX_ID 填充 `rows_[i].tag`（px- 前缀内部标识）；独立行填充真实 indexId——用户看到的池成员「ID」= px-tag。
- `PoolMember::indexId` / `PoolMemberView::indexId` 均为 `int`（StandaloneProxyPool.h:34、:48）；`injectMember` 用 `std::atoi(profile.indexid.c_str())`（StandaloneProxyPool.cpp:86）——int64 字符串截断/溢出，`members_` map 键（`std::map<int, PoolMember>`）为垃圾值；两个不同 int64 ID 低 32 位相同时 map 键碰撞 → 数据错乱风险。
- `onItemSelected`（:912-921）与 `onMenuLocateProxy`（:806-814）均以 `GetItemText(row, COL_INDEX_ID)` 提取定位 ID → 池行取到 px-tag → `LocateProxyEvent` 携带无效 ID → 主界面定位失败。
- `onContextMenu`（:609-655）分支：池行 = ID_MENU_REMOVE_POOL_MEMBER「删除成员」→ `onMenuRemovePoolMember` → `removePoolMember(std::stoi(...), true)`；独立行 = ID_MENU_CLOSE_PROXY「关闭代理」→ `onMenuCloseProxy` → `stopStandaloneProxy` + pid 兜底 TerminateProcess。同一列表两类行为语义不统一。

## 3. 根因分析

### 3.1 缺陷①：类型截断 + tag 显示

数据链：`profile.indexid`（string，int64 级）→ `std::atoi` 截断 → `PoolMember.indexId`（int）→ `members_` map<int> 键 → `snapshotMembers` → `PoolMemberView.indexId`（int）→ `buildUnifiedMonitorRows`（UnifiedMonitorRows.h:50 `std::to_string(m.indexId)`）→ 悬浮窗显示。tag（`"px-" + profile.indexid` 原始字符串）本身正确，但被当作显示标识使用。

### 3.2 缺陷②：点选定位失败 + 菜单不统一

定位链依赖 COL_INDEX_ID 列文本 = 真实 indexId 的隐含约定；池行显示 tag 破坏该约定。菜单分支沿袭统一监控改造前的两套入口（StandalonePoolDialog 时代遗物），未随列表合并而统一。

## 4. 修复方案

### 4.1 类型改造（int → long long）

- `StandaloneProxyPool.h`：`PoolMember::indexId`、`PoolMemberView::indexId` → `long long`；`members_` → `std::map<long long, PoolMember>`；`removeMember(int, bool)` → `removeMember(long long, bool)`。
- `StandaloneProxyPool.cpp`：`injectMember` `std::atoi` → `std::atoll`；`removeMember` 定义签名同步。
- `AppController.h/.cpp`：`removePoolMember(int, bool)` → `(long long, bool)`（调用点 `onMenuCloseProxy` 统一后用 `std::stoll` 提取真实 ID）。

### 4.2 显示统一

- `refreshRows` 池行 COL_INDEX_ID 显示 `rows_[i].indexId`（真实完整 ID，与独立行一致），不再显示 tag。
- `UnifiedMonitorRows.h`：`std::to_string(long long)` 自动适配（注释同步）。

### 4.3 菜单统一

- `onContextMenu`：删除池行/独立行分支——统一菜单「关闭代理」+「定位到代理列表」。
- `onMenuCloseProxy` 内按 `rows_[sel].type` 分支：Pool → `removePoolMember(真实ID, graceful=true)`（两阶段优雅移除）；Standalone → 现有 stopStandaloneProxy + pid 兜底逻辑。
- 删除 `ID_MENU_REMOVE_POOL_MEMBER` 枚举与 `onMenuRemovePoolMember` 处理器（.h 声明同步）。
- 定位提取 `std::stoi` → `std::stoll`（独立行/池行均为 int64 串，stoll 对两者安全）。

### 4.4 TDD

`tests/test_unified_monitor_rows.cpp`：makePoolMember 首参 int→long long；新增用例 `Int64IndexIdRenderedFully`（indexId=5720942700011514210LL → row.indexId=="5720942700011514210" 完整串、无 px- 前缀）。现有小值断言（"7"/"42"）因字面量提升保持不变。

## 5. 风险与边界

- 类型改造波及 `members_` map 键类型——编译器静态检查全部调用点（evaluatorLoop autoPrune 内 removeMember、TestStandaloneProxyPool 字面量调用自动提升）。
- 菜单统一删除「删除成员」入口——UI 测试（grep 确认）不引用该菜单名/处理器，无测试破坏。
- 显示从 tag → 真实 ID 为用户可见变化，正是需求本身。
- 池成员 map 键碰撞风险随类型改造一并消除。

## 6. 验证记录（实测，2026-09-11）

| 项 | 结果 |
|---|---|
| 构建 `cmake --build build --parallel 8` | ✅ EXIT=0（bin/validproxy.exe 重链 15:55:05；test_unified_monitor_rows.exe 重编） |
| 新用例 `UnifiedMonitorRows.Int64IndexIdRenderedFully` | ✅ PASSED（单跑 0ms；19 位完整串渲染） |
| 非 UI `ctest --test-dir build -E "^UI_" --output-on-failure` | ✅ 38/38 PASSED，0 failed，90.97s（含 UnifiedMonitorRowsTest 全部用例，无回归） |
| UI 自动化 `.\scripts\build-and-test.bat` | ✅ **7/7 ALL TESTS PASSED**（29.33s：UI_MAINWINDOW 1.18s / UI_SEARCH 1.35s / UI_CLEAR 1.09s / UI_FLOATINGWIDGET 3.88s / UI_POOL 10.75s / UI_PORTCLOSE 10.35s / UI_EXITASSERT 0.72s） |
| 手动（交付用户验证） | 池行显示真实 ID；单击/右键定位成功；池行右键「关闭代理」从池优雅移除 |

## 7. 关联文档

- 统一监控变更 Spec：docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md
- 前置四层关闭崩溃修复：docs/bugfix/2026-09-10-Bugfix-AppShutdown-PoolWatcher-UAF-v1.0.md、2026-09-11-Bugfix-FloatingWidget-CloseDoubleDelete-v1.0.md、2026-09-11-Bugfix-TrayIcon-HelperWindow-DoubleDelete-v1.0.md、2026-09-11-Bugfix-PoolEvaluator-RecursiveLock-Hang-v1.0.md
