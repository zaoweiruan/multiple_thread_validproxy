# 功能实现状态清单

> 状态日期: 2026-06-09
> 最后更新: 工具栏 dbpath 显示去除 / searchbox 右移 50px / ProxyDetail 默认隐藏 / xray.executable 配置值校验双层级实现

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
| 列排序 (Name/Proxies/Update) | ✅ | 点击列标题 → `onColumnHeaderClick` → `SubscriptionListModel::Compare()` 三态循环排序 |
| 订阅排序清除后 ID 映射恢复 | ✅ | **2026-06-02 修复**: `detectIdOffset()` 在排序清除后正确调用 |
| 右键 → 添加订阅 | ✅ | `showAddDialog` → `controller_->importSubscription()` + `loadSubscriptions()` |
| 右键 → 从 URL 导入 | ✅ | `onImportSubscription` → `controller_->importSubscription()` |
| 右键 → 编辑订阅 | ✅ | `showEditDialog()` → `controller_->updateSubitem()` 持久化 + 重载列表 |
| 右键 → 删除订阅 | ✅ | `onDeleteSubscription` → `confirmDelete()` → `controller_->deleteSubscription()` |
| 右键 → 刷新 | ✅ | `onRefreshSubscription` → `loadSubscriptions()` |
| 已选订阅 ID 获取 | ✅ | `getSelectedSubId()` |

## 2. 代理列表

| 功能 | 状态 | 说明 |
|------|------|------|
| 按订阅加载代理 | ✅ | `loadProxies(subId)` → `ProfileitemDAO::getAll()` |
| 带颜色 DataView | ✅ | `GetAttrByRow`：类型/延迟/连续失败数着色 |
| 右键 → 生成配置 | ✅ | `onGenerateConfig` → `AppController::generateConfig()` |
| 右键 → 测试单个代理 | ✅ | `onTestProxy` → `controller_->testSingleProxyAsync()` + 双向事件通知 TestPanel/MainFrame |
| 右键 → 查看详情 | ✅ | `onViewDetail` 显示 IndexId/Remarks/类型/地址/延迟等完整信息 |
| 类型筛选 | ❌ | 原类型选择下拉框已移除，功能未实现 |
| 搜索框 | ✅ | `filterBySearch()` → 地址/备注/IndexId 三字段模糊匹配，支持 Enter/实时/清除 |
| 列排序 | ✅ | `onColumnHeaderClick` Asc/Desc/None 三态循环，支持 Address/Delay/Speed/IndexId/Row# 排序 |

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
| 从 URL 导入订阅 | ✅ | `onMenuImportSub` / `onMenuAddSub` → 弹出 URL 输入框 → `controller_->importSubscription()` |
| 设置对话框 | ✅ | `onMenuConfig` → `ConfigDialog` |
| 关于对话框 | ✅ | `onMenuAbout` → `wxAboutBox` |
| **操作中禁止冲突 UI** | ✅ | `setOperationState()` → 禁用冲突工具栏/菜单按钮，防止重入 |
| 系统托盘 | ✅ | `TrayIcon`：Show/Hide/Exit 菜单 + balloon 通知 |
| 工具栏去除 dbpath 显示 | ✅ | 删除 `m_dbPathLabel`（`wxStaticText`），dbpath 仅保留状态栏 field 2 显示 |
| Searchbox 右移 50px | ✅ | `AddSpacer(20)` → `AddSpacer(70)`，视觉位置更居中 |
| ProxyDetail 面板默认隐藏 | ✅ | `.Hide()` + `detailPaneVisible_ = false`，用户按需通过"详情"按钮打开 |
| AUI 布局持久化 | ⚠️ | `loadSettings()` 空函数 — 面板布局关闭后不恢复 |
| 程序退出清理 | ✅ | 窗口关闭时删除 `TrayIcon` + 释放 `XrayManager` |

## 4. 配置校验

| 功能 | 状态 | 说明 |
|------|------|------|
| xray.executable 文件存在性校验（加载时） | ✅ | ERROR 日志 + 弹窗（不中断加载） |
| xray.executable 文件存在性校验（GUI 编辑器） | ✅ | 错误弹窗 + 阻止保存 |
| xray.executable .exe 扩展名警告（加载时） | ✅ | WARN 日志 |
| xray.executable .exe 扩展名警告（GUI 编辑器） | ✅ | 警告弹窗（允许继续） |

## 6. 缺失功能汇总

### ⚠️ 桩函数（界面存在，逻辑未完成）

1. **AUI 布局持久化** — `MainFrame::loadSettings()`：空函数，面板布局关闭后不恢复

### ❌ 完全未实现

1. **代理类型筛选** — 原类型选择下拉框已移除，筛选功能未实现

### 已修复（不再缺失）

| 原问题 | 修复日期 | 修复内容 |
|--------|---------|---------|
| 编辑订阅不持久化 | 2026-06-02+ | `showEditDialog()` 调用 `controller_->updateSubitem()` |
| 代理搜索未实现 | 2026-06-02+ | `filterBySearch()` 三字段模糊匹配 |
| 订阅右键测试无 handler | 2026-06-02+ | MainFrame 绑定 `wxEVT_SUBSCRIPTION_TEST` 完整链路 |
| 订阅删除未实现 | 2026-06-02 | `onDeleteSubscription` → `controller_->deleteSubscription()` |
| 订阅刷新不存在 | 2026-06-02 | `onRefreshSubscription` → `loadSubscriptions()` |
