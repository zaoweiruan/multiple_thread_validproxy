建议把这份 `AGENTS.md` 优化为“核心约束常驻、专题知识按需读取、动态状态单独维护”的**分层记忆**。下面给出一版可直接作为替换草案的精简内容；这是对你上传文档的整理，尚未修改项目文件或写入跨会话记忆。\[[ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/82110049/c0e2694b-8812-4f01-b45b-cc85cebdcf6e/AGENTS.md?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Checksum-Mode=ENABLED&X-Amz-Credential=ASIA2F3EMEYE4I5AURJ5%2F20261109%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261109T060648Z&X-Amz-Expires=3200&X-Amz-Security-Token=IQoJb3JpZ2luX2VjEFYaCXVzLWVhc3QtMSJHMEUCIFsZ0bJQUQ7P8QElJBmIw3AI7qBEHUOEutHw%2Bw%2B26XCZAiEAw9JcyrK9MAqQk63byLqG16KHGEJ5eaVn8hnDEmRVEkkqgAUIHxABGgw2OTk3NTMzMDk3MDUiDCUfNpLSM2FiNTqNxyrdBL81DbsgceQyU3NFMeHrJLpkDFEw9aHAzrDplw7P5ZuD5oO%2FgCZOMugvTwMy5Qst6xbwOdUDBWCI%2BTdJyuwxrplly%2FAs1QjnOC9NzQ9twxsNLbmL%2BRa7wNl0GvTLIbRYwJOFtBhp%2FRk2oyPcVT50m7%2FzsgqdSC3kLHs%2FneBn7mXFPoaUxn2cozdNL%2F3PxNuCEGLF6d3GnouOSpdfzRjMvm1P8ZyL8%2FV8%2BqHe9%2F00ZKggOmza5M8vttVL9TTcdY0RFKQCnOuSvo7Ps8kY1xc1CcL%2BhmNC3AwgrmX5x4C7hnsHeMDfx1Xcum1smAvcgQN5iOGTs%2BrduNs4roNMR7g%2BuFebBO0TXRe6I4epTRMs2VkPSd7FiSgDl6Dd4grmSDpbg8%2BlxfF3KxZaZYMsApvukG0dd4uNt5zwc6jRIkTZgA%2Fbtyd28iyq8oAj9glgLUWsvIlWSq%2BKXhztJbfUVlZOgV2DFCtVOGjZLiWUmNC6ub7xabMe43e2v68H1v8MIQNIXfdGaODUNunHTNSLruNbcOUGOTf7pIgyywWV8kqXLTtwwt0DzHlcMoCXDoo%2Fz7vfk9LtdqpCDVK0zvP0bVuT6EGBmO%2B8W7U2TVbGlgak8uqLX9gTqDsW2vGUIPRpHNlNHN828cIhbn2NTEUa2QtG8gEpbyCv5xPiOgal%2BLlxbgR2ReN2SQKrJJv3oZwBL4H2E5Tldr6y3Ay6uUl9CLlrtYkgFB3IDJ%2BBt9sqiSPSBiVTdg8R8l%2BSUsliXBOCnYMhiuWN9nYXCXAGw%2FJ19Z%2B9ImstLH2RDzEbYvX9W2fKMKXDxdcGOpgBxM%2B7O%2BEkmrIZaVuWfGjAYu1CwM1MfaO7j5K35cufzwkcN5jJFtcorBA8ipuRXLbj2TX%2BPkntFSm6U4F2yCWid6EFK%2Fb3a%2BCARyWaPhvyeU47b4tPuQO12qLi9fJM86tc7MidePS%2B17eyDqCpi7cHZxm2hVgiVkMOYJ1oUv7DVKrzyMNNOaq09L%2BIOU0Q9BNbWH2wQvAvXWU%3D&X-Amz-SignedHeaders=host&x-id=GetObject&X-Amz-Signature=06d064becf8536ef0830c65d832c214d066f231f64a0b2bdad7da36d14710f94)\]

## 一、原文需要优先解决的问题

