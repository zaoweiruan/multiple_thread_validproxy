---
title: "docs: Project Plans Tracker — global plan index and progress tracker"
type: docs
status: maintained
date: 2026-05-11
updated: 2026-08-07
---

# Project Plans Tracker

> **只记录文档引用，不记录修改、修复、操作等具体操作条目。**
> 替代旧有逐条状态更新写法；只保留文档路径和摘要说明。

---

## 近期文档引用

| 日期 | 类型 | 文档路径 | 说明 |
|------|------|----------|------|
| 2026-08-07 | report | `docs/reports/2026-08-07-Report-GitMasterSkill-Install-v1.0.md` | git-master 技能安装记录 — AGENTS.md §6.2 路由引用的 skill(name="git-master") 环境缺失；GitHub 直连不通致 npx skills add 失败（各镜像 403/404），配置 git 全局代理 socks5://127.0.0.1:10808 后手动 clone josiahsiegel/claude-plugin-marketplace 并复制 skills\git-master\ 至全局技能目录（10 文件）；重启 Kilo 后生效 | ✅ completed |
| 2026-08-07 | bugfix | `docs/bugfix/2026-08-07-Bugfix-GUI-AboutBuildTime-LogLevelSync-v1.0.md` | About 窗口编译时间不准确 — version.h 生成依赖 build.ninja 致仅 configure 时刷新，改 add_custom_target(update_version_h ALL) 每次构建重生成 APP_BUILD_TIME；二次修复：Ninja _unscanned 规则无 depfile 致未改源码 TU 保留旧时间戳，OBJECT_DEPENDS 钉死 version.h 到 main_gui/main_cli/MainFrame 3 个 TU；配置窗口保存 console 日志级别未同步 LogPanel 界面过滤，保存回调补 setInitialLogLevel(cfg.log_console_level) | ✅ completed |
| 2026-08-07 | plan | `docs/plans/2026-08-03-Plan-CodeAudit-Optimization-Implementation-v1.0.md` | 状态变更：draft → ❌ terminated（2026-08-07 终止）— Phase 1-4 全部落地（A1-A6/B1-B14/C1-C9/D1-D8/E1-E14，21/21 ctest）；Phase 5 尾项未完成：T5.1 全仓 auto 残留 15 处（触碰文件 XrayApi.cpp 10 处 + CurlEasyHandle.h L149 已清）、T5.2 无记录，按 §2 边界顺延单独立项 | ❌ terminated |
| 2026-08-07 | plan | `docs/plans/feature-status.md` §7 | 计划文档落地核验（源码 grep 证据）— Sing-box 全链路（Spec 2026-07-02）✅ / GitTag 版本头（Spec 2026-07-15）✅ / Region 列迁移（Spec 2026-07-07 Phase 2 之迁移部分）✅ / 批量 INSERT Phase B（Spec 2026-07-24）❌ / 去重阶段分区富化 deduplicateRegionPhase（Spec 2026-07-07 Phase 2）❌ | 📝 核验记录 |
| 2026-08-07 | bugfix | `docs/bugfix/2026-08-07-Bugfix-SubitemUpdater-SkipLogLevel-v1.0.md` | 订阅更新跳过提示误报 ERROR — updateAll() 全部订阅因更新间隔被跳过时（非错误，随后 return true）以 ERR 输出 "All subscriptions skipped by update interval - nothing to update"；修复=ERR→REPORT；真正失败的 "failed to update" 保持 ERR | ✅ completed |
| 2026-08-07 | bugfix | `docs/bugfix/2026-08-07-Bugfix-NetworkMonitor-ProbeLogLevel-Warn-v1.0.md` | 网络监控探测错误详情日志级别过低 — CheckURLWithDnsFlag 内 probe failed(DNS 错误 TRACE) 与 http_code 详情从 DEBUG/TRACE 提升为 WARN，使故障信号在生产 console_level=WARN 下可见；ThreadLoop 状态机日志（LOST/RESTORED）保持 ERR | ✅ completed |
| 2026-08-07 | bugfix | `docs/bugfix/2026-08-07-Bugfix-GuiLogFileLevel-v1.0.md` | GUI 启动日志未按 config.json file_level 过滤写入文件 — main_gui.cpp Logger::init 前预解析 log 段，用真实 file_level/console_level 初始化（INFO gui entry 与 ConfigReader::load 内部 DEBUG SQL 不再写入 file_level=ERROR 的日志文件） | ✅ completed |
| 2026-08-06 | bugfix | `docs/bugfix/2026-08-06-Bugfix-Logger-GUI-ConsoleLevel-v1.0.md` | GUI 入口 Logger 启动级别补全 console_level 配置 + LogPanel 筛选器同步 config.json log_console_level |
| 2026-06-12 | bugfix | `docs/bugfix/2026-06-12-Bugfix-ProxyBatchTester-Worker0-JoinTimeout-v1.0.md` | Worker join 超时修复 v3 — 动态 join timeout（v1 7s 不足，v2 ping 轮询过重导致启动延迟） |
| 2026-06-11 | bugfix | `docs/bugfix/2026-06-11-Bugfix-ProxyBatchTester-ZeroProxyEarlyReturn-v1.0.md` | 批量测试零代理时 printSummary/stopAll 缺失修复 |
| 2026-06-11 | bugfix | `docs/bugfix/2026-06-11-Bugfix-ProxyListPanel-RefreshFreeze-v1.0.md` | 大代理集测试后 UI 冻结修复 |
| 2026-06-11 | bugfix | `docs/plans/2026-06-11-Spec-ProxyFinder-findWorkingProxy-bug.md` | ProxyFinder::findWorkingProxy 返回端口时未重新注入代理 |
| 2026-06-11 | plan | `docs/plans/2026-06-11-Plan-Stability-Hardening-v1.0.md` | 稳定性加固计划（ASAN/UBSan/MiniDump/静态分析/覆盖率） |
| 2026-06-11 | bugfix | `docs/bugfix/2026-06-11-Bugfix-ConfigGenerator-NetworkFallback-v1.0.md` | 无效网络兜底与 splithttp→xhttp 回退 |
| 2026-06-09 | plan | `docs/plans/2026-06-09-config-validation-improvements-plan.md` | Config validation improvements |
| 2026-06-09 | bugfix | `docs/bugfix/2026-06-09-configreader-load-error-popup-fix.md` | ConfigReader load double-popup fix |
| 2026-06-09 | plan | `docs/plans/2026-06-09-feat-ui-resizable-splitter-v1.0.md` | Resizable splitter |
| 2026-06-04 | bugfix | `docs/bugfix/2026-06-04-subitemupdater-hardcoded-binconfig-path.md` | SubitemUpdaterV2 hardcoded path fix |
| 2026-06-12 | spec | `docs/plans/2026-06-11-Spec-Refactoring-Phase1-v1.0.md` | Phase 1 重构方案状态更新 — auto(17/17)✅ / SQL注入(3处)✅ / Logger(实例化+委托)✅ / 全部 92 tests pass |
| 2026-08-03 | spec | `docs/plans/2026-08-03-Plan-CodeAudit-Optimization-Implementation-v1.0.md` | 代码审计与优化实施计划（Phase 1-5，T1.1-T5.2，含 ASAN/性能/稳定性） | ❌ terminated |
| 2026-06-16 | bugfix | `docs/bugfix/2026-06-16-Bugfix-Review-AutoTask-CLI-StateFile-v1.0.md` | Code Review 修复: #2 状态文件路径集中 + ConfigDialog 移除, #4 未使用图标删除 |
| 2026-06-18 | spec | `docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md` | 过度职责分解方案 — ProxyBatchTester/AppController/ShareLink/ConfigGenerator/ConfigReader 五阶段拆分 |
| 2026-06-18 | spec | `docs/specs/2026-06-18-Spec-SubitemUpdaterV2-Decomposition-v1.0.md` | SubitemUpdaterV2 分解已完成 — 提取 SubscriptionParser/Deduplicator/Importer/SubscriptionUpdater 四个类 |
| 2026-06-18 | plan | `docs/plans/2026-06-18-Plan-Decomposition-Five-Phase-Implementation-v1.0.md` | 五阶段实施计划 — 逐任务拆解 ConfigReader → ShareLink → ConfigGenerator → ProxyBatchTester → AppController |
| 2026-05-19 | — | 批量状态更新 | P4→❌ cancelled; 003->completed; DLL fix->completed |
| 2026-07-09 | spec | `docs/specs/2026-07-09-Spec-DnsCache-v1.0.md` | DNS 缓存解析模块 — DnsCache 类 getaddrinfo + 惰性 WSAStartup + 互斥锁缓存 | ✅ completed |
| 2026-07-09 | bugfix | `docs/bugfix/2026-07-09-Bugfix-RegionResolver-Hang-v1.0.md` | RegionResolver 假死修复 — DNS 超时(std::async+10s) + 缓冲互斥锁解耦 + Worker 退出日志 | ✅ completed |
| 2026-07-24 | spec | `docs/specs/2026-07-24-Spec-SubscriptionWritePerformance-v1.0.md` | 订阅写入性能优化 — PrAGMA 调优(2-5x) + 批量INSERT + 内存哈希去重(10-50x) + 复合索引(50-100x)，4阶段实施方案 | 📝 draft |
| 2026-07-24 | plan | `docs/superpowers/plans/2026-07-24-SubscriptionWritePerformance-Optimization-v1.0.md` | 订阅写入性能优化实施计划——Phase A Pragma调优 / Phase B 批量INSERT+哈希去重 / Phase C 复合索引+Dedupulator优化，含完整代码变更步骤和验收标准 | 📝 draft |
| 2026-07-29 | bugfix | `docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md` | addOutboundDirect TypedMessage field 2→3 (proxy_settings) + 两处 gRPC 路径 CommandService→HandlerService 修复，4 新增单元测试，23/23 tests pass | ✅ completed |
| 2026-07-29 | bugfix | `docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md` | addOutboundDirect JSON parse 回退修复 — read `outbounds[]` 数组而非 `outbound` 单对象，parseOutboundJson 静态提取，10 协议映射，33/33 tests pass | ✅ completed |
| 2026-08-03 | plan | `docs/plans/2026-08-03-Plan-CodeAudit-Optimization-Implementation-v1.0.md` | 代码审查优化实施计划 — 26 任务 5 阶段（崩溃/UB → 正确性 → 性能 → 低危收尾 → 规范清理），每任务含位置/变更/验收，A3 测试期望同步修正；Phase 1-4 全部落地：A1-A6/B1-B14/C1-C9/D1-D8/E1-E14（C2/C5/C8/C9/E3/E4/E8/E9b 验证为已实现），21/21 ctest 通过；Phase 5 尾项未完成（T5.1 全仓 auto 残留 15 处 / T5.2 无记录），2026-08-07 终止 | ❌ terminated |
| 2026-08-04 | bugfix | 本会话 `docs/context.md` 2026-08-04 条目 | 批量测试网络监控误报断网 — waitForNetworkRecovery 无超时 CV 死等重写为 250ms 轮询 + 30s 上限；清理 fc15eec 调试残留（2×listOutboundsDirect/代理 + outbound_json TRACE）；21/21 通过 | ✅ completed |
| 2026-08-04 | bugfix | `docs/bugfix/2026-08-04-Bugfix-SubscriptionParser-GarbageSS-v1.0.md` | 订阅解析乱码 Shadowsocks 节点 — Argh94 订阅 malformed ss:// 链接（VLESS 参数伪装成 ss）+ decodeBase64 非法字符解码为索引 0 → 全库 29811 个代理 Security/Id 二进制垃圾致批量测试全失败；修复：decodeBase64 跳过非法字符 + ss:// 分支 isPrintableAscii 校验丢弃 + Deduplicator 去重阶段删除非法配置代理 + 日志降噪（XRAY_ERROR 不再打印完整 outbound JSON 消除密码泄露）；21/21 通过 | ✅ completed |
| 2026-08-04 | bugfix | `docs/bugfix/2026-08-04-Bugfix-XrayApi-XHTTP-SplitHTTP-Mapping-v1.0.md` | gRPC xhttp 传输编码与 Xray v26.2.4 兼容修复 — 新构建首次真正编码 xhttp（protocolName="xhttp" + `xray.transport.internet.xhttp.Config`）但 v26.2.4 已移除 xhttp 协议、该消息类型未注册 → AddOutbound `proto: not found`（旧构建是静默退化裸 TCP 的假成功，非代码回退）；修复 network=="xhttp" 统一按 splithttp 编码（protocolName="splithttp" + `xray.transport.internet.splithttp.Config`，与 v26.2.4 JSON 适配器 xhttp→splithttp 重映射一致）；单元测试 3/3 + 真实 v26.2.4 端到端 xhttp OK=true 验证 | ✅ completed |
| 2026-08-05 | spec | `docs/specs/2026-08-05-Spec-DnsCache-Permanent-v1.0.md` | DNS 解析结果永久缓存 — DnsShareCache 程序级共享（CURLSH + DNS_CACHE_TIMEOUT=-1）已实施 + NetworkMonitor dnsCache_ 整体删除（只写不读死代码）；NetworkMonitorTest 11 用例通过 | ✅ completed |
| 2026-08-05 | spec | `docs/specs/2026-08-05-Spec-ImportProxyValidation-v1.0.md` | 导入订阅稽核 — utils::isPrintableAscii 提取统一 + isValidProxy Security/Id 可打印 ASCII 校验（与去重阶段判定一致）；test_utils 单测 | ✅ completed |
| 2026-08-05 | bugfix | `docs/bugfix/2026-08-05-Bugfix-ProxyBatchTester-PreGenParseError-v1.0.md` | 批量测试 PreGen 失败节点触发 boost.json 解析错误刷屏与卡顿 — preGenerateConfigs 失败 push 空 outbound_json → worker 对空串 addOutboundDirect → parseOutboundJson 抛 `syntax error ... parse_string`（3 次重试×5s 超时放大）；E7 skip（pregenFailedFlags_ + worker 入口跳过）+ DIAG 防御输出坏 config 前 80 字节 hex；PreGenFailedSkipTest 7 用例；21/21 通过 | ✅ completed |

