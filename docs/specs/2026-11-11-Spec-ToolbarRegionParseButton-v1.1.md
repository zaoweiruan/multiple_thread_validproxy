# Spec — 工具栏移除「最佳代理」按钮 + 新增「解析地区」按钮（批量地区解析）

- **日期**: 2026-11-11
- **模块**: `src/ui/MainFrame`（enum / 事件表 / `initToolBar` / handler）、`src/ui/icons.rc`、`tests/ui/UIIds.h`、`tests/ui/TestToolbarRegionButton.cpp`、`CMakeLists.txt`
- **版本**: v1.1（2026-11-11 增补：图标换新去背景+1:1、「同步」「清洗」位置交换）

## 1. 背景

用户原话诉求：**「调整功能： 1.去除工具栏"最佳代理" 按钮 2.用[Image 1] 作为icon，添加"解析地区"按钮，进行批量地区解析」**（用户提供图标附件，已落盘 `bin/icons/parsingregion.png`）。

现状勘察：

- 工具栏「最佳代理」= `MainFrame.cpp` `initToolBar()` 的 `AddTool(ID_TOOL_FIND, "最佳代理", ToolbarIcons::load("tool_find"), "查找最佳代理")`；`ID_TOOL_FIND = wxID_HIGHEST + 202`，事件表走 `EVT_MENU(ID_TOOL_FIND, onToolFind)`，handler 仅转发 `onMenuFindBest`（菜单「查找最佳代理 Ctrl+Shift+F」是另一入口，不受影响）。
- 批量地区解析的完整链路已存在且已验收：右键「批量解析地区」→ `AppController::resolveRegionsBatchAsync(handler, subId)` → `taskQueue_.submit(RESOLVE_REGION)`；状态事件 `RESOLVE_REGION_START` / `REGION_RESOLVE_DONE` 与状态栏文案（RefreshCommands `kStatusResolved` /「地区解析完成，代理列表已刷新」）均已接通，G8 无门禁（spec 2026-11-06 §2.2-A：入队即互斥，无弹框拒绝）。

## 2. 需求

| # | 需求 | 验收锚点 |
|---|---|---|
| R1 | 工具栏去除「最佳代理」按钮 | enum / 事件表 / AddTool / handler / `UIIds.h` 常量五处清理；菜单「查找最佳代理 Ctrl+Shift+F」保留 |
| R2 | 新增「解析地区」按钮，icon 为用户提供的图片 | `AddTool(ID_TOOL_REGION, "解析地区", ToolbarIcons::load("parsingregion"), "批量解析地区")`，位于原「最佳代理」槽位（label 同为 4 字，工具栏宽度不变） |
| R3 | 点击进行批量地区解析 | 与右键入口同路径：`resolveRegionsBatchAsync(this, "")`（全量订阅）；入队反馈 `setStatusNotice`（前方还有 N 个任务 / 已开始），无模框 |
| R4 | 图标可入库可复现 | `docs/design/ui/icon/png/parsingregion.png`（git 跟踪源）+ `icons.rc` 嵌入 `parsingregion_png`；运行时 `ToolbarIcons::load("parsingregion")` 资源优先、磁盘文件次之 |

## 3. 设计方案

### 3.1 改动点（`src/ui/MainFrame.cpp`，逐处）

1. enum：删 `ID_TOOL_FIND = wxID_HIGHEST + 202`（留退役注释）；新增 `ID_TOOL_REGION = wxID_HIGHEST + 212`（= 6212，全仓 grep 无冲突）。
2. 事件表 Toolbar 块：`EVT_MENU(ID_TOOL_FIND, onToolFind)` → `EVT_MENU(ID_TOOL_REGION, onToolRegion)`。
3. `initToolBar()`：原 `ID_TOOL_FIND` AddTool 行替换为 `ID_TOOL_REGION`（label「解析地区」/ tooltip「批量解析地区」，遵循 label 从简、tooltip 描述性惯例）。
4. handler：删 `onToolFind`（仅转发 `onMenuFindBest` 的薄壳）；新增 `onToolRegion`，仿 `ProxyListPanel::onBatchResolveRegion` 与 `onToolTest` 模式：

```cpp
void MainFrame::onToolRegion(wxCommandEvent& event) {
    if (!controller_) { (void)event; return; }
    // G8 ungated (spec 2026-11-06 §2.2-A): queueing IS the mutual exclusion.
    const std::size_t ahead = controller_->getTaskQueueSnapshot().queueDepth;
    setStatusNotice(ahead > 0
        ? wxString::Format(L"已加入队列：批量解析地区，前方还有 %zu 个任务", ahead)
        : wxString(L"批量解析地区已开始"));
    controller_->resolveRegionsBatchAsync(this, "");
    (void)event;
}
```