原文已经包含完整的项目约束，但部分信息重复或互相冲突，建议先做以下调整。\[[ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/82110049/c0e2694b-8812-4f01-b45b-cc85cebdcf6e/AGENTS.md?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Checksum-Mode=ENABLED&X-Amz-Credential=ASIA2F3EMEYE4I5AURJ5%2F20261109%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261109T060648Z&X-Amz-Expires=3200&X-Amz-Security-Token=IQoJb3JpZ2luX2VjEFYaCXVzLWVhc3QtMSJHMEUCIFsZ0bJQUQ7P8QElJBmIw3AI7qBEHUOEutHw%2Bw%2B26XCZAiEAw9JcyrK9MAqQk63byLqG16KHGEJ5eaVn8hnDEmRVEkkqgAUIHxABGgw2OTk3NTMzMDk3MDUiDCUfNpLSM2FiNTqNxyrdBL81DbsgceQyU3NFMeHrJLpkDFEw9aHAzrDplw7P5ZuD5oO%2FgCZOMugvTwMy5Qst6xbwOdUDBWCI%2BTdJyuwxrplly%2FAs1QjnOC9NzQ9twxsNLbmL%2BRa7wNl0GvTLIbRYwJOFtBhp%2FRk2oyPcVT50m7%2FzsgqdSC3kLHs%2FneBn7mXFPoaUxn2cozdNL%2F3PxNuCEGLF6d3GnouOSpdfzRjMvm1P8ZyL8%2FV8%2BqHe9%2F00ZKggOmza5M8vttVL9TTcdY0RFKQCnOuSvo7Ps8kY1xc1CcL%2BhmNC3AwgrmX5x4C7hnsHeMDfx1Xcum1smAvcgQN5iOGTs%2BrduNs4roNMR7g%2BuFebBO0TXRe6I4epTRMs2VkPSd7FiSgDl6Dd4grmSDpbg8%2BlxfF3KxZaZYMsApvukG0dd4uNt5zwc6jRIkTZgA%2Fbtyd28iyq8oAj9glgLUWsvIlWSq%2BKXhztJbfUVlZOgV2DFCtVOGjZLiWUmNC6ub7xabMe43e2v68H1v8MIQNIXfdGaODUNunHTNSLruNbcOUGOTf7pIgyywWV8kqXLTtwwt0DzHlcMoCXDoo%2Fz7vfk9LtdqpCDVK0zvP0bVuT6EGBmO%2B8W7U2TVbGlgak8uqLX9gTqDsW2vGUIPRpHNlNHN828cIhbn2NTEUa2QtG8gEpbyCv5xPiOgal%2BLlxbgR2ReN2SQKrJJv3oZwBL4H2E5Tldr6y3Ay6uUl9CLlrtYkgFB3IDJ%2BBt9sqiSPSBiVTdg8R8l%2BSUsliXBOCnYMhiuWN9nYXCXAGw%2FJ19Z%2B9ImstLH2RDzEbYvX9W2fKMKXDxdcGOpgBxM%2B7O%2BEkmrIZaVuWfGjAYu1CwM1MfaO7j5K35cufzwkcN5jJFtcorBA8ipuRXLbj2TX%2BPkntFSm6U4F2yCWid6EFK%2Fb3a%2BCARyWaPhvyeU47b4tPuQO12qLi9fJM86tc7MidePS%2B17eyDqCpi7cHZxm2hVgiVkMOYJ1oUv7DVKrzyMNNOaq09L%2BIOU0Q9BNbWH2wQvAvXWU%3D&X-Amz-SignedHeaders=host&x-id=GetObject&X-Amz-Signature=06d064becf8536ef0830c65d832c214d066f231f64a0b2bdad7da36d14710f94)\]

| 问题  | 优化处理 |
| --- | --- |
| 技术栈写成“C++20/17”，同时又明确“标准 C++17” | 常驻规则统一写为 C++20；升级标准需要独立决策 |
| 规定验证使用 `bin/` 产物，运行示例却使用 `build/` | 保留 `bin/` 验证红线，实际目标路径以构建配置核验结果为准 |
| 测试命令有 `ctest -V`，也有 `ctest --test-dir build` | 统一显式指定测试构建目录，避免依赖当前工作目录 |
| 测试数据库同时出现 `test/guindb.db` 和 `test/guiNDB.db` | 标记待核验，不擅自合并或改名 |
| 禁止在 `docs/` 根目录放新文档，但又存在根目录文档引用 | 区分既有入口文件与新建专题文档；已有文件不自动搬迁 |
| 技能路由、角色职责、研发流程反复描述同一规则 | 每条硬约束只定义一次，角色表只描述职责 |
| 强制调用特定工具，却未定义工具不可用的处理方式 | 增加能力探测与降级规则，不虚构调用 |
| 版本号、数据库条数、修复历史、模型名称混入长期规则 | 移出常驻记忆，保存在可核验的专题文档中 |

