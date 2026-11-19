---
title: "bugfix: Win7 运行乱码 + wx 断言崩溃（窄字节中文字面量经 ANSI 代码页解码）"
type: bugfix
status: completed
date: 2026-11-17
module: ui
version: v1.0
---

# Win7 中文乱码 + 菜单断言崩溃

## 1. 症状（用户提供，Win7 SP1 x64 + `validproxy-1.6.1-win64`）

1. **中文乱码**：主窗口工具栏按钮标签、右键菜单、部分提示文字显示为「鏇存柊」式乱码；
   截图见 `bin/log/win7 故障/QQ图片20261116201008.png`。
2. **崩溃**：弹出 `wxWidgets Debug Alert` 模态框，两处断言：
   - `SetItemLabel(): A non-stock menu item with an empty label?`（`src/common/menucmn.cpp`）
   - `wxGetStockLabel(): invalid stock item ID`（`src/common/stockitem.cpp`）
   见 `bin/log/win7 故障/QQ图片20261116201237.png`、`QQ图片20261116201317.png`；
   并产生 `crash_20261116_200339.dmp`、`crash_20261116_200606.dmp`。

## 2. 证据链

| 证据 | 结论 |
| --- | --- |
| dmp `lm` 段：`validproxy` 映像大小 `0x1530000`（22,216,704 B） | 与 `bin/validproxy.exe` 的 `SizeOfImage` **完全一致** ⇒ 现场二进制就是 `build-tc-clion-msvcrt-Release`（MSVCRT + 静态 wx Release）产物 |
| 两个 dmp 的 `ExceptionAddress` 相对偏移均为 `+0x5731f6`，`c0000005` 读 `[rax]`、`rax=0` | 同一处空指针读；`objdump` 在该偏移处为 `movzwl (%rax),%edx`，位于 `wxFormatString::AsWChar()`（`nm` 最近符号 `0x140572dc0`）的格式化串扫描/拷贝循环 |
| `bin/log/ui_20261117_083643.log`：`Build: Release \| 2026-11-16 17:36:56`、`Compiler: 15.2.0` | 现场构建 = `clion-msvcrt`（GCC 15.2.0 / MSVCRT）+ `VALIDPROXY_STATIC_WX=ON` |
| 静态 wx Release 树的 `wxDEBUG_LEVEL != 0` | wx 断言在发布包里**仍然生效** ⇒ 断言触发即弹模态框，用户点 Stop 即中止/落盘 dmp |

## 3. 根因

`wxString` 隐式 `const char*` 构造使用 **`wxConvLibc`**（`include/wx/strconv.h:722`
`#define wxSTRING_DEFAULT_CONV_ARG = wxConvLibc`），即 Windows **ANSI 代码页**（本机 CP936）。
项目源文件是 UTF-8，`"更新"` 之类窄字面量是 UTF-8 字节：

- **偶数长度**的 UTF-8 片段（如 `"更新"` = 6 字节）按 GBK 解码“成功” ⇒ 随机汉字 = **乱码**（工具栏）。
- **奇数长度**的 UTF-8 片段（如 `"加入代理池"` = 15 字节、`"删除订阅下代理"` = 21 字节）在 GBK 下
  末尾剩 1 个 ≥0x80 的悬空字节 ⇒ 转换失败、`ConvertStr()` 返回**空串**
  （`src/common/string.cpp:360-376`）⇒ `wxMenu::Append(id, "")`
  ⇒ `wxMenuItemBase::SetItemLabel()` 空标签断言 → `wxGetStockLabel(自定义 id)` 非法 stock 断言
  （`src/common/menucmn.cpp:310-321`、`src/common/stockitem.cpp:218`）。

同一根因同时解释「乱码」与「两个菜单断言」；断言模态框与随后的 `AsWChar` 空指针读是同一现场的
连带后果（发布包 wx 断言未关闭 + 断言后流程继续在异常路径上做格式化）。

### 3.1 已有的历史不一致

仓内已有 71 处 `wxString::FromUTF8` / `wxConvUTF8` 显式转换（属正确写法），但同时残留
**386 处窄字节非 ASCII 字面量**（`src/` 全量统计），其中 `src/ui/` 264 处属用户可见文本。
Win10/11 上因为多数是偶数长度片段，仅表现为乱码；Win7 用户点开含奇数长度标签的右键菜单时
便命中空标签断言。

## 4. 修复方案

1. **字面量**：`src/ui/**` 全部非 ASCII 窄字面量改为宽字面量 `L"..."`（编译期即完成 UTF-16
   转换，与既有 `L"&文件"` 菜单写法一致）；`u8"..."` 仅保留在 `std::string` /
   `wxString::FromUTF8(u8"...")` 等显式 UTF-8 上下文。
2. **运行时 `std::string`（UTF-8）显示边界**：状态栏/提示等入口统一用
   `wxString::FromUTF8(...)`，不再隐式转换：
   `MainFrame::onStatusUpdate`（`evt.getMessage()` / `statusTextFor()` / `statusTextPending_`）、
   `setLogFileLabel`、状态栏 DB 路径、`LogPanel::appendLog`、
   `ProxyListModel`（Address / Delay / Remarks / Message / Region）、
   `SubscriptionListModel`（remarks）、`BundleChainEditor` / `BundleSelectorDialog` /
   `ConfigDialog`（模板与 Bundle 列表）。
