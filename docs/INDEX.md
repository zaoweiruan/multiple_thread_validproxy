updated: 2026-08-21
title: "docs: project document index"
type: meta
status: maintained
---

# Project Document Index

> 项目: `validproxy` — C++ 多线程代理验证工具 (v2rayN / Xray-core)
> 本文档为所有 `docs/` 目录文件的分类索引，按最佳实践组织，可作为长期记忆入口点。
> **本文档只记录文档引用，不记录修改、修复、操作等具体操作条目。**
> 维护规则: 每新增一类文档时更新对应分类行，并追加 `updated` 日期。

---

## 0. 快速总览

| 类别 | 文件数 | 入口 |
|------|--------|------|
| [术语表](#1-术语表) | 1 | `docs/glossary.md` |
| [项目上下文](#2-项目上下文) | 4 | `docs/context.md` |
| [模型配置](#25-模型配置) | 1 | `docs/model-config.md` |
| [开发流程规范](#3-开发流程规范) | 1 | `docs/DEV-PROCESS.md` |
| [提示模式规范](#35-提示模式规范) | 1 | `docs/prompt-patterns.md` |
| [可用技能参考](#36-可用技能参考) | 1 | `docs/available-skills-reference.md` |
| [整体架构](#4-整体架构) | 3 | `docs/architecture.md` |
| [设计规范](#5-设计规范) | 9 | `docs/design/` |
| [需求与脑暴](#6-需求与脑暴) | 4 | `docs/superpowers/brainstorm/` |
| [技术方案](#7-技术方案) | 6 | `docs/superpowers/specs/` |
| [规范化设计](#75-规范化设计) | 28 | `docs/specs/` |
| [实施计划](#8-实施计划) | 57 | `docs/plans/` |
| [分析报告](#9-分析报告) | 13 | `docs/reports/` |
| [Bug 修复记录](#91-bug-修复记录) | 57 | `docs/bugfix/` |
| [测试报告](#10-测试报告) | 1 | `docs/test/` |
| [长期记忆](#13-长期记忆) | 1 | `docs/project-knowledge.md` (6.2 KB) |

---

## 1. 术语表

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/glossary.md`](./glossary.md) | **核心术语表** — 代理协议(SS/VMess/VLESS/Trojan)、传输层(KCP/mKCP/QUIC)、重写行、分级日志、导出格式等 40+ 条定义 | 12.9 KB |

**阅读顺序**: 所有新贡献者先读本章。

---

## 2. 项目上下文

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/context.md`](./context.md) | **项目上下文** — v2rayn/Xray-core 源码位置、关键文件映射、项目配置文件路径 | 0.5 KB |
| 2 | [`docs/1776914861549-gentle-panda.md`](./1776914861549-gentle-panda.md) | **早期架构计划**（已归档）— 项目整体需求分析、文件结构、分阶段实现顺序、CLI 命令、错误处理策略 | 11.1 KB |
| 3 | [`docs/日志重构需求.md`](./日志重构需求.md) | **日志重构需求描述** — 原始需求文档 | 6.6 KB |
| 4 | [`docs/model-config.md`](./model-config.md) | **模型配置参考 v2.0** — OpenCode 代理与类别的主模型及备降链配置（OpenCode Zen + OpenRouter 免费模型） | 4.2 KB |

---

## 2.5 模型配置

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/model-config.md`](./model-config.md) | **模型配置参考 v2.0** — OpenCode 代理(sisyphus/oracle/explore 等)与类别(deep/quick/writing 等)的主模型分配及备降链设计，含变更历史 | 4.2 KB |

> **约束**: 模型配置变更前应更新本文档。当前全部模型为免费 tier，无付费模型依赖。

---

## 3. 开发流程规范

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/plans/DEV-PROCESS.md`](./plans/DEV-PROCESS.md) | **开发流程核心规则** — "先创建计划文档、审核后再执行" 7 步工作流；LogLevel 等级规范(INFO/WARN/ERR/REPORT/DEBUG)；计划文档 frontmatter 模板 | 2.7 KB |

> **约束**: 任何源代码修改前**必须先创建计划文档**并经过审核。详见 DEV-PROCESS.md §1–2。

---

## 3.5 提示模式规范

 | # | 文件 | 说明 | 大小 |
 |---|------|------|------|
 | 1 | [`docs/prompt-patterns.md`](./prompt-patterns.md) | **提示词模式指南** — 针对本项目各工作类型（功能开发/Bug 修复/代码审查/调研等）的最优提示结构模板和反模式 | 8.5 KB |
 
 ## 3.6 可用技能参考
 
 | # | 文件 | 说明 |
 |---|------|------|
 | 1 | [`docs/available-skills-reference.md`](./available-skills-reference.md) | **可用技能参考** — 代码审查、重构、代码质量评估技能清单及安装方法 |
 
 > **用途**: 作为与 AI agent 协作的接口规范，确保每次交互信息完备。新贡献者或首次使用 agent 前先读本章。

---

## 4. 整体架构

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/architecture.md`](./architecture.md) | **系统架构概览** — 核心模块(XrayManager/ProxyFinder/ConfigGenerator/ProxyBatchTester 等)、数据层(SQLite DAO 模式)、数据流、构建系统、CLI 命令 | 1.1 KB |
| 2 | [`docs/1776914861549-gentle-panda.md`](./1776914861549-gentle-panda.md) | 见[项目上下文](#2-项目上下文) — 含更详细的 Phases 2–4 架构分解 | — |
| 3 | [`docs/plans/2026-05-14-003-proxy-validation-tool-architecture.md`](./plans/2026-05-14-003-proxy-validation-tool-architecture.md) | 同上经规范化迁移的计划文档（含 CMakeLists.txt 模板） | 7.7 KB |

---

## 5. 设计规范

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/design/cli-gui-binary-split-design.md`](./design/cli-gui-binary-split-design.md) | **CLI/GUI 二进制拆分设计** — 解决 WIN32_EXECUTABLE 导致 CLI 非阻塞问题的设计方案 | 6.8 KB |
| 2 | [`docs/design/ui-design-plan.md`](./design/ui-design-plan.md) | **UI 图形界面设计** — 架构分层、数据模型映射、组件选型矩阵(MainFrame/SubscriptionPanel/ProxyListPanel/TestPanel/LogPanel/ConfigDialog/TrayIcon) | 24.7 KB |
| 2 | [`docs/design/dedup-blacklist-design.md`](./design/dedup-blacklist-design.md) | **去重黑名单设计** — 无效代理过滤(REALITY/racetxt/内部性检测) 的完整设计与实现规范 | 14.6 KB |
| 3 | [`docs/design/invalid-proxy-filter-map.md`](./design/invalid-proxy-filter-map.md) | **无效代理过滤策略映射** — 条件索引、SQL 缺失/脏字段关系图、去重行为参考 | 16.4 KB |
| 4 | [`docs/plans/feature-status.md`](./plans/feature-status.md) | **功能状态矩阵** — 订阅管理/代理列表/批量测试/日志面板/主框架的逐项状态(✅⚠️❌)及缺失功能汇总 | 5.3 KB |
| 5 | [`docs/plans/impl-items-6-7-10.md`](./plans/impl-items-6-7-10.md) | **功能实施清单** — Item 6(单代理测试)/7(订阅右键串联 TestPanel)/10(代理列表列排序) 的详细设计与工作量估算 | 8.2 KB |
| 6 | [`docs/plans/DEV-PROCESS.md`](./plans/DEV-PROCESS.md) | 见[开发流程规范](#3-开发流程规范) — 也包含日志等级规范 | — |

---

## 6. 需求与脑暴

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/superpowers/brainstorm/2026-05-07-config-transaction-batch-requirements.md`](./superpowers/brainstorm/2026-05-07-config-transaction-batch-requirements.md) | **config transaction batch 需求脑暴** — ConfigReader 范围绑定问题、Transaction RAII 方案 | 7.7 KB |
| 2 | [`docs/superpowers/brainstorm/2026-05-07-curl-raii-wrapper-requirements.md`](./superpowers/brainstorm/2026-05-07-curl-raii-wrapper-requirements.md) | **curl RAII Wrapper 需求脑暴** — CurlEasyHandle 封装、异常安全性、中文路径处理 | 6.7 KB |
| 3 | [`docs/superpowers/brainstorm/2026-05-07-sharelinkparser-requirements.md`](./superpowers/brainstorm/2026-05-07-sharelinkparser-requirements.md) | **ShareLink Parser 需求脑暴** — KA·HA 选项解析、路径保留、强制 TLS 注入 | 9.0 KB |
| 4 | [`docs/ideation/2026-05-07-improvement-ideas-ideation.md`](./ideation/2026-05-07-improvement-ideas-ideation.md) | **改进想法脑暴** — 整体性能/可靠性/用户体验优化方向 | 10.3 KB |

**阅读顺序**: Superpowers workflow 输出的 `brainstorm/` 产出对应 `docs/superpowers/specs/` 设计方案，再进入 `docs/plans/` 实施阶段。

---

## 7. 技术方案

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/superpowers/specs/2026-04-13-module-refactoring-design.md`](./superpowers/specs/2026-04-13-module-refactoring-design.md) | **模块重构设计方案** — 三层模块重构(DAO/Helper/业务)、接口大纲、依赖关系、阶段划分 | 13.4 KB |
| 2 | [`docs/superpowers/specs/2026-04-16-subitem-updater-v2-design.md`](./superpowers/specs/2026-04-16-subitem-updater-v2-design.md) | **SubitemUpdaterV2 设计方案** — V1 问题分析、V2 策略(complete/incremental/merge)、数据流程图、错误恢复机制 | 9.7 KB |
| 3 | [`docs/superpowers/specs/2026-04-16-subitem-updater-v2-optimization.md`](./superpowers/specs/2026-04-16-subitem-updater-v2-optimization.md) | **SubitemUpdaterV2 优化方案** — 批量插入代替逐行 insert-or-replace、状态机合并标签更新、clone() 合理性检查 | 2.5 KB |
| 4 | [`docs/superpowers/specs/2026-04-17-dedup-design.md`](./superpowers/specs/2026-04-17-dedup-design.md) | **去重方案设计** — 去重算法(DH 模式兼容 REALITY)、前置/后置过滤、网络字段污损清理、NULL 安全策略 | 10.8 KB |
| 5 | [`docs/superpowers/specs/2026-04-24-proxy-sync-design.md`](./superpowers/specs/2026-04-24-proxy-sync-design.md) | **代理同步方案设计** — 主+副本 DB 同步、strategy 路由(insert_only/update_only/upsert)、事务批处理、数据流图 | 13.1 KB |
| 6 | [`docs/superpowers/specs/2026-04-28-subitem-batch-import-design.md`](./superpowers/specs/2026-04-28-subitem-batch-import-design.md) | **Subitem 批量导入设计** — URL Fetcher 更新、StreamBuffer+PushParser、parseSubscription()→parseContext() 分割、增量防重 | 16.1 KB |
| 7 | [`docs/superpowers/specs/2026-06-15-Spec-NetworkMonitor-batch-network-abort-on-disconnect-v1.0.md`](./superpowers/specs/2026-06-15-Spec-NetworkMonitor-batch-network-abort-on-disconnect-v1.0.md) | **NetworkMonitor 批量网络中断中止** — 后台线程周期性 HEAD 探测大陆站点连通性，auto atomic<bool> 标志，ProxyBatchTester/SubitemUpdaterV2 检测中断自动中止 | draft |
| 8 | [`docs/superpowers/specs/2026-06-16-Spec-NetworkMonitor-enabled-toggle-v1.0.md`](./superpowers/specs/2026-06-16-Spec-NetworkMonitor-enabled-toggle-v1.0.md) | **NetworkMonitor 启用开关** — 当 `enabled=false` 时 `IsConnected()` 返回 true 跳过网络检查，避免误触发中止 | draft |

---

## 7.5 规范化设计 (docs/specs/)

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/specs/2026-06-03-subscription-timeout-config.md`](./specs/2026-06-03-subscription-timeout-config.md) | **订阅超时配置化** — 将连接超时和请求超时纳入 config.json 配置管理 | 78 lines |
| 2 | [`docs/specs/2026-06-03-network-field-improvements.md`](./specs/2026-06-03-network-field-improvements.md) | **network 字段处理改进** — splithttp→xhttp 映射，无效值默认 tcp | 69 lines |
| 3 | [`docs/specs/2026-06-25-Spec-NetworkMonitor-ProbeOnDisconnect-v1.0.md`](./specs/2026-06-25-Spec-NetworkMonitor-ProbeOnDisconnect-v1.0.md) | **NetworkMonitor 断连探测机制** — 断连后暂停测试，按配置阈值连续探测 N 次后再决定是否终止 |  |
| 3 | [`docs/specs/2026-06-03-ui-improvements.md`](./specs/2026-06-03-ui-improvements.md) | **UI 体验改进** — 弹窗居中 + ProxyDetail 可选择拷贝 | 54 lines |
| 4 | [`docs/specs/2026-06-03-proxy-context-menu-disabled.md`](./specs/2026-06-03-proxy-context-menu-disabled.md) | **批量操作时禁止右键菜单** — 防止干预进行中的操作 | 43 lines |
| 5 | [`docs/specs/2026-06-11-Spec-ProxyFinder-findWorkingProxy-bug.md`](./specs/2026-06-11-Spec-ProxyFinder-findWorkingProxy-bug.md) | **ProxyFinder::findWorkingProxy Bug Fix** — 返回端口时未重新注入代理导致代理不可用 | draft |
| 6 | [`docs/plans/2026-06-11-Spec-Refactoring-Phase1-v1.0.md`](./plans/2026-06-11-Spec-Refactoring-Phase1-v1.0.md) | **重构方案 Phase 1 v1.1** — 审计更新: 3/8 任务已由历史提交完成 (writeCallback/TestResult/DAO), 其余 4 项未开始 + 1 项部分完成 | draft → v1.1 |
| 7 | [`docs/specs/2026-06-17-Spec-Code-Refactoring-Review-v1.0.md`](./specs/2026-06-17-Spec-Code-Refactoring-Review-v1.0.md) | **Code Refactoring Phase 1** — Dead code removal, redundant includes cleanup, duplicated logic merging | draft |
| 8 | [`docs/specs/2026-06-18-Spec-SubitemUpdaterV2-Decomposition-v1.0.md`](./specs/2026-06-18-Spec-SubitemUpdaterV2-Decomposition-v1.0.md) | **SubitemUpdaterV2 分解** — 提取 SubscriptionParser/Deduplicator/Importer/SubscriptionUpdater 四个类 | completed |
| 9 | [`docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md`](./specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md) | **过度职责分解方案** — ProxyBatchTester/AppController/ShareLink/ConfigGenerator/ConfigReader 的五阶段拆分方案 | ✅ completed |
| 10 | [`docs/specs/2026-07-01-Spec-PortDetectionRefactor-v1.0.md`](./specs/2026-07-01-Spec-PortDetectionRefactor-v1.0.md) | **端口探测重构方案** — 消除 `PortManager::isInUse()` 与 `utils::isPortAvailable()` 的重复，修复 `bind()` 假阳性，统一使用 connect+select 检测 | ✅ completed |
| 11 | [`docs/specs/2026-07-01-Spec-DialogCentering-v1.0.md`](./specs/2026-07-01-Spec-DialogCentering-v1.0.md) | **对话框屏幕居中** — SubscriptionPanel/ProxyListPanel 中 6 处 wxDialog/wxMessageDialog 添加 CentreOnScreen() 调用 | completed |
| 12 | [`docs/specs/2026-07-01-Reference-wxwidgets-datavgen-analysis.md`](./specs/2026-07-01-Reference-wxwidgets-datavgen-analysis.md) | **wxWidgets datavgen 双击事件分析** — MSW 平台 wxDataViewMainWindow 缺失 CS_DBLCLKS，selection-change-based Workaround 引用 | reference |
| 13 | [`docs/specs/2026-07-01-Spec-AppController-Responsibility-Decomposition-v1.0.md`](./specs/2026-07-01-Spec-AppController-Responsibility-Decomposition-v1.0.md) | **AppController 职责边界重构** — 5 个服务化分解方案 (SubscriptionOrchestrator/ProxyOrchestrator/StandaloneProxyManager/DatabaseCoordinator/NetworkMonitorCoordinator) | draft |
| 14 | [`docs/specs/2026-07-02-Spec-Singbox-Support-v1.0.md`](./specs/2026-07-02-Spec-Singbox-Support-v1.0.md) | **Sing-box Support Implementation** — Add sing-box as alternative proxy core to Xray; config fields, UI integration, outbound format mapping, REST/API differences | draft |
| 15 | [`docs/specs/2026-07-03-Spec-Echconfiglist-DualFormat-Support-v1.0.md`](./specs/2026-07-03-Spec-Echconfiglist-DualFormat-Support-v1.0.md) | **echconfiglist 双格式解析** — 区分 Base64 ECH 配置与 DNS URL 格式，Base64 → ech.config 数组，DNS URL → ech.enabled:true | ✅ completed |
| 16 | [`docs/specs/2026-07-06-Spec-RegionDetection-v1.0.md`](./specs/2026-07-06-Spec-RegionDetection-v1.0.md) | **代理地域检测 Phase 1** — RegionDetector 类 TLD/域名后缀/备注关键词检测，Profileitem region 字段，DB 列数保护 | ✅ completed |
| 17 | [`docs/specs/2026-07-07-Spec-RegionDetection-Phase2-v2.0.md`](./specs/2026-07-07-Spec-RegionDetection-Phase2-v2.0.md) | **代理地域检测 Phase 2** — 去重阶段地域富化（Deduplicator），DB 迁移（Region 列），ProxyListPanel Region 列显示 | draft |
| 18 | [`docs/specs/2026-07-09-Spec-DnsCache-v1.0.md`](./specs/2026-07-09-Spec-DnsCache-v1.0.md) | **DNS 缓存解析模块** — DnsCache 类，静态 resolve()，getaddrinfo IPv4 + 惰性 WSAStartup + 互斥锁保护的 unordered_map 缓存，解析失败空字符串缓存（防重试风暴） | ✅ completed |
| 19 | [`docs/specs/2026-07-15-Spec-GitTagVersioning-v1.0.md`](./specs/2026-07-15-Spec-GitTagVersioning-v1.0.md) | **Git Tag Versioning** — 以 Git tag 为编译/发布版本唯一来源，`cmake/GetGitVersion.cmake` 检测 tag → `configure_file()` 生成 `include/version.h` → About 窗口消费 | draft |
| 20 | [`docs/specs/2026-07-24-Spec-SubscriptionWritePerformance-v1.0.md`](./specs/2026-07-24-Spec-SubscriptionWritePerformance-v1.0.md) | **订阅写入性能优化** — Pragma 调优(2-5x)、批量 INSERT + 内存哈希去重(10-50x)、复合索引 + 分阶段去重(50-100x)，4 阶段实施方案 | draft |
| 21 | [`docs/specs/2026-07-28-Spec-BatchTestingEfficiency-v1.0.md`](./specs/2026-07-28-Spec-BatchTestingEfficiency-v1.0.md) | **批量测试效率优化** — gRPC 直连替代子进程 API、去冗余 removeOutbound、睡眠减量、批量 DB 写入、动态 Worker 数，预估 8.5× 提速；**A-F 六阶段全部实现**，实测墙钟快 2.23×（见 §9 报告 #9） | completed |
| 22 | [`docs/specs/2026-08-05-Spec-DnsCache-Permanent-v1.0.md`](./specs/2026-08-05-Spec-DnsCache-Permanent-v1.0.md) | **DNS 解析结果永久缓存** — DnsShareCache 程序级共享（CURLSH + CURLOPT_DNS_CACHE_TIMEOUT=-1，7 模块复用，进程生命周期）+ NetworkMonitor dnsCache_ 整体删除（只写不读死代码）；NetworkMonitorTest 11 用例通过 | ✅ completed |
| 23 | [`docs/specs/2026-08-05-Spec-ImportProxyValidation-v1.0.md`](./specs/2026-08-05-Spec-ImportProxyValidation-v1.0.md) | **导入订阅稽核：去除无法正确生成配置的代理** — utils::isPrintableAscii 提取统一 + isValidProxy 增加 Security/Id 可打印 ASCII 稽核（与去重阶段判定一致）；trojan/hy2 不误杀 | ✅ completed |
| 24 | [`docs/specs/2026-08-11-Spec-ProfileExMessage-v1.0.md`](./specs/2026-08-11-Spec-ProfileExMessage-v1.0.md) | **ProfileExItem message 双时间戳** — message 仅写入两类信息：测试成功时间 + 代理启动时间（恒含 '+'，任一侧缺失时保留占位）；测试成功/启动写入时仅替换自身一侧、保留另一侧（严格 19 字符时间戳校验，旧值 OK/FAILED/curlMsg/NOT_TESTED 整体替换）；测试失败不再覆盖 message；导入初始 message 置空；formatTestMessage/formatStartupMessage/updateStartupTime 实现 + ProfileExItemDAOTest 单测 | ✅ completed |
| 25 | [`docs/specs/2026-08-12-Spec-ProfileExMessage-Sort-v1.0.md`](./specs/2026-08-12-Spec-ProfileExMessage-Sort-v1.0.md) | **ProfileExItem message 字段排序（最近活跃时间）** — Message 列排序键 = 测试时间与启动时间中较新者（`ProfileExItemDAO::messageActiveTime`/`compareMessage` 静态方法，严格 19 字符校验，空/遗留值排最后）；`ProxyListModel::Compare` COL_MESSAGE 接入 | ✅ completed |
| 26 | [`docs/specs/2026-08-12-Spec-LogPanel-OpenLogButton-v1.0.md`](./specs/2026-08-12-Spec-LogPanel-OpenLogButton-v1.0.md) | **LogPanel 工具栏新增「打开日志」按钮** — Clear 后新增按钮一键打开当前日志文件（`Logger::getFilePath()` + ShellExecuteA/wxLaunchDefaultApplication）；事件表由 wxID_ANY 改为具体 ID（消除新增按钮后 Clear 被重复触发陷阱）；移除状态栏整条 `wxEVT_LEFT_DCLICK` 双击绑定（消除 field2/3 误触发），`onStatusBarDClick`/`logFilePath_` 删除；Field1 日志文件名显示保留；构建 341/341 | ✅ completed |
| 27 | [`docs/specs/2026-08-12-Spec-LogPanel-Statistics-v1.0.md`](./specs/2026-08-12-Spec-LogPanel-Statistics-v1.0.md) | **LogPanel 清空按钮文案调整 + 异步日志统计弹窗** — Clear 文本改「清空日志窗口」；新增「日志统计」按钮异步解析当前日志文件（`LogStatistics.h`/`LogStatistics.cpp` 纯 std 解析器 + std::thread + wxQueueEvent 投递 `LogStatisticsEvent`），弹窗输出 TRACE/DEBUG/INFO/REPORT/WARN/ERROR 六级别计数，WARN/ERROR 额外输出原因（消息原文）及次数（降序）；工具栏顺序=清空日志窗口→日志统计→打开日志；析构 join 防悬垂；LogStatisticsTest 8 用例；构建 348/348、ctest 24/24 | ✅ completed |
| 28 | [`docs/specs/2026-08-13-Spec-ProxyScoring-History-v1.0.md`](./specs/2026-08-13-Spec-ProxyScoring-History-v1.0.md) | **代理评分引入历史服务记录**（v1.3，已吸收 R1-R6 评审修订）— 明细表 `proxy_runtime_history`（一次启动一行，ended_at/exit_code/duration_ms 退出时回填）+ `ProfileExItem` 3 聚合列（start_count/total_runtime_ms/crash_count，写时同步同事务）；`ProcessInspector` 进程枚举+command line 比对（R1 启动前查重重复配置弹窗退出）；**退出检测事件驱动（R5）**：`ProcessExitListener` 每受管进程一个专用等待线程阻塞 `WaitForSingleObject(handle,INFINITE)`（进程句柄即系统级终止通知，零延迟零轮询），finalize 时一次性 command line 检查区分正常退出/崩溃/外部重启接管；**外部重启自动接管新进程（R6 定案）**：旧会话正常收尾 + 枚举新 pid → `OpenProcess` → `insertStart` 新会话 → 重启等待线程继续监控，接管失败放弃下次重走全流程；R2 启动后 curl 经 socks 端口连通性验证，通过才 `insertStart` 入表，不连通弹窗询问关闭/弃管（弃管=不入表不监控）；`ProxyScorer` 纯函数三因子评分（**速度20%+稳定性30%+历史服务50%**，历史=启动次数 kicker + 贝叶斯平滑正常退出率 + 平均运行时长，冷启动中性 50 分、40:60 归一化不惩罚）；D1 仅 StandaloneProxy / D2 明细+聚合 / D3 ProcessExitListener+自动接管已确认 | 🔄 completed |
| 29 | [`docs/specs/2026-08-18-Spec-ProxyListRefresh-v2.0.md`](./specs/2026-08-18-Spec-ProxyListRefresh-v2.0.md) | **ProxyListPanel 定时刷新异步化（评价列 UI 卡死修复）** — 根因：`refreshHistoryPeriodic()` 每 2 秒 UI 线程全表查询 5.3 万行 + computeBatch + 全量 rebuildMaps + 第二次 DB 查询；方案：后台线程独立 sqlite3 连接（WAL/busy_timeout/cache_size）查 running 会话时长 → `wxQueueEvent` 回 UI 线程 `RunningDurationsLoadedEvent` → `setRunningDurations` 增量更新 + `notifyHistoryChanged`（仅 start_count>0 行）+ `Refresh`；防抖 `refreshInFlight_`（std::atomic<bool>）防线程堆积；定时器 2000→3000ms；网络监控 onNetMonTimer 非根因保持 2s 不动；refreshResults() 全量路径保留（测试完成事件低频触发） | ✅ completed |
| 30 | [`docs/specs/2026-08-20-Spec-RuntimeHistory-PidMatching-v1.0.md`](./specs/2026-08-20-Spec-RuntimeHistory-PidMatching-v1.0.md) | **ProxyRuntimeHistory 会话匹配引入 PID 因子 + 系统进程启动时间** — ①`findInProgressHistory` 判断因子升级为 `indexId + pid + started_at` 三因子（杜绝崩溃重启/外部新启进程误复用旧 in-progress 会话：新 pid → 不命中 → insertStart starts+1；同 pid+同 started_at → 复用 starts 不 +1）；②`proxy_runtime_history` 新增 `pid` 列（幂等 ALTER，旧行 pid=NULL 不再被复用，审计保留）；③`started_at` 改为从系统获取进程启动时间（`ProcessInspector::processCreationTime`：GetProcessTimes creation time → FILETIME→`yyyy-MM-dd HH:mm:ss`，新增 `ProcessInfo.creationTime` 字段 + `enumerateByName` 填充）；④`insertStart` 签名增加 `int64_t pid`（3 调用点：正常启动 pi.dwProcessId / R6 takeover procs[i].pid / 悬垂纳管 procs[i].pid 均可获取）；⑤配套修正心跳基准：`ProcessExitListener::watch` 增加 `baselineElapsedMs`（= 纳管时刻−进程启动时间），duration_ms 含纳管前已运行时间，finalizeStop 聚合更真实；含 TDD 测试计划（DAO 三因子 5 用例 + ProcessInspector 时间工具 + baseline 心跳）与风险表 | draft |
| 31 | [`docs/specs/2026-08-21-Spec-ConfigDialog-RemoveIpinfoToken-v1.0.md`](./specs/2026-08-21-Spec-ConfigDialog-RemoveIpinfoToken-v1.0.md) | **配置编辑窗口移除 ipinfo.io Token 项（含 Phase 2 死代码清理）** — Region 解析经两次迁移（ipinfo.io→ip-api.com→ipwho.is）后 `config.test.ipinfo_token` 已无任何代码消费者；Phase 1=ConfigDialog.cpp 移除 3 处 UI 触点；Phase 2（用户确认）=删除 ConfigReader.h 字段声明 + TestConfigParser 解析分支 + ConfigJsonSerializer 写回 + ConfigDialog 过时注释——旧 config.json 中该键静默忽略，下次保存后从文件消失；验证=构建 0 error + ctest 31/31 ×2 轮 + grep 无功能引用残留 | ✅ completed |
| 32 | [`docs/specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.0.md`](./specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.0.md) | **独立代理监控弹窗（StandaloneMonitorDialog）** — 新增非模态弹窗实时展示正在 watch 的代理进程会话数据（索引ID/起始时间/运行时长(分)/监听端口(新增)/PID），2 秒 wxTimer 刷新，Proxy 菜单「独立代理监控…Ctrl+M」入口（懒创建+隐藏复用）；数据链=`getWatchedStandaloneMonitors()` 锁内快照 standaloneProxies_ (running&&managed) ∪ DAO 新方法 `getInProgressSessions()`（ProxyRuntimeHistoryItem 补 pid 字段）按 historyId 联结、时长实时计算 now−started_at；**顺带修复**：①正常启动路径未回填 runtimeHistoryId/watchKey/startedAt 至 map 致 stopStandaloneProxy 跳过 unwatch（watcher 泄漏）；②悬垂纳管进程 socksPort 恒 0——新增 `readStandaloneInboundPort()` 解析 config 文件 inbound 端口回填 + 纳管/停止日志追加端口（吸收前次"监控日志加端口"请求）；测试=DAO getInProgressSessions 3 用例 + 构建 0 error + ctest 31/31；**v1.1 修订**（`docs/specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.1.md`）：IndexId 后新增 Host 列显示 ProfileItem.Address——StandaloneMonitorRow 加 host 字段，getWatchedStandaloneMonitors Pass2 经 `ProfileitemDAO::getByIndexId()` 参数化逐行填充（watched ≤个位数，无 IN 拼接转义风险），对话框 COL_HOST=1 插入渲染（空值"-"）、窗口 720→860；host 不入历史表（运行时状态与生命周期审计分离，Address 权威源在 ProfileItem）；验证=构建 0 error + ctest 回归通过 | ✅ completed |

---

## 8. 实施计划 (docs/plans/)

> 共 60 个计划文件，按日期倒序排列。
> 状态标记: ✅ completed ｜ 🔄 in_progress ｜ 📝 draft｜ ❌ blocked
> 全局跟踪: [`docs/plans/project-plans-tracker.md`](./plans/project-plans-tracker.md)

### 8.1 元规范 & 跟踪 (Meta)

| # | 文件 | 说明 |
|---|------|------|
| 1 | [`docs/plans/DEV-PROCESS.md`](./plans/DEV-PROCESS.md) | 开发流程 7 步规范（见 §3） |
| 2 | [`docs/plans/project-plans-tracker.md`](./plans/project-plans-tracker.md) | 全局进度总表 — UI 实现、`.kilo/` 迁移、`.kilo/plans/` 归档 |
| 3 | [`docs/plans/feature-status.md`](./plans/feature-status.md) | 功能状态矩阵（见 §5） |
| 4 | [`docs/plans/impl-items-6-7-10.md`](./plans/impl-items-6-7-10.md) | Item 6/7/10 详细实施清单（见 §5） |

### 8.2 待执行/草稿计划 (docs/plans/ all ongoing + draft)

| 日期 | 编号 | 文件 | 类型 | 说明 |
|------|------|------|------|------|
| 2026-08-10 | fix | [`2026-08-10-Plan-ImportProxyValidation-v1.0.md`](./plans/2026-08-10-Plan-ImportProxyValidation-v1.0.md) | fix draft | **订阅导入数据污染治理** — 私网/回环地址、非法 UUID（vmess/vless）、旧流式 SS 加密三层拦截（导入闸门 isValidProxy 扩展 / 存量清洗 deduplicateConfigErrorPhase 扩展 / 测试前 preGenerateConfigs 显式预过滤）+ utils 3 纯函数（isPublicAddress 含 172.16-31 数值判定防误伤 Cloudflare、isValidUuid 8-4-4-4-12、isSupportedSsCipher AEAD+2022-blake3 白名单）；依据报告 §4.7/§6 P1 |
| 2026-08-03 | refactor | [`2026-08-03-Plan-CodeAudit-Optimization-Implementation-v1.0.md`](./plans/2026-08-03-Plan-CodeAudit-Optimization-Implementation-v1.0.md) | refactor ✅ | **代码审查优化实施计划** — 26 任务 5 阶段：Phase1 崩溃/UB(A1-A6) → Phase2 正确性(B+E1/E2) → Phase3 性能(D) → Phase4 低危收尾(C/E) → Phase5 规范清理(auto)；每任务含位置/变更/验收，A3 测试期望同步修正。Phase 1-4 全部落地：A1-A6 / B1-B14 / C1-C9 / D1-D8 / E1-E14（C2/C5/C8/C9/E3/E4/E8/E9b 验证为已实现），21/21 ctest 通过 | ✅ completed |
| 2026-06-26 |  | [`./superpowers/plans/2026-06-26-batch-write-transaction-fix.md`](./superpowers/plans/2026-06-26-batch-write-transaction-fix.md) | fix | **批量写入事务安全修复** — syncDatabases()/deleteBySubId()/updateTestResultBatch() 事务包装，防止部分写入导致数据不一致 |
|| 2026-06-18 |  | [`./plans/2026-06-18-Plan-Decomposition-Five-Phase-Implementation-v1.0.md`](./plans/2026-06-18-Plan-Decomposition-Five-Phase-Implementation-v1.0.md) | refactor ✅ | **五阶段职责分解实施计划** — 70+ 新文件，逐任务拆解 ConfigReader/ShareLink/ConfigGenerator/ProxyBatchTester/AppController — ✅ ALL COMPLETED (2026-06-22) |
| 2026-06-15 |  | [`./superpowers/plans/2026-06-15-NetworkMonitor-batch-network-abort-on-disconnect.md`](./superpowers/plans/2026-06-15-NetworkMonitor-batch-network-abort-on-disconnect.md) | feat draft | **NetworkMonitor 批量网络中断中止** — 12 tasks: NetworkMonitor class, ConfigReader, ProxyBatchTester/SubitemUpdaterV2/AutoTaskManager/AppController integration, MainFrame UI, unit tests |
| 2026-06-16 |  | [`./superpowers/plans/2026-06-16-Plan-NetworkMonitor-enabled-toggle-v1.0.md`](./superpowers/plans/2026-06-16-Plan-NetworkMonitor-enabled-toggle-v1.0.md) | feat draft | **NetworkMonitor 启用开关** — 5 tasks: enabled flag, IsConnected logic, AppController, MainFrame status bar, ConfigDialog |
| In Progress | 14-002 | [`2026-05-14-002-ui-implementation-plan.md`](./plans/2026-05-14-002-ui-implementation-plan.md) | feat | **UI 完整实现** — 11 个 U# 单元含 wxWidgets 集成、事件系统、AppController、MainFrame、6 个面板(XAUI)、AUI 布局；分 4 Phase 执行 |
| 2026-05-18 |  | [`2026-05-18-001-fix-null-testpanel-pointer.md`](./plans/2026-05-18-001-fix-null-testpanel-pointer.md) | fix draft | MainFrame initPanels() 构造顺序修复 (nullptr 前置) |
| 2026-05-18 |  | [`2026-05-18-003-unify-proxy-testing-ui-flow.md`](./plans/2026-05-18-003-unify-proxy-testing-ui-flow.md) | plan draft | 统一右键菜单与工具栏代理测试的 UI 流程 |
| 2026-05-18 |  | [`2026-05-18-004-impl-find-proxy-async.md`](./plans/2026-05-18-004-impl-find-proxy-async.md) | plan draft | **异步 Find Proxy + Delay 列刷新** P1/P2/P3 |
| 2026-05-19 |  | [`2026-05-19-ui-enhancements-sort-find-link.md`](./plans/2026-05-19-ui-enhancements-sort-find-link.md) | feat draft | 列排序 + 查找单个代理 + 订阅联动 |
| 2026-05-14 |  | [`2026-05-14-004-gh-mcp-list-top-repos.md`](./plans/2026-05-14-004-gh-mcp-list-top-repos.md) | plan draft | GitHub MCP 查询 Top 5 仓库 (read-only) |
| 2026-05-20 | | [`2026-05-20-proxy-ui-enhancement-plan.md`](./plans/2026-05-20-proxy-ui-enhancement-plan.md) | plan draft | Add consecutive_failures/message columns, ProxyDetailPanel, search box |
| 2026-05-20 | | [`2026-05-20-ui-layout-redesign.md`](./plans/2026-05-20-ui-layout-redesign.md) | plan draft | Redesign main layout to 3-column + bottom log panel |
| 2026-05-21 | | [`2026-05-21-search-dbpath-implementation.md`](./plans/2026-05-21-search-dbpath-implementation.md) | plan draft | Real-time search, clear button, auto-load first subscription, DB path label |
| 2026-05-22 | | [`2026-05-22-custom-toolbar-icons.md`](./plans/2026-05-22-custom-toolbar-icons.md) | plan draft | Replace stock wxArtProvider icons with custom 48px PNG toolbar icons |
| 2026-05-27 | | [`2026-05-27-disable-conflict-ui-during-ops.md`](./plans/2026-05-27-disable-conflict-ui-during-ops.md) | plan draft | Disable conflicting toolbar/menu buttons during long-running ops |
| 2026-05-27 | | [`2026-05-27-reject-reentry.md`](./plans/2026-05-27-reject-reentry.md) | plan draft | Prevent reentry of long-running operations with reject message |
| 2026-05-27 | | [`2026-05-27-sync-toolbar-button.md`](./plans/2026-05-27-sync-toolbar-button.md) | plan draft | Add Sync toolbar button for database sync |
| 2026-05-28 | | [`2026-05-28-non-reentrant-update-cancel.md`](./plans/2026-05-28-non-reentrant-update-cancel.md) | plan draft | Add cancel support for subscription update + dynamic cancel button |
| 2026-05-29 | | [`2026-05-29-proxylist-virtual-model-implementation.md`](./plans/2026-05-29-proxylist-virtual-model-implementation.md) | plan draft | Replace wxDataViewListStore with wxDataViewIndexListModel |
| 2026-05-29 | | [`2026-05-29-subscription-panel-ui-chinese-i18n.md`](./plans/2026-05-29-subscription-panel-ui-chinese-i18n.md) | plan draft | Localize subscription panel to Chinese, remove buttons, widen DB path |
| 2026-05-13 | P4 | [`2026-05-13-P4-extract-sqlite-helper-and-transaction-plan.md`](./plans/2026-05-13-P4-extract-sqlite-helper-and-transaction-plan.md) | refactor draft | SQLite Helper + Transaction RAII 萃取 |

### 8.3 已完成计划 (Completed)

| 日期 | 编号 | 文件 | 类型 | 说明 |
|------|------|------|------|------|
| 2026-08-20 | deploy | [`2026-08-20-Plan-Deploy-Bugfix-SpiderY-ConfigDialog-v1.0.md`](./plans/2026-08-20-Plan-Deploy-Bugfix-SpiderY-ConfigDialog-v1.0.md) | deploy ✅ | **部署方案：SpiderY nil-panic 修复 + ConfigDialog 全路径显示修复** — 两个纯源码修复的构建/部署/验证/回滚指南；影响面 XrayApi(gRPC 注入)+ProxyBatchTester(U4 skip 移除)+ConfigDialog(UI 显示层)；wxWidgets 3.3.3 版本锁定；无 DB schema/配置迁移 | ✅ completed |
| 2026-06-11 |  | [`2026-06-11-Plan-Stability-Hardening-v1.0.md`](./plans/2026-06-11-Plan-Stability-Hardening-v1.0.md) | feat ✅ | **稳定性加固** — CMake ASAN/UBSan, MiniDumpWriteDump, cppcheck, gcov |
| 2026-05-18 |  | [`2026-05-18-002-fix-empty-test-results.md`](./plans/2026-05-18-002-fix-empty-test-results.md) | fix ✅ | TestPanel onProgress 提前返回 问题 (ProxyTestProgressEvent isCompleted 修复) |
| 2026-05-14 | 14-001 | [`2026-05-14-001-dedup-filter-invalid-proxies-plan.md`](./plans/2026-05-14-001-dedup-filter-invalid-proxies-plan.md) | fix ✅ | 去重功能过滤无效代理 (REALITY 缺失 key/sni, 脏 Network 字段) |
| 2026-05-13 | P0-P1 | [`2026-05-13-P0-fix-silent-sqlite3-exec-and-finalize-plan.md`](./plans/2026-05-13-P0-fix-silent-sqlite3-exec-and-finalize-plan.md) | fix ✅ | 修复静默 sqlite3_exec + finalize 工具断开 Bug |
| 2026-05-13 | P1 | [`2026-05-13-P1-extract-sqlite3-open-helper-and-exec-template-plan.md`](./plans/2026-05-13-P1-extract-sqlite3-open-helper-and-exec-template-plan.md) | refactor ✅ | 萃取 sqlite3_open 辅助函数 |
| 2026-05-13 | P3 | [`2026-05-13-P3-extract-log-helper-functions-plan.md`](./plans/2026-05-13-P3-extract-log-helper-functions-plan.md) | refactor ✅ | 萃取 logInfo/logError 辅助函数 |
| 2026-05-13 | P5 | [`2026-05-13-P5-fix-loglevel-ordering-report-below-err-plan.md`](./plans/2026-05-13-P5-fix-loglevel-ordering-report-below-err-plan.md) | fix ✅ | LogLevel ordering 修复 (REPORT 必须放在 ERR 之后) |
| 2026-05-12 |  | [`2026-05-12-007-log-config-sql-queries-plan.md`](./plans/2026-05-12-007-log-config-sql-queries-plan.md) | feat ✅ | Debug SQL 查询日志 |
| 2026-05-12 |  | [`2026-05-12-009-remove-dead-log-level-field-plan.md`](./plans/2026-05-12-009-remove-dead-log-level-field-plan.md) | refactor ✅ | 移除死字段 LogLevel 及相关代码 |
| 2026-05-11 | 001 | [`2026-05-11-001-agents-md-improvement-plan.md`](./plans/2026-05-11-001-agents-md-improvement-plan.md) | feat ✅ | AGENTS.md 核心规则完善 |
| 2026-05-11 | 003 | [`2026-05-11-003-fix-sync-config-path-errors-plan.md`](./plans/2026-05-11-003-fix-sync-config-path-errors-plan.md) | fix ✅ | 同步数据库 path 错误修复 |
| 2026-05-11 | 004 | [`2026-05-11-004-subitem-updater-v2-optimization-plan.md`](./plans/2026-05-11-004-subitem-updater-v2-optimization-plan.md) | fix ✅ | SubitemUpdaterV2 优化 |
| 2026-05-11 | 005 | [`2026-05-11-005-config-transaction-batch-plan.md`](./plans/2026-05-11-005-config-transaction-batch-plan.md) | feat ✅ | 配置事务批处理 |
| 2026-05-11 | 006 | [`2026-05-11-006-sharelinkparser-plan.md`](./plans/2026-05-11-006-sharelinkparser-plan.md) | feat ✅ | ShareLink Parser |
| 2026-05-10 |  | [`2026-05-10-merge-log-level-standardization-plan.md`](./plans/2026-05-10-merge-log-level-standardization-plan.md) | refactor ✅ | 日志等级标准化 (合并计划) |
| 2026-05-09 | 006–013 | [`006`](./plans/2026-05-09-006-fix-sub-update-interval-not-skipping-plan.md) `[007]` `[008]` `[009]` `[010]` `[011]` `[012]` `[013]` | fix ✅ | Bug 修复批处理 (订阅跳过、REPORT 等级、sync SQL、Notification 配置、NUL Header、LogFile Close、LogLevel 不一致、SQL Placeholder) |
| 2026-05-08 | 001–005 | [`001`](./plans/2026-05-08-001-fix-sync-logger-plan.md) `[002]` `[003]` `[004]` `[005]` | fix ✅ | 同步 Logger 修复、CLI 参数处理修复、错误等级标准化、死字段清理、间隔检查恢复 |
| 2026-05-07 | 001–002 | [`001`](./plans/2026-05-07-001-refactor-curl-raii-wrapper-plan.md) `[002]` | refactor ✅ | cURL RAII Wrapper 萃取、去重优化重写 |
| 2026-05-13 | 001–002 | [`001`](./plans/2026-05-13-001-adjust-sub-update-log-level-to-report-plan.md) `[002]` | refactor ✅ | 订阅/Profile 进度插入日志 INFO→REPORT |
| 2026-04-24 |  | [`2026-04-24-proxy-sync.md`](./plans/2026-04-24-proxy-sync.md) | feat ✅ | 代理同步 (主副本 DB) |
| 2026-04-17 |  | [`2026-04-17-subscription-url-proxy-fallback-plan.md`](./plans/2026-04-17-subscription-url-proxy-fallback-plan.md) | feat ✅ | 订阅 URL proxy fallback |
| 2026-04-13 |  | [`2026-04-13-module-refactoring-plan.md`](./plans/2026-04-13-module-refactoring-plan.md) | refactor ✅ | 模块重构计划 |
| 2026-06-01 |  | [`2026-06-01-cli-gui-binary-split.md`](./plans/2026-06-01-cli-gui-binary-split.md) | refactor ✅ | CLI/GUI 二进制拆分 — 解决 WIN32_EXECUTABLE 导致 cmd.exe 非阻塞问题 |
| 2026-06-15 |  | [`2026-06-15-Spec-AutoTask-v1.0.md`](./plans/2026-06-15-Spec-AutoTask-v1.0.md) | feat ✅ | **AutoTask 自动化管道** — 可配置的订阅更新→批量测试→去重→同步→导出自动化工作流，含命令行集成 (-AT/-RS/-CA) |

### 8.4 `.kilo/plans/` 已迁移归档

> 以下计划原存于 `.kilo/plans/`，已按规范命名并迁移至 `docs/` 相应位置。
> `.kilo/plans/` 原始文件名（+ `architecture.md`/`context.md`）保留作归档。

| 原文件名 | 规范命名 | 目标位置 | 状态 |
|---------|---------|---------|------|
| `1776931315746-sunny-rocket.md` | `2026-04-23-sharelink-export-repair-report.md` | `docs/reports/` | ✅ completed |
| `1776415141516-jolly-mountain.md` | `2026-04-23-001-fix-sql-delay-filter.md` | `docs/plans/` | ✅ completed |
| `1778814717912-tidy-meadow.md` | `2026-05-18-002-fix-empty-test-results.md` | `docs/plans/` | ✅ completed |
| `1779070922736-shiny-comet.md` | `2026-05-18-001-fix-null-testpanel-pointer.md` | `docs/plans/` | 📝 draft |
| `1779072849830-eager-moon.md` | `2026-05-18-003-unify-proxy-testing-ui-flow.md` | `docs/plans/` | 📝 draft |
| `1779073276415-silent-harbor.md` | `2026-05-18-004-impl-find-proxy-async.md` | `docs/plans/` | 📝 draft |
| `1776914861549-gentle-panda.md` | `2026-05-14-003-proxy-validation-tool-architecture.md` | `docs/plans/` | 📝 参考文档 |
| `1776215451920-nimble-wolf.md` | `2026-05-14-004-gh-mcp-list-top-repos.md` | `docs/plans/` | 📝 draft |

### 8.5 `docs/superpowers/plans/` 已迁移归档

> 以下计划原存于 `docs/superpowers/plans/`，已于 2026-06-01 按统一命名规范迁移至 `docs/plans/`。
> `docs/superpowers/plans/` 目录已删除。

| 文件 | Type | 说明 | 状态 |
|------|------|------|------|
| [2026-05-20-proxy-ui-enhancement-plan.md](./plans/2026-05-20-proxy-ui-enhancement-plan.md) | plan draft | Add columns, detail panel, search | 📝 draft |
| [2026-05-20-ui-layout-redesign.md](./plans/2026-05-20-ui-layout-redesign.md) | plan draft | 3-column layout redesign | 📝 draft |
| [2026-05-21-search-dbpath-implementation.md](./plans/2026-05-21-search-dbpath-implementation.md) | plan draft | Search + DB path panel | 📝 draft |
| [2026-05-22-custom-toolbar-icons.md](./plans/2026-05-22-custom-toolbar-icons.md) | plan draft | Custom toolbar icons | 📝 draft |
| [2026-05-27-disable-conflict-ui-during-ops.md](./plans/2026-05-27-disable-conflict-ui-during-ops.md) | plan draft | Disable UI during ops | 📝 draft |
| [2026-05-27-reject-reentry.md](./plans/2026-05-27-reject-reentry.md) | plan draft | Reentry protection | 📝 draft |
| [2026-05-27-sync-toolbar-button.md](./plans/2026-05-27-sync-toolbar-button.md) | plan draft | Sync toolbar button | 📝 draft |
| [2026-05-28-non-reentrant-update-cancel.md](./plans/2026-05-28-non-reentrant-update-cancel.md) | plan draft | Non-reentrant update cancel | 📝 draft |
| [2026-05-29-proxylist-virtual-model-implementation.md](./plans/2026-05-29-proxylist-virtual-model-implementation.md) | plan draft | ProxyList virtual model | 📝 draft |
| [2026-05-29-subscription-panel-ui-chinese-i18n.md](./plans/2026-05-29-subscription-panel-ui-chinese-i18n.md) | plan draft | Chinese i18n + cleanup | 📝 draft |

---

## 9. 分析报告

 | # | 文件 | 说明 | 大小 |
 |---|------|------|------|
 | 1 | [`docs/reports/2026-04-23-sharelink-export-repair-report.md`](./reports/2026-04-23-sharelink-export-repair-report.md) | **ShareLink 导出修复报告** — 5 项关键问题修复并验证(路径参数/ECH 编码/TLS 参数/VMess Payload/Path Query)，v2rayN 格式 100% 兼容 | 4.3 KB |
 | 2 | [`docs/reports/db-schema-analysis-20260514.md`](./reports/db-schema-analysis-20260514.md) | **数据库 Schema 分析** — ProfileItem/Subscription/ProfileExItem 表结构、字段映射、类型不一致排查 | 10.8 KB |
 | 3 | [`docs/reports/error-report_20260514.md`](./reports/error-report_20260514.md) | **错误报告** — 2026-05-14 运行时错误汇总与分析 | 4.5 KB |
 | 4 | [`docs/reports/2026-05-19-single-proxy-test-fix-report.md`](./reports/2026-05-19-single-proxy-test-fix-report.md) | **单代理测试修复报告** — runWithIndexId, Delay 刷新, 事件流程修复技术细节 | 5.6 KB |
 | 5 | [`docs/reports/2026-05-19-ui-close-hang-fix-report.md`](./reports/2026-05-19-ui-close-hang-fix-report.md) | **UI 关闭挂起修复报告** — AppController/XrayInstance 析构竞态条件 + 进程句柄 BUG | 3.2 KB |
| 6 | [`docs/reports/2026-06-01-diag-log-level-adjustment.md`](./reports/2026-06-01-diag-log-level-adjustment.md) | **诊断日志级别调整报告** — 10 处 `[DIAG]` 日志从 `INFO`/`DEBUG` 降为 `TRACE` | 0.7 KB |
| 7 | [`docs/reports/2026-06-11-Debug-Tools-Assessment.md`](./reports/2026-06-11-Debug-Tools-Assessment.md) | **C++ 调试工具评估报告** — 日志系统、单元测试现状分析，AddressSanitizer/静态分析缺失评估 | 2.1 KB |
| 8 | [`docs/reports/2026-06-26-Report-NetworkMonitorProbeFlow.md`](./reports/2026-06-26-Report-NetworkMonitorProbeFlow.md) | **网络探测逻辑分析报告** — NetworkMonitor + ProxyBatchTester 生产者-消费者双线程协作机制、ThreadLoop 探测流程、状态机、通信机制 | 5.2 KB |
| 9 | [`docs/reports/2026-07-31-Report-XrayApi-gRPC-vs-Subprocess-Performance-v1.0.md`](./reports/2026-07-31-Report-XrayApi-gRPC-vs-Subprocess-Performance-v1.0.md) | **实测量化报告: XrayApi gRPC vs subprocess 性能** — 同一订阅 178 代理实测 gRPC 路径 88s vs subprocess 196s（快 2.23 倍）；每代理注入开销 ~6s→~130ms（约 45 倍理论提升）；机制对比、日志实证、证据留存 | — |
| 10 | [`docs/reports/2026-08-03-Report-CodeAudit-Optimization-v1.0.md`](./reports/2026-08-03-Report-CodeAudit-Optimization-v1.0.md) | **代码审查优化方案报告** — XrayApi/ProxyBatchTester/支撑模块三路并行审计：6 HIGH（detach UAF、gRPC status 不解析、protobuf 编码错位、XrayInstance 孤儿进程、PortManager 无锁、lastResult_ 数据竞争）+ 14 MED 正确性 + 14 性能 + 14 错误处理空洞；含分四批实施顺序与测试期望同步提示 | — |
| 11 | [`docs/reports/2026-08-04-Report-CurlEasyHandle-Audit-Fixes-v1.0.md`](./reports/2026-08-04-Report-CurlEasyHandle-Audit-Fixes-v1.0.md) | **CurlEasyHandle 审计修复报告** — C6 移动语义补全 cancelFlag_/secondaryCancelFlag_、E3 超时下限（仅 0/负值兜底 1000ms，修复误伤 NetworkMonitor 快速探测）、E4c 二级取消标志支持；21/21 测试通过 | — |
| 12 | [`docs/reports/2026-08-05-Report-v148-vs-Current-ProxyTest-v1.0.md`](./reports/2026-08-05-Report-v148-vs-Current-ProxyTest-v1.0.md) | **v1.4.8 vs 当前版批量测试对比报告** — 同一订阅 5544（207 代理）同配置公平对比：OK 73 vs 69（16 个差异节点全为 5s 超时边界抖动）；两版均零 parse error/WARN/ERR；测试窗口 4:41→2:05（快 2.24×，DnsCache/预生成/E7/端口管理优化成果）；FAIL 均记录 Delay=-1 无遗漏 | — |
| 13 | [`docs/reports/2026-08-07-Report-GitMasterSkill-Install-v1.0.md`](./reports/2026-08-07-Report-GitMasterSkill-Install-v1.0.md) | **git-master 技能安装报告** — AGENTS.md §6.2 路由表引用 `skill(name="git-master")` 但环境缺失；`npx skills add` 因 GitHub 直连不通失败（镜像 ghfast.top/gh-proxy.com 403、gitclone.com 404、github.moeyy.xyz 不存在）；配置 git 全局代理 `socks5://127.0.0.1:10808` 后手动 `git clone --depth 1` josiahsiegel/claude-plugin-marketplace（563 安装量）→ 复制 `plugins\git-master\skills\git-master\` 至全局技能目录（10 文件：SKILL.md 149 行 + references/ 9 文档）；当前会话不可用需重启 Kilo 生效；附代理移除命令 | — |
| 14 | [`docs/reports/2026-08-20-Report-ProxyListModel-StatsColumns-v1.0.md`](./reports/2026-08-20-Report-ProxyListModel-StatsColumns-v1.0.md) | **ProxyListPanel Starts/Runtime/Health 三列计算逻辑提取报告（v1.1 修订）** — ①Starts=`startCountMap_[id]=ex.start_count`（rebuildMaps L68，不含运行中会话；v1.1 补充：PID 三因子匹配后崩溃重启新实例 +1 / 同实例纳管复用不 +1）；②Runtime=历史 `runtimeMap_[id]`（total_runtime_ms>0 才覆盖，保留非零）+ 实时 `runningDurations_[id]`（setRunningDurations 整体替换幂等），显示 `%lld:%02lld`（M:SS；v1.1 补充：duration_ms 含纳管前 baseline，从进程系统启动时间起算）；③Health=贝叶斯平滑 `(stable+1)/(start_count+2)`（start_count==0 → 0.0，2026-08-20 修复）+ 运行加成 `min(running/30min,1)*0.3` 封顶 1.0（仅 start_count>0 授予），显示 `%.3f`；排序：Starts/Health 与显示一致，**Runtime 排序仅比较 runtimeMap_ 历史部分（不含运行中时长，与显示不一致，候选改进，v1.1 确认仍存在）**；含数据流图与相关历史修复（2026-08-18 幂等/异步化、2026-08-20 冷启动归零、2026-08-20 PID 匹配+baseline） | — |

---

## 9.1 Bug 修复记录

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/bugfix/2026-05-28-sql-error-console-output-fix.md`](./bugfix/2026-05-28-sql-error-console-output-fix.md) | **SQL 错误输出到控制台修复** — `std::cerr` 改为 `Logger::write()`，确保 GUI 模式下错误显示在日志窗口 | 2.5 KB |
| 2 | [`docs/bugfix/2026-05-28-cancel-sub-update-proxyfinder-phase.md`](./bugfix/2026-05-28-cancel-sub-update-proxyfinder-phase.md) | **订阅更新 ProxyFinder 阶段取消无法立即终止修复** — SubitemUpdaterV2::getProxyPorts() 未传递取消标志给 ProxyFinder | 2.8 KB |
| 3 | [`docs/bugfix/2026-05-20-ctrl-c-exit.md`](./bugfix/2026-05-20-ctrl-c-exit.md) | **Ctrl+C 信号无法正常退出修复** — AppController 析构函数添加 5 秒超时 + detach 机制 | 4.2 KB |
| 4 | [`docs/bugfix/2026-06-01-batch-test-cmd-window-flicker.md`](./bugfix/2026-06-01-batch-test-cmd-window-flicker.md) | **批量测试 CMD 窗口闪烀修复** — `_popen()` 替换为 `CreateProcessA(CREATE_NO_WINDOW)` + Win32 管道 | 2.1 KB |
| 5 | [`docs/bugfix/2026-06-01-cli-ctrl-c-interrupt-fix.md`](./bugfix/2026-06-01-cli-ctrl-c-interrupt-fix.md) | **CLI Ctrl+C 中断修复** — 添加取消标志、传递给 ProxyBatchTester、析构超时 + detach 机制 | 2.1 KB |
| 6 | [`docs/bugfix/2026-06-03-subscription-update-timeout-and-updatetime-fix.md`](./bugfix/2026-06-03-subscription-update-timeout-and-updatetime-fix.md) | **订阅更新 curl timeout 和 UpdateTime 修复** — curl 连接超时配置、UpdateTime 仅在成功时更新 | 94 lines |
| 7 | [`docs/bugfix/2026-06-03-config-dialog-improvements.md`](./bugfix/2026-06-03-config-dialog-improvements.md) | **配置编辑改进** — 禁止运行中切换数据库、日志级别保存应用 | 57 lines |
| 8 | [`docs/bugfix/2026-06-11-Bugfix-ConfigGenerator-NetworkFallback-v1.0.md`](./bugfix/2026-06-11-Bugfix-ConfigGenerator-NetworkFallback-v1.0.md) | **无效网络兜底与 splithttp→xhttp 回退** — ConfigGenerator invalid network 改为回退 tcp，并补充 splithttp 遗留值映射 | — |
| 9 | [`docs/bugfix/2026-06-11-Bugfix-ProxyListPanel-RefreshFreeze-v1.0.md`](./bugfix/2026-06-11-Bugfix-ProxyListPanel-RefreshFreeze-v1.0.md) | **大代理集批量测试后 UI 冻结修复** — 逐行 ValueChanged 风暴改为单次 listCtrl_->Refresh() | — |
| 10 | [`docs/bugfix/2026-06-11-Bugfix-ProxyBatchTester-ZeroProxyEarlyReturn-v1.0.md`](./bugfix/2026-06-11-Bugfix-ProxyBatchTester-ZeroProxyEarlyReturn-v1.0.md) | **零代理批量测试 printSummary/stopAll 缺失修复** — run()/runWithSubId() 空代理分支补齐汇总与 Xray 清理 | — |
| 11 | [`docs/bugfix/2026-06-12-Bugfix-ProxyBatchTester-Worker0-JoinTimeout-v1.0.md`](./bugfix/2026-06-12-Bugfix-ProxyBatchTester-Worker0-JoinTimeout-v1.0.md) | **Worker join 超时修复 v3** — 动态 join timeout (v1 7s 不足，v2 ping 轮询过重) | 64 lines |
| 12 | [`docs/bugfix/2026-06-15-Bugfix-AutoTask-SubitemUpdater-logging-and-pipeline.md`](./bugfix/2026-06-15-Bugfix-AutoTask-SubitemUpdater-logging-and-pipeline.md) | **AutoTask: invisible diagnostics under file_level:ERROR** — SubitemUpdaterV2 all-skipped return false, log levels too low, step name aliases missing | — |
| 13 | [`docs/bugfix/2026-06-16-Bugfix-Review-AutoTask-CLI-StateFile-v1.0.md`](./bugfix/2026-06-16-Bugfix-Review-AutoTask-CLI-StateFile-v1.0.md) | **AutoTask CLI cancel, state file path, config SQL threshold** — CLI Ctrl+C silent ignore, double-nested worker/worker/ path, SQL 99% proxy pool reduction, unused icons | 177 lines |
| 14 | [`docs/bugfix/2026-06-16-Bugfix-Dedup-NestedTransaction-v1.0.md`](./bugfix/2026-06-16-Bugfix-Dedup-NestedTransaction-v1.0.md) | **Nested SQLite transaction error in deduplicateConfigErrorPhase** — deduplicate() starts transaction, deleteByIndexId() tried nested BEGIN, added deleteByIndexIdNoTx() | 24 lines |
| 15 | [`docs/bugfix/2026-06-25-Bugfix-NetworkDisconnect-CancelChain-v1.0.md`](./bugfix/2026-06-25-Bugfix-NetworkDisconnect-CancelChain-v1.0.md) | **Network disconnect does not stop batch testing** — NetworkMonitor detects LOST but has no way to signal ProxyBatchTester; added setCancelOnDisconnect() to wire cancellation chain | 65 lines |
| 16 | [`docs/bugfix/2026-06-26-Bugfix-SubitemUpdaterV2-MissingNetMon-v1.0.md`](./bugfix/2026-06-26-Bugfix-SubitemUpdaterV2-MissingNetMon-v1.0.md) | **SubitemUpdaterV2 未传入 netMon_ 导致网络断开时静默绕过** — doUpdateSubscription/doUpdateAllSubscriptions 构建 SubitemUpdaterV2 时未传第7参数 netMon，6处 IsConnected() 空指针短路永不触发 | — |
| 17 | [`docs/bugfix/2026-07-01-Bugfix-isPortAvailable-WildcardListener-v1.0.md`](./bugfix/2026-07-01-Bugfix-isPortAvailable-WildcardListener-v1.0.md) | **isPortAvailable() 对 0.0.0.0 通配监听者返回假阳性** — bind(INADDR_LOOPBACK) 在 Windows 下与已有 0.0.0.0:port 监听共存，改为非阻塞 connect(127.0.0.1:port) + select(200ms) | — |
| 18 | [`docs/bugfix/2026-07-01-Bugfix-Sync-Subscription-EnabledDefault-v1.0.md`](./bugfix/2026-07-01-Bugfix-Sync-Subscription-EnabledDefault-v1.0.md) | **Sync 目标库新订阅 enabled 默认值为 0** — migrateSubscription() 直接复制源库 enabled 状态，目标库新订阅应默认禁用，与 Importer 行为一致 | — |
| 19 | [`docs/bugfix/2026-07-01-Bugfix-DoubleClick-V1.0.md`](./bugfix/2026-07-01-Bugfix-DoubleClick-V1.0.md) | **SubscriptionPanel / ProxyListPanel 双击无反应** — MSW wxDataViewMainWindow 缺失 CS_DBLCLKS，使用 selection-change-based 双击检测绕过限制 | — |
| 20 | [`docs/bugfix/2026-07-20-Bugfix-CurlGlobalInit-GUI-v1.0.md`](./bugfix/2026-07-20-Bugfix-CurlGlobalInit-GUI-v1.0.md) | **GUI 入口缺失 curl_global_init() 导致右键解析地区崩溃** — main_gui.cpp 未调用 curl_global_init，线程中 curl_easy_perform 访问违例 | — |
| 21 | [`docs/bugfix/2026-07-28-Bugfix-DatabaseIndex-applyPragmas-v1.0.md`](./bugfix/2026-07-28-Bugfix-DatabaseIndex-applyPragmas-v1.0.md) | **新建数据库缺失 idx_profile_dedup 索引** — main_gui/UIApp detached/main_cli 三处数据库打开路径未调用 applyPragmas()，统一接入 DatabaseConnectionService | — |
| 22 | [`docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md`](./bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md) | **gRPC addOutboundDirect 修复 (2026-07-29/31)** — (1) TypedMessage field 2→3 (proxy_settings) (2) gRPC path CommandService→HandlerService (3) outbound JSON parse 回退：`{"outbounds":[...]}` 代码误读 rootObj["outbound"]，提取 parseOutboundJson 与 protocol→typeUrl 映射 (4) [07-31] protocol 原值被忽略(恒0)改用配置原值 (5) [07-31] SenderConfig 字段号 1/3→2/4 (6) [07-31] encodeStreamConfig port int64 被 is_uint64 守卫静默丢弃已修复；XrayApiDirectTest 54/54 pass | — |
| 23 | [`docs/bugfix/2026-08-04-Bugfix-SubscriptionParser-GarbageSS-v1.0.md`](./bugfix/2026-08-04-Bugfix-SubscriptionParser-GarbageSS-v1.0.md) | **订阅解析乱码 Shadowsocks 节点致批量测试全失败** — Argh94-ShadowSocks 订阅 malformed ss:// 链接 + decodeBase64 非 base64 字符误解码为索引 0 → 29811 个乱码代理（Security/Id 二进制垃圾）；修复 decodeBase64 跳过非法字符 + ss:// method/password 可打印 ASCII 校验丢弃节点 + 代理配置错误日志降级 DEBUG（去完整 outbound JSON 防密码泄露，ensureWinsock 保留 ERR）；21/21 通过 | — |
| 24 | [`docs/bugfix/2026-08-04-Bugfix-XrayApi-XHTTP-SplitHTTP-Mapping-v1.0.md`](./bugfix/2026-08-04-Bugfix-XrayApi-XHTTP-SplitHTTP-Mapping-v1.0.md) | **gRPC xhttp 传输编码与 Xray v26.2.4 兼容修复** — 新构建首次真正编码 xhttp（protocolName="xhttp" + `xray.transport.internet.xhttp.Config`）但 v26.2.4 已移除 xhttp 协议、该消息类型未注册 → AddOutbound `proto: not found`（旧构建是静默退化裸 TCP 的假成功，非代码回退）；修复 network=="xhttp" 统一按 splithttp 编码（protocolName="splithttp" + `xray.transport.internet.splithttp.Config`，与 v26.2.4 JSON 适配器 xhttp→splithttp 重映射一致）；单元测试 3/3 + 真实 v26.2.4 端到端 xhttp OK=true 验证 | — |
| 25 | [`docs/bugfix/2026-08-05-Bugfix-NetworkMonitor-StaleObj-LayoutMismatch-v1.0.md`](./bugfix/2026-08-05-Bugfix-NetworkMonitor-StaleObj-LayoutMismatch-v1.0.md) | **NetworkMonitor 陈旧 .obj 布局不匹配（ODR 违例）致启动期崩溃** — NetworkMonitor.h 8/4 新增 dnsCache_/dnsCacheMutex_ 成员后仅 NetworkMonitor.cpp 重编，AppController.cpp.obj 仍为 9:05 旧布局（.ninja_deps 依赖缺失），监控线程首轮 dnsCache_.find() 访问未初始化 _M_buckets 读地址 0 确定性崩溃；修复=全量重建 399/399（顺带解决 clang 抢占编译器/CMAKE_CXX_FLAGS_DEBUG 污染/oldnames 空库/windres -O coff 四环境障碍）+ 同步 worker；验证 NetworkMonitorTest 8.07s Passed + GUI 前台启动存活 + 日志完整走到 Constructor end | — |
| 26 | [`docs/bugfix/2026-08-05-Bugfix-ProxyBatchTester-PreGenParseError-v1.0.md`](./bugfix/2026-08-05-Bugfix-ProxyBatchTester-PreGenParseError-v1.0.md) | **批量测试 PreGen 失败节点触发 boost.json 解析错误刷屏与卡顿** — preGenerateConfigs 对垃圾节点抛异常后 push 空 outbound_json，worker 对空串调 addOutboundDirect → parseOutboundJson 用 boost.json 解析空串抛 `syntax error ... parse_string`，3 次重试×5s 超时放大卡顿（12:31 日志 13:25 起刷屏）；修复 E7 skip：pregenFailedFlags_ 标记 + worker 循环入口跳过（不再进入 addOutboundDirect，杜绝 boost.json 空串解析）+ DIAG 防御输出坏 config 前 80 字节 hex；新增 PreGenFailedSkipTest 7 用例；21/21 通过 | — |
| 27 | [`docs/bugfix/2026-08-06-Bugfix-NetworkMonitorProbeCounterOverflow-v1.0.md`](./bugfix/2026-08-06-Bugfix-NetworkMonitorProbeCounterOverflow-v1.0.md) | **NetworkMonitor 探针计数器溢出致误触发** — ThreadLoop 第一、三分支无条件 store maxProbes_ 至 consecutiveFailures_（即使 probeEnabled_=false），1 次网络失败即可达到探针阈值并误判断网；修复=两处条件化写入（仅当 probeEnabled_ 且 fails≥maxProbes_ 时 store）；NetworkMonitorTest 11/11 通过 | — |
| 28 | [`docs/bugfix/2026-08-06-Bugfix-Logger-GUI-ConsoleLevel-v1.0.md`](./bugfix/2026-08-06-Bugfix-Logger-GUI-ConsoleLevel-v1.0.md) | **GUI 入口 Logger 启动级别遗漏 console_level 配置 + LogPanel 筛选器硬编码 INFO** — main_gui.cpp 补全 setConsoleLevel 调用与 CLI 一致；LogPanel 构造函数 setSelection(2)/minLevel_=INFO 硬编码，不读 config.json log_console_level；修复=新增 setInitialLogLevel(LogLevel) 方法，MainFrame 构造后同步配置值至下拉框；21/21 测试通过 | — |
| 29 | [`docs/bugfix/2026-08-06-Bugfix-MainFrame-StatusBar-LogFile-v1.0.md`](./bugfix/2026-08-06-Bugfix-MainFrame-StatusBar-LogFile-v1.0.md) | **状态栏日志文件名不可见** — repositionNetMonPanel() 用 GetFieldRect(1) 将不透明网络状态面板 netMonPanel_ 铺满 Field1，遮挡已正确设置的日志文件名；修复=状态栏 3→4 字段（Field1 日志文件名 / Field2 网络面板 / Field3 数据库路径）+ SetStatusWidths 显式宽度 + SendSizeEvent + 双击事件改 Bind 到状态栏；UIA 验证 Field1=ui_*.log 正常显示；附带教训：跨进程 SendMessage 带指针调 SB_GETPARTS/SB_GETTEXT 是 GUI 0xC000041D 崩溃源，验证必须用 UIA；[08-06 补充] SetStatusWidths 末字段改 -1（可变宽度）修复状态栏右侧未铺满（固定总和 920 < 逻辑宽 1280，UIA 验证 Field3 右缘=1280 铺满） | — |
| 30 | [`docs/bugfix/2026-08-07-Bugfix-GuiLogFileLevel-v1.0.md`](./bugfix/2026-08-07-Bugfix-GuiLogFileLevel-v1.0.md) | **GUI 启动日志未按 config.json file_level 过滤写入文件** — main_gui.cpp Logger::init 用默认级别（file=DEBUG）先于配置加载，INFO "gui entry" 与 ConfigReader::load 内部 DEBUG SQL 日志在 file_level=ERROR 时仍写入文件；修复=Logger::init 前用 ConfigFileStore+ConfigJsonParser+LogConfigParser 预解析 log 段并以真实级别初始化（含缺文件/坏 JSON 回退默认级别），load 后 L70-73 应用级别保留为幂等兜底；21/21 测试通过 | — |
| 31 | [`docs/bugfix/2026-08-07-Bugfix-NetworkMonitor-ProbeLogLevel-Warn-v1.0.md`](./bugfix/2026-08-07-Bugfix-NetworkMonitor-ProbeLogLevel-Warn-v1.0.md) | **网络监控探测错误详情日志级别过低** — CheckURLWithDnsFlag 内探测失败详情（curl 错误 DEBUG / DNS 错误 TRACE / 非 2xx-3xx 状态码 DEBUG）在生产 file_level=ERROR、console_level=WARN 下完全不可见；修复=3 处全部提升为 WARN（probe failed / DNS error / http_code）；ThreadLoop 连接状态机日志（LOST/RESTORED 等）保持 ERR 不变；NetworkMonitorTest 不受影响（仅断言 LOST 的 ERR 级别） | — |
| 32 | [`docs/bugfix/2026-08-07-Bugfix-SubitemUpdater-SkipLogLevel-v1.0.md`](./bugfix/2026-08-07-Bugfix-SubitemUpdater-SkipLogLevel-v1.0.md) | **订阅更新跳过提示误报 ERROR** — updateAll() 在全部订阅因更新间隔被跳过时（`successCount<=0 && attemptedCount==0`，随后 return true 属正常完成）以 ERR 输出 "All subscriptions skipped by update interval - nothing to update"，非错误却被记入 ERROR 告警；修复=该消息级别 ERR→REPORT；相邻 "All subscriptions failed to update - check network connectivity"（真正失败）保持 ERR；无测试引用该消息文本；历史文档 2026-06-15-Bugfix-AutoTask-SubitemUpdater-logging-and-pipeline.md L37 记载原 ERR 级别不改写 | — |
| 33 | [`docs/bugfix/2026-08-07-Bugfix-GUI-AboutBuildTime-LogLevelSync-v1.0.md`](./bugfix/2026-08-07-Bugfix-GUI-AboutBuildTime-LogLevelSync-v1.0.md) | **About 窗口编译时间不准确 + 配置窗口 console 日志级别不同步 LogPanel** — CMakeLists add_custom_command 依赖 build.ninja 致 version.h 仅在 configure 时生成（增量构建 APP_BUILD_TIME 停留上次 configure 时刻），改为 add_custom_target(update_version_h ALL) 每次构建重生成；onMenuConfig 保存回调仅更新 Logger 全局级别未同步 logPanel_ 界面过滤，补 setInitialLogLevel(cfg.log_console_level)；21/21 测试通过（CurlEasyHandleTest/NetworkMonitorTest 2 项网络环境性失败除外） | 112 lines |
| 34 | [`docs/bugfix/2026-08-07-Bugfix-MainFrame-StatusBar-Field0-v1.0.md`](./bugfix/2026-08-07-Bugfix-MainFrame-StatusBar-Field0-v1.0.md) | **状态栏 Field0 被菜单帮助文本机制清空且点击关闭不恢复** — wxFrameBase::DoGiveHelp 在菜单打开时以空 help 串清空 Field0（framecmn.cpp L577 官方开关：m_statusBarPane<0 直接返回）；ESC 关闭可恢复但点击关闭不恢复（MSWFindMenuFromHMENU 仅匹配右键弹出菜单，菜单栏下拉关闭不触发恢复）；修复=构造 initStatusBar 后 SetStatusBarPane(-1) 根治 + doTestAllProxies/doTestSingleProxy 启动时经 wxQueueEvent 发送 "Testing all proxies..." / "Testing proxy <id>..." + 移除 dumpStatusBarGeometry 诊断；菜单全链路 SB_GETTEXTLENGTH 实测 lens=[5,22,0,61] 恒定 | — |
| 35 | [`docs/bugfix/2026-08-07-Xray-instances-are-not-automatically-restarted.md`](./bugfix/2026-08-07-Xray-instances-are-not-automatically-restarted.md) | **Xray 实例崩溃/挂死后自动恢复 + gRPC 错误日志提升 ERROR** — (Fix1) isRunning() 改为 GetExitCodeProcess 实时查询 + running_ 同步；(Fix2) 连续两次 grpcConnect 失败触发 connectFailureHook，worker 停止重试并调用 evaluateInstanceHealth(apiPort)：崩溃/挂死统一走 释放资源(stop+freePort 双端口+erase) → 用原配置(same socksPort→same config file) 两阶段锁外拉起，relaunch 失败释放端口待下次 start() 重建；(Fix3) XrayApi addOutbound/removeOutbound/removeOutboundDirect/addOutboundDirect/listOutboundsDirect FAILED 与 worker XRAY_ERROR 日志 DEBUG→ERR；(Fix4) 在途代理重拉成功后重试一次（injectError 在 remove 前保存修复空错误消息；evaluateInstanceHealth 返回 true=找到且重拉成功，false=未找到或重拉失败）；构建 302/302，ctest 21/21（XrayApiDirectTest 94 用例含 2 新生命周期测试） | — |
| 36 | [`docs/bugfix/2026-08-10-Bugfix-XrayInstance-StderrCapture-v1.0.md`](./bugfix/2026-08-10-Bugfix-XrayInstance-StderrCapture-v1.0.md) | **Xray 实例崩溃观测增强（stderr/退出码捕获）** — gRPC 注入期间 xray 进程死亡但 panic 堆栈不可见（CreateProcessW 无 STARTF_USESTDHANDLES、无日志文件，stderr 全丢弃）；修复=start() 重定向 stdout/stderr 至 `<configDir>/xray_stdout_<socksPort>.log`/`xray_stderr_<socksPort>.log`（CREATE_ALWAYS+FILE_SHARE_READ|WRITE，SA.bInheritHandle=TRUE，bInheritHandles=TRUE，hStdInput=NUL）+ 死亡时 ERR 日志记录退出码与 stderr 尾部 4KB（readFileTail 按完整行截断）；新增 lastExitCode()；测试=编译型假 xray 辅助程序 fake_xray_helper（文件名含 startup 则退出码 7，否则 ~8s 后退出码 42）替代 .bat（CreateProcessW 不执行 .bat 体），XrayInstanceStderrTest 2/2 通过（运行期死亡 42+stderr tail PANIC / 启动期死亡 7） | — |
| 37 | [`docs/bugfix/2026-08-10-Bugfix-SplitHTTP-nil-request-Panic-v1.0.md`](./bugfix/2026-08-10-Bugfix-SplitHTTP-nil-request-Panic-v1.0.md) | **SplitHTTP nil-request Panic 应用侧防御** — Xray-core splithttp OpenStream `req, _ := http.NewRequestWithContext(...)` 丢弃错误：host/path 含控制/空白字符（≤0x20 或 0x7F）时 url.Parse 失败返回 (nil,err)，FillStreamRequest 首行 `request.Header = ...` 对 nil 解引用 panic（commit d2758a0 验证）；修复=addOutboundDirect 注入前调用 validateSplitHTTPSettings 校验 streamSettings（network∈{splithttp,xhttp} 时 host/path 禁控制字符），非法代理 ERR 日志拒绝注入不再崩溃；空 host 放行（Xray dest.Address 兜底）；新增 7 例单测；上游补丁（OpenStream 处理 err）列入后续 | — |
| 38 | [`docs/bugfix/2026-08-10-Bugfix-RelaunchedInstanceContinuesQueue-v1.0.md`](./bugfix/2026-08-10-Bugfix-RelaunchedInstanceContinuesQueue-v1.0.md) | **重启实例继续服务后续队列项** — 实例崩溃经 connectFailureHook→evaluateInstanceHealth 成功重启后，worker 循环顶 `if (instanceDead.load()) break;` 且 instanceDead 从不复位 → worker 立即退出，有效并发永久减一（重启实例仅服务在途代理一次重试）；修复=循环顶改为 `if (instanceDead && relaunchedOk) { WARN 日志; instanceDead.store(false); } else if (instanceDead) break;` —— 重启成功则复位标志继续主循环服务剩余队列项，仅重拉失败（relaunchedOk==false）才退出；本地 XrayApi 每次 grpcConnect 新建 socket 天然兼容同 apiPort 重启实例；反复崩溃自愈（每次 hook 重写两标志）；L202 重试循环/在途重试块/evaluateInstanceHealth 均不改；静态审查五类路径通过，构建 309/309、ctest 22/22 | — |
| 39 | [`docs/bugfix/2026-08-10-Analysis-InjectionError-ConfigRootCause-v1.0.md`](./bugfix/2026-08-10-Analysis-InjectionError-ConfigRootCause-v1.0.md) | **addOutboundDirect 注入错误配置问题根因分析** — `bin/log/ui_20260810_113211.log` 批次③ 2715 条（Success 131/Failed 2266）失败归类：A1 id 非法 UUID（hex invalid byte `U+002D`/`U+0054`，L26-40/86-90/96-100）；A2 双重 URL 编码明文 ID（`%2550...%256D`=Parashahonam-299 / `Telegram-%2540Cooonfig...`，L133-142）；B SS Unsupported cipher（最高频，Xray v26 仅 AEAD+2022-blake3，L41-85/91-95/128-132/185-214）；C1 11:49:15 REALITY+gRPC panic（reality.go:273 越界，socks=10000/api=10080）；C2 12:04:08 splithttp nil-request panic（config.go:296←client.go:63←dialer.go:481，socks=10001/api=10081，修复前 build）；D 11:49:13 重启窗口内 `xray api ado` 回退失败（exitCode=1 空输出）；结论=注入编码无缺陷，根因为数据污染（A+B 绝大多数）+ Xray-core 运行时缺陷（C）；建议 P1 导入期 UUID/cipher 校验+存量清洗、P2 REALITY+gRPC 调度层规避、P3 回退重试；§4.7 补充私网地址专项调查：indexid 5323085616270219902（vmess/127.0.0.1:1080/伪 UUID baacbac-…，Argh94-vmess 订阅）上游 Vmess.txt base64 解码逐字段命中（`"add":"127.0.0.1","port":1080,"id":"baacbac-…"` 完全一致）+127.0.0.53×10（88888888 测试 UUID）+127.1.1.127（ts-sf/fly 再导入）；全库精确统计私网/回环 42 条（0.014%，10 订阅源）；源码 14 处 127.0.0.1 全为本地绑定 → 私网 100% 来自上游；P1-c 增补地址校验（拒 127/8、10/8、172.16/12、192.168/16、169.254/16、0/8、::1、fc00::/7、fe80::/10 及 0 开头垃圾域名；RFC1918 必须按第二八位组数值判定） | — |
| 40 | [`docs/bugfix/2026-08-11-Bugfix-Reality-SpiderY-nil-Panic-v1.0.md`](./bugfix/2026-08-11-Bugfix-Reality-SpiderY-nil-Panic-v1.0.md) | **Reality SpiderY nil 越界 panic 致 Xray 实例崩溃** — gRPC 注入路径 `encodeRealitySettings` 手工编码 protobuf 遗漏 field 27（spider_y）→ xray-core 接收端 `SpiderY` 为 nil 切片；REALITY 握手验证失败（批量测试死节点）进入 spider goroutine 访问 `config.SpiderY[0..9]`（reality.go:232/238/262/267/273）越界 panic 整个 xray 进程崩溃；修复=新增 `encodePackedInt64Field`（packed varint）+ `parseSpiderYParams`（镜像 transport_internet.go p/c/t/i/r→槽 0/2/4/6/8）+ `encodeRealitySettings` 无条件编码 10 元素全 0 spider_y（与 JSON 路径初始状态一致，全 0 时 RandBetween(0,0)=0 安全）；3 新回归测试 + 修复 EncodeStreamConfigRealitySecurity 期望字节；**U4（ProxyBatchTester REALITY+gRPC 降级 skip）已随修复移除**；上游补丁（reality.go `len(SpiderY)<10` 守卫）列入后续；构建 327/327、ctest 22/22 | — |
| 41 | [`docs/bugfix/2026-08-11-Bugfix-SsCipher-Whitelist-v1.0.md`](./bugfix/2026-08-11-Bugfix-SsCipher-Whitelist-v1.0.md) | **SS 加密算法白名单扩充 chacha20-ietf-poly1305（含日志分析报告）** — `bin/worker/log/ui_20260811_112935.log` CONFIG_ERROR 统计 **14146 条**（invalid UUID format 6927 / unsupported SS cipher 6020 / private/invalid address 1199，与 `Phase ConfigError deleted: 14146` 完全一致）；判定依据 `Deduplicator::deduplicateConfigErrorPhase` 5 重检查（checkRequired / isPrintableAscii / isPublicAddress / isValidUuid / isSupportedSsCipher，任一失败 WARN 级 CONFIG_ERROR + 删除）；其中 **6018 条 `chacha20-ietf-poly1305`**（SS 标准 IETF AEAD，非废弃 CFB）被白名单误拒；修复=`isSupportedSsCipher` 白名单新增第 9 项（src/Utils.cpp line 441），Deduplicator Phase 4/6 与 SubitemUpdaterV2 导入闸门自动生效；`aes-256-cfb`（废弃流模式）维持拒收；构建 327/327、ctest 22/22 | — |
| 42 | [`docs/bugfix/2026-08-12-Bugfix-IsPublicAddress-NumericDomain-v1.0.md`](./bugfix/2026-08-12-Bugfix-IsPublicAddress-NumericDomain-v1.0.md) | **isPublicAddress 误判数字开头合法域名（private/invalid address 误杀）** — `bin/worker/log/ui_20260812_084452.log` 导入闸门 `SKIP: private/invalid address` 162 条中约 **80 条为合法域名被误判**（`8103.shomaparvazroyadetonnist-bypassishere.lat`、`*.outline-vpn.cloud`、超长数字子域 `*.poki-pakipon.ir`、`1744156156.tencentapp.cn`、`*.webnama.com.tr` 等，真实私网 79 条如 127.*/0.0.0.0 拦截正确）；根因=`isPublicAddress` 含点字符串边解析边短路，数字段 >255 时在未遇字母前立即 `return false`，合法 FQDN 永远走不到 `isPublicDomain` 回退分支；附带=超长数字串 32 位整型溢出 UB；修复=src/Utils.cpp `isPublicAddress`/`isPublicDomain` 判定重写 + src/SubitemUpdaterV2.cpp 导入闸门同步；tests/test_utils.cpp 新增 IsPublicAddressTest 14 用例，UtilsTest 39/39 通过 | — |
| 43 | [`docs/bugfix/2026-08-12-Bugfix-LogPanel-Statistics-Reopen-v1.0.md`](./bugfix/2026-08-12-Bugfix-LogPanel-Statistics-Reopen-v1.0.md) | **LogPanel 日志统计按钮关闭弹窗后无法再次打开** — `std::thread::joinable()` 在线程执行完毕后仍返回 true 直至显式 join/detach；`onLogStatistics` 防叠加守卫 `if (statsThread_.joinable()) return;` 首次点击后 statsThread_ 恒 joinable，`onLogStatisticsResult` 弹窗后从不回收线程 → 后续点击被守卫拦截无法再启动；修复=结果回调开头 `if (statsThread_.joinable()) { statsThread_.join(); }` 回收已结束线程（赋值与回收均在 UI 线程事件上下文无竞争，解析进行中重复点击仍防叠加，析构 join 兜底不变）；构建 347/347、ctest 24/24（CurlEasyHandleTest 网络偶发失败重跑通过） | — |
| 44 | [`docs/bugfix/2026-08-12-Bugfix-LogPanel-Statistics-ReasonAggregation-v1.0.md`](./bugfix/2026-08-12-Bugfix-LogPanel-Statistics-ReasonAggregation-v1.0.md) | **日志统计原因归一化聚合（去具体 host）** — 统计弹窗 WARN/ERROR 原因 521 类全因按含 host 消息原文聚合拆类（如 `SKIP: 140.248.186.45:443 - invalid UUID format` 每 host 一类）；根因=`parseLogFile` 按 `++warnAgg[message]`/`++errorAgg[message]` 原文聚合；修复=src/LogStatistics.cpp 新增 `extractReason` 归一化聚合键（`SKIP: <host>[:port] - <reason>` → `<reason>` 去首尾空白；无 ` - ` 分隔符/非 SKIP 前缀保留原文，如 `Failed to parse vmess: not JSON`、`unsupported SS cipher: 'aes-256-cfb'`）+ WARN/ERROR 分支聚合键改 `warnAgg[extractReason(message)]`/`errorAgg[extractReason(message)]` + ReasonCount 注释同步；标题行格式 `===== WARN 原因明细 (N 类) =====` 已具备；LogStatisticsTest 10 用例、构建 347/347、ctest 24/24 | — |
| 45 | [`docs/bugfix/2026-08-12-Bugfix-Ss2022-XrayApi-Protobuf-v1.0.md`](./bugfix/2026-08-12-Bugfix-Ss2022-XrayApi-Protobuf-v1.0.md) | **shadowsocks-2022 出站 gRPC 注入编码修复** — `jsonConfigToProtobuf` 将 SS2022（`2022-blake3-*`）按 legacy `xray.proxy.shadowsocks.ClientConfig`/`Account` 编码（cipher 枚举无 2022 值）→ Xray 拒 `unsupported shadowsocks cipher method`、SS2022 节点批量测试全失败；修复（Method A 编码层）= `isShadowsocks2022` 特征标志（`method.rfind("2022-",0)==0`，parseOutboundJson 分支方法检测）+ 新编码分支按 `xray.proxy.shadowsocks_2022.ClientConfig` 扁平结构（field1 address / field2 port / field3 method / field4 key=password 原样，无 Account 嵌套，method/key 空回退子进程）+ `supportedTypeUrls[]` 注册 + 空配置回退检查；无公共 API 变更、legacy SS 路径不变；7 新单测、构建 347/347、ctest 24/24 | — |
| 46 | [`docs/bugfix/2026-08-13-Bugfix-ProfileExItemDAO-NestedTransaction-v1.0.md`](./bugfix/2026-08-13-Bugfix-ProfileExItemDAO-NestedTransaction-v1.0.md) | **updateTestResultBatch 外层事务嵌套导致 SQLite 报错** — `updateTestResultBatch()` 对每个子批次无判断直接 `BEGIN TRANSACTION`，当调用方已在同一 `sqlite3*` 连接上开启外层事务时触发 `cannot start a transaction within a transaction`；修复=调用 `sqlite3_get_autocommit(execDb)` 判断当前连接是否处于 autocommit，仅在外层无事务时才 `BEGIN`，且仅对自身开启的事务执行 `COMMIT`/`ROLLBACK`，安全参与外层事务；回归测试 `UpdateTestResultBatch_SucceedsInsideOuterTransaction` 覆盖外层 `BEGIN` → 调用 → 数据验证 → 外层 `ROLLBACK` 不残留；构建 347/347、ctest 24/24 | — |
| 47 | [`docs/bugfix/2026-08-17-Bugfix-ProxyConfigParser-WrongTypeWarning-v1.0.md`](./bugfix/2026-08-17-Bugfix-ProxyConfigParser-WrongTypeWarning-v1.0.md) | **合法 proxy 配置误报 "has wrong type" 伪警告** — `ProxyConfigParser.h:95` 将 `Logger::write("WARNING: config.proxy has wrong type", ...)` 误放在 `if (obj.contains("proxy") && obj.at("proxy").is_object())` 分支**内部**（正常解析字段后无条件执行），合法 proxy 对象也触发伪警告，且错误类型时消息缺 `(expected object), using default` 后缀（其余 9 个解析器均在 `else if` 分支且带后缀）；修复=警告移至 `else if (obj.contains("proxy"))` 分支并补全后缀；TDD 新增 2 回归测试（`ProxySectionValidObject_NoWrongTypeWarning` / `ProxySectionWrongType_UsesDefaultAndWarns`），RED→GREEN 验证，test_config_reader 36/36、ctest ConfigReaderTest 100% passed | — |
| 48 | [`docs/bugfix/2026-08-17-Bugfix-StandaloneProxy-WatchLifecycle-v1.0.md`](./bugfix/2026-08-17-Bugfix-StandaloneProxy-WatchLifecycle-v1.0.md) | **Standalone 代理 watch 生命周期完善（查重前置 / 心跳 / 悬垂纳管）** — ①查重前置：`ProxyListPanel::onStartProxy` 端口检查前调 `AppController::isStandaloneProxyRunning`（`standalone_<indexId>-xray.json` 同名 config 探测）拦截重复启动，`startStandaloneProxy` 内部查重提前至 profile 获取后；②watch 心跳：`ProcessExitListener` 新增 `HeartbeatFn` + watch 线程 30s 循环（WAIT_OBJECT_0 原退出逻辑 / WAIT_TIMEOUT 心跳 / WAIT_FAILED+!running break），`ProxyRuntimeHistoryDAO::touchHeartbeat` 定期刷新进行中会话 duration_ms（不改 ended_at，聚合仅 finalizeStop 累加）；③悬垂进程纳管：`ProcessInspector::extractConfigFileName`（命令行提取 standalone_*.json）+ DAO `findInProgressHistory`（复用未结束会话）+ `AppController::adoptDanglingStandaloneProxies`（MainFrame 构造后自动纳管 xray/sing-box 残留进程：历史会话 + watch + 心跳）；验证=98/98 构建 + 实机 PID 4084/5544 自动纳管 + taskkill 后 finalizeStop 自动收尾（ended_at/exit_code=1/duration_ms≈31s） | — |
| 49 | [`docs/bugfix/2026-08-18-Bugfix-Build-Deps-VcpkgMigration-v1.0.md`](./bugfix/2026-08-18-Bugfix-Build-Deps-VcpkgMigration-v1.0.md) | **构建依赖迁移 E:/vcpkg → D:/vcpkg + Boost 改用 vcpkg 管理 + wxWidgets 3.3.3 适配** — ①CMakeLists.txt 删除 `BOOST_ROOT`（D:/boost_1_88_0）与 boost include/link_directories，9 处 `libboost_json-mgw14-mt-x64-1_88.a` → `${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a`（vcpkg boost-json 独立端口，直连 GitHub 需代理安装）；②`copy_wx_dlls()` 列表更新为 3.3.3 命名（`wxbase333u_gcc_x64_*`，libzlibd1.dll→libzd.dll、libtiffd.dll→libtiffd-6.dll、新增 libwebp*.dll），`test_log_statistics_event` 追加复制；③ConfigDialog.cpp 4 处 `wxPG_FILE_DIALOG_TITLE`→`wxPG_DIALOG_TITLE`（3.3.3 移入 WXWIN_COMPATIBILITY_3_0 条件块）；④zlib 包缺陷需显式 `-DZLIB_LIBRARY=.../libzs.a`（zlibTargets 指向不存在的 libzlib.a）；⑤配置必须显式指定 gcc 编译器、覆盖 `CMAKE_CXX_FLAGS_DEBUG`（防 clang 抢占/-Xclang 注入）、`CMAKE_RC_FLAGS=-O coff`；验证=构建 464 目标 ✅、ctest 29/29 ✅、CLI -h ✅、compile_commands.json 无 E:/vcpkg/mgw14 残留 | — |
| 50 | [`docs/bugfix/2026-08-18-Bugfix-TestExe-Comctl32-Manifest-v1.0.md`](./bugfix/2026-08-18-Bugfix-TestExe-Comctl32-Manifest-v1.0.md) | **test_proxy_list_model.exe 启动 0xC0000139（comctl32 v6 manifest 缺失）** — wxcore 3.3.3 导入 comctl32 **v6-only** 导出（GetWindowSubclass/SetWindowSubclass/DefSubclassProc/RemoveWindowSubclass），`test_proxy_list_model.exe`/`minwxtest.exe` 无 manifest → 加载 system32 comctl32 **5.82**（无此 4 符号）→ STATUS_ENTRYPOINT_NOT_FOUND；validproxy 通过 `src/ui/icons.rc` 内嵌 `app.manifest`（Common-Controls 6.0.0.0）→ 加载 WinSxS 6.0 正常；修复=`tests/test_proxy_list_model.rc`（新建，`1 24 "src/ui/app.manifest"`）追加至目标源列表；排除假说：wxbase 670 符号全存在、系统 DLL 差集为 forwarded/解析误报、gtest/代码无关（minwxtest 仅 base+core 复现）；验证=manifest 嵌入确认 + ProxyListModelTest 4/4 + ctest **30/30 全绿**（此前 29/30） | — |
| 51 | [`docs/bugfix/2026-08-18-Bugfix-ProxyListRefresh-IdleRedraw-v1.0.md`](./bugfix/2026-08-18-Bugfix-ProxyListRefresh-IdleRedraw-v1.0.md) | **未启动代理评价列空转刷新 + Runtime 累加膨胀（双 bug 同源）** — ①Bug A：`onRunningDurationsLoaded` 收到**空 map**（无 running 会话）仍无条件 `notifyHistoryChanged()` + `listCtrl_->Refresh()` → 未启动代理时 UI 每 3 秒强制重绘；②Bug B：`getRunningDurations()` 返回**当前总时长绝对值**（heartbeat 累计），但 `setRunningDurations()` 用 `+=` **累加** → Runtime 列每 3 秒翻倍膨胀（5s→15s→30s，应恒为 total+当前时长）；修复=`ProxyListModel` 新增 `runningDurations_` 成员（与 runtimeMap_ total 分离），`setRunningDurations` 改**整体替换**语义并返回 `bool changed`（空/相同快照返回 false），`getRuntime()`/`COL_TOTAL_RUNTIME_MS` = `runtimeMap_[id] + runningDurations_[id]`（幂等），`clear()` 同步清空；`onRunningDurationsLoaded` 仅 `changed==true` 才 notify + Refresh；行为=无会话稳态跳过重绘/启动/心跳增长/结束均正确刷新；TDD 新增 2 回归测试（`RepeatedRefreshDoesNotAccumulateRuntime` / `EmptySnapshotReportsNoChange`）；验证=test_proxy_list_model 6/6 + ctest 29/30（NetworkMonitorTest.LoggingOnConnectionLost 为环境依赖失败：example.com 当前不可达，与本次无关） | — |
| 52 | [`docs/bugfix/2026-08-19-Bugfix-AppController-MutexDeadlock-wxMessageBox-v1.0.md`](./bugfix/2026-08-19-Bugfix-AppController-MutexDeadlock-wxMessageBox-v1.0.md) | **startStandaloneProxy wxMessageBox 同线程死锁致程序假死** — `startStandaloneProxy` 函数入口 `lock_guard<mutex>` 持有 `standaloneMutex_` 整个函数，连通性失败后调 `wxMessageBox` 弹模态对话框；Win32 `MessageBox` 消息循环分发 `WM_TIMER` → `onProxyMonTimer` → `getRunningStandaloneCount()` 尝试重锁同一 `std::mutex` → 同线程 undefined behavior（Windows/MSVC 确定性挂死）；修复=`lock_guard` 改 `unique_lock`，两处 `wxMessageBox` 前 `lock.unlock()`、返回后 `lock.lock()`；附带修复：新增 `ProfileExItemDAO exDao_` + `updateStartupTime` 调用（spec §3.4 #3 补全）、NotifyFn 补 `running=false`（状态栏计数）、事件 message 改 `utils::getCurrentTimestamp()`；验证=构建 0 error + ctest 30/30 | — |
| 53 | [`docs/bugfix/2026-08-19-Bugfix-ConfigDialog-FilePropertyShowFullPath-v1.0.md`](./bugfix/2026-08-19-Bugfix-ConfigDialog-FilePropertyShowFullPath-v1.0.md) | **ConfigDialog wxFileProperty 只显示文件名（wxWidgets 3.3.3 wxPG_FILE_SHOW_FULL_PATH 失效）** — `config.json` 中所有文件路径字段（`database.path`、`proxy.xray_executable`、`proxy.xray_asset_dir` 等 8 处）均存储**绝对路径**，但 ConfigDialog 中所有 `wxFileProperty` 字段**仅显示文件名**（如 `guiNDB.db`），用户无法确认配置指向；根因=wxWidgets 3.3.3 `src/propgrid/props.cpp` 中 `DoSetAttribute` 处理 `wxPG_FILE_SHOW_FULL_PATH` 时调用 `ChangeFlag(wxPGPropertyFlags_ShowFullFileName, ...)`（= `Reserved_1` = `0x10000000`），但 `ValueToString` 渲染时检查的是 `wxPGFlags::ShowFullFileName`（`0x00100000`）——两个 bit 不同，属性设置永远不影响渲染；修复=绕过有 bug 的 `DoSetAttribute` 路径，对全部 8 个 `wxFileProperty` 直接调用 `prop->ChangeFlag(wxPGFlags::ShowFullFileName, true)` 设置正确 bit；保留原有 `SetPropertyAttribute` 调用（向后兼容，不产生编译错误）；验证=ConfigDialog 打开后文件路径字段显示完整绝对路径，构建 0 error，ctest 30/30 | — |
| 54 | [`docs/bugfix/2026-08-20-Bugfix-ProxyListModel-HealthColdStartZero-v1.0.md`](./bugfix/2026-08-20-Bugfix-ProxyListModel-HealthColdStartZero-v1.0.md) | **ProxyListPanel Health 列冷启动初始值 0.5 → 0.0** — 未测试代理（`start_count==0`）经贝叶斯平滑 `(stable+1)/(start_count+2)` 得出 `0.5` 的"假健康"，与无数据语义不符（`getHealth` fallback 为 0.0）；修复=`ProxyListModel::rebuildMaps()` 对 `start_count==0` 强制 `healthMap_[id]=0.0`，`setRunningDurations()` 将 base 与运行 bonus 均纳入 `start_count>0` 条件（冷启动代理即使运行中也不授予 +0.3 加成）；有历史代理的贝叶斯平滑与 30 分钟运行加成保留；UI 展示与 `ProxyScorer::compute` 冷启动 `history_score=0.0` 语义对齐；TDD 新增 `ColdStartProxyHasZeroHealth` 回归测试（含运行中不授予 bonus 断言）；验证=构建 0 error + ctest 30/30 | — |
| 55 | [`docs/bugfix/2026-08-21-Bugfix-RegionBatchResolver-IpWhoIs-Accuracy-v1.0.md`](./bugfix/2026-08-21-Bugfix-RegionBatchResolver-IpWhoIs-Accuracy-v1.0.md) | **RegionBatchResolver 数据源切换 ip-api.com → ipwho.is（Cloudflare anycast 判定差异）** — 用户报告 region 解析与 ipinfo.io 基准差异大：3 个 Cloudflare anycast IP 实测 ip-api.com 全部不一致且跨洲偏差（US→CA、LT→TR、US→CA）；横向对比多数据源确认根因为 GeoIP 数据库分歧（ip-api.com free 对 Cloudflare 网段 0/3 一致，ipwho.is 3/3 一致，freeipapi.com 返回 HTML 不可用），解析代码无缺陷；修复=`fetchRegionFromIpApi()`→`fetchRegionFromIpWhoIs()`：恢复 `isIpPattern` 域名判断 + `utils::DnsCache::resolve()` 预解析（ipwho.is 仅接受 IP）、请求 `https://ipwho.is/{ip}` + `setSslVerifyPeer(false)`（项目惯例）；批量/单代理两调用点同步；`parseRegionFromJson()` 无需修改（ipwho.is 同样返回完整国名 `country` 字段，失败响应无该字段自然落入空分支）；测试样例更新为 ipwho.is 格式 + 新增 `ParseRegionFromJson_IpWhoIsFailureResponse` 用例；验证=构建 0 error + RegionBatchResolverTest 6/6 + ctest 31/31；已知限制=已有 Region 数据不自动刷新、ipwho.is 免费档官方限额 10k 次/月（增量解析可控） | — |
| 56 | [`docs/bugfix/2026-08-21-Bugfix-ProxyListPanel-RegionAutoRefresh-v1.0.md`](./bugfix/2026-08-21-Bugfix-ProxyListPanel-RegionAutoRefresh-v1.0.md) | **单代理/批量地区解析完成后 ProxyListPanel Region 列不自动刷新** — 解析完成事件链路最终调用 `refreshResults()`，该方法语义为仅重载 ProfileExItem 测试结果列（exItems_），从不重载 ProfileItem（proxies_），而 Region 存储于 `ProfileItem.Region` → 新值永不显示；修复=遵循既有 StatusUpdateEvent 字符串命令模式（先例 DEDUP_OK / RESOLVE_REGION_START）新增 `REGION_RESOLVE_DONE`：批量（doResolveRegionsBatch）与单代理（doResolveSingleProxyRegion）完成时向顶层窗口广播该命令，MainFrame::onStatusUpdate 新增分支调 `proxyPanel_->reloadFromDatabase()`（新公有方法，经 loadProxiesAsync 异步全量重载+预构建 maps，不阻塞 UI）；不改 refreshResults()（高频路径附加 5 万行重载不可接受）；验证=构建 0 error + ctest 31/31；已知限制=AutoTask 流水线 region 步骤后无此刷新 | — |
| 57 | [`docs/bugfix/2026-08-21-Bugfix-StandaloneMonitorDialog-v1.0.md`](./bugfix/2026-08-21-Bugfix-StandaloneMonitorDialog-v1.0.md) | **独立监控对话框构造期 wxASSERT 崩溃修复** — `buttonSizer->Add(wxButton, 0, wxALIGN_RIGHT)` 在 wxHORIZONTAL sizer 中使用非法水平对齐标志，触发 wxWidgets 3.3 `wxBoxSizer::DoInsert` 的 `wxFAIL_MSG`（仅允许垂直对齐标志）；经 objdump 全量反汇编+基址差分匹配解码断言堆栈定位（R=0x7ff69aea0000，ret=0x1400d2bd2）；修复=移除冗余内层标志，整行右对齐由外层 topSizer->Add(buttonSizer,0,wxALIGN_RIGHT\|...) 承担 | — |
| 58 | [`docs/bugfix/2026-08-21-Bugfix-StandaloneMonitor-DataColumns-v1.0.md`](./bugfix/2026-08-21-Bugfix-StandaloneMonitor-DataColumns-v1.0.md) | **独立监控对话框数据列异常（运行时长恒 0 / 监控端口显示 "-"）** — Bug B：`utils::getCurrentTimestamp()` 返回纪元秒字符串却被当作 `"yyyy-MM-dd HH:mm:ss"` 喂给 `durationMsBetween`（sscanf 解析失败→返回0），AppController 共 7 处误用（对话框 now 基准、纳管/接管 started_at 回退、正常启动/纳管/接管三处 baselineElapsedMs 等）；Bug A：悬垂进程由 worker 实例启动时配置在 `<exeDir>\worker\config\`，纳管仅探测 `<exeDir>\config\` → 端口解析失败；修复=新增 `utils::getCurrentTimestampFormatted()`（本地时间 `%Y-%m-%d %H:%M:%S`）替换全部误用点 + 纳管路径双候选探测；TDD 新增 `GetCurrentTimestampFormattedTest` 2 用例；验证=UtilsTest GREEN + ctest 30/31（NetworkMonitorTest 环境抖动复跑通过，已知偶发） | — |
| 59 | [`docs/bugfix/2026-08-21-Bugfix-ProfileExItem-StartupTimeFallback-v1.0.md`](./bugfix/2026-08-21-Bugfix-ProfileExItem-StartupTimeFallback-v1.0.md) | **监控代理启动时 message 测试时间为空的补位策略** — `ProfileExItemDAO::formatStartupMessage`（src/ProfileExItemDAO.cpp:100）原实现测试侧空/无效时输出 `+<启动时间>`（测试侧留空），现改为用启动时间补位：`<启动时间>+<启动时间>`，保证监控启动后 message 恒为双合法时间戳；兼容性=消费方 `messageActiveTime()` 对 `<T>+<T>` 返回 max=T 语义正确、存量旧数据 `+<T>` 向后兼容；对称函数 `formatTestMessage` 不动（测试写入时启动侧空=`从未启动过`真实语义）；TDD RED→GREEN：更新 `FormatStartupMessage_LegacyReplaced`/`InvalidTestSideDropped` 断言 + 新增 `EmptyTestSideFilledWithStartupTime` 用例 + 同步落库断言 `UpdateStartupTime_NewRow_WritesStartupTime`/`LegacyReplaced`（20→39 字符双侧相等）；验证=test_profile_ex_item_dao 26/26 + ctest 31/31 | ✅ completed |
| 60 | [`docs/bugfix/2026-08-21-Bugfix-StandaloneProxy-PortExternalConfigDir-v1.0.md`](./bugfix/2026-08-21-Bugfix-StandaloneProxy-PortExternalConfigDir-v1.0.md) | **独立代理由其它程序启动且配置文件在其它目录时无法解析监听端口** — 纳管仅双目录探测 `<exeDir>\config\`/`<exeDir>\worker\config\`；外部程序把 standalone config 放在任意目录时文件找不到→`readStandaloneInboundPort` 返回 0→端口恒 0；修复=新增 `ProcessInspector::extractConfigFullPath`（命令行提取含目录的完整配置路径，向左回扫至路径边界、要求含分隔符、裸文件名返回空交由既有逻辑）+ 纳管端口解析新增第三步回退：双目录探测失败后再用完整路径直读端口；权威无歧义（规避 OS 套接字查询在多 inbound 下的端口错配，尤其 sing-box 多个 DNS inbound）；TDD 新增 `test_process_inspector` 9 用例（extractConfigFileName/extractConfigFullPath 覆盖引号/绝对/相对/等号赋值/裸名）+ CMake 注册；验证=构建 0 error + ctest 32/32（含 ProcessInspectorTest + NetworkMonitorTest 环境抖动复跑通过） | ✅ completed |
| 61 | [`docs/bugfix/2026-08-21-Bugfix-ProxyListPanel-ColumnSortModelVsVisual-v1.0.md`](./bugfix/2026-08-21-Bugfix-ProxyListPanel-ColumnSortModelVsVisual-v1.0.md) | **ProxyListPanel 列排序模型列 vs 视觉位置修复** — `wxDataViewEvent::GetColumn()` 返回模型列索引，而 `wxDataViewCtrl::GetColumn(pos)` 需要视觉位置；列重排后二者不一致导致 Latency/Health/Starts/Runtime 点击排序映射到错误列；修复=新增 `resolveColumnByModel(int)` 辅助函数（遍历视觉位置按 `GetModelColumn()` 匹配），替换 3 处 `GetColumn(dvEvent.GetColumn())` 调用（ProxyListPanel.cpp:334、:697；SubscriptionPanel.cpp:524）；验证=构建 0 error + ctest 32/32（含 test_proxy_list_model 15/15、Runtime/Health 排序比较用例） | ✅ completed |
| 62 | [`docs/specs/2026-08-21-Spec-Toolbar-MonitorProxyButton-v1.0.md`](./specs/2026-08-21-Spec-Toolbar-MonitorProxyButton-v1.0.md) | **工具栏新增「监控代理」按钮（打开独立代理监控对话框）** — 新增工具 ID `ID_TOOL_STANDALONE_MON = wxID_HIGHEST + 211`；复用 `MainFrame::onMenuStandaloneMonitor`（懒创建+Show+Raise），不新增处理函数；`m_toolbar->AddTool` 插入于 `ID_TOOL_CONFIG`（配置）之前（左侧）；图标 `tool_monitoring_proxy_process.png` 经 `src/ui/icons.rc` 嵌入为 `tool_monitoring_proxy_process_png` 资源 + 复制 `bin/icons/` 开发期回退；验证=构建 0 error + ctest 32/32 | ✅ completed |
| 63 | [`docs/specs/2026-08-21-Spec-ProxyListPanel-ColumnReorder-v1.0.md`](./specs/2026-08-21-Spec-ProxyListPanel-ColumnReorder-v1.0.md) | **ProxyListPanel 列显示顺序调整（用户指定顺序）** — 仅调整 `ProxyListPanel::onColumnsInit` 的 `AppendTextColumn` 调用顺序为 Region / Latency ↕ / Health ↕ / Type / Host ↕ / Port / Message ↕ / Starts ↕ / Runtime ↕ / # / IndexId / Failures ↕ / Remarks；不动 `ProxyListModel`（COL_* 模型列索引、`GetValueByRow`、`Compare`、`SetValueByRow` 全部基于模型列索引）；排序正确性由框架保证——`wxDataViewColumn::SetSortOrder` 触发 `Resort` 时 `Compare` 收到的 `column` 为该列 `GetModelColumn()`（datavgen.cpp:1833-1838 `m_sortOrder.GetColumn()`），与视觉位置解耦，故仅重排视觉顺序不影响排序；验证=构建 0 error + ctest 32/32 | ✅ completed |

---

## 10. 测试报告

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/test/test-report_20260512_135738.md`](./test/test-report_20260512_135738.md) | **集成测试报告** — 706/706 代理迁移成功(验证 PassRate ~99.43%) | 6.1 KB |

---

## 11. 代码审查

 | # | 文件 | 说明 | 大小 |
 |---|------|------|------|
 | 1 | [`docs/code-reviews/2026-05-07-logger-configreader-fixes-review.md`](./code-reviews/2026-05-07-logger-configreader-fixes-review.md) | **Logger + ConfigReader 修复审查报告** — P0 日志格式 BUG + Logger 审计发现 | 5.1 KB |
 | 2 | [`docs/ce-code-review/20260507-e45fc331/`](./ce-code-review/20260507-e45fc331/) | **Code-Embodiment 综合审查** — 6 个维度(adv/agent/native/correctness/maintainability/testing/reliability + learnings) | 7 文件 |

 ## 12. 进展与状态跟踪

> 以下文档是元跟踪文档，用于跟踪计划执行状态，不属于实施计划本身。

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/plans/project-plans-tracker.md`](./plans/project-plans-tracker.md) | **全局计划跟踪器** — 已完成草稿、优先级、`.kilo/` 迁移、UI Implementation Phase 跟踪 | 8.0 KB |
| 2 | [`docs/plans/feature-status.md`](./plans/feature-status.md) | **功能实现状态清单** — 6 大功能模块逐项状态(✅⚠️❌) | 5.3 KB |

---

## 文档关系图

```
项目上下文 (context) + 术语表 (glossary)
      │
      ▼
需求脑暴 (superpowers/brainstorm/)
      │
      ▼
架构设计 (architecture) + 方案设计 (superpower/specs/ + specs/)
       │
       ▼
实施计划 (docs/plans/) × 54 份 ──→ 全局跟踪 (project-plans-tracker)
       │
       ▼
代码修改 (src/*.cpp) ←── 流程规范 (DEV-PROCESS.md)
       │
       ▼
分析报告 (reports/) + 测试报告 (test/) + 代码审查 (code-reviews/) + 规范化设计 (specs/)
      │
      ▼
功能状态 (feature-status) + 实施清单 (impl-items-6-7-10)
```

---

---

## 13. 长期记忆

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/project-knowledge.md`](./project-knowledge.md) | **项目长期记忆** — 测试规范、错误级别分类、Google Test 规范、错误分析、三文档协同模型、工具模式、架构决策记录、调试与稳定性规则（含 ASAN/UBSan/MiniDump/cppcheck/gcovr 用法、工具选择矩阵、Logger 深度调试）。内容与 AGENTS.md 和 `docs/` 文档不重复。 | 6.2 KB |

**维护规则**: 会话结束时如有新的架构决策或跨模块知识，追加至 `docs/project-knowledge.md` §7 关键决策记录。

---

*最后更新: 2026-08-20 (v13) | 维护者: Kilo AI*