## 二、优化后的 AGENTS.md 草案

以下是基于原文重组的建议版本。其中“工具不可用时的降级”“记忆冲突处理”和“证据要求”属于新增治理规则；项目路径与硬约束来自原文，存在冲突的部分保留为待核验项。\[[ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/82110049/c0e2694b-8812-4f01-b45b-cc85cebdcf6e/AGENTS.md?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Checksum-Mode=ENABLED&X-Amz-Credential=ASIA2F3EMEYE4I5AURJ5%2F20261109%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261109T060648Z&X-Amz-Expires=3200&X-Amz-Security-Token=IQoJb3JpZ2luX2VjEFYaCXVzLWVhc3QtMSJHMEUCIFsZ0bJQUQ7P8QElJBmIw3AI7qBEHUOEutHw%2Bw%2B26XCZAiEAw9JcyrK9MAqQk63byLqG16KHGEJ5eaVn8hnDEmRVEkkqgAUIHxABGgw2OTk3NTMzMDk3MDUiDCUfNpLSM2FiNTqNxyrdBL81DbsgceQyU3NFMeHrJLpkDFEw9aHAzrDplw7P5ZuD5oO%2FgCZOMugvTwMy5Qst6xbwOdUDBWCI%2BTdJyuwxrplly%2FAs1QjnOC9NzQ9twxsNLbmL%2BRa7wNl0GvTLIbRYwJOFtBhp%2FRk2oyPcVT50m7%2FzsgqdSC3kLHs%2FneBn7mXFPoaUxn2cozdNL%2F3PxNuCEGLF6d3GnouOSpdfzRjMvm1P8ZyL8%2FV8%2BqHe9%2F00ZKggOmza5M8vttVL9TTcdY0RFKQCnOuSvo7Ps8kY1xc1CcL%2BhmNC3AwgrmX5x4C7hnsHeMDfx1Xcum1smAvcgQN5iOGTs%2BrduNs4roNMR7g%2BuFebBO0TXRe6I4epTRMs2VkPSd7FiSgDl6Dd4grmSDpbg8%2BlxfF3KxZaZYMsApvukG0dd4uNt5zwc6jRIkTZgA%2Fbtyd28iyq8oAj9glgLUWsvIlWSq%2BKXhztJbfUVlZOgV2DFCtVOGjZLiWUmNC6ub7xabMe43e2v68H1v8MIQNIXfdGaODUNunHTNSLruNbcOUGOTf7pIgyywWV8kqXLTtwwt0DzHlcMoCXDoo%2Fz7vfk9LtdqpCDVK0zvP0bVuT6EGBmO%2B8W7U2TVbGlgak8uqLX9gTqDsW2vGUIPRpHNlNHN828cIhbn2NTEUa2QtG8gEpbyCv5xPiOgal%2BLlxbgR2ReN2SQKrJJv3oZwBL4H2E5Tldr6y3Ay6uUl9CLlrtYkgFB3IDJ%2BBt9sqiSPSBiVTdg8R8l%2BSUsliXBOCnYMhiuWN9nYXCXAGw%2FJ19Z%2B9ImstLH2RDzEbYvX9W2fKMKXDxdcGOpgBxM%2B7O%2BEkmrIZaVuWfGjAYu1CwM1MfaO7j5K35cufzwkcN5jJFtcorBA8ipuRXLbj2TX%2BPkntFSm6U4F2yCWid6EFK%2Fb3a%2BCARyWaPhvyeU47b4tPuQO12qLi9fJM86tc7MidePS%2B17eyDqCpi7cHZxm2hVgiVkMOYJ1oUv7DVKrzyMNNOaq09L%2BIOU0Q9BNbWH2wQvAvXWU%3D&X-Amz-SignedHeaders=host&x-id=GetObject&X-Amz-Signature=06d064becf8536ef0830c65d832c214d066f231f64a0b2bdad7da36d14710f94)\]

