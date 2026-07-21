# Current Context

> **角色**: 提供跨会话的上下文锚点。每轮会话开始时，Agent 应检查并更新 §三。
> **维护规则**: §三 在每轮会话结束时更新：移除已完成的旧条目，添加本会话的 Bug 修复引用。
> **边界**: 只记录"在哪里"和"最近做了什么"的上下文锚点，不记录深度知识（归档至 `docs/project-knowledge.md`）。

---

## 一、基于 xray、v2rayn 开源项目

- **v2rayn 源码目录**: `E:\eclipse_workspace\v2rayn`
  - 关键文件:
    - `v2rayN\ServiceLib\Handler\ConfigHandler.cs` — DedupServerList() 方法
    - `v2rayN\ServiceLib\Models\ProfileItem.cs` — 数据模型
    - `v2rayN\ServiceLib\Manager\AppManager.cs` — ProfileItems() 获取代理列表
- **xray 源码目录**: `E:\eclipse_workspace\Xray-core`
- **wxWidgets 源码目录**: `E:\eclipse_workspace\wxWidgets`
  - 关键文件:
    - `src/generic/datavgen.cpp:2091` — wxDataViewMainWindow Create() 缺失 CS_DBLCLKS 导致 MSW 平台双击无效

## 二、项目配置文件

- `E:\eclipse_workspace\multiple_thread_validproxy\bin\config.json`

## 三、本会话引用

> 每轮会话开始时，将前一轮的已关闭条目移至 `docs/project-knowledge.md §7` 或 `docs/bugfix/`。
> 格式: 条目简述 → `docs/bugfix/YYYY-MM-DD-xxx.md`

