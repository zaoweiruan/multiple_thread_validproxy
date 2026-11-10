# AGENTS.md — 项目长期约束与 AI 工作入口

## 1. 用途与信息边界

本文件是项目级 AI 上下文入口，只保存必须长期生效的规则：

- 稳定的技术栈、目录边界和数据安全约束。
- 用户明确的长期开发偏好。
- 文档入口、工具使用原则和研发门禁。
- 关键模块导航。
- 关键词→技能路由表与 CLI 速查表（用户明确要求保留，2026-11-09 登记）。

本文件不保存当前任务进度、临时待办、详细工具参数、易变数据库统计、发布版本或具体故障过程。

- 静态文档分类索引：`docs/INDEX.md`
- 工程进度与状态：`docs/plans/project-plans-tracker.md`
- 专题设计、排障、评审和工具说明：放在 `docs/` 已有的合适子目录中。

只按任务需要读取专题文档，不默认加载全部项目资料。

## 2. 项目硬约束

### 2.1 技术栈

- C++ 标准：**C++17**。升级标准必须经过独立决策，不得从"c++20/17"等混合表述自行推断。
- 全部项目 C++ 代码禁止使用 `auto` 进行类型推导。
- 目标平台：Windows，必须兼容 MinGW/GCC。
- 构建系统：CMake + Ninja。
- GUI：wxWidgets 3.2+（wxMSW）。
- 默认终端：PowerShell；命令须兼容当前实际 PowerShell 版本。
- 核心业务：代理验证、订阅管理与网络转发。
- 主要依赖：Xray-core、gRPC、cURL、SQLite。

### 2.2 目录边界

- `src/`：业务实现。
- `include/`：公共头文件。
- `src/ui/`：wxWidgets UI 与控制器。
- `tests/`：测试源码、测试辅助工具及约定的测试产物。
- `test/`：测试数据、配置和模拟脚本；不得放置可编译测试源码或测试可执行文件。
- `bin/`：项目约定的运行配置及构建产物。
- `bin/worker/`：运行时工作目录，不是构建输出目录。
- `docs/`：项目文档；新专题文档放入已有合适子目录，不擅自创建新子目录。
- `skills/`：Agent 技能。
- `scripts/`：维护与辅助脚本。
- `temp/`：临时调试产物。

`AGENTS.md` 不记录动态进度；静态文档索引和工程进度分别维护在上述指定入口。

### 2.3 构建与数据安全

- 普通构建不得自动向 `bin/worker/` 复制产物。
- 禁止使用 `bin/worker/validproxy.exe` 进行修复验证或问题复现；它可能是陈旧副本。
- 验证必须使用本次构建产生的最新可执行文件。项目现有规范指定优先使用 `bin/` 下产物；若实际 CMake 输出与此不一致，先核对构建配置和产物时间/路径，再确定目标，禁止猜测。
- 测试默认使用测试配置和测试数据库，不得把生产数据库作为默认测试目标。
- 任何可能修改数据库或生产数据的操作，执行前必须确认配置路径、数据库路径、操作范围及是否可恢复。
- 测试数据库名称在现有资料中存在 `test/guindb.db`、`test/guiNDB.db`、`test/guiNDB_empty.db` 等不同写法。**已核验（2026-11-09）**：`test/guiNDB.db`（153 MB）为全量测试库、`test/guiNDB_empty.db`（0.1 MB）为精简库、`test/guindb.db` 不存在；`bin/test_config.json` 指向 `test/guiNDB.db`。未核验实际文件和配置引用前，不得擅自合并、改名或认定它们是同一数据库。

## 3. 任务开始前的上下文读取

根据任务范围执行：

1. 先遵守本文件中的硬约束。
2. 根据主题查阅 `docs/INDEX.md`。
3. 涉及在途任务时读取 `docs/plans/project-plans-tracker.md`。
4. 查阅当前模块相关 Spec、设计文档、开发规范和历史排障记录。
5. 架构调整、跨模块改动、核心模块实现、构建/调试问题或需要历史决策时，若项目记忆工具可用，检索相关记忆。

只读取与任务相关的内容。

### 信息冲突处理

- 区分用户当前明确要求、项目长期规则、当前代码/配置事实和历史记忆。
- 用户当前明确决策不得被旧记忆覆盖；历史记忆也不得覆盖当前代码和验证证据。
- 代码与配置用于核验当前实现，不代表可以擅自违反数据安全或用户明确约束。
- 对构建产物路径、数据库目标、目录规则等存在冲突的事项，先核验；影响安全或行为且无法核实时先澄清。
- 不得静默选择某一条互相矛盾的旧规则并将其描述为已确认事实。