```
# AGENTS.md — 项目长期约束与上下文入口

## 1. 用途与边界

本文件只保存：
- 稳定的项目约束。
- 用户明确的长期偏好。
- 文档、代码与工作流入口。
- 必须遵守的安全和验证门禁。

本文件不保存：
- 当前任务进度、临时排障过程、待办状态。
- 数据库记录数量、当前发布版本等易变信息。
- 完整 CLI 手册、完整模块清单、冗长工具说明。
- 密钥、Token、密码、证书、私钥及私密原文。

动态进度唯一入口：
`docs/plans/project-plans-tracker.md`

静态文档分类总入口：
`docs/INDEX.md`

## 2. 项目硬约束

### 2.1 技术栈

- 语言标准：C++17。
- 全部项目 C++ 代码禁止使用 auto 进行类型推导。
- 目标平台：Windows，兼容 MinGW/GCC。
- 构建系统：CMake + Ninja。
- GUI：wxWidgets 3.2+，wxMSW。
- 默认终端：PowerShell；命令必须兼容实际 PowerShell 版本。
- 核心业务：代理验证、订阅管理与网络转发。
- 主要依赖：Xray-core、gRPC、cURL、SQLite。

### 2.2 目录边界

- src/：业务实现。
- include/：公共头文件。
- src/ui/：wxWidgets UI 与控制器。
- tests/：测试源码、测试工具及约定的测试产物。
- test/：测试数据、配置和模拟脚本；禁止放可编译测试源码或可执行文件。
- bin/：生产配置及约定的构建产物。
- bin/worker/：运行时工作目录，不是构建输出目录。
- docs/：项目文档。
- skills/：Agent 技能。
- scripts/：维护与辅助脚本。
- temp/：临时调试产物。

### 2.3 数据与构建安全

- 禁止在普通构建过程中自动向 bin/worker/ 复制产物。
- 禁止使用 bin/worker/validproxy.exe 做修复验证或问题复现。
- 验证必须使用 bin/ 中本次最新构建产物；
  若实际构建输出不同，先核对 CMake 配置，不猜测路径。
- 测试默认使用测试配置及测试数据库。
- 可能修改数据的操作执行前，确认配置路径、数据库路径和影响范围。
- 不得把生产数据库作为默认测试目标。

## 3. 开始任务前的读取顺序

1. 读取本文件。
2. 根据任务查阅 docs/INDEX.md。
3. 涉及在途工作时，读取 docs/plans/project-plans-tracker.md。
4. 读取与当前模块相关的 Spec、设计和历史排障文档。
5. 复杂任务或需要历史决策时，若记忆工具可用，检索相关记忆。

只读取与当前任务有关的材料，不默认加载全部文档。

信息冲突时：
- 明确区分用户当前要求、长期规则、实际代码和历史记忆。
- 实际代码与配置用于核验当前实现。
- 历史记忆不能覆盖新的明确决策或当前验证结果。
- 涉及数据安全、强制门禁或重大行为差异时，先澄清再执行。
- 不静默修正规范中的冲突。

## 4. 研发流程

### 4.1 功能与重构

1. 明确目标、范围、边界和验收条件。
2. 修改代码前创建或更新 Spec/设计文档。
3. 实现之前或同时，在 tests/ 提供 Google Test 测试；
   按项目约定使用 TEST_F。
4. 遵循相邻代码模式，保持改动范围聚焦。
5. 执行构建和相关测试，按交付要求执行全量回归。
6. 登记文档索引并更新项目跟踪器。

### 4.2 故障修复

1. 先复现、取证和提出可证伪假设。
2. 定位根因，指出受影响文件和行为。
3. 修复并补充回归测试。
4. 执行必要验证。
5. 将可复用根因与修复结论写入 docs/bugfix/。
6. 更新 INDEX 和项目跟踪器。

### 4.3 文档规则

- 新建专题文档放入 docs/ 的既有合适子目录。
- 不擅自在 docs/ 下创建新子目录。
- 既有入口文档保留原位，不自动搬迁。
- Spec、设计、评审、修复等文档命名：
  [YYYY-MM-DD]-[DocType]-[Module]-[Version].md
- Spec 至少包含：
  目标、输入、输出、前置条件、边界条件和验收标准。
- 新建或变更的专题文档登记到 docs/INDEX.md。
- 工程进度仅写入项目跟踪器，不写入本文件。

## 5. 工具使用偏好

### 5.1 能力检查

- 仅使用当前环境实际提供的工具。
- 不虚构 skill、MCP、记忆检索、测试执行或写入结果。
- 指定工具不可用时，说明限制，并采用可用的等价方式；
  无法保持必要验证或安全要求时，停止相关步骤并说明原因。
- 技能按任务语义选择，不因引用文本或关键词偶然出现而机械触发。

### 5.2 CLion MCP 优先

CLion MCP 可用时，开发、定位、测试和调试优先使用它。

- 符号与文本定位：search_symbol / search_text / search_regex。
- 静态诊断：get_file_problems。
- 行为、竞态、死锁问题：优先 xdebug_* 断点调试闭环。
- 数据核验：使用数据库连接与 SQL 查询能力。
- 所有 CLion 工具显式传入 projectPath。
- 含中日韩文本的修改禁用 CLion apply_patch，
  使用能可靠保留字符的精确编辑工具。

CLion 不替代：
- 项目 Ninja 构建。
- 全量 CTest 回归。
- UI 一键验收。
- 既有 ASAN、覆盖率和 cppcheck 流程。

详细参考：
`docs/2026-11-06-Reference-CLion-MCP-Usage-v1.1.md`

## 6. 构建与验证门禁

### 6.1 基础构建

PowerShell：

    cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel 8
    ctest --test-dir build -V

逐条执行并检查退出码；前一步失败，不把后续结果当作有效验收。

### 6.2 专项验证

- UI 相关变更交付前：
  执行 scripts/build-and-test.bat，并要求相关验收全部通过。
- ASAN、覆盖率和静态分析：
  使用项目既有配置与脚本，不自行假设工具链支持情况。
- 局部测试不能冒充全量回归。
- CLion 检查不能冒充 CTest 或 UI 验收。

### 6.3 完成声明

声称构建成功、测试通过或修复完成时，提供：
- 实际执行命令。
- 退出码。
- 测试通过、失败、跳过数量或对应输出摘要。
- 未执行项目及原因。
- 尚存风险或环境限制。

没有执行验证时，明确写“未验证”，不得写“验证通过”。

## 7. 跨会话记忆

### 7.1 检索时机

记忆工具可用时，以下任务开始前检索相关历史：
- 架构调整、重构、新功能和跨模块修改。
- Xray、gRPC、cURL、SQLite 核心实现。
- 构建、部署、调试问题。
- 需要确认历史决策或长期用户偏好。

### 7.2 写入条件

仅沉淀：
- 已确认的长期偏好。
- 已接受的架构决策及理由。
- 在明确环境中验证过的命令。
- 已验证的根因、修复结论和失败教训。

不沉淀：
- 未验证猜测、临时日志、重复规则。
- 当前任务进度和临时待办。
- 密钥、Token、密码、证书、私钥和私密原文。

需要外部持久化时，遵循当前环境的授权与确认流程。

### 7.3 推荐记录结构

- 主题：
- 类型：约束 / 偏好 / 决策 / 命令 / 排障经验
- 结论：
- 适用范围：
- 原因：
- 来源：文档、文件或验证记录
- 验证状态：用户确认 / 已验证 / 待核验
- 日期：
- 替代或失效条件：

### 7.4 更新规则

- 同主题优先更新或合并，避免重复追加。
- 新结论替代旧结论时，明确标注替代关系。
- 命令记忆必须带适用环境。
- 工具不可用时，不声称已检索或已保存。
- 正式规范保存在项目文档中，记忆只保存摘要和定位线索。

## 8. 角色分工

- planner：需求、方案和验收设计；不直接修改业务代码。
- implementer：按 Spec 实现并补测试。
- debugger：复现、取证、根因定位和修复验证。
- tester：构建、回归、UI 验收与验证证据。
- reviewer：独立评审，发现锚定文件和行号，不直接修改被评审代码。
- ui-analyst：UI 截图、布局和交互分析，遵循既有 UI 定位规范。

角色按任务需要切换，不为简单任务机械执行完整角色链。
任何角色都不能绕过安全边界和交付门禁。

## 9. 核心代码导航

- Xray 生命周期：XrayManager / XrayInstance。
- gRPC：XrayApi。
- 测试调度：ProxyBatchTester。
- 单代理测试：ProxyTester。
- 代理查找：ProxyFinder。
- 配置：ConfigReader / ConfigGenerator。
- 订阅更新：SubitemUpdaterV2。
- 数据访问：DatabaseHelper。
- 网络请求：UrlFetcher / CurlEasyHandle。
- UI 调度与事件：AppController / Events。
- 主窗口：MainFrame。

完整模块路径、CLI 参数、环境配置及历史分析：
从 docs/INDEX.md 定向检索。
```

