---
title: "docs: project document index"
type: meta
status: maintained
updated: 2026-05-25
---

# Project Document Index

> 项目: `validproxy` — C++ 多线程代理验证工具 (v2rayN / Xray-core)
> 本文档为所有 `docs/` 目录文件的分类索引，按最佳实践组织，可作为长期记忆入口点。
> 维护规则: 每新增一类文档时更新对应分类行，并追加 `updated` 日期。

---

## 0. 快速总览

| 类别 | 文件数 | 入口 |
 |------|--------|------|
 | [术语表](#1-术语表) | 1 | `docs/glossary.md` |
  | [项目上下文](#2-项目上下文) | 4 | `docs/context.md` |
  | [模型配置](#25-模型配置) | 1 | `docs/model-config.md` |
 | [开发流程规范](#3-开发流程规范) | 1 | `docs/plans/DEV-PROCESS.md` |
 | [提示模式规范](#35-提示模式规范) | 1 | `docs/prompt-patterns.md` |
 | [整体架构](#4-整体架构) | 3 | `docs/architecture.md` |
 | [设计规范](#5-设计规范) | 9 | `docs/design/` |
 | [需求与脑暴](#6-需求与脑暴) | 4 | `docs/superpowers/brainstorm/` |
 | [技术方案](#7-技术方案) | 6 | `docs/superpowers/specs/` |
 | [实施计划](#8-实施计划) | 44 | `docs/plans/` |
 | [分析报告](#9-分析报告) | 5 | `docs/reports/` |
 | [Bug 修复记录](#91-bug-修复记录) | 1 | `docs/bugfix/` |
 | [测试报告](#10-测试报告) | 1 | `docs/test/` |

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
| 1 | [`docs/design/ui-design-plan.md`](./design/ui-design-plan.md) | **UI 图形界面设计** — 架构分层、数据模型映射、组件选型矩阵(MainFrame/SubscriptionPanel/ProxyListPanel/TestPanel/LogPanel/ConfigDialog/TrayIcon) | 24.7 KB |
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

---

## 8. 实施计划 (docs/plans/)

> 共 44 个计划文件，按日期倒序排列。
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
| In Progress | 14-002 | [`2026-05-14-002-ui-implementation-plan.md`](./plans/2026-05-14-002-ui-implementation-plan.md) | feat | **UI 完整实现** — 11 个 U# 单元含 wxWidgets 集成、事件系统、AppController、MainFrame、6 个面板(XAUI)、AUI 布局；分 4 Phase 执行 |
| 2026-05-18 |  | [`2026-05-18-001-fix-null-testpanel-pointer.md`](./plans/2026-05-18-001-fix-null-testpanel-pointer.md) | fix draft | MainFrame initPanels() 构造顺序修复 (nullptr 前置) |
| 2026-05-18 |  | [`2026-05-18-003-unify-proxy-testing-ui-flow.md`](./plans/2026-05-18-003-unify-proxy-testing-ui-flow.md) | plan draft | 统一右键菜单与工具栏代理测试的 UI 流程 |
| 2026-05-18 |  | [`2026-05-18-004-impl-find-proxy-async.md`](./plans/2026-05-18-004-impl-find-proxy-async.md) | plan draft | **异步 Find Proxy + Delay 列刷新** P1/P2/P3 |
| 2026-05-19 |  | [`2026-05-19-ui-enhancements-sort-find-link.md`](./plans/2026-05-19-ui-enhancements-sort-find-link.md) | feat draft | 列排序 + 查找单个代理 + 订阅联动 |
| 2026-05-14 |  | [`2026-05-14-004-gh-mcp-list-top-repos.md`](./plans/2026-05-14-004-gh-mcp-list-top-repos.md) | plan draft | GitHub MCP 查询 Top 5 仓库 (read-only) |
| 2026-05-13 | P4 | [`2026-05-13-P4-extract-sqlite-helper-and-transaction-plan.md`](./plans/2026-05-13-P4-extract-sqlite-helper-and-transaction-plan.md) | refactor draft | SQLite Helper + Transaction RAII 萃取 |

### 8.3 已完成计划 (Completed)

| 日期 | 编号 | 文件 | 类型 | 说明 |
|------|------|------|------|------|
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

---

## 9. 分析报告

 | # | 文件 | 说明 | 大小 |
 |---|------|------|------|
 | 1 | [`docs/reports/2026-04-23-sharelink-export-repair-report.md`](./reports/2026-04-23-sharelink-export-repair-report.md) | **ShareLink 导出修复报告** — 5 项关键问题修复并验证(路径参数/ECH 编码/TLS 参数/VMess Payload/Path Query)，v2rayN 格式 100% 兼容 | 4.3 KB |
 | 2 | [`docs/reports/db-schema-analysis-20260514.md`](./reports/db-schema-analysis-20260514.md) | **数据库 Schema 分析** — ProfileItem/Subscription/ProfileExItem 表结构、字段映射、类型不一致排查 | 10.8 KB |
 | 3 | [`docs/reports/error-report_20260514.md`](./reports/error-report_20260514.md) | **错误报告** — 2026-05-14 运行时错误汇总与分析 | 4.5 KB |
 | 4 | [`docs/reports/2026-05-19-single-proxy-test-fix-report.md`](./reports/2026-05-19-single-proxy-test-fix-report.md) | **单代理测试修复报告** — runWithIndexId, Delay 刷新, 事件流程修复技术细节 | 5.6 KB |
 | 5 | [`docs/reports/2026-05-19-ui-close-hang-fix-report.md`](./reports/2026-05-19-ui-close-hang-fix-report.md) | **UI 关闭挂起修复报告** — AppController/XrayInstance 析构竞态条件 + 进程句柄 BUG | 3.2 KB |

## 9.1 Bug 修复记录

| # | 文件 | 说明 | 大小 |
|---|------|------|------|
| 1 | [`docs/bugfix/2026-05-28-cancel-sub-update-proxyfinder-phase.md`](./bugfix/2026-05-28-cancel-sub-update-proxyfinder-phase.md) | **订阅更新 ProxyFinder 阶段取消无法立即终止修复** — SubitemUpdaterV2::getProxyPorts() 未传递取消标志给 ProxyFinder | 2.8 KB |
| 2 | [`docs/bugfix/2026-05-20-ctrl-c-exit.md`](./bugfix/2026-05-20-ctrl-c-exit.md) | **Ctrl+C 信号无法正常退出修复** — AppController 析构函数添加 5 秒超时 + detach 机制 | 4.2 KB |

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
架构设计 (architecture) + 方案设计 (superpower/specs/)
      │
      ▼
实施计划 (docs/plans/) × 44 份 ──→ 全局跟踪 (project-plans-tracker)
      │
      ▼
代码修改 (src/*.cpp) ←── 流程规范 (DEV-PROCESS.md)
      │
      ▼
分析报告 (reports/) + 测试报告 (test/) + 代码审查 (code-reviews/)
      │
      ▼
功能状态 (feature-status) + 实施清单 (impl-items-6-7-10)
```

---

*最后更新: 2026-05-28 | 维护者: Kilo AI*