| 日期 | 条目 | 文档 |
|------|------|------|
| 2026-06-09 | xray.executable 配置值校验 — 加载时 ERROR+弹窗、GUI 文件存在性+扩展名检查 | `docs/superpowers/specs/2026-06-09-Spec-Validproxy-xray-executable-validation-v1.0.md` |
| 2026-06-09 | test_config_reader.exe 输出目录从 bin/ 移至 tests/ — 遵循测试文件目录规范 (AGENTS.md #8) | `CMakeLists.txt` |
| 2026-06-09 | Config validation improvements plan — 5 tasks: in-class initializers, type-warn logs, xray existence check, save failure notification, load() unit tests | `docs/plans/2026-06-09-config-validation-improvements-plan.md` |
| 2026-06-09 | UI 布局调整 — 工具栏去除 dbpath 显示 / searchbox 右移 50px / ProxyDetail 默认隐藏 | `docs/superpowers/specs/2026-06-09-Spec-Validproxy-ui-layout-tweaks-v1.0.md` |
| 2026-06-09 | 订阅列表面板 — 新增"有效"列 (delay>0) + 列名中文化（名称/有效/代理数/更新） | `docs/superpowers/specs/2026-06-09-Spec-Validproxy-subscription-column-i18n-and-valid-count-v1.0.md` |
| 2026-06-09 | Resizable splitter for subscription/proxy panels — wxSplitterWindow implementation with parent fix | `docs/plans/2026-06-09-feat-ui-resizable-splitter-v1.0.md` |
| 2026-06-04 | SubitemUpdaterV2 硬编码 `"bin/config"` 路径修复 | `docs/bugfix/2026-06-04-subitemupdater-hardcoded-binconfig-path.md` |
| 2026-06-12 | Worker join 超时修复 v3 — 动态 join timeout + 轻量 warmup（v2 XrayApi::ping 轮询过重，v1 7s 不足） | `docs/bugfix/2026-06-12-Bugfix-ProxyBatchTester-Worker0-JoinTimeout-v1.0.md` |
| 2026-06-15 | AutoTask 管道诊断不可见修复 — SubitemUpdaterV2 return false + 日志级别低 + step name 别名缺失 + ConfigDialog 改进 + ProxyBatchTester zero-proxy return true | `docs/bugfix/2026-06-15-Bugfix-AutoTask-SubitemUpdater-logging-and-pipeline.md` |
| 2026-06-15 | NetworkMonitor 批量网络中断中止 — 独立 NetworkMonitor 类，后台线程 HEAD 探测，auto atomic 标志，ProxyBatchTester/SubitemUpdaterV2 检测中断自动中止 | `docs/superpowers/specs/2026-06-15-Spec-NetworkMonitor-batch-network-abort-on-disconnect-v1.0.md` |
| 2026-06-16 | **Code Review 全量修复: #1 CLI Ctrl+C / #2 状态文件路径 / #3 SQL(abandon) / #4 未用图标 / #5 重复方法合并 / #6 版本验证 DONE** + 额外修复: NetworkMonitor 启动虚假 RESTORED (firstCheckDone_), MainFrame re-entry guard (5 handlers), REALITY publicKey ERR→INFO, onUpdateType→updateMethod 重命名, "任务完成通知"移至通知分类 | `docs/bugfix/2026-06-16-Bugfix-Review-AutoTask-CLI-StateFile-v1.0.md` |
| 2026-06-16 | **修复 deduplicate() 嵌套事务错误** — deduplicate() 已 BEGIN 事务后，deduplicateConfigErrorPhase() 调用 deleteByIndexId() 再次 BEGIN 导致 SQLite 错误 | `docs/bugfix/2026-06-16-Bugfix-Dedup-NestedTransaction-v1.0.md` |
| 2026-06-18 | SubitemUpdaterV2.cpp 2422→1198 行重构 — 提取 SubscriptionParser/Deduplicator/Importer/SubscriptionUpdater 四个类 | `docs/specs/2026-06-18-Spec-SubitemUpdaterV2-Decomposition-v1.0.md` |
| 2026-06-25 | **NetworkMonitor 断连探测机制** — 断连后暂停测试，按配置阈值连续探测 N 次后再决定是否终止。修改: ConfigReader (probe_on_disconnect 结构体)、NetworkMonitor (consecutiveFailures_ 延迟取消)、ProxyBatchTester (waitForNetworkRecovery 暂停等待) | `docs/specs/2026-06-25-Spec-NetworkMonitor-ProbeOnDisconnect-v1.0.md` |
| 2026-06-26 | **SubitemUpdaterV2 未传入 netMon_** — doUpdateSubscription/doUpdateAllSubscriptions 构造 SubitemUpdaterV2 时未传第7参数 netMon (默认 nullptr)，6处 IsConnected() 空指针短路永不触发 | `docs/bugfix/2026-06-26-Bugfix-SubitemUpdaterV2-MissingNetMon-v1.0.md` |
| 2026-07-01 | **Sync 目标库新订阅 enabled 默认 0** — migrateSubscription() 直接复制源库 enabled 状态，目标库新订阅应默认禁用 | `docs/bugfix/2026-07-01-Bugfix-Sync-Subscription-EnabledDefault-v1.0.md` |
| 2026-07-01 | **SubscriptionPanel/ProxyListPanel 双击无反应** — MSW wxDataViewMainWindow 缺失 CS_DBLCLKS，使用 selection-change-based 双击检测绕过限制 | `docs/bugfix/2026-07-01-Bugfix-DoubleClick-V1.0.md` |
| 2026-07-01 | **对话框屏幕居中** — 6 处 wxDialog/wxMessageDialog 添加 CentreOnScreen() 调用 | `docs/specs/2026-07-01-Spec-DialogCentering-v1.0.md` |
| 2026-07-02 | **Sing-box Support Implementation** — Design spec created; add sing-box as alternative proxy core to Xray with config fields, UI integration, outbound format mapping | `docs/specs/2026-07-02-Spec-Singbox-Support-v1.0.md` |
| 2026-07-03 | **echconfiglist 双格式解析** — 区分 Base64 ECH 配置与 DNS URL 格式，生成正确 sing-box ech.config；计划文档已创建 | `docs/superpowers/plans/2026-07-03-echconfiglist-dual-format-support.md` |
| 2026-07-09 | **DNS 缓存解析模块 DnsCache** — DnsCache 类，静态 resolve()，getaddrinfo IPv4 + 惰性 WSAStartup + 互斥锁保护的 unordered_map 缓存；集成至 RegionDetector Strategy 3；18/18 ctest 通过 | `docs/specs/2026-07-09-Spec-DnsCache-v1.0.md` |
| 2026-07-20 | **GUI 入口缺失 curl_global_init() 崩溃** — main_gui.cpp 未调用 curl_global_init()，右键解析代理地区时线程内 curl_easy_perform 访问违例崩溃 | `docs/bugfix/2026-07-20-Bugfix-CurlGlobalInit-GUI-v1.0.md` |