## 三、应迁出常驻记忆的内容

建议保留信息，但不要继续全部塞进 `AGENTS.md`。以下内容在原文中已经存在，适合迁入专题文档并由索引链接。\[[ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/82110049/c0e2694b-8812-4f01-b45b-cc85cebdcf6e/AGENTS.md?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Checksum-Mode=ENABLED&X-Amz-Credential=ASIA2F3EMEYE4I5AURJ5%2F20261109%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261109T060648Z&X-Amz-Expires=3200&X-Amz-Security-Token=IQoJb3JpZ2luX2VjEFYaCXVzLWVhc3QtMSJHMEUCIFsZ0bJQUQ7P8QElJBmIw3AI7qBEHUOEutHw%2Bw%2B26XCZAiEAw9JcyrK9MAqQk63byLqG16KHGEJ5eaVn8hnDEmRVEkkqgAUIHxABGgw2OTk3NTMzMDk3MDUiDCUfNpLSM2FiNTqNxyrdBL81DbsgceQyU3NFMeHrJLpkDFEw9aHAzrDplw7P5ZuD5oO%2FgCZOMugvTwMy5Qst6xbwOdUDBWCI%2BTdJyuwxrplly%2FAs1QjnOC9NzQ9twxsNLbmL%2BRa7wNl0GvTLIbRYwJOFtBhp%2FRk2oyPcVT50m7%2FzsgqdSC3kLHs%2FneBn7mXFPoaUxn2cozdNL%2F3PxNuCEGLF6d3GnouOSpdfzRjMvm1P8ZyL8%2FV8%2BqHe9%2F00ZKggOmza5M8vttVL9TTcdY0RFKQCnOuSvo7Ps8kY1xc1CcL%2BhmNC3AwgrmX5x4C7hnsHeMDfx1Xcum1smAvcgQN5iOGTs%2BrduNs4roNMR7g%2BuFebBO0TXRe6I4epTRMs2VkPSd7FiSgDl6Dd4grmSDpbg8%2BlxfF3KxZaZYMsApvukG0dd4uNt5zwc6jRIkTZgA%2Fbtyd28iyq8oAj9glgLUWsvIlWSq%2BKXhztJbfUVlZOgV2DFCtVOGjZLiWUmNC6ub7xabMe43e2v68H1v8MIQNIXfdGaODUNunHTNSLruNbcOUGOTf7pIgyywWV8kqXLTtwwt0DzHlcMoCXDoo%2Fz7vfk9LtdqpCDVK0zvP0bVuT6EGBmO%2B8W7U2TVbGlgak8uqLX9gTqDsW2vGUIPRpHNlNHN828cIhbn2NTEUa2QtG8gEpbyCv5xPiOgal%2BLlxbgR2ReN2SQKrJJv3oZwBL4H2E5Tldr6y3Ay6uUl9CLlrtYkgFB3IDJ%2BBt9sqiSPSBiVTdg8R8l%2BSUsliXBOCnYMhiuWN9nYXCXAGw%2FJ19Z%2B9ImstLH2RDzEbYvX9W2fKMKXDxdcGOpgBxM%2B7O%2BEkmrIZaVuWfGjAYu1CwM1MfaO7j5K35cufzwkcN5jJFtcorBA8ipuRXLbj2TX%2BPkntFSm6U4F2yCWid6EFK%2Fb3a%2BCARyWaPhvyeU47b4tPuQO12qLi9fJM86tc7MidePS%2B17eyDqCpi7cHZxm2hVgiVkMOYJ1oUv7DVKrzyMNNOaq09L%2BIOU0Q9BNbWH2wQvAvXWU%3D&X-Amz-SignedHeaders=host&x-id=GetObject&X-Amz-Signature=06d064becf8536ef0830c65d832c214d066f231f64a0b2bdad7da36d14710f94)\]