### 3.2 图标入库

- `bin/icons/parsingregion.png`（用户提供；`bin/` 整体 .gitignore，运行时副本）→ 复制到 `docs/design/ui/icon/png/parsingregion.png`（既有图标的唯一跟踪源）。
- `icons.rc` 新增 `parsingregion_png RCDATA "docs/design/ui/icon/png/parsingregion.png"`。
- 体积影响如实登记：v1.0 源图 1536×1024、2.2 MB；v1.1 起源图换为 `parsingregion1.png`（320×320，§7.1），嵌入体积相应缩小；运行期 `resizeForToolbar(img, 24)` 降采样到 24×24 显示，不改用户原图（保真优先，不擅自裁剪/压图）。

### 3.3 `tests/ui/UIIds.h`

`ToolBarFindBestLabel = L"最佳代理"`（已无引用）→ `ToolBarRegionLabel = L"解析地区"`，维持「工具栏文案唯一真源」块定位（wxAuiToolBar 自绘、单按钮无 UIA Name，铁律 1 不伪造 locator —— 沿用 spec 2026-10-15 §3.2 勘察结论）。

### 3.4 回归测试（`tests/ui/TestToolbarRegionButton.cpp`，tag `[toolbarregion]`，ctest `UI_TOOLBARREGION`）

- 正向：`WM_COMMAND` 6212（与 TestChainPreview 驱动 6205 同机制）→ 任务台账出现类型列「批量解析地区 (RESOLVE_REGION)」行并落终态（COMPLETED/FAILED/CANCELLED；轮询预算 90 s 覆盖沙箱 0 目标即时完成与 DNS 阻断快败两路径）；进程存活。
- 负向：退役 id 6202 → 2.5 s 内无 `FIND_BEST` 台账行、进程存活。
- 既有覆盖复用：`[layout]`（搜索框可见/在界内，label 同为 4 字无宽度回归面）、RefreshCommandsTest（REGION_RESOLVE_DONE 文案映射未动）。

**Google Test（TEST_F）决策留痕**：本变更为纯 UI 接线，无新增纯逻辑；`ToolbarIcons::load` 依赖运行时资产与 wxMSW 环境，项目无 wx+GTest 先例（`test_wx_*.cpp` 为未注册 CMake 的孤儿 app）。按 spec 2026-10-15 §3.3 同一仪器原则，回归由 Catch2 UI 测试承载；若评审要求 TEST_F，落地位置是 `tests/test_region_batch_resolver.cpp` 既有 fixture，不为本接线变更虚构断言。

## 4. 边界与约束

- C++17、禁 `auto`、MinGW/GCC + wxWidgets 3.2+。
- 仅工具栏按钮移除：`ID_MENU_FIND_BEST`、`onMenuFindBest`、菜单项「查找最佳代理 Ctrl+Shift+F」、`FIND_BEST` 任务类型与右键单代理解析全部不动。
- 不动：`EnableTool(ID_TOOL_CANCEL, false)`、`ToggleTool(ID_TOOL_STANDALONE_MON, ...)`、`AddSpacer(70)`、搜索框与详情开关布局。
- 提交纪律：只暂存本轮文件；他会话脏文件（`bin/config.json`、5 处 docs、`oh-my-opencode-slim.json` 删除、`docs/agetns优化建议/`、`docs/plans/2026-11-06-Plan-Cleanup-...`）不扫入；提交后不推送。
- 验证用本次构建产物；严禁 `bin/worker/validproxy.exe`。

## 5. 验证

