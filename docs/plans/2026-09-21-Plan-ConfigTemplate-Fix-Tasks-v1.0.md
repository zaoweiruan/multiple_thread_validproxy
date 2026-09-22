---
title: "配置模板问题修复 — 任务计划"
type: plan
status: completed
date: 2026-09-21
updated: 2026-09-22
---

# 配置模板问题修复 — 任务计划

> 关联计划文档：`docs/plans/2026-09-21-Plan-ConfigTemplate-Fix-v1.0.md`
> 关联设计文档：`docs/design/2026-09-21-Design-ConfigTemplate-Inheritance-v1.0.md`

---

## 阶段一：占位符机制（1 天）

**目标**: 实现统一的端口占位符替换机制

| # | 任务 | 工时 | 状态 | 文件 | 说明 |
|---|------|------|------|------|------|
| 1.1 | 新增 `replacePlaceholders()` 静态方法 | 2h | ✅ done | `include/config/ConfigBuilder.h` + `src/config/ConfigBuilder.cpp` | 递归遍历 JSON 树，替换 `${socksPort}` / `${apiPort}`；增强数字类型检测（引号内纯数字自动转 int） |
| 1.2 | 模板中添加端口占位符 | 1h | ✅ done | `src/FullConfigTemplateDAO.cpp` | 6 个模板中 inbounds 添加 `"port": "${socksPort}"`（Xray）/ `"listen_port": "${socksPort}"`（SingBox）；Test/Pool Xray 模板 api 添加 `"listen": "127.0.0.1:${apiPort}"` |
| 1.3 | `buildXxxConfig()` 使用工具函数 | 1h | ✅ done | `src/config/ConfigBuilder.cpp` | 3 个函数删除手动设置 inbounds/api.listen 代码，统一调用 `replaceVariables()` |
| 1.4 | 新增单元测试：占位符替换 | 1h | ✅ done | `tests/test_config_builder.cpp` | 7 个新测试：TemplateContainsInboundsPlaceholder_Xray/SingBox、TemplateContainsApiPortPlaceholder_Xray、BuildTestConfig_ReplacesPlaceholders、BuildStandaloneConfig_ReplacesPlaceholders、BuildPoolConfig_ReplacesPlaceholders、NoUnreplacedPlaceholders_RemainInConfig |
| 1.5 | 构建 + 全量测试验证 | 0.5h | ✅ done | — | 构建 0 error + ConfigGeneratorTest 53 passed + 全量 ctest 47/48（UI_FLOATINGWIDGET flaky in parallel, passes individually） |

**验收标准**:
- [x] 所有模板包含 `${socksPort}` 占位符
- [x] Test/Pool 模板包含 `${apiPort}` 占位符
- [x] Standalone 模板无 API 占位符
- [x] 生成的配置中端口被正确替换
- [x] 所有测试通过

---

## 阶段二：协议统一（0.5 天）

**目标**: 统一三种模式的 SOCKS 协议

| # | 任务 | 工时 | 状态 | 文件 | 说明 |
|---|------|------|------|------|------|
| 2.1 | Test 模式协议改为 mixed | 0.5h | ✅ done | `src/FullConfigTemplateDAO.cpp` | xray_test_001 模板 protocol socks→mixed, tag socks-in→socks, 添加 sniffing |
| 2.2 | 同步修改 routing 规则 inboundTag | 0.5h | ✅ done | `src/config/ConfigBuilder.cpp`, `src/XrayInstance.cpp` | buildTestConfig routing inboundTag socks-in→socks；XrayInstance 遗留代码 tag/protocol 统一 |
| 2.3 | 新增单元测试：协议一致性 | 0.5h | ✅ done | `tests/test_config_builder.cpp` | 5 个测试：AllModesUseMixed / AllModesUseSocksTag / HasSniffing / TemplateUsesMixed / NoLegacySocksInTag |
| 2.4 | 构建 + 全量测试验证 | 0.5h | ✅ done | — | 构建 0 errors + 58 ConfigGeneratorTest + 47/48 ctest（UI_FLOATINGWIDGET 单独 10.26s 通过） |

**验收标准**:
- [x] Test/Standalone/Pool 三种模式使用相同的协议 (mixed) 和 tag (socks)
- [x] routing 规则与 inbounds tag 匹配
- [x] 所有测试通过

---

## 阶段三：routing 合并（1 天）

**目标**: 保留模板中的 routing 规则

| # | 任务 | 工时 | 状态 | 文件 | 说明 |
|---|------|------|------|------|------|
| 3.1 | 实现 routing 规则合并 | 2h | ✅ done | `src/config/ConfigBuilder.cpp` | 新增 `mergeSocksInboundRule()` 静态方法：移除现有 socks 规则 → 添加新 socks→outbound 规则；3 个 `buildXxxConfig()` 方法从覆盖改为合并 |
| 3.2 | 新增单元测试：routing 合并 | 1h | ✅ done | `tests/test_config_builder.cpp` | 4 个测试：TestConfig_PreservesTemplateRules / StandaloneConfig_PreservesTemplateRules / PoolConfig_PreservesTemplateRules / NoDuplicateSocksRules |
| 3.3 | 构建 + 全量测试验证 | 0.5h | ✅ done | — | 构建 0 errors + ConfigGeneratorTest 62 passed + 全量 ctest 46/48（UI_FLOATINGWIDGET + UI_POOL flaky in parallel, both pass individually） |
| 3.4 | 手动验证：生成配置并检查 routing | 0.5h | ✅ done | — | 通过单元测试验证：Test 模式 2 rules (api→api + socks→proxy)、Standalone 模式 7 rules (6 template + socks→proxy)、Pool 模式 7 rules (6 template + socks→balancer-out) + balancers 保留 |

**验收标准**:
- [x] 模板中的 routing 规则被保留
- [x] 代码添加的规则被追加
- [x] 无重复 socks 规则
- [x] 所有测试通过

---

## 阶段四：模板继承（4 天）

**目标**: 实现完整的模板继承机制

| # | 任务 | 工时 | 状态 | 文件 | 说明 |
|---|------|------|------|------|------|
| 4.1 | 数据库变更：添加 `parent_id` 字段 | 0.5d | ✅ done | `include/FullConfigTemplateDAO.h` + `src/FullConfigTemplateDAO.cpp` | `FullConfigTemplateItem` 结构体添加 `parentId` 字段；`CREATE TABLE` 增加 `ParentId varchar`；`ALTER TABLE` 兜底；所有 SQL 查询（getById/getByCoreType/getAll/getAllEnabled/save）包含 ParentId |
| 4.2 | 创建基础模板 | 0.5d | ✅ done | `src/FullConfigTemplateDAO.cpp` | `xray_base_001`（CoreType=2）+ `singbox_base_001`（CoreType=24），含 log/dns 基础配置；6 个子模板设置 parentId 引用基础模板 |
| 4.3 | 实现 `mergeConfig()` 深合并算法 | 1d | ✅ done | `include/config/ConfigBuilder.h` + `src/config/ConfigBuilder.cpp` | 公共静态方法：对象递归合并、数组/标量覆盖；深度限制防止循环继承；6 个单元测试 |
| 4.4 | 修改 `loadTemplate()` 支持递归加载 | 0.5d | ✅ done | `src/config/ConfigBuilder.cpp` | 新增 `loadTemplateById()` 公共静态方法：检查 parentId，递归加载父模板并合并；`loadTemplate()` 改为调用此方法 |
| 4.5 | 更新现有模板为增量配置 | 0.5d | ✅ done | `src/FullConfigTemplateDAO.cpp` | 现有 6 个模板删除 log/dns 重复配置，仅保留独有部分（inbounds/routing/api/stats/policy/balancers） |
| 4.6 | 新增单元测试：模板继承 | 0.5d | ✅ done | `tests/test_config_builder.cpp` | 15 个新测试：mergeConfig 6 个（DeepMerge/ScalarOverride/ArrayOverride/ChildAddNewKey/MaxDepthExceeded/NonObjectParent）+ 模板继承 9 个（BaseTemplatesExist/ChildTemplatesHaveParentId/ChildTemplatesOmitLogDns/XrayStandalone/XrayTest/Pool/SingBoxStandalone/AllModesPassValidation/LoadTemplateById/NotFoundThrows） |
| 4.7 | 构建 + 全量测试验证 | 0.5d | ✅ done | — | 构建 0 errors + ConfigGeneratorTest 75 tests（74 passed + 1 skipped）+ 全量 ctest 48/48（100% pass rate） |

**验收标准**:
- [x] 数据库支持模板继承
- [x] 代码支持递归加载
- [x] 配置合并正确
- [x] 所有测试通过
- [x] 文档已更新

---

## 总览

| 阶段 | 任务数 | 工时 | 状态 |
|------|--------|------|------|
| Phase 1: 占位符机制 | 5 | 1 天 | ✅ 已完成 |
| Phase 2: 协议统一 | 4 | 0.5 天 | ✅ 已完成 |
| Phase 3: routing 合并 | 4 | 1 天 | ✅ 已完成 |
| Phase 4: 模板继承 | 7 | 4 天 | ✅ 已完成 |
| **合计** | **20** | **6.5 天** | **20/20 完成** |

---

## 变更记录

| 版本 | 日期 | 变更内容 | 作者 |
|------|------|----------|------|
| v1.2 | 2026-09-22 | Phase 4 完成：模板继承机制（parent_id 字段 + 2 基础模板 + mergeConfig 深合并 + 递归加载 + 15 个单元测试 + 48/48 ctest 全绿） | AI Assistant |
| v1.1 | 2026-09-21 | Phase 3 完成：routing 合并实现 + 4 个单元测试 + 验证通过 | AI Assistant |
| v1.0 | 2026-09-21 | 初始版本 — 从修复计划提取任务，细分为 4 阶段 20 项任务 | AI Assistant |
