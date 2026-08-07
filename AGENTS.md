# AGENTS.md — 长期记忆与 AI 上下文注入点

## 1\. 项目概述与技术栈边界

| 关键要素 | 规范与版本 | 备注 / 约束限制 |
| :--- | :--- | :--- |
| **编程语言** | C++ 20/17 (**标准: C++17**) | **核心约束：全栈代码中禁止使用 `auto` 进行类型推导** |
| **构建系统** | CMake + Ninja | Debug 模式编译：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` |
| **UI 框架** | wxWidgets 3.2+ (wxMSW) | 仅在 GUI 模式下生效，入口文件为 `src/ui/UIApp.cpp`。本地源码路径：`E:\eclipse_workspace\wxWidgets` |
| **目标平台** | Windows (MinGW/GCC) | 必须保证 GCC 编译器的兼容性。**默认终端：PowerShell**，所有模型、MCP tools、skills 必须使用 PowerShell 命令语法 |
| **核心业务** | 代理验证与网络转发 | 涉及 Xray-core、gRPC API、cURL HTTP 请求及 SQLite 存储 |
| **文档总图** | **`docs/INDEX.md`** ⭐ | **全量静态文档、历史分析及变更记录的唯一分类总索引入口** |
| **系统版本** | 1.0.3 | 入口点：`src/main_gui.cpp`（包含 `main()`，创建并启动 `UIApp` wxWidgets 实例） |

* * *

## 2\. 目录结构与设计规范

```text
./src/            源文件 — Xray 核心控制与业务逻辑
./include/        头文件
./src/ui/         UI 层 — wxWidgets (AppController, MainFrame, 6 面板)
./tests/          单元测试 — Google Test（所有可编译测试源码必须在此）
./test/           测试数据集 — 仅存放测试数据库、配置文件、模拟脚本，禁止包含源码
./bin/            生产运行目录 (配置文件 config.json + 数据库 worker/guindb.db)
./docs/           项目文档主目录
  ├── INDEX.md   ⭐ 核心主索引：全量长短期文档分类索引入口点（AI 检索必读）
  ├── DEV-PROCESS.md 开发规范
  ├── plans/     全局实施计划与状态跟踪器 (tracker.md)
  ├── design/    UI 设计、无效过滤等基础设计规范
  └── specs/     各个核心重构模块的技术方案与规格说明书
./skills/         Agent 技能集
./scripts/        Python 辅助脚本
./temp/           调试临时文件 (脚本、输出、临时配置)

```

### 2.1 测试目录与文档约束

- **`tests/` 目录**：存放所有测试相关文件（测试源码 `.cpp`/`.h`、测试可执行文件、测试辅助工具）。
- **`test/` 目录**：仅存放测试所需数据（如 `guindb.db`、配置文件、模拟脚本），**不得包含任何可编译的测试源码或可执行文件**。
- **状态隔离红线**：`AGENTS.md` 本身不记录任何动态跟踪状态，不得将任何状态跟踪写入。全量静态文件的分类总索引必须收拢至 `docs/INDEX.md`，而工程进度状态必须写入 `docs/plans/project-plans-tracker.md`。
- **文档目录约束**：只能在 `docs/` 目录下子目录中放置文档，不得在 `docs/` 中新建目录。
- **规范参考**：项目设计、开发、文档等规范参考 `docs/INDEX.md` 文档。

* * *

## 3\. 核心模块与文件映射

### 3.1 业务核心逻辑

- **`XrayManager`** (`include/XrayManager.h`, `src/XrayManager.cpp`)：Xray 实例单例管理。
- **`XrayInstance`** (`include/XrayInstance.h`, `src/XrayInstance.cpp`)：单个 Xray 进程生命周期维护。
- **`XrayApi`** (`include/XrayApi.h`, `src/XrayApi.cpp`)：与 Xray 进行 gRPC API 通信（注意：已修复 `CreateProcessA` 引起的 CMD 窗口闪烁问题）。
- **`ProxyFinder`** (`include/ProxyFinder.h`, `src/ProxyFinder.cpp`)：代理查找策略（F/FMIN）。
- **`ProxyBatchTester`** (`include/ProxyBatchTester.h`, `src/ProxyBatchTester.cpp`)：多线程并发测试调度引擎。
- **`ProxyTester`** (`include/ProxyTester.h`, `src/ProxyTester.cpp`)：单个代理测试核心（CURL + Xray 联动）。
- **`ConfigGenerator`** (`include/ConfigGenerator.h`, `src/ConfigGenerator.cpp`)：Xray JSON 配置文件动态生成。
- **`ConfigReader`** (`include/ConfigReader.h`, `src/ConfigReader.cpp`)：全局 `config.json` 解析。
- **`SubitemUpdaterV2`** (`include/SubitemUpdaterV2.h`, `src/SubitemUpdaterV2.cpp`)：订阅更新、解析与高效率去重。
- **`DatabaseHelper`** (`include/DatabaseHelper.h`)：基于 SQLite DAO 的纯数据访问层。
- **`Logger`** (`include/Logger.h`, `src/Logger.cpp`)：全局日志（级别：`TRACE` < `DEBUG` < `INFO` < `REPORT` < `WARN` < `ERR`；DIAG 日志已从 INFO 调优至 TRACE）。
- **`ShareLink`** (`include/ShareLink.h`, `src/ShareLink.cpp`)：分享链接导出与解析。
- **`PortManager`** (`include/PortManager.h`, `src/PortManager.cpp`)：并发测试中的本地端口动态分配与回收。
- **`UrlFetcher`** (`include/UrlFetcher.h`, `src/UrlFetcher.cpp`)：底层的 cURL HTTP/HTTPS 请求器。
- **`CurlEasyHandle`** (`include/CurlEasyHandle.h`)：cURL RAII 模式的 header-only 安全封装。

### 3.2 数据模型 (Data Models)

- **`Profileitem`** (`include/Profileitem.h`)：底层基础代理配置项模型。
- **`Subitem`** (`include/Subitem.h`)：订阅源节点模型。
- **`ProfileExItem`** (`include/ProfileExItem.h`)：携带测试状态、延迟等动态数据的扩展代理模型。

### 3.3 UI 表现层 (wxWidgets)

- **`AppController`** (`src/ui/AppController.h`, `src/ui/AppController.cpp`)：控制器层，负责业务调度、解耦 UI 与核心逻辑。
- **`MainFrame`** (`src/ui/MainFrame.h`, `src/ui/MainFrame.cpp`)：主窗口布局、状态栏与工具栏交互。
- **`SubscriptionPanel`** (`src/ui/SubscriptionPanel.h`, `src/ui/SubscriptionPanel.cpp`)：订阅源管理面板。
- **`ProxyListPanel`** (`src/ui/ProxyListPanel.h`, `src/ui/ProxyListPanel.cpp`)：代理列表面板。
- **`LogPanel`** (`src/ui/LogPanel.h`, `src/ui/LogPanel.cpp`)：日志展示面板。
- **`Events`** (`src/ui/Events.h`, `src/ui/Events.cpp`)：自定义事件系统，处理多线程测试回调与 UI 刷新的异步通信。

* * *

## 4\. 自动化运维与 CLI 常用命令

### 4.1 构建与测试命令 (PowerShell 语法)

> **构建规范**: CMake 构建输出仅写入 `bin/` 和 `tests/` 目录。**不得**在构建过程中自动写入 `bin/worker/` 目录。
> `bin/worker/` 是运行时工作目录（存放 `guindb.db`、`validproxy.exe` 副本等），由用户手动或有明确意图的脚本维护。
> 构建时自动复制到此目录会因文件锁定（运行中的 `validproxy.exe`）导致 `POST_BUILD` 阶段失败。

```powershell
# 1. 配置并生成 Debug 模式构建流
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug

# 2. 并行编译 (指定 8 线程)
cmake --build build --parallel 8

# 3. 运行主程序
# 优先使用 CLI 进行功能测试，通过分析产生的日志进行维护、修复、功能验证等
.\build\validproxy.exe            # GUI 入口（默认启动图形界面）
.\build\validproxy-cli.exe        # CLI 入口（无参启动时，进行默认批量测试）

# 4. 执行全量集成测试
ctest -V

# 5. 运行指定前缀的单项测试
ctest -R DedupTest -V

```

### 4.2 调试命令 (PowerShell 语法)

```powershell
# 1. Sanitizer 构建（拦截内存泄漏/越界/未定义行为）
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build --parallel 8 && ctest -V

# 2. 覆盖率构建
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build --parallel 8 && .\scripts\run_coverage.ps1

# 3. 静态分析（cppcheck）
.\scripts\run_static_analysis.ps1

# 4. Logger 级别控制（在 bin/config.json 中设置）
# log.file_level: "TRACE" | "DEBUG" | "INFO" | "REPORT" | "WARN" | "ERR"
# log.network_failures: true  （网络失败日志从 INFO 降级为 TRACE）
```

### 4.3 CLI 工具参数速查表

> CLI 入口程序：`validproxy-cli.exe`。

```text
（无参）                    : 无参启动时，进行默认批量测试（对数据库中全部代理执行连通性测试）
-c, --config <path>         : 指定配置文件路径 (默认: bin/config.json)
-show-sub                   : 控制台输出并显示所有订阅源
-G, -generator <id>         : 根据特定的 indexId 导出标准 outbound Xray JSON 文本
-F, -find-proxy             : 快速查找并返回第一个连通性完好的可用代理
-FMIN, -findminproxy        : 测试并返回当前数据库中延迟最小的优质代理
-U, -update <id>            : 强制更新指定的单个订阅源
-UA, -update-all            : 批量更新所有处于启用状态的订阅源
-T, -test-sub <id>          : 针对指定订阅源内部的所有代理进行连通性测试
-TU, -tourl                 : 过滤并导出所有有效的 (delay > 0) 代理为分享链接
-D, -dedup                  : 触发去重算法，移除数据库中的高度重复代理
-S, -sync [src[:dst]]       : 数据库双向/单向高效同步
-IS, -import-sub-config <f> : 从本地文件或网络 URL 批量导入订阅节点
-h, --help                  : 打印 CLI 帮助菜单

```

* * *

## 5\. 数据源与环境配置文件路由

| 数据库环境 | 相对路径 | 核心物理说明 |
| --- | --- | --- |
| **生产运行时数据库** | `bin/worker/guindb.db` | 实际业务存储，由 `bin/config.json` 的 `database.path` 指定 |
| **全局生产配置文件** | `bin/config.json` | 包含默认超时、并发数、高优先级字段配置 |
| **默认测试数据库 (全量)** | `test/guindb.db` | 内含 53,837 profiles, 44 SubIDs，用于除导入、迁移数据功能外全部测试 |
| **测试数据库 (精简)** | `test/guiNDB_empty.db` | 内含 711 profiles, 8 SubIDs，专门用于导入、迁移数据功能回归单测 |
| **默认测试专用配置文件** | `bin/test_config.json` | 预先配置指向 `test/guiNDB.db`，防止污染生产环境 |

* * *

## 6\. AI 行为规范与核心研发路由表

为了让 AI 助手提供最符合项目预期的代码及文书，编写代码或制定设计方案时请**必须**遵循以下路由：

### 6.1 文档先行与命名规范

任何涉及功能修改、重构的代码，**必须在修改代码前**先在 `docs/` 下创建或更新设计文档/技术方案（Spec/SDD），其文件命名必须精确遵循：  
`[YYYY-MM-DD]-[DocType]-[Module]-[Version].md`  
*(例如: `2026-06-10-Spec-ProxyTester-v1.0.md`)*  
**修改完成后，必须将文档路径同步登记至 `docs/INDEX.md` 总索引中**。

### 6.2 智能路由跳转表（关键词定向约束）

> **强制前置检查（每次用户输入后、生成回复前，必须执行）：**
> 1. 扫描用户输入是否命中下表关键词。
> 2. 对每个命中项，立刻调用 `skill` 工具加载对应技能。
> 3. 在生成的回复中遵循该技能的行为约束。

当监测到提示词中包含以下核心意图时，AI 应当自动引入项目对应的标准开发思想：

| 用户输入意图包含的关键词 | 匹配的技能/方法论 (Skills) | AI 应当输出的特定行为约束 |
| :--- | :--- | :--- |
| `构思` / `brainstorm` / `头脑风暴` / `创意方案` / `需求分析` / `需求探索` | **brainstorming** | 1. 加载技能 `skill(name="brainstorming")`。<br>2. 引导用户明确需求边界、核心目标和约束条件。<br>3. 输出多个可选方案供用户决策。<br>4. 决策完成后再进入实现阶段（路由至 using-superpowers）。 |
| `调整功能` / `开发功能` / `新增功能` / `重构` / `架构调整` / `修改行为` | **using-superpowers** |1. 严格检查是否**禁止了 `auto`**。<br>2. 优先通过读取 docs/INDEX.md 检索开发规范、历史技术规格设计文档（Spec）。<br>3. 在修改后将其更新至 `docs/plans/project-plans-tracker.md`。 |
| `bug` / `修复` / `fix` / `异常` / `崩溃` / `错误` / `测试失败` / `故障` | **systematic-debugging** | 1. 启动根因分析（RCA）。 <br> 2. 明确指出受影响的模块文件（如 `XrayApi.cpp` ）。 <br> 3. 提供异常捕获加固方案，并输出修复日志到 `docs/bugfix/`。 |
| `ASAN` / `sanitizer` / `调试` / `debug` / `dump` / `cppcheck` / `覆盖率` / `coverage` / `稳定性` / `MiniDump` | — | 1. 优先引用 **AGENTS.md §4.2** 获取调试命令速查。<br>2. 深度工作流（ASAN 输出解读 / dump 分析 / Logger 调优）参考 **`docs/project-knowledge.md#8`**。 |
| `计划` / `方案` / `制定计划` / `设计文档` / `design doc` / `spec` / `技术方案` | **writing-plans** | 1. 严格遵循产品/工程视角区分（PRD 与 Spec 隔离）。<br>2. 产出包含输入、输出、边界条件、前置条件的标准 Markdown。 |
| `实现` / `编码` / `写代码` / `implement` | **test-driven-development** | 1. 强制要求在编写实现代码的同时，或之前，在 `tests/` 目录下提供 Google Test（`TEST_F`）测试用例。 |
| `测试` / `test` / `单元测试` / `ctest` | **cmake-build** | 1. 执行 `cmake --build build` 编译。 <br> 2. 执行 `ctest -V` 运行全量测试。 |
| `构建` / `编译` / `build` / `cmake` / `CI` | **cmake-build** | 1. 按 Debug 模式配置构建：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`。<br>2. 并行编译：`cmake --build build --parallel 8`。 |
| `commit` / `提交` / `git` / `分支` / `branch` / `合并` / `merge` / `推送` / `push` / `PR` / `pull request` / `rebase` / `stash` | **git-master** | 1. 加载技能 `skill(name="git-master")`。<br>2. 严格遵循该技能的分阶段工作流，确保原子提交和版本管理规范。 |
| `代码审查` / `code review` / `代码评审` / `review` / `PR review` | **requesting-code-review** | 1. 加载技能 `skill(name="requesting-code-review")`。<br>2. 在功能完成并验证测试通过后，提交代码审查。 |
| `完成` / `complete` / `验证通过` / `verify` / `验证完成` | **verification-before-completion** | 1. 加载技能 `skill(name="verification-before-completion")`。<br>2. 在声称任务完成前必须运行所需验证命令，确认输出后再声明完成。 |
| `画图` / `生成图片` / `画一只XX` / `image generation` / `generate image` / `draw a` | **agnes-image-gen** | 1. 加载技能 `skill(name="agnes-image-gen")`。<br>2. 提取用户描述，增强为详细英文 prompt（风格、光影、构图）。<br>3. 调用 Agnes Image 2.1 Flash API，下载生成的图像。<br>4. 报告保存路径给用户。 |
| `生成视频` / `制作动画` / `画一段视频` / `video generation` / `create video` / `make a clip` | **agnes-video-gen** | 1. 加载技能 `skill(name="agnes-video-gen")`。<br>2. 提取用户描述，增强为详细英文 cinematic prompt（镜头运动、光影、风格、画质）。<br>3. 调用 Agnes Video V2.0 API 创建异步任务。<br>4. 轮询任务状态直到 completed，下载 MP4 视频。<br>5. 报告保存路径给用户。 |
| `部署` / `发布` / `release` / `deploy` | **release-skills** | 1. 自动检测版本文件与 changelog，按语义化版本规范发布。 |

* * *
