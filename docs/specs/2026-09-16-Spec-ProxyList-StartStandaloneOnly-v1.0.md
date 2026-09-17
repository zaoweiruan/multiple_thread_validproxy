# Spec: ProxyListPanel 右键「开启代理」改为始终启动独立代理进程

- 日期: 2026-09-16
- 类型: Spec（功能调整）
- 模块: `ProxyListPanel`（UI 代理列表面板右键菜单）
- 版本: v1.0
- 代码基线: `src/ui/ProxyListPanel.cpp:444-482`（`onContextMenu`）、`src/ui/ProxyListPanel.cpp:655-758`（`onStartProxy`）、`src/ui/AppController.h:209-214`（`isProxyPoolEnabled`/`startProxyPool`/`isProxyPoolRunning`/`injectProxyToPool`）

> **触发**：用户请求「将 ProxyListPanel 右键『开启代理』改为开启独立代理，功能为：开启独立代理进程，不管代理池是否开启」。

---

## 1. 概述

`ProxyListPanel` 右键菜单「开启代理」当前有**双路径**行为：

1. **池运行时**（`controller_->isProxyPoolRunning() == true`）→ 调用 `controller_->injectProxyToPool(indexId)` 将代理作为成员注入代理池（tag `px-<indexId>`）
2. **否则** → 调用 `controller_->startStandaloneProxy(indexId, actualPort)` 启动独立代理进程（单 Xray 进程 + `standalone_<indexId>-xray.json` 配置）

该分支导致**同名菜单项「开启代理」在不同运行时状态下语义不一致**：池开启时是「加入池」，池未开启时是「启动独立进程」。用户诉求明确：将「开启代理」的语义**固定为「开启独立代理进程」**，与代理池启停状态解耦。

---

## 2. 变更范围

### 2.1 变更点（2 处代码修改 + 1 处注释更新）

| # | 文件 | 行号 | 变更前 | 变更后 |
| :---: | :--- | :---: | :--- | :--- |
| 1 | `src/ui/ProxyListPanel.cpp` | L472 | `menu.Append(ID_CONTEXT_START_PROXY, "开启代理");` | `menu.Append(ID_CONTEXT_START_PROXY, "开启独立代理");` |
| 2 | `src/ui/ProxyListPanel.cpp` | L667-679 | 池运行时走 `injectProxyToPool` 并 return | **删除整段**（13 行），始终走独立代理启动路径 |
| 3 | `src/ui/ProxyListPanel.cpp` | L667-670 | 注释说明「池运行时注入成员」 | **删除注释**（与新行为不符） |

### 2.2 不变项

| 项 | 说明 |
| :--- | :--- |
| `ID_CONTEXT_START_PROXY` 常量值 | 保持 `wxID_HIGHEST + 402`（不改变 ID，避免影响现有绑定） |
| `ID_CONTEXT_ADD_TO_POOL` 菜单项 | 保持独立存在（L476-479，`poolEnabled` 时才显示「加入代理池」） |
| `onStartProxy` 其他逻辑 | 保留：`isStandaloneProxyRunning` 查重、`getProxyValidityReason` 未测速拦截、`isPortAvailable` 端口占用处理、`startStandaloneProxy` 调用 |
| 双击触发路径（L807 `onStartProxy(dummy)` + L107 `wxEVT_DATAVIEW_ITEM_ACTIVATED` 绑定） | 保留；双击行为同步为「开启独立代理」 |
| `AppController::injectProxyToPool` | 保留（`AddPoolMemberDialog` 仍消费） |
| `AppController::isProxyPoolRunning` | 保留（悬浮窗 `onStartStopPool` 仍消费） |

### 2.3 语义变化总结

| 场景 | 变更前 | 变更后 |
| :--- | :--- | :--- |
| 池运行中 + 右键「开启代理」 | 注入为池成员 | 启动独立代理进程 |
| 池运行中 + 双击 | 注入为池成员 | 启动独立代理进程 |
| 池未运行 + 右键「开启代理」 | 启动独立代理进程 | 启动独立代理进程（不变） |
| 池未运行 + 双击 | 启动独立代理进程 | 启动独立代理进程（不变） |
| 池启用 + 右键「加入代理池」 | 加入池成员 | 加入池成员（不变） |

---

## 3. 设计决策

### 3.1 为什么保留「加入代理池」菜单项独立？

代理池成员注入功能仍需入口。当前 L476-479 的 `ID_CONTEXT_ADD_TO_POOL` 菜单项已在 `isProxyPoolEnabled()` 为 true 时显示「加入代理池」，是**语义明确**的池入口。将「开启代理」固定为独立代理后，两个菜单项**语义正交**：

- **开启独立代理**（`ID_CONTEXT_START_PROXY`）：始终启动单进程独立代理
- **加入代理池**（`ID_CONTEXT_ADD_TO_POOL`）：仅在池启用时可见，将代理作为成员注入池

用户可根据需求选择对应菜单项，避免同名菜单项在不同运行时状态下语义漂移。

### 3.2 为什么不动 ID 常量？

`ID_CONTEXT_START_PROXY` 是文件内 `enum` 局部常量（`ProxyListPanel.cpp:28`），仅通过 `EVT_MENU` 宏绑定到 `onStartProxy`。重命名菜单文本不影响 ID 值与事件路由。保留 ID 常量名可避免连锁改动。

### 3.3 为什么删除 pool-running 分支而非改为「池运行时仍注入」？

用户诉求明确「不管代理池是否开启」，语义即「池运行与否不影响该菜单行为」。若保留分支，则用户诉求未被实现。删除分支是**唯一正确实现**。

### 3.4 潜在风险

| 风险 | 缓解 |
| :--- | :--- |
| 池运行时用户右键「开启代理」期望注入池成员，改后启动独立代理，可能困惑 | 用户在池启用时右键菜单仍可见「加入代理池」入口，语义清晰 |
| 同一代理既作为池成员又作为独立代理启动，可能端口冲突 | 独立代理走 `isPortAvailable` + `findAvailablePort` 端口检查，池端口由 PortManager 分配，理论上无冲突；若冲突，用户会收到端口占用提示 |
| 现有测试失败 | 已 grep 确认无测试直接断言 `onStartProxy` 的池运行分支（`TestStandalonePortClose.cpp:13` 仅注释提及，测试的是端口占用提示路径，不受影响） |

---

## 4. 验证计划

### 4.1 构建验证

```powershell
cmake --build build --parallel 8
```

期望：0 error。

### 4.2 单元测试验证

```powershell
ctest -R "UI_|ProxyList|Standalone" -V
```

期望：无回归（无测试直接断言被删除的分支）。

### 4.3 手工验证清单

1. **池未启用**：右键「开启独立代理」→ 弹窗提示「请先测速」或启动独立代理进程（原行为不变）
2. **池启用但未运行**：右键「开启独立代理」→ 启动独立代理进程（不再路由到池注入）
3. **池启用且运行中**：右键「开启独立代理」→ 启动独立代理进程（**关键场景：不再注入池**）
4. **池启用且运行中**：右键「加入代理池」→ 加入池成员（原行为不变）
5. **双击行为**：双击代理行 → 启动独立代理进程（不再注入池）
6. **菜单文本**：右键菜单显示「开启独立代理」（不再是「开启代理」）

---

## 5. 文档关联

- `docs/specs/2026-08-26-Spec-StandaloneProxyPool-v1.0.md` — 代理池总体设计（Phase1 双击=注入）
- `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md` — 悬浮窗统一接管（`ID_CONTEXT_ADD_TO_POOL` 独立入口）
- `docs/specs/2026-09-16-Spec-PoolConfigDialogAdjust-v1.0.md` — 同轮 ConfigDialog 调整
- `docs/reports/2026-09-16-Report-StandalonePool-ConfigSemantics-v1.0.md`（v1.1）— 配置语义与合并评估

---

## 6. 附录：证据索引

- `ID_CONTEXT_START_PROXY` 定义：`src/ui/ProxyListPanel.cpp:28`
- 事件绑定：`src/ui/ProxyListPanel.cpp:43`（`EVT_MENU`）、`:107`（双击 `wxEVT_DATAVIEW_ITEM_ACTIVATED` 绑定）、`:807`（双击检测调用）
- 菜单项文本：`src/ui/ProxyListPanel.cpp:472`
- 池运行分支：`src/ui/ProxyListPanel.cpp:667-679`
- 独立代理启动：`src/ui/ProxyListPanel.cpp:749`
- 加入代理池菜单项：`src/ui/ProxyListPanel.cpp:476-479`（`ID_CONTEXT_ADD_TO_POOL`）
- 控制器接口：`src/ui/AppController.h:209-214`