- CLI 完整参数表：迁入现有 CLI 使用文档。
- 完整模块与文件映射：迁入架构或代码导航文档。
- 数据库条数、订阅数量、发布版本：迁入环境说明、版本记录或验证报告。
- 特定故障已修复的历史描述：迁入对应 Bugfix 文档。
- CLion 工具全表、参数和坑点：保留在现有 CLion 参考文档。
- 图片、视频生成流程及具体模型名称：迁入对应技能说明；它们不必占用每次 C++ 开发会话的常驻上下文。
- 六角色的重复职责细则：保留简表，详细规则引用统一流程。

实际替换前，应先核验三项：构建产物的真实路径、测试数据库的准确文件名，以及现有文档目录与引用是否匹配。原文对这些位置存在不一致，当前草案没有把任何一方擅自认定为事实。\[[ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/82110049/c0e2694b-8812-4f01-b45b-cc85cebdcf6e/AGENTS.md?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Checksum-Mode=ENABLED&X-Amz-Credential=ASIA2F3EMEYE4I5AURJ5%2F20261109%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261109T060648Z&X-Amz-Expires=3200&X-Amz-Security-Token=IQoJb3JpZ2luX2VjEFYaCXVzLWVhc3QtMSJHMEUCIFsZ0bJQUQ7P8QElJBmIw3AI7qBEHUOEutHw%2Bw%2B26XCZAiEAw9JcyrK9MAqQk63byLqG16KHGEJ5eaVn8hnDEmRVEkkqgAUIHxABGgw2OTk3NTMzMDk3MDUiDCUfNpLSM2FiNTqNxyrdBL81DbsgceQyU3NFMeHrJLpkDFEw9aHAzrDplw7P5ZuD5oO%2FgCZOMugvTwMy5Qst6xbwOdUDBWCI%2BTdJyuwxrplly%2FAs1QjnOC9NzQ9twxsNLbmL%2BRa7wNl0GvTLIbRYwJOFtBhp%2FRk2oyPcVT50m7%2FzsgqdSC3kLHs%2FneBn7mXFPoaUxn2cozdNL%2F3PxNuCEGLF6d3GnouOSpdfzRjMvm1P8ZyL8%2FV8%2BqHe9%2F00ZKggOmza5M8vttVL9TTcdY0RFKQCnOuSvo7Ps8kY1xc1CcL%2BhmNC3AwgrmX5x4C7hnsHeMDfx1Xcum1smAvcgQN5iOGTs%2BrduNs4roNMR7g%2BuFebBO0TXRe6I4epTRMs2VkPSd7FiSgDl6Dd4grmSDpbg8%2BlxfF3KxZaZYMsApvukG0dd4uNt5zwc6jRIkTZgA%2Fbtyd28iyq8oAj9glgLUWsvIlWSq%2BKXhztJbfUVlZOgV2DFCtVOGjZLiWUmNC6ub7xabMe43e2v68H1v8MIQNIXfdGaODUNunHTNSLruNbcOUGOTf7pIgyywWV8kqXLTtwwt0DzHlcMoCXDoo%2Fz7vfk9LtdqpCDVK0zvP0bVuT6EGBmO%2B8W7U2TVbGlgak8uqLX9gTqDsW2vGUIPRpHNlNHN828cIhbn2NTEUa2QtG8gEpbyCv5xPiOgal%2BLlxbgR2ReN2SQKrJJv3oZwBL4H2E5Tldr6y3Ay6uUl9CLlrtYkgFB3IDJ%2BBt9sqiSPSBiVTdg8R8l%2BSUsliXBOCnYMhiuWN9nYXCXAGw%2FJ19Z%2B9ImstLH2RDzEbYvX9W2fKMKXDxdcGOpgBxM%2B7O%2BEkmrIZaVuWfGjAYu1CwM1MfaO7j5K35cufzwkcN5jJFtcorBA8ipuRXLbj2TX%2BPkntFSm6U4F2yCWid6EFK%2Fb3a%2BCARyWaPhvyeU47b4tPuQO12qLi9fJM86tc7MidePS%2B17eyDqCpi7cHZxm2hVgiVkMOYJ1oUv7DVKrzyMNNOaq09L%2BIOU0Q9BNbWH2wQvAvXWU%3D&X-Amz-SignedHeaders=host&x-id=GetObject&X-Amz-Signature=06d064becf8536ef0830c65d832c214d066f231f64a0b2bdad7da36d14710f94)\]