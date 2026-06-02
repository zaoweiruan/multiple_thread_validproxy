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

## 当前状态 (2026-06-01)

- **全部 54 份计划文档** 已归集至 `docs/plans/`
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