3. **回归门禁**：新增源码扫描测试 `tests/test_ui_string_encoding.cpp`
   （`UiStringEncodingTest`，含扫描器自检 2 例 + `src/ui` 零违规断言；
   识别 `L"..."` / `u8"..."` / `wxT("...")` 为合规，注释与原始字符串不计）与
   `scripts/check-ui-literals.ps1`（本地一键排查，违规返回 1）。

> 说明：`std::string` 载荷仍用 `u8"..."` 保留 UTF-8 字节（Logger / JSON / 数据库 / 事件），
> 只在「进入 wx 显示层」的一处转换，避免二次编码漂移。

## 5. 验收（2026-11-17 实测）

| 项 | 命令 / 方式 | 结果 |
| --- | --- | --- |
| 构建 | `cmake --build build-tc-clion-msvcrt-Release --parallel 8` | **EXIT 0**（Release / MSVCRT / 静态 wx）；`bin/validproxy.exe` 内嵌戳 `Build: Release \| 2026-11-17 09:07:06`、`Compiler: 15.2.0` |
| 新增回归 | `tests\test_ui_string_encoding.exe` | **3/3 PASSED**（扫描器自检 2 + `src/ui` 零违规 1） |
| 源码扫描 | `powershell -File scripts/check-ui-literals.ps1` | `[check-ui-literals] OK (src/ui)`，EXIT 0 |
| 全量回归 | `ctest --test-dir build-tc-clion-msvcrt-Release --output-on-failure` | 第一轮 **73/75**（269 s）、第二轮 **74/75**（367 s），EXIT 8；唯一稳定失败为既有 `LogStatisticsEventTest`（测试基建 manifest 缺口，见 §5.1）；`UI_FLOATINGWIDGET` 第二轮通过 |
| GUI 目视 | 沙箱实例 `bin\validproxy.exe -c test\ui-sandbox\config.json`（`VALIDPROXY_ASSERT_LOG=1`） | 截图 `temp/win7fix_main.png`：工具栏「更新/批量测试/终止当前测试/清洗/解析地区/同步/添加订阅/自动任务/悬浮窗/配置」、列头与「全部」订阅均显示正常；**`bin\ui-assert-<pid>.log` 未生成**（未触发任何 wx 断言），进程稳定存活 |

### 5.1 两个既有失败（与本次改动无关）

- `43 - LogStatisticsEventTest`（`0xC0000139` STATUS_ENTRYPOINT_NOT_FOUND，**两轮均失败**）：该测试
  exe 链接 `wx::wxcore` 但**未嵌入** `src/ui/app.manifest`（Common Controls v6），加载到
  `System32` 的 `COMCTL32.dll 5.82`，`wxcore` 延迟加载的 `GetWindowSubclass` 不存在
  （cdb 模块加载序列已证实）。属该 Release/静态 wx 构建目录的测试基建缺口；
  该 target 源码（`tests/test_log_statistics_event.cpp`、`src/ui/Events.cpp`）本次未改动。
  尝试过去掉 `wx::wxcore` 以规避，但测试文件本身使用 `wx/wx.h`，链接失败，已回滚。
  **已修复（2026-11-19，双语言分支）**：新增共享资源 `tests/wx_test_manifest.rc`
  （`1 24 "src/ui/app.manifest"`），并加入 `test_log_statistics_event` 与
  `test_unified_monitor_rows` 两个链接 `wx::wxcore` 的测试 target；
  `ctest -E "^UI_"` 61/61 全通过（含 `LogStatisticsEventTest`、`UnifiedMonitorRowsTest`）。
- `63 - UI_FLOATINGWIDGET`（仅第一轮失败，第二轮通过）：既有已知 flaky；第一轮失败由桌面上
  的 UWP 覆盖窗口（探针命中 `Windows.UI.Core.CoreWindow`）导致悬浮球位置采样漂移。

### 5.2 未验证项

- 真实 **Win7 SP1** 环境回归（需用户侧复验）：本机为 Win10/11，无法复现 Win7 的
  ANSI 代码页组合；但根因（`wxConvLibc` 与源字面量 UTF-8 不匹配）与平台无关，
  修复后源码中已不存在可触发该路径的窄字节非 ASCII 字面量。

## 6. 关联

- `docs/bugfix/2026-11-17-Bugfix-Win7-UCRT-libwinpthread-v1.0.md`（同批次 Win7 修复 #2：
  启动即报丢失 `api-ms-win-crt-convert-l1-1-0.dll`）
- `docs/2026-11-16-Reference-MinGW-Toolchain-Usage-v1.0.md`（工具链/静态 wx 构建）
- `docs/bugfix/2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md`（同批次 GUI 崩溃）
- `src/ui/app.manifest`（Common Controls v6：`wxcore` 需要 `comctl32!GetWindowSubclass`）