| 项 | 结果 |
|---|---|
| `cmake --build cmake-build-debug --parallel 8` | **EXIT 0**（validproxy.exe / validproxy-cli.exe / UITests.exe / test_toolbar_region_icon.exe 全链接；警告均为既有文件 unused-parameter） |
| UI 门禁 `ctest -R ^UI_` | **16/16 Passed**（UI_TOOLBARREGION 5.89 s；UI_TASKLEDGER 8.85 s；UI_LAYOUTSENSING 5.43 s 复跑绿） |
| 正式门禁 `scripts/build-and-test.bat` | **EXIT 0 — 100% tests passed out of 16, ALL TESTS PASSED**（UI_FLOATINGWIDGET 17.26 s 同跑绿；UI_TOOLBARREGION 6.39 s） |
| 全量 `ctest`（71 例） | 70 过 / 1 失败 = **#46 XrayManagerStartupTest 既有冷启动噪声**（编号由 #45 顺延：新增 GTest 插入排序；`RealXrayStartsBounded (6146 ms)` > 3 s，与 2026-11-06 tracker 记载的 6472 ms 同签名；同套件 FlashCrashDoesNotHang OK；单跑复现同文本，排除本轮回归） |
| TDD 红绿（ToolbarRegionIconTest） | 红：HEAD 旧图标 1536×1024 → `IsSquareOneToOne` 失败（1536 vs 1024）；绿：新版 320×320 → 4/4 通过 |
| 字节级（bin/validproxy.exe） | 含窄串 `parsingregion`（1 处）、UTF-8「解析地区」、宽串入队反馈；窄串 `tool_find` 仅 1 处 = ToolbarIcons.h fallback 表（源未动） |
| 字节级（tests/UITests.exe） | 含 `[toolbarregion]` tag、宽串 `RESOLVE_REGION`/`FIND_BEST`/「最佳代理」（负向用例名） |
| 图标文件契约 | docs 源 = bin 副本字节一致；320×320 Format32bppArgb 1:1=True；透明像素 38.5%（清白 37% + pad 边）；IHDR colorType=6、bitDepth=8 |
| 源码级负向 | `ID_TOOL_FIND`/`onToolFind`/`ToolBarFindBestLabel` 全仓仅余 2 处退役注释（MainFrame.cpp:62、TestToolbarRegionButton.cpp:36） |
| CLion 静态诊断 | MainFrame.cpp/.h、TestToolbarRegionButton.cpp、UIIds.h、CMakeLists.txt、test_toolbar_region_icon.cpp 共 6 文件 `errors: []` |
| 新增 `auto` | 0 处（git diff 核定） |

## 6. 风险

| 风险 | 评估 |
|---|---|
| 6202 残留引用 | 全仓 grep 仅 `MainFrame.cpp:62` 定义处（`test_vless.cpp` 的 GUID 字符串无关）；移除后 6202 无绑定，负向测试兜底 |
| 图标缺失回退 | RC 嵌入 + 磁盘文件双路径；仅两者皆无时回退 `wxART_MISSING_IMAGE`，不影响功能 |
| 2.2 MB 图标撑 exe / 撑宽工具栏 | exe +~2.2 MB 已登记（§3.2）；label 4 字同宽，`[layout]` 复跑兜底 |
| RESOLVE_REGION 网络依赖 | 沙箱 DB 小（155 KB），DNS 阻断快败；终态断言接受 COMPLETED/FAILED/CANCELLED 三态，不锁网络结果 |
| 去背景 fringe 残留 | BFS 阈值 245 严格白，边缘 1px 浅灰抗锯齿残留；24 px 工具栏显示下不可辨；如需像素级 fringe 可后续加邻域 alpha 衰减 |

## 7. v1.1 增补（2026-11-11，同日第二轮）

用户追加诉求两点：

### 7.1 图标换新（去背景 + 1:1）

- 输入：用户提供 `bin/icons/parsingregion1.png`（320×320、Format24bppRgb、无 alpha、白底卡通地球+放大镜）。
- 处理（一次性 PowerShell 脚本，System.Drawing，算法完整记录于此可复现）：四边种子 BFS flood fill 清除连通白底（r,g,b ≥ 245；不穿透镜片高光/云等图形内部白色）→ 内容 bbox 309×320 → 透明底居中 pad 为 320×320 正方形（不二次缩放，保真）。
- 输出：`docs/design/ui/icon/png/parsingregion.png` 与 `bin/icons/parsingregion.png` 双写、字节一致（320×320 Format32bppArgb，透明像素 38.5%）。原附件 `parsingregion1.png` 保留不动；`icons.rc` 资源名 `parsingregion_png` 不变，exe 重新嵌入。
- 回归：`tests/test_toolbar_region_icon.cpp`（GTest TEST_F ×4，纯 IHDR 文件契约：PNG 签名 / 1:1 正方 / colorType 6 RGBA / 尺寸 sanity），TDD 红绿完备（见 §5）。

### 7.2 工具栏「同步」与「清洗」交换位置

- `initToolBar()` 中 `ID_TOOL_SYNC` 与 `ID_TOOL_DEDUP` 两条 AddTool 行互换（「解析地区」位置不变，居中）。
- id 随功能不随位置：enum、事件表、handler 零改动；工具顺序变化不改变任何 id 绑定。
- 可测性如实说明：wxAuiToolBar 自绘、单按钮无 UIA Name（spec 2026-10-15 §3.2 结论沿用），顺序无自动化断言面；证据 = 源码 diff（两行互换）+ UI 门禁布局套件复跑绿 + BAT 全绿。

### 7.3 本轮验证增量

- UI 门禁 16/16（UI_TOOLBARREGION 5.89 s）；`build-and-test.bat` EXIT 0 — 16/16 ALL TESTS PASSED（首跑即绿）。
- 全量 71 例：70 过 / #46 已知冷启动噪声（#45 顺延，solo 6146 ms 同签名）。
- CLion 静态 6 文件 0 error；新增 `auto` 0 处。
