## 统一文档命名规范(Unified-document-naming-conventions)

| 文档类型 | 推荐英文简称 | 建议中文名示例 | 主要内容简述 |
| --- | --- | --- | --- |
| 产品需求文档 | PRD | 《xxx 功能 PRD》 | 从用户/业务视角描述要做什么、为什么做，包含用户故事、场景、优先级、验收标准等。 [qracorp](https://qracorp.com/guides_checklists/use-the-most-common-requirements-documents/) |
| 软件需求规格 | SRS / SRD | 《xxx 服务 SRS》 | 从工程视角细化需求，覆盖功能、非功能、约束、接口和验收标准，通常是设计和开发的输入。 [altamira](https://www.altamira.ai/blog/types-of-software-development-documentation/) |
| 技术方案 / 技术规格 | Spec | 《xxx 模块 Tech Spec》 | 针对某个需求或改动的详细技术实现方案，包含设计思路、关键技术点、数据结构、接口、边界与风险等。 [ardura](https://ardura.consulting/glossary/technical-specifications/) |
| 系统/服务设计文档 | SDD / Design | 《xxx 系统 Design Doc》 | 描述系统或服务的结构、组件划分、数据流、依赖关系等，是整体或中观层面的设计说明。 [slite](https://slite.com/learn/engineering-documentation) |
| 架构文档 | SAD / Architecture | 《xxx 平台架构文档》 | 高层架构蓝图，包括分层、子系统、技术选型、容量规划和非功能属性（性能、可用性等）。 [docuwriter](https://www.docuwriter.ai/posts/software-design-documentation-example) |
| 架构决策记录 | ADR | 《ADR-0001 使用 Kafka 作为消息总线》 | 一条记录一个架构决策，说明背景、问题、候选方案、取舍及影响，方便后续追溯决策原因。 [adr.github](https://adr.github.io) |
| 接口文档 | API Doc | 定义对外 API：路径、方法、请求参数、响应体、错误码、示例等，可对应 OpenAPI/Swagger 文档。 [slite](https://slite.com/learn/engineering-documentation) |
| 测试计划 | Test Plan | 列出测试范围、策略、环境、里程碑、负责人和资源，规划本次版本怎么测、测到什么程度。 [altamira](https://www.altamira.ai/blog/types-of-software-development-documentation/) |
| 测试用例 / 测试规格 | Test Spec / Test Case | 《xxx 模块 Test Spec》 | 结构化列出具体测试用例：前置条件、步骤、输入、预期结果，用于执行和回归测试。 [slite](https://slite.com/learn/engineering-documentation) |
| 运维手册 | Runbook / Playbook | 《xxx 系统 Runbook》 | 定义日常运维和故障处理步骤：如何巡检、如何扩容、常见告警处理流程和回滚步骤等。 [slite](https://slite.com/learn/engineering-documentation) |
| 部署说明 | Deployment Guide | 《xxx 服务 Deployment Guide》 | 描述部署/升级/回滚流程、环境依赖、配置项说明以及注意事项，通常配合 CI/CD 使用。 [altamira](https://www.altamira.ai/blog/types-of-software-development-documentation/) |
| 发版说明 | Release Notes | 《Release Notes - vX.Y.Z》 | 记录版本变更列表、重大新特性、兼容性变更、修复缺陷和已知问题等。 [altamira](https://www.altamira.ai/blog/types-of-software-development-documentation/) |
| 项目计划 | Project Plan | 《xxx 项目计划》 | 描述项目目标、范围、里程碑、资源分配、风险和沟通机制，偏管理视角。 [linkedin](https://www.linkedin.com/pulse/different-types-technical-document-you-must-know-adamocompany-l7xdc) |
| 路线图 | Roadmap | 《2026 产品 Roadmap》 | 以时间轴展示产品/平台未来版本规划和主要里程碑，用于对齐中长期方向。 [slite](https://slite.com/learn/engineering-documentation) |
| 会议纪要 | Meeting Minutes | 《xxx 方案评审会议纪要》 | 固化会议结论、决策、行动项、责任人和截止时间，避免信息仅停留在口头。 [slite](https://slite.com/learn/engineering-documentation) |
| 代码仓库入口文档 | README | 《README.md》（固定） | 仓库入口说明：项目简介、目录结构、依赖、构建/运行方式、常见问题等。 [slite](https://slite.com/learn/engineering-documentation) |
| 代码规范 | Coding Guidelines |  | 统一命名风格、格式、错误处理、日志、安全等编码约定，可按语言/端拆分。 [learn.microsoft](https://learn.microsoft.com/en-us/dotnet/standard/design-guidelines/general-naming-conventions) |
| bug修复 | Bugfix | 《xxx 模块 Bugfix》 | title、type、status、date、origin、Summary、Problem Frame、Requirements、Scope Boundaries、Context & ResearchUsage、Options、、Key Technical Decisions、Implementation (Already Completed)、System-Wide Impact、Risks & Mitigation、Documentation / Operational Notes、Sources & References。 [learn](E:\eclipse_workspace\multiple_thread_validproxy\docs\plans\2026-05-08-001-fix-sync-logger-plan.md) |
| 分析报告 | Report | 《xxx 分析报告》 | title、type、status、date、origin、Summary、etc。 [learn](E:\eclipse_workspace\multiple_thread_validproxy\docs\reports\2026-05-27-toolbar-cancel-test-button-report.md) |

## 统一文件命名格式
	1. 文件格式Markdown
	2. 扩展名.md	

- 格式建议：`[YYYY-MM-DD]-[DocType]-[Project]-[Module]-[Version].[ext]`  
- 示例：`2026-06-02-PRD-Payments-Checkout-v1.2.md`。
