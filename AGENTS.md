# AGENTS.md — 长期记忆入口点

## 项目概述

- **语言**: C++ 20/17 (标准: C++17)
- **类型**: 代理验证工具
- **目标平台**: Windows (MinGW/GCC)
- **构建系统**: CMake + Ninja
- **版本**: 1.0.3
- **入口文件**: `src/main.cpp`

---

## 目录结构

```
src/             源文件 (21 .cpp)
include/         头文件 (19 .h)
src/ui/          UI 层 — wxWidgets (AppController, MainFrame, 6 面板)
tests/           单元测试 — Google Test (7 文件)
bin/             配置 (config.json) + 数据库 (worker/guindb.db)
docs/            项目文档
skills/          Agent 技能 (14)
scripts/         Python 辅助脚本
temp/            调试临时文件 (脚本、输出、临时配置)
```

---

## 核心模块 → 文件映射

| 模块 | 头文件 | 源文件 | 职责 |
|------|--------|--------|------|
| XrayManager | `include/XrayManager.h` | `src/XrayManager.cpp` | xray 实例单例管理 |
| XrayInstance | `include/XrayInstance.h` | `src/XrayInstance.cpp` | 单个 xray 生命周期 |
| XrayApi | `include/XrayApi.h` | `src/XrayApi.cpp` | xray gRPC API 通信 |
| ProxyFinder | `include/ProxyFinder.h` | `src/ProxyFinder.cpp` | 代理查找 (F/FMIN) |
| ProxyBatchTester | `include/ProxyBatchTester.h` | `src/ProxyBatchTester.cpp` | 多线程并发测试 |
| ProxyTester | `include/ProxyTester.h` | `src/ProxyTester.cpp` | 单个代理测试 (CURL+Xray) |
| ConfigGenerator | `include/ConfigGenerator.h` | `src/ConfigGenerator.cpp` | Xray JSON 配置生成 |
| ConfigReader | `include/ConfigReader.h` | `src/ConfigReader.cpp` | config.json 解析 |
| SubitemUpdaterV2 | `include/SubitemUpdaterV2.h` | `src/SubitemUpdaterV2.cpp` | 订阅更新+去重 |
| DatabaseHelper | `include/DatabaseHelper.h` | — | SQLite DAO 数据层 |
| Logger | `include/Logger.h` | `src/Logger.cpp` | 统一日志系统 |
| ShareLink | `include/ShareLink.h` | `src/ShareLink.cpp` | 分享链接导出 |
| PortManager | `include/PortManager.h` | `src/PortManager.cpp` | 端口分配回收 |
| UrlFetcher | `include/UrlFetcher.h` | `src/UrlFetcher.cpp` | cURL HTTP 请求 |
| CurlEasyHandle | `include/CurlEasyHandle.h` | — | CURL RAII 封装 (header-only) |

### 数据模型

| 模型 | 头文件 | 说明 |
|------|--------|------|
| Profileitem | `include/Profileitem.h` | 代理配置项 |
| Subitem | `include/Subitem.h` | 订阅项 |
| ProfileExItem | `include/ProfileExItem.h` | 扩展代理配置项 |

### UI 模块

| 组件 | 文件 | 职责 |
|------|------|------|
| AppController | `src/ui/AppController.cpp/.h` | 控制器层 (业务调度) |
| MainFrame | `src/ui/MainFrame.cpp/.h` | 主窗口布局 + 工具栏 |
| SubscriptionPanel | `src/ui/SubscriptionPanel.cpp/.h` | 订阅列表面板 |
| ProxyListPanel | `src/ui/ProxyListPanel.cpp/.h` | 代理列表面板 |
| TestPanel | `src/ui/TestPanel.cpp/.h` | 测试结果面板 |
| LogPanel | `src/ui/LogPanel.cpp/.h` | 日志面板 |
| ConfigDialog | `src/ui/ConfigDialog.cpp/.h` | 配置编辑对话框 |
| TrayIcon | `src/ui/TrayIcon.cpp/.h` | 系统托盘 |
| UIApp | `src/ui/UIApp.cpp/.h` | wxApp 子类 |
| Events | `src/ui/Events.cpp/.h` | 自定义事件系统 |

---

## 构建与测试

```bash
# Debug 构建
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8

# 运行
./build/validproxy.exe

# 运行所有测试
ctest -V

# 运行单个测试
ctest -R DedupTest -V
```

---

## CLI 参数速查

| 参数 | 功能 |
|------|------|
| `-c, --config <path>` | 配置文件路径 (默认: config.json) |
| `-show-sub` | 显示所有订阅 |
| `-G, -generator <id>` | 根据 indexId 生成 outbound JSON |
| `-F, -find-proxy` | 查找第一个可用代理 |
| `-FMIN, -findminproxy` | 查找延迟最小的代理 |
| `-U, -update <id>` | 更新单个订阅 |
| `-UA, -update-all` | 更新所有启用的订阅 |
| `-T, -test-sub <id>` | 测试订阅中的代理 |
| `-TU, -tourl` | 导出分享链接 (delay>0 的代理) |
| `-D, -dedup` | 移除重复代理 |
| `-S, -sync [src[:dst]]` | 同步数据库 |
| `-IS, -import-sub-config <file\|url>` | 批量导入订阅 |
| `-h, --help` | 显示帮助 |

> 无参数 → 启动 GUI 模式；`-ui`/`--ui` 显式标志也可启动 GUI

---

## 数据库路径

| 用途 | 路径 | 说明 |
|------|------|------|
| 生产数据库 | `bin/worker/guindb.db` | 实际运行用 |
| 配置文件 | `bin/config.json` | 生产配置 (database.path 指定路径) |
| 测试数据库(完整) | `test/guindb.db` | 53,837 profiles, 44 SubIDs |
| 测试数据库(精简) | `test/guiNDB_empty.db` | 711 profiles, 8 SubIDs (标准 CLI 测试用) |
| 测试配置文件 | `bin/test_config_empty.json` | 指向 test/guiNDB_empty.db |

---

## 统一文档命名规范

依据 `docs/Unified-document-naming-conventions.md`，所有文档必须遵循以下规范：

### 文档类型简称表

| 文档类型 | 英文简称 | 说明 |
|----------|----------|------|
| 产品需求文档 | PRD | 用户/业务视角，含用户故事、场景、优先级、验收标准 |
| 软件需求规格 | SRS / SRD | 工程视角细化需求，覆盖功能/非功能/约束/接口 |
| 技术方案/技术规格 | Spec | 详细技术实现方案，含设计思路、数据结构、接口、边界与风险 |
| 系统/服务设计文档 | SDD / Design | 系统结构、组件划分、数据流、依赖关系 |
| 架构文档 | SAD / Architecture | 高层架构蓝图，分层/子系统/技术选型/容量规划 |
| 架构决策记录 | ADR | 单条架构决策，说明背景、候选方案、取舍及影响 |
| 接口文档 | API Doc | 对外 API 定义：路径、方法、参数、响应、错误码 |
| 测试计划 | Test Plan | 测试范围、策略、环境、里程碑 |
| 测试用例/规格 | Test Spec / Test Case | 结构化测试用例：前置条件、步骤、输入、预期结果 |
| 运维手册 | Runbook / Playbook | 日常运维和故障处理步骤 |
| 部署说明 | Deployment Guide | 部署/升级/回滚流程、环境依赖、配置项 |
| 发版说明 | Release Notes | 版本变更列表、新特性、兼容性变更、修复缺陷 |
| 项目计划 | Project Plan | 项目目标、范围、里程碑、资源分配、风险 |
| 路线图 | Roadmap | 时间轴展示产品/平台版本规划 |
| 会议纪要 | Meeting Minutes | 会议结论、决策、行动项 |
| 代码仓库入口 | README | 项目简介、目录结构、构建方式（固定文件名 README.md） |
| 代码规范 | Coding Guidelines | 命名风格、格式、错误处理、日志、安全等编码约定 |

### 文件命名格式

```
[YYYY-MM-DD]-[[DocType]-[Project]-[Module]-[Version].md
```

- **格式**: 日期-文档类型-项目-模块-版本.md
- **示例**: `2026-06-02-PRD-Payments-Checkout-v1.2.md`
- **所有文档必须使用 `.md` 扩展名** (Markdown)
- 创建新文档时严格按照此格式命名

---

## 当前状态 (2026-06-02)

- **全部 54 份计划文档** 已归集至 `docs/plans/`
- **统一文档命名规范** 已纳入 AGENTS.md 核心规范
- **近期 Bug 修复**: CMD 窗口闪烁 (XrayApi.cpp → CreateProcessA)、DIAG 日志级别调整 (INFO→TRACE)
- **待执行草稿**: 列排序+查找+订阅联动 (`2026-05-19-ui-enhancements-sort-find-link.md`)
- **全量跟踪**: `docs/plans/project-plans-tracker.md`
- **功能状态矩阵**: `docs/plans/feature-status.md`

---

## 核心规范

1. **文档先行**: 任何代码修改前必须先创建/更新设计文档，制定计划文档，审批后纳入计划跟踪器 (`docs/plans/DEV-PROCESS.md`)
2. **禁止 `auto`**: 代码中禁止 `auto` 类型推导 (项目 convention)
3. **框架**: wxWidgets 3.2+ (wxMSW) — GUI 模式
4. **构建**: CMake + Ninja — Debug 模式 `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`
5. **日志**: Logger 类 — 级别: TRACE < DEBUG < INFO < REPORT < WARN < ERR
6. **测试**: Google Test — `ctest -V` 全量测试，`ctest -R <TestName> -V` 单测

---

> 所有关键文档入口均归集于 [docs/INDEX.md](./docs/INDEX.md)。

| 类别 | 入口 | 说明 |
|------|------|------|
| 术语表 | [`docs/glossary.md`](./docs/glossary.md) | 代理协议、传输层、数据库字段、CLI 命令等 40+ 条定义 |
| 项目上下文 | [`docs/context.md`](./docs/context.md) | v2rayn/Xray-core 源码位置、关键文件映射、配置文件路径 |
| 模型配置 | [`docs/model-config.md`](./docs/model-config.md) | 模型配置参考 v2.0，主模型及备降链设计 |
| 开发流程规范 | [`docs/plans/DEV-PROCESS.md`](./plans/DEV-PROCESS.md) | 计划优先原则、LogLevel 等级规范、计划文档模板 |
| 提示模式规范 | [`docs/prompt-patterns.md`](./docs/prompt-patterns.md) | 提示词模式指南，各类工作最优提示结构 |
| 整体架构 | [`docs/architecture.md`](./architecture.md) | 核心模块(6)/数据层(SQLite DAO)/数据流/构建系统/CLI 命令 |
| 设计规范 | [`docs/design/`](./design/) | UI 设计 / 去重黑名单 / 无效代理过滤 / 功能状态矩阵 / 实施清单 |
| 需求与脑暴 | [`docs/superpowers/brainstorm/`](./superpowers/brainstorm/) | curl RAII / ShareLink Parser / Config Transaction Batch / 改进想法 |
| 技术方案 | [`docs/superpowers/specs/`](./superpowers/specs/) | 模块重构 / SubitemUpdaterV2 × 2 / 去重设计 / 代理同步 / 批量导入 |
| 实施计划 | [`docs/plans/`](./plans/) | 54 份计划；全局跟踪见 tracker.md |
| 分析报告 | [`docs/reports/`](./reports/) | ShareLink 导出修复 / DB Schema 分析 / 错误报告 / 日志调整 |
| Bug 修复记录 | [`docs/bugfix/`](./bugfix/) | CMD 闪烁 / 订阅取消 / Ctrl+C 退出 / SQL 错误输出 |
| 测试报告 | [`docs/test/`](./test/) | 集成测试报告 (706/706 通过) |
| 代码审查 | [`docs/code-reviews/`](./code-reviews/) + [`docs/ce-code-review/`](./ce-code-review/) | Logger 修复审查 / Code Embodiment 6 维综合审查 |
| 状态跟踪 | [`docs/plans/project-plans-tracker.md`](./plans/project-plans-tracker.md) + [`docs/plans/feature-status.md`](./plans/feature-status.md) | 全局进度总表 + 功能实现状态矩阵 |
| **文档索引** | **[`docs/INDEX.md`](./INDEX.md)** ⭐ | **所有 `docs/` 文件的分类总索引** |