## 4. 研发工作流程

### 4.1 功能开发与重构

1. 明确目标、范围、输入输出、前置条件、边界条件和验收标准。
2. 修改业务代码前，先创建或更新 Spec/设计文档；简单且不改变行为的文档性修改可按实际范围处理。
3. 实现之前或同时，在 `tests/` 中补充 Google Test 回归测试；按项目现有约定使用 `TEST_F`。
4. 遵循相邻代码模式，控制改动范围。
5. 执行构建、相关测试及任务要求的全量/专项验收。
6. 将新建或变更的专题文档登记到 `docs/INDEX.md`，并更新 `docs/plans/project-plans-tracker.md`。

### 4.2 故障修复

1. 先复现并收集证据，提出可证伪的根因假设。
2. 定位根因，明确受影响模块、文件和行为。
3. 修复并补充回归测试。
4. 执行与风险相匹配的验证。
5. 将可复用的根因、证据和修复结论记录到 `docs/bugfix/`（使用已有目录；若目录不存在，先核对文档目录规范，不擅自新建）。
6. 更新 `docs/INDEX.md` 和项目跟踪器。

### 4.3 文档规范

- 新建专题文档放入 `docs/` 下已有的合适子目录；不擅自在 `docs/` 下新建目录。
- 既有入口文件保留原位，不因目录规则自动搬迁。
- Spec、设计、评审、修复等专题文档命名遵循：`[YYYY-MM-DD]-[DocType]-[Module]-[Version].md`。
- Spec 至少包含：目标、输入、输出、前置条件、边界条件和验收标准。
- 新建或变更的专题文档登记到 `docs/INDEX.md`。
- 工程进度只写入 `docs/plans/project-plans-tracker.md`，不得写入本文件。

### 4.4 CLI 工具参数速查表（用户明确要求保留，2026-11-09 登记）

> CLI 入口程序：`validproxy-cli.exe`。完整 CLI 手册以 `docs/INDEX.md` 定向检索为准。

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

## 5. 工具与技能使用原则

### 5.1 能力探测与降级

- 仅使用当前会话实际提供且可调用的工具、MCP 和技能。
- 根据任务语义选择技能；不得仅因引用文本或偶然出现关键词而机械触发。
- 调用前确认工具/技能可用，并遵循其实际参数契约。
- 工具不可用时，明确说明限制；可用等价方式满足同等安全和验证要求时再降级。
- 若缺少必要能力会导致数据安全、正确性或验收无法保证，应暂停对应操作并说明原因。
- 不得声称已调用、检索、写入、构建或测试未实际执行的事项。

### 5.2 CLion MCP 优先

若 CLion MCP 在当前环境可用，开发、定位、测试和调试活动优先使用它：

- 符号与文本定位：优先 `search_symbol`、`search_text`、`search_regex`。
- 静态诊断：优先 `get_file_problems`。
- 行为异常、竞态和死锁：优先使用 `xdebug_*` 断点调试闭环，而非仅依赖临时日志。
- 数据核验：可用时使用数据库连接与 SQL 查询能力。
- 所有 CLion 工具调用显式传入 `projectPath`。
- 含中日韩文本的修改禁用已知可能吞字符的 `clion apply_patch`，改用能可靠保留字符的精确编辑方式。

CLion 检查不能替代项目正式门禁：

- Ninja 构建。
- 全量 CTest 回归。
- UI 自动化验收。
- 项目既有 ASAN、覆盖率和 cppcheck 流程。

CLion 工具名称、参数、已知限制和调试步骤以项目现有 CLion 参考文档为准。引用路径在使用前应通过 `docs/INDEX.md` 核实；不要因本文件保留历史路径就假定该文件当前仍存在。

### 5.3 关键词→技能/方法论路由表（用户明确要求保留，2026-11-09 登记）

> **意图路由辅助**：命中关键词时按 §5.1 先做能力探测与降级，按任务语义判断是否启用技能，不机械触发；技能不存在时明确说明限制并采用等价方式，不虚构调用。

| 用户输入意图包含的关键词 | 匹配的技能/方法论 (Skills) | AI 应当输出的特定行为约束 |
| :--- | :--- | :--- |
| `构思` / `brainstorm` / `头脑风暴` / `创意方案` / `需求分析` / `需求探索` | **brainstorming** | 1. 引导用户明确需求边界、核心目标和约束条件。<br>2. 输出多个可选方案供用户决策。<br>3. 决策完成后再进入实现阶段（路由至 using-superpowers）。 |
| `调整功能` / `开发功能` / `新增功能` / `重构` / `架构调整` / `修改行为` | **using-superpowers** | 1. 严格检查是否**禁止了 `auto`**。<br>2. 优先通过读取 docs/INDEX.md 检索开发规范、历史技术规格设计文档（Spec）。<br>3. 在修改后将其更新至 `docs/plans/project-plans-tracker.md`。 |
| `bug` / `修复` / `fix` / `异常` / `崩溃` / `错误` / `测试失败` / `故障` | **systematic-debugging** | 1. 启动根因分析（RCA）。<br>2. 明确指出受影响的模块文件（如 `XrayApi.cpp`）。<br>3. 提供异常捕获加固方案，并输出修复日志到 `docs/bugfix/`。 |
| `ASAN` / `sanitizer` / `调试` / `debug` / `dump` / `cppcheck` / `覆盖率` / `coverage` / `稳定性` / `MiniDump` | — | 1. **优先按 §5.2 使用 CLion MCP（`xdebug_*` 断点调试 / `get_file_problems`）做定位**。<br>2. 优先引用 **§6.1** 获取构建/测试命令速查。<br>3. 深度工作流（ASAN 输出解读 / dump 分析 / Logger 调优）参考 `docs/project-knowledge.md`。 |
| `计划` / `方案` / `制定计划` / `设计文档` / `design doc` / `spec` / `技术方案` | **writing-plans** | 1. 严格遵循产品/工程视角区分（PRD 与 Spec 隔离）。<br>2. 产出包含输入、输出、边界条件、前置条件的标准 Markdown。 |
| `实现` / `编码` / `写代码` / `implement` | **test-driven-development** | 1. 强制要求在编写实现代码的同时，或之前，在 `tests/` 目录下提供 Google Test（`TEST_F`）测试用例。 |
| `测试` / `test` / `单元测试` / `ctest` | **cmake-build** | 1. 执行 `cmake --build build` 编译。<br>2. 执行 `ctest --test-dir build -V` 运行全量测试。 |
| `构建` / `编译` / `build` / `cmake` / `CI` | **cmake-build** | 1. 按 Debug 模式配置构建：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`。<br>2. 并行编译：`cmake --build build --parallel 8`。 |
| `commit` / `提交` / `git` / `分支` / `branch` / `合并` / `merge` / `推送` / `push` / `PR` / `pull request` / `rebase` / `stash` | **git-master** | 1. 严格遵循该技能的分阶段工作流，确保原子提交和版本管理规范。 |
| `代码审查` / `code review` / `代码评审` / `review` / `PR review` | **requesting-code-review** | 1. 在功能完成并验证测试通过后，提交代码审查。 |
| `完成` / `complete` / `验证通过` / `verify` / `验证完成` | **verification-before-completion** | 1. 在声称任务完成前必须运行所需验证命令，确认输出后再声明完成。 |
| `画图` / `生成图片` / `画一只XX` / `image generation` / `generate image` / `draw a` | **agnes-image-gen** | 1. 提取用户描述，增强为详细英文 prompt（风格、光影、构图）。<br>2. 调用 Agnes Image 2.1 Flash API，下载生成的图像。<br>3. 报告保存路径给用户。 |
| `生成视频` / `制作动画` / `画一段视频` / `video generation` / `create video` / `make a clip` | **agnes-video-gen** | 1. 提取用户描述，增强为详细英文 cinematic prompt。<br>2. 调用 Agnes Video V2.0 API 创建异步任务。<br>3. 轮询任务状态直到 completed，下载 MP4 视频。<br>4. 报告保存路径给用户。 |
| `部署` / `发布` / `release` / `deploy` | **release-skills** | 1. 自动检测版本文件与 changelog，按语义化版本规范发布。 |
| `图片分析` / `截图分析` / `界面分析` / `GUI分析` / `OCR` / `UI界面分析` / `图片理解` | **ui-analyst**（角色；原 `wxgui-analyzer-sub`，2026-11-09 更名对齐角色表） | 1. 使用 `ui-analyst` 角色进行 GUI 截图/界面分析。<br>2. 多模态模型选用建议（主模型 `stepfun/step-3.7-flash:free`，第一备降 `nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free`）。 |
| `记忆` / `Caura` / `recall` / `caura_recall` / `caura_write` / `经验` / `教训` / `历史决策` | **caura-memory** | 1. 遵循 §7 跨会话记忆规范。<br>2. 在关键节点主动检索/写入 Caura，避免跨会话知识丢失。<br>3. 严禁写入密钥、Token、密码或私密原文。 |

## 6. 构建与验证门禁

### 6.1 基础构建与测试

PowerShell 基础命令（逐条执行并检查退出码；前一步失败时，不得把后续结果当作有效验收；若项目当前构建目录或测试注册方式不同，先核对 CMakePresets.json、CMake 配置和实际构建目录，再调整命令）：

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest --test-dir build -V
```

### 6.2 专项验收

- UI 相关变更交付前，执行 `scripts/build-and-test.bat`，并确认相关 UI 自动化验收通过。
- ASAN、覆盖率和静态分析使用项目既有配置与脚本，不自行假设工具链支持情况。
- 局部测试不能称为全量回归。
- 静态分析或 CLion 检查不能称为构建、CTest 或 UI 验收。
- 运行程序进行验证时，使用本次构建的最新产物（`bin/` 下）；严禁使用 `bin/worker/validproxy.exe`。

### 6.3 完成声明与证据

声称构建成功、测试通过或修复完成时，提供：

- 实际执行的命令和目标构建目录。
- 退出码。
- 测试通过、失败、跳过数量或可靠的输出摘要。
- 未执行的验证项目及原因。
- 尚存风险、环境限制和未解决问题。

未执行验证时明确标记"未验证"；不得将计划执行、工具静态检查或推测结果表述为"验证通过"。

## 7. 跨会话项目记忆

项目记忆工具可用时，遵循"检索 → 执行 → 沉淀"：

### 7.1 检索时机

以下任务开始前检索相关历史决策和经验：

- 架构调整、重构、新功能和跨模块修改。
- Xray、gRPC、cURL、SQLite 核心实现。
- 构建、部署、调试、ASAN、MiniDump 或覆盖率问题。
- 涉及 `ProxyBatchTester`、`SubitemUpdaterV2`、`XrayApi` 等既有模块。
- 需要确认历史决策、已验证命令或长期偏好。

### 7.2 写入条件

仅沉淀已确认且可复用的信息：

- 长期偏好与稳定约束。
- 已接受的架构决策及理由。
- 在明确环境中验证过的命令。
- 有证据支持的根因、修复结论和失败教训。

不得沉淀临时待办、未验证猜测、重复规则或私密凭据。严禁保存密钥、Token、密码、证书、私钥、会话令牌和未脱敏私密原文。

正式规范以项目文档为准；记忆保存摘要、适用范围、来源、验证状态、日期及失效/替代条件。工具不可用时不得声称已检索或已保存。

## 8. 角色分工

按任务需要切换角色，不为简单任务机械执行完整角色链：

- **planner**：需求澄清、PRD/Spec 边界、方案和验收设计；不直接修改业务代码。
- **implementer**：按已确认 Spec 实现，遵守 C++17、禁用 `auto`、TDD 和相邻代码模式。
- **debugger**：复现、取证、根因定位、断点调试和修复验证。
- **tester**：构建、CTest、UI 验收、专项测试和验证证据。
- **reviewer**：独立评审；发现按严重程度分级，并锚定真实文件、行号和证据；不直接修改被评审代码。
- **ui-analyst**：GUI 截图、窗口、布局和交互分析；遵循项目 UI 自动化规范。

关键协作要求：

- 功能/重构通常按 planner → implementer → tester → reviewer → 交付推进；debugger 可在需要时介入。
- reviewer 重点检查多线程竞态、锁使用、悬垂指针、重复释放、UAF 和 DLL 加载冲突。
- UI 自动化定位以项目 `tests/ui/UIIds.h` 等正式定位源为准，禁止在测试中随意硬编码控件标识；跨进程 UIA 的验收不能依赖调试器可附加这一假设。
- UI 截图和临时分析产物按项目约定存入 `temp/`。
- 角色交接使用项目跟踪器记录必要状态，不重复劳动。
- 任何角色均不得绕过数据安全、构建和验收门禁。

## 9. 核心模块导航

- Xray 生命周期：`XrayManager` / `XrayInstance`。
- gRPC：`XrayApi`。
- 测试调度：`ProxyBatchTester`。
- 单代理测试：`ProxyTester`。
- 代理查找：`ProxyFinder`。
- 配置：`ConfigReader` / `ConfigGenerator`。
- 订阅更新：`SubitemUpdaterV2`。
- 数据访问：`DatabaseHelper`。
- 网络请求：`UrlFetcher` / `CurlEasyHandle`。
- UI 调度与事件：`AppController` / `Events`。
- 主窗口：`MainFrame`。

完整文件路径、CLI 参数、环境配置、模块规格、UI 自动化细节、CLion 工具说明和历史分析，从 `docs/INDEX.md` 定向检索。