---

*项目全量文档索引见 `docs/INDEX.md`*

---
- [x] U1: `ProxyBatchTester.cpp` — 4× `LogLevel::WARN` added for boundary conditions
- [x] U1: `ProxyBatchTester.cpp` — 4× `LogLevel::INFO` replaced REPORT (xray lifecycle + network)
- [x] U2: `SubitemUpdaterV2.cpp` — sync failure summary log added (`LogLevel::ERR`)
- [x] U3: `XrayInstance.cpp` — 4× `LogLevel::ERR` added, 3× REPORT→INFO
- [x] U4: `XrayManager.cpp` — `LogLevel::ERR` (port fail) + `LogLevel::WARN` (instance fail)
- [x] U5: `ConfigGenerator.cpp` — 4× missing `LogLevel` params added (2×INFO, 2×WARN)
- [x] U6: Residual `std::cerr`/`std::cout` removed from `ProxyBatchTester.cpp`

---
- [x] `bin/config.json` notification block has `enabled`/`on_update`/`on_test`
- [x] `bin/worker/config_test.json` notification block complete
- [x] `bin/test_config.json` notification block added
- [x] `bin/test_config_full.json` notification block added

### Production Sync Fix
- [x] `bin/worker/config.json` `sync.target_db` corrected
- [x] `bin/worker/config.json` `database.path` corrected to absolute path
- [x] Sync verified: 706/706 proxies migrated successfully
- [x] `docs/plans/2026-05-11-003-fix-sync-config-path-errors-plan.md` created

