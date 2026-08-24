# Spec: 代理监控窗口双击记录定位到代理列表

- 日期: 2026-08-21
- 模块: StandaloneMonitorDialog / Events / MainFrame / ProxyListPanel
- 关联: 前序 `docs/specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.0.md`（独立代理监控弹窗）
- 规范: C++17，禁止 `auto` 类型推导；修改最少代码、保持风格一致

## 1. 背景与目标

独立代理监控弹窗（`StandaloneMonitorDialog`）以 `wxListCtrl` 列出正在 watch 的
standalone 代理进程会话（索引ID / Host / 起始时间 / 运行时长 / 监听端口 / PID）。
用户希望**双击某一行记录**后：

1. 关闭（隐藏）监控弹窗；
2. 在主代理列表（`ProxyListPanel`）中**定位并选中**该代理。

## 2. 现状调研

- `ProxyListPanel::selectProxyByIndexId(const std::string&)` 已存在
  （`src/ui/ProxyListPanel.cpp:280`）：按 `indexId` 在模型内查找行 →
  `listCtrl_->Select(item)` + `listCtrl_->EnsureVisible(item)`。
- `MainFrame::onStatusUpdate` 已对 `FOUND:<indexId>` 负载调用
  `proxyPanel_->selectProxyByIndexId(...)`（`src/ui/MainFrame.cpp:156`），
  但**该路径附带 `wxMessageBox` 提示**，不适用于"静默定位"场景。
- `StandaloneMonitorRow` 含 `indexId` 字段（`src/ui/AppController.h:49`），即代理唯一标识；
  `refreshRows()` 将 `indexId` 原样写入列表第 0 列（`COL_INDEX_ID`）。
- 弹窗由 `MainFrame::onMenuStandaloneMonitor` 以 `new StandaloneMonitorDialog(this, controller_)`
  创建（父窗口即 MainFrame），关闭时仅 `Show(false)` 隐藏复用。

## 3. 方案

复用项目既有自定义事件机制（`src/ui/Events.h` / `Events.cpp`），新增
`LocateProxyEvent`（`wxEVT_LOCATE_PROXY`，携带 `indexId`）：

1. **Events.h / Events.cpp**：新增 `LocateProxyEvent` 类（携带 `indexId_`）
   + `wxDECLARE_EVENT` / `wxDEFINE_EVENT`。
2. **StandaloneMonitorDialog**：
   - 事件表新增 `EVT_LIST_ITEM_ACTIVATED(wxID_ANY, onItemActivated)`。
   - 实现 `onItemActivated`：取双击行第 0 列文本（=indexId），
     `wxQueueEvent(GetParent(), new LocateProxyEvent(indexId))` 投递给 MainFrame，
     随后 `Show(false)` 隐藏弹窗。
3. **MainFrame**：`Bind(wxEVT_LOCATE_PROXY, ...)` →
   `proxyPanel_->selectProxyByIndexId(evt.getIndexId())`。

> 不复用 `FOUND:` 字符串命令通道，避免误触发其 `wxMessageBox` 提示；
> 采用与 `StandaloneProxyEvent` / `RunningDurationsLoadedEvent` 一致的
> "事件 → MainFrame Bind → proxyPanel_ 调用" 解耦模式。

## 4. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ui/Events.h` | 新增 `class LocateProxyEvent;` 前向声明 + `wxDECLARE_EVENT(wxEVT_LOCATE_PROXY, LocateProxyEvent)` + 类定义（携带 `indexId_`，含 `getIndexId()` / `Clone()`） |
| `src/ui/Events.cpp` | `wxDEFINE_EVENT(wxEVT_LOCATE_PROXY, LocateProxyEvent);` |
| `src/ui/StandaloneMonitorDialog.h` | private 声明 `void onItemActivated(wxListEvent& event);` |
| `src/ui/StandaloneMonitorDialog.cpp` | 事件表登记 `EVT_LIST_ITEM_ACTIVATED` + 实现 `onItemActivated`（取 indexId → 投递事件 → 隐藏弹窗） |
| `src/ui/MainFrame.cpp` | `Bind(wxEVT_LOCATE_PROXY, ...)` 调用 `proxyPanel_->selectProxyByIndexId` |
| `ProxyListPanel` / `ProxyListModel` | 无需改动（能力已具备） |

## 5. 验证

- [x] 构建 0 error：`./build/validproxy.exe` 与 `./build/validproxy-cli.exe` 链接通过
- [x] ctest 全量通过（无回归）
- [ ] 手动 GUI 验证（CI 无法点击表项，建议人工确认）：
      启动监控弹窗（Proxy 菜单「独立代理监控…Ctrl+M」），确保存在正在 watch 的代理；
      双击某一行 → 弹窗关闭，主列表该 `indexId` 代理被选中且滚动至可见区

## 6. 限制 / 后续

- `selectProxyByIndexId` 仅在当前已加载（订阅过滤后的）代理集合内查找；
  若目标代理不在当前过滤视图中，定位将静默失败。
  后续可扩展为未命中时 `reloadFromDatabase()` 全量重载后再定位
  （`reloadFromDatabase` 为异步，需二次回调串联）。

## 7. 关联文档

- 实现与根因归档：`docs/bugfix/` 不适用（本项为新增功能，无 defect）
- 前序弹窗规格：`docs/specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.0.md`
