# 功能实现状态清单

> 状态日期: 2026-05-15

## 约定

| 标记 | 含义 |
|------|------|
| ✅ | 完整实现，正常工作 |
| ⚠️ | 界面存在，逻辑未完成（桩函数） |
| ❌ | 未实现 |

---

## 1. 订阅管理

| 功能 | 状态 | 说明 |
|------|------|------|
| 订阅列表加载展示 | ✅ | `SubscriptionPanel::loadSubscriptions()` → `SubitemDAO::getAll()` |
| 点选订阅 → 代理列表联动 | ✅ | `onSelectionChanged` → `wxEVT_SUBSCRIPTION_SELECTED` → `ProxyListPanel::loadProxies()` |
| 订阅启用/禁用切换 | ✅ | 勾选状态通过 `wxEVT_DATAVIEW_ITEM_VALUE_CHANGED` 读入内存 |
| 右键 → 更新单个订阅 | ✅ | `onUpdateSubscription` → `controller_->updateSubscriptionAsync()` |
| 右键 → 测试订阅 | ⚠️ | `onTestSubscription` 只 `wxPostEvent(GetParent(), evt)`，**未串联到 TestPanel** |
| 右键 → 编辑订阅 | ⚠️ | `showEditDialog` 对话框展示但**未保存到数据库** |
| 右键 → 删除订阅 | ⚠️ | `// TODO: implement DAO delete method` — 确认弹框有，不执行 SQL |
| 右键 → 添加订阅 | ⚠️ | `showAddDialog` 对话框 UI 完整，但保存时只弹 `"TODO: Save to database"` |
| 右键 → 从 URL 导入 | ✅ | `onImportSubscription` → `controller_->importSubscription()` |
| 已选订阅 ID 获取 | ✅ | `getSelectedSubId()` |

## 2. 代理列表

| 功能 | 状态 | 说明 |
|------|------|------|
| 按订阅加载代理 | ✅ | `loadProxies(subId)` → `ProfileitemDAO::getAll()` |
| 带颜色 DataView | ✅ | `GetAttrByRow`：类型/延迟/连续失败数着色 |
| 右键 → 生成配置 | ✅ | `onGenerateConfig` → `AppController::generateConfig()` (已修复 SQL 查询传播 bug) |
| 右键 → 测试单个代理 | ⚠️ | `onTestProxy` 只弹 `wxMessageBox("Single proxy test: " ...)` — **未调用 ProxyTester** |
| 右键 → 查看详情 | ✅ | `onViewDetail` 显示 IndexId/Remarks/类型/地址/延迟等完整信息 |
| 类型筛选 | ⚠️ | `// TODO: apply filters` — 选择器有但未过滤 |
| 搜索框 | ⚠️ | `// TODO: implement search filtering` — 输入框有但未过滤 |
| 列排序 | ⚠️ | 列标记了 `wxDATAVIEW_COL_SORTABLE`，`onColumnHeaderClick` 仅 `event.Skip()`，**无 Compare 实现** |

## 3. 批量测试

| 功能 | 状态 | 说明 |
|------|------|------|
| 工具栏 → 测试选中订阅 | ✅ | `onToolTest` → `controller_->testSubscriptionAsync()` |
| 进度条显示 | ✅ | `TestPanel::onProgress` 接收 `wxEVT_PROXY_TEST_PROGRESS` |
| 结果列表 | ✅ | `wxListCtrl` 显示 Remarks/Address/Delay/Message |
| 取消测试 | ✅ | `TestPanel::onCancel` → `controller_->cancelTest()` |
| 测试面板状态管理 | ✅ | `startTest()` / `cancelTest()` / `isRunning()` |

## 4. 日志面板

| 功能 | 状态 | 说明 |
|------|------|------|
| 彩色分级日志 | ✅ | `LogCallback` → `wxEVT_LOG_MESSAGE`，按级别着色 |
| 日志层级过滤 | ✅ | `onFilterChange` → `wxChoice` 选择 TRACE~ERROR |
| 清除日志 | ✅ | `onClear` → `logCtrl_->Clear()` |
| 自动滚动 | ✅ | `wxTE_RICH` 支持 + `wxHSCROLL` |

## 5. 主框架功能

| 功能 | 状态 | 说明 |
|------|------|------|
| 更新所有订阅 | ✅ | `onMenuUpdateAll` / `onToolUpdateAll` → `controller_->updateAllSubscriptionsAsync()` |
| 查找首个可用代理 | ✅ | `onMenuFindProxy` → `ProxyFinder::findFirstWorkingProxy()` |
| 查找延迟最小代理 | ✅ | `onMenuFindBest` → `ProxyFinder::findWorkingProxy()` |
| 去重 | ✅ | `onMenuDedup` / `onToolDedup` → `SubitemUpdaterV2::deduplicate()` |
| 同步数据库 | ✅ | `onMenuSyncDb` → `SubitemUpdaterV2::syncDatabases()` |
| 导出分享链接 | ✅ | `onMenuExportShareLink` → `share::ShareLink::toShareUri()` |
| 按 IndexId 生成配置 | ✅ | `onMenuGenerateConfig` → 弹出输入框 → `controller_->generateConfig()` |
| 导入订阅文件 | ⚠️ | `onMenuImportSub` 只打开文件对话框并设状态栏文本，**未执行实际导入** |
| 设置对话框 | ✅ | `onMenuConfig` → `ConfigDialog` |
| 关于对话框 | ✅ | `onMenuAbout` → `wxAboutBox` |
| 系统托盘 | ✅ | `TrayIcon`：Show/Hide/Exit 菜单 + balloon 通知 |
| AUI 布局持久化 | ⚠️ | `loadSettings()` 空函数 — 面板布局关闭后不恢复 |
| 程序退出清理 | ✅ | 窗口关闭时删除 `TrayIcon` + 释放 `XrayManager` |

## 6. 缺失功能汇总

### ⚠️ 桩函数（界面存在，逻辑未完成）

1. **导入订阅文件** — `MainFrame::onMenuImportSub`：只打开文件对话框，不执行导入
2. **添加订阅保存** — `showAddDialog`：对话框 UI 完整但不写入数据库
3. **编辑订阅保存** — `showEditDialog`：数据显示但不持久化
4. **删除订阅** — `onDeleteSubscription`：确认框后 `// TODO: implement DAO delete method`
5. **代理类型/状态筛选** — `onFilterChanged`：`// TODO: apply filters`
6. **代理搜索** — `onSearch`：`// TODO: implement search filtering`
7. **单代理测试** — `onTestProxy`：仅弹信息对话框
8. **订阅右键测试** — `onTestSubscription`：未串联到 TestPanel
9. **列排序** — `onColumnHeaderClick`：需实现 `Compare()`
10. **AUI 布局持久化** — `loadSettings()`：空函数

### ❌ 完全未实现

（无 — 所有 CLI 核心功能在 GUI 中都有对应的入口）