### Config File Fixes
- [x] `bin/config.json` `log.file_level` changed from `"ERROR"` to `"DEBUG"`
- [x] `bin/config.json` `log.network_failures` changed from `false` to `true`

### Documentation
- [x] AGENTS.md — "核心开发规则" section added
- [x] `docs/project-knowledge.md` — "开发流程规则" section added (moved from `memory/project_knowledge.md`)
- [x] DEV-PROCESS.md — created with workflow + template + status definitions

## Execution History (post-tracker-creation)

### Recently Completed Plans

| # | Plan | Description | Status |
|---|------|-------------|--------|
| 1 | 13-001 | Sub-update log level INFO→REPORT | ✅ completed |
| 3 | 14-001 | Dedup: filter invalid proxies (REALITY missing key/sni, dirty Network) | ✅ completed |
| 2 | 13-002 | Profile progress/inserted log level INFO→REPORT | ✅ completed |
| 3 | 30 (P0) | Fix silent sqlite3_exec + finalize audit | ✅ completed |
| 4 | 31 (P1) | Extract sqlite3_open helper (U1 done, U2 kept as-is) | ✅ completed |
| 5 | 32 (P3) | Extract logInfo/logError helper functions | ✅ completed |
| 6 | 33 (P4) | Extract sqlite::exec helper + Transaction RAII guard | ❌ cancelled |
| 7 | 14-001 | 在去重功能中去除无效代理 | ✅ completed |
| 8 | 34 (P5) | Fix LogLevel ordering REPORT below ERR | ✅ completed |
| 9 | conse ui-fixes **consolidated-ui-fixes** | Fix single proxy test delay refresh + event flow | ✅ completed |
| 10 | DLL-fix **DLL Issue** | Fix wxmsw32ud_aui_gcc_custom.dll missing (Build type mismatch) | ✅ completed |
| 11 | DLL-fix **libpng16d/libtiffd** | Fix libpng16d.dll and libtiffd.dll missing for Debug build | ✅ completed |
| 12 | **Single proxy test report** | Technical report: TestSubId→IndexId, Delay refresh, event flow | ✅ completed |
| 13 | **UI enhancements plan** | Column sorting + Find individual proxy + Subscription linkage | ✅ completed |
| 28 | **Resizable splitter** | wxSplitterWindow for subscription/proxy panel resize | ✅ completed |
| 14 | **UI close hang fix** | Fix AppController/XrayInstance destructor race + process handle bugs | ✅ completed |
| 15 | **Cancel support for update** | Add runtime cancel support for subscription update (SubitemUpdaterV2 + MainFrame dynamic cancel button) | ✅ completed |
| 16 | **ProxyFinder cancel propagation** | Wire externalCancel_ to ProxyFinder in SubitemUpdaterV2::getProxyPorts() | ✅ completed |

### In Progress Plans

| # | Plan | Description | Status |
|---|------|-------------|--------|

### Recently Completed Plans

| # | Plan | Description | Status |
|---|------|-------------|--------|
| 1 | Five-Phase | Responsibility Decomposition (Phase 0-5 completed) | ✅ completed |
| — | Phase 4 | ProxyBatchTester components: ProxyBatchQuery, XrayWorkerPool, ProxyTestCounters, ProxyTestResultSink | ✅ completed |
| — | Phase 5 | AppController services: DatabaseConnectionService, ConfigService, SubscriptionService, ProxyListService, ProxyTestService, ShareLinkExportService, DatabaseMaintenanceService, AutoTaskService, UiOperationRunner | ✅ completed |
| 17 | 2026-06-02 | Subscription panel right-click delete functionality | ✅ completed |
| — | 14-002 | UI 图形界面实现 — 基于 wxWidgets (见下方 UI Implementation 详表) | ✅ completed |
| — | Phase 0 | Characterization tests for 5 decomposition targets (115 tests) | ✅ completed |
| 18 | 2026-06-02 | Subscription panel column sorting (Name, Proxies, Update) | ✅ completed |
| 19 | 2026-06-02 | ProxyListPanel Row# column sorts by IndexId instead of row number | ✅ completed |
| 20 | 2026-06-02 | SubscriptionPanel missing detectIdOffset after sort clear | ✅ completed |
| 21 | 2026-06-02 | PortManager clearPorts() for port leak fix | ✅ completed |
| 22 | 2026-06-03 | Subscription timeout config - curl CONNECTTIMEOUT + config.json | ✅ completed |
| 23 | 2026-06-03 | Network field improvements - splithttp→xhttp mapping (frontend parse) | ✅ completed |
| 24 | 2026-06-03 | UI improvements - dialog centering + copyable text | ✅ completed |
| 25 | 2026-06-03 | Proxy context menu disabled during operations | ✅ completed |
| 26 | 2026-06-03 | Config dialog improvements - block DB switch during ops | ✅ completed |
| 27 | 2026-06-04 | SubitemUpdaterV2 hardcoded `"bin/config"` path fallback fix | ✅ completed |
| 32 | 2026-06-11 | Zero-proxy run must call printSummary/stopAll on early return; cleared redundant guard in worker entry | ✅ completed |
| 33 | 2026-06-11 | ProxyListPanel refreshResults freeze — drop per-row ValueChanged storm, use listCtrl_->Refresh() | ✅ completed |
| 34 | 2026-06-11 | ConfigGenerator invalid-network fallback — defaults to tcp instead of skipping; added splithttp→xhttp mapping | ✅ completed |
| 35 | 2026-06-11 | **Stability Hardening** | CMake ASAN/UBSan, MiniDump, cppcheck, gcov — 调试基础设施加固 | ✅ completed |
| 38 | 2026-08-03 | **代码审计与优化 Phase 1（T1.2/T1.3）** — HPACK 解码 + grpc-status trailer 解析 + protobuf 编码修正（transportConfig.protocol_name 字段号 1、security_type TLS=2/REALITY=3 varint）| ✅ completed |
| 29 | 2026-06-11 | ProxyListPanel refreshResults(): replace per-row ValueChanged storm with listCtrl_->Refresh() to prevent UI freeze on large proxy sets | ✅ completed |
| 30 | 2026-06-11 | ConfigGenerator::loadProfiles(): invalid network defaults to tcp instead of skipping; added splithttp→xhttp mapping as fallback after frontend conversion | ✅ completed |
| 31 | 2026-06-09 | Resizable horizontal splitter for subscription/proxy panels | ✅ completed |
| 37 | 2026-06-17 | **Code Refactoring Phase 1** — Cleaned dead code files, removed redundant curl include, merged proxy type string mapping into ProxyTypeStrings.h | ✅ completed |
---

