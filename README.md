# validproxy — 代理验证与网络转发管理工具

> **版本**: v1.6.0 · **Git**: `84fdfae` · **平台**: Windows (MinGW/GCC) · **语言**: C++17

`validproxy` 是一个基于 **Xray-core** 的代理验证与网络转发管理工具，提供 **wxWidgets 图形界面** 与 **命令行（CLI）** 双入口。它面向 v2rayN 生态，支持订阅源管理、代理批量连通性测试、去重清洗、分享链接导入导出、独立代理/代理池启动与监控、以及实时日志与统计面板。

---

## 目录

- [功能特性](#功能特性)
- [技术栈](#技术栈)
- [环境要求](#环境要求)
- [构建与测试](#构建与测试)
- [CLI 使用](#cli-使用)
- [配置与数据](#配置与数据)
- [项目结构](#项目结构)
- [核心模块](#核心模块)
- [调试工具链](#调试工具链)
- [文档索引](#文档索引)
- [开发规范](#开发规范)

---

## 功能特性

- **订阅源管理**：多订阅源（URL/本地文件）拉取、解析（VMess/VLESS/Trojan/Shadowsocks/Hysteria2/Sing-Box 等）、高效率去重、404 失效标记、更新间隔控制。
- **批量连通性测试**：多线程并发调度（`ProxyBatchTester`），Xray 实例动态注入 + cURL 探测，支持网络断连暂停/恢复、实例崩溃自动重拉。
- **代理查找**：`-F` 快速查找第一个可用代理；`-FMIN` 按延迟排序返回优质代理。
- **独立代理 / 代理池**：单代理启动/停止/监控，代理池并发管理，端口动态分配（`PortManager`）与回收。
- **分享链接**：标准 share link 导入/导出（`ShareLink`），`-TU` 导出全部有效代理。
- **去重清洗**：`-D` 触发去重算法，移除高度重复/非法代理（含配置错误阶段清洗）。
- **GUI 面板**：订阅面板、代理列表（延迟/健康/启动次数/运行时长排序）、日志面板（六级别 + 统计弹窗）、任务进度监控。
- **跨会话记忆与文档驱动**：Caura 跨会话记忆 + `docs/INDEX.md` 文档总索引 + 角色分工（见 [开发规范](#开发规范)）。

---

## 技术栈

| 要素 | 说明 |
|------|------|
| 语言 | C++17（**核心约束：禁止 `auto` 类型推导**） |
| 构建 | CMake + Ninja（Debug） |
| UI | wxWidgets 3.2+ (wxMSW) |
| 代理核心 | Xray-core（gRPC API 注入 / CLI 回退） |
| 网络 | cURL（HTTP/HTTPS，RAII 封装 `CurlEasyHandle`） |
| 存储 | SQLite（DAO 分层数据访问层 `DatabaseHelper`） |
| 测试 | Google Test + ctest + UI 自动化（`UITests`，Win32/UIA/COM） |
| 平台 | Windows (MinGW/GCC) |

---

## 环境要求

- **Windows** + MinGW GCC（CLion 自带或 w64devkit）
- **CMake ≥ 3.x** + **Ninja**
- **wxWidgets 3.2+**（MSW，vcpkg：`x64-mingw-dynamic`）
- **Xray-core**（`xray.exe`）与可选 **Sing-Box**（`sing-box.exe`）
- **geoip.dat / geosite.dat**（`xray_location_asset` 路径）
- **vcpkg**（默认 `D:/vcpkg`；Boost、wxWidgets、SQLite、cURL 等依赖）

---

## 构建与测试

```powershell
# 1. 配置并生成 Debug 构建流
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug

# 2. 并行编译 (8 线程)
cmake --build build --parallel 8

# 3. 运行主程序
.\build\validproxy.exe            # GUI 入口
.\build\validproxy-cli.exe        # CLI 入口（无参启动 = 默认批量测试）

# 4. 全量集成测试
ctest -V

# 5. 指定前缀的单项测试
ctest -R DedupTest -V

# 6. UI 自动化一键验收（UI 相关变更交付前必须全绿）
.\scripts\build-and-test.bat
```

> **构建规范**：CMake 输出仅写入 `bin/` 和 `tests/`；`bin/worker/` 是运行时工作目录（用户手动维护）。调试/修复验证一律使用 `bin/` 下的最新构建产物，**禁止**以 `bin/worker/validproxy.exe`（陈旧副本）作为验证对象。

---

## CLI 使用

`validproxy-cli.exe` 参数速查：

```text
（无参）                    无参启动时，进行默认批量测试（对数据库中全部代理执行连通性测试）
-c, --config <path>         指定配置文件路径 (默认: bin/config.json)
-show-sub                   控制台输出并显示所有订阅源
-G, -generator <id>         根据特定的 indexId 导出标准 outbound Xray JSON 文本
-F, -find-proxy             快速查找并返回第一个连通性完好的可用代理
-FMIN, -findminproxy        测试并返回当前数据库中延迟最小的优质代理
-U, -update <id>            强制更新指定的单个订阅源
-UA, -update-all            批量更新所有处于启用状态的订阅源
-T, -test-sub <id>          针对指定订阅源内部的所有代理进行连通性测试
-TU, -tourl                 过滤并导出所有有效的 (delay > 0) 代理为分享链接
-D, -dedup                  触发去重算法，移除数据库中的高度重复代理
-S, -sync [src[:dst]]       数据库双向/单向高效同步
-IS, -import-sub-config <f> 从本地文件或网络 URL 批量导入订阅节点
-h, --help                  打印 CLI 帮助菜单
```

---

## 配置与数据

| 数据/配置 | 路径 | 说明 |
|-----------|------|------|
| 生产运行时数据库 | `bin/worker/guindb.db` | 实际业务存储，由 `bin/config.json` 的 `database.path` 指定 |
| 全局生产配置 | `bin/config.json` | 默认超时、并发数、高优先级字段等 |
| 默认测试数据库（全量） | `test/guindb.db` | 53,837 profiles / 44 SubIDs，用于除导入、迁移数据外全部测试 |
| 测试数据库（精简） | `test/guiNDB_empty.db` | 711 profiles / 8 SubIDs，专门用于导入、迁移数据回归单测 |
| 测试专用配置 | `bin/test_config.json` | 指向 `test/guiNDB.db`，防止污染生产环境 |

---

## 项目结构

```text
./src/            源文件 — Xray 核心控制与业务逻辑
./include/        头文件
./src/ui/         UI 层 — wxWidgets (AppController, MainFrame, 6 面板)
./tests/          单元测试 — Google Test（所有可编译测试源码必须在此）
./test/           测试数据集 — 仅测试数据库/配置/模拟脚本，禁止源码
./bin/            生产运行目录 (config.json + 数据库 worker/guindb.db)
./docs/           项目文档主目录（docs/INDEX.md 为分类总索引入口）
./skills/         Agent 技能集
./scripts/        Python/批处理辅助脚本
./temp/           调试临时文件
```

---

## 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| `XrayManager` | `src/XrayManager.cpp` | Xray 实例单例管理 |
| `XrayInstance` | `src/XrayInstance.cpp` | 单个 Xray 进程生命周期维护 |
| `XrayApi` | `src/XrayApi.cpp` | 与 Xray 进行 gRPC API 通信 |
| `ProxyFinder` | `src/ProxyFinder.cpp` | 代理查找策略（F/FMIN） |
| `ProxyBatchTester` | `src/ProxyBatchTester.cpp` | 多线程并发测试调度引擎 |
| `ProxyTester` | `src/ProxyTester.cpp` | 单个代理测试核心（cURL + Xray 联动） |
| `ConfigGenerator` | `src/ConfigGenerator.cpp` | Xray JSON 配置动态生成 |
| `ConfigReader` | `src/ConfigReader.cpp` | 全局 `config.json` 解析 |
| `SubitemUpdaterV2` | `src/SubitemUpdaterV2.cpp` | 订阅更新、解析与高效率去重 |
| `DatabaseHelper` | `include/DatabaseHelper.h` | SQLite DAO 纯数据访问层 |
| `Logger` | `src/Logger.cpp` | 全局日志（TRACE < DEBUG < INFO < REPORT < WARN < ERR） |
| `ShareLink` | `src/ShareLink.cpp` | 分享链接导出与解析 |
| `PortManager` | `src/PortManager.cpp` | 并发测试中本地端口动态分配与回收 |
| `UrlFetcher` | `src/UrlFetcher.cpp` | 底层 cURL HTTP/HTTPS 请求器 |
| `CurlEasyHandle` | `include/CurlEasyHandle.h` | cURL RAII 安全封装（header-only） |

**数据模型**：`Profileitem`（底层配置项）、`Subitem`（订阅源节点）、`ProfileExItem`（携带测试状态/延迟的动态扩展模型）。

---

## 调试工具链

```powershell
# 1. Sanitizer 构建（内存泄漏/越界/未定义行为）
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build --parallel 8 && ctest -V

# 2. 覆盖率构建
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build --parallel 8 && .\scripts\run_coverage.ps1

# 3. 静态分析（cppcheck）
.\scripts\run_static_analysis.ps1

# 4. Logger 级别控制（bin/config.json）
#    log.file_level: "TRACE" | "DEBUG" | "INFO" | "REPORT" | "WARN" | "ERR"
```

---

## 文档索引

- **`docs/INDEX.md`** — 全量静态文档、历史分析及变更记录的唯一分类总索引入口（AI 检索必读）。
- `docs/plans/project-plans-tracker.md` — 工程进度状态跟踪器。
- `docs/specs/` — 各核心重构模块的技术方案与规格说明书（Spec/SDD）。
- `docs/bugfix/` — 根因分析与修复记录。
- `docs/reviews/` — 独立评审记录（三视角 / 5-persona 并行）。
- `docs/design/` — UI 设计、无效过滤等基础设计规范。
- `CHANGELOG.md` — 版本变更记录（当前 v1.6.0）。

---

## 开发规范

- **C++17，全栈禁止 `auto`**（显式类型）。
- **文档先行**：任何功能/重构代码修改前，先在 `docs/` 创建设计文档/技术方案，命名 `[YYYY-MM-DD]-[DocType]-[Module]-[Version].md`，完成后登记 `docs/INDEX.md`。
- **TDD**：实现代码的同时/之前在 `tests/` 提供 Google Test（`TEST_F`）用例；测试源码在 `tests/`、数据在 `test/`。
- **角色分工**（`AGENTS.md` §6.4）：planner → implementer → tester → reviewer → 交付；debugger 任意环节介入；ui-analyst 负责 GUI 截图/界面分析。
- **CLion MCP 优先**（`AGENTS.md` §6.3.5）：调试/静态诊断优先 CLion；含中日韩文本改动禁用 `apply_patch`，用 `edit` 精确替换。
- **文档/规范总入口**：`AGENTS.md` 为 AI 上下文注入点；`docs/INDEX.md` 为文档分类总索引。

---

## License

详见项目仓库许可证文件（本工具基于 v2rayN / Xray-core 开源生态构建）。