### `.kilo/plans/` 迁移条目

> 来源: `.kilo/plans/` → `docs/plans/` 合并迁移 (已格式化为规范命名)
> 日期: 2026-05-19

#### 已完成

| # | Plan File | Original Source | Description | Status |
|---|-----------|----------------|-------------|--------|
| 1 | `2026-05-14-003-proxy-validation-tool-architecture.md` | `1776914861549-gentle-panda.md` | 项目整体架构设计 + 构建系统 + 阶段的实现顺序 | 📝 参考文档 |
| 2 | `2026-05-18-002-fix-empty-test-results.md` | `1778814717912-tidy-meadow.md` | 修复单代理测试时 TestPanel 结果列表为空 | ✅ completed |
| 3 | `2026-04-23-001-fix-sql-delay-filter.md` | `1776415141516-jolly-mountain.md` | main.cpp SQL Delay>0 条件修正 + ShareLink 导出修复（2026-04-23） | ✅ completed |
| 4 | `2026-05-19-consolidated-ui-fixes.md` | 合并计划 | 单代理测试 Delay 列刷新 + 事件流程修复 | ✅ completed |

#### 报告归档 (docs/reports/ )

| # | Report File | Original Source | Description | Status |
|---|-------------|----------------|-------------|--------|
| 1 | `docs/reports/2026-04-23-sharelink-export-repair-report.md` | `1776931315746-sunny-rocket.md` | ShareLink 导出分享链接修复完成报告（5项修复已验证） | ✅ completed |

#### 草稿进行中

| # | Plan File | Original Source | Description | Status |
|---|-----------|----------------|-------------|--------|
| ~~1-5~~ | ~~See below - merged into consolidated plan~~ | ~~N/A~~ | ~~See 2026-05-19-consolidated-ui-fixes.md~~ | ✅ **MERGED** |

### UI Implementation — Phase Tracking

| Phase | U# | Description | Status |
|-------|----|-------------|--------|
| Phase 1 | U1 | CMakeLists.txt wxWidgets 集成 | ✅ |
| Phase 1 | U2 | 自定义事件系统 (Events.h) | ✅ |
| Phase 1 | U3 | AppController 控制器层 | ✅ |
| Phase 2 | U8 | 日志面板 (LogPanel) | ✅ |
| Phase 2 | U9 | 配置对话框 (ConfigDialog) | ✅ |
| Phase 2 | U10 | 系统托盘 (TrayIcon) | ✅ |
| Phase 4 | U11 | main.cpp `-ui` 入口修改 | ✅ |
| Phase 3 | U5 | 订阅面板 (SubscriptionPanel) | ✅ |
| Phase 3 | U6 | 代理列表面板 (ProxyListPanel) | ✅ |
| Phase 3 | U7 | 测试面板 (TestPanel) | ✅ |
| Phase 4 | U4 | 主窗口布局 (MainFrame) — 依赖前述所有面板 | ✅ |
| Phase 4 | INT | 集成测试 + 构建验证 | ✅ |
| **已完成** | sort/find/link | 列排序 + 查找单个代理 + 订阅联动 | ✅ completed |

**状态标记:** ✅ completed ｜ 🔄 in_progress ｜ 📝 draft ｜ ❌ blocked ｜ ❌ terminated

---

### `.kilo/plans/` 归档清理记录

> 原始文件迁移完成后，`.kilo/plans/` 目录已删除 (2026-05-19)。
> 以下计划原存于 `.kilo/plans/`，均已按规范命名迁移至 `docs/` 相应位置。
> `.kilo/plans/` 源文件保留情况: `architecture.md` / `context.md` 已确认无需迁移(同内容在 `docs/`)，其余 8 份均为临时 plan ID 命名，已完全迁移，`docs/` 即为唯一权威存放地。

| 原计划文件 (MD5) | 规范命名 | 目标位置 | 状态 |
|-----------------|---------|---------|------|
| `1776215451920-nimble-wolf.md` | `2026-05-14-004-gh-mcp-list-top-repos.md` | `docs/plans/` | 📝 draft |
| `1776415141516-jolly-mountain.md` | `2026-04-23-001-fix-sql-delay-filter.md` | `docs/plans/` | ✅ completed |
| `1776931315746-sunny-rocket.md` | `2026-04-23-sharelink-export-repair-report.md` | `docs/reports/` | ✅ completed |
| `1776914861549-gentle-panda.md` | `2026-05-14-003-proxy-validation-tool-architecture.md` | `docs/plans/` | ✅ completed |
| `1778814717912-tidy-meadow.md` | `2026-05-18-002-fix-empty-test-results.md` | `docs/plans/` | ✅ completed |
| `1779070922736-shiny-comet.md` + `eager-moon.md` + `silent-harbor.md` | `2026-05-19-consolidated-ui-fixes.md` | `docs/plans/` | ✅ **MERGED & COMPLETED** |
| `architecture.md` | 同名 | `docs/architecture.md` | 同内容已存在，不迁移 |
| `context.md` | 同名 | `docs/context.md` | 同内容已存在，不迁移 |
