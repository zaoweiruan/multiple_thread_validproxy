# 实施计划：ConfigPipeline 组件化重构 v1.6

## 概述

基于 `docs/design/2026-09-22-Design-ConfigPipeline-Component-v1.0.md` (v1.6) 实施组件化重构。

**总工时**: 57h | **阶段数**: 11 | **预计完成**: 2026-09-29

---

## 阶段计划

### 阶段 1：ComponentBundle 表结构 + DAO + 数据库迁移 (4h)

**任务**:
- [ ] 1.1 创建 ComponentBundle 表结构（SQLite DDL）
- [ ] 1.2 创建 ComponentBundle 数据结构体
- [ ] 1.3 创建 ComponentBundleDAO 类（CRUD + 版本管理）
- [ ] 1.4 添加数据库迁移脚本（v1.5 → v1.6）
- [ ] 1.5 添加种子数据（base_xray_001, base_singbox_001 等）
- [ ] 1.6 单元测试（DAO CRUD + 版本递增）

**交付物**:
- `include/config/ComponentBundle.h`
- `src/config/ComponentBundle.cpp`
- `include/config/ComponentBundleDAO.h`
- `src/config/ComponentBundleDAO.cpp`
- `tests/test_component_bundle.cpp`

---

### 阶段 2：组件类型元数据结构 + 注册表 (4h)

**任务**:
- [ ] 2.1 定义 ComponentType 枚举（15 个类型）
- [ ] 2.2 定义 ComponentMetadata 结构（配置节、单例标志、组合策略）
- [ ] 2.3 创建 ComponentRegistry 单例（注册/查询/校验）
- [ ] 2.4 实现 CoreType 过滤（Xray vs SingBox 支持差异）
- [ ] 2.5 单元测试（注册表查询 + CoreType 过滤）

**交付物**:
- `include/config/ComponentRegistry.h`
- `src/config/ComponentRegistry.cpp`
- `tests/test_component_registry.cpp`

---

### 阶段 3：组件解析器核心逻辑 (7h)

**任务**:
- [ ] 3.1 定义 ComponentConfig 结构（type, params, id）
- [ ] 3.2 实现 JSON 解析（BundleChain → ComponentConfig 列表）
- [ ] 3.3 实现占位符替换（${socksPort} 等）
- [ ] 3.4 实现 CoreType 过滤（跳过不支持的组件）
- [ ] 3.5 实现组件合并（override vs merge vs append）
- [ ] 3.6 单元测试（解析 + 替换 + 过滤 + 合并）

**交付物**:
- `include/config/ComponentParser.h`
- `src/config/ComponentParser.cpp`
- `tests/test_component_parser.cpp`

---

### 阶段 4：冲突检测算法 (5h)

**任务**:
- [ ] 4.1 定义 Conflict 结构（含 sourceBundle1/2, isCrossBundle）
- [ ] 4.2 实现单例冲突检测（同包 ERROR，跨包 WARN）
- [ ] 4.3 实现 Tag 冲突检测（同包跨包均 ERROR）
- [ ] 4.4 实现依赖检测（routing 引用不存在的 inbound/outbound）
- [ ] 4.5 实现参数格式校验
- [ ] 4.6 单元测试（跨包冲突场景）

**交付物**:
- `include/config/ConflictDetector.h`
- `src/config/ConflictDetector.cpp`
- `tests/test_conflict_detector.cpp`

---

### 阶段 5：BundleChain 解析器 (5h)

**任务**:
- [ ] 5.1 定义 BundleChain 结构（bundleIds, params）
- [ ] 5.2 实现 BundleChainResolver（加载多个 Bundle 并合并）
- [ ] 5.3 实现版本同步（Bundle 更新 → 模板标记过期）
- [ ] 5.4 实现占位符参数提取
- [ ] 5.5 单元测试（多 Bundle 组合 + 版本同步）

**交付物**:
- `include/config/BundleChainResolver.h`
- `src/config/BundleChainResolver.cpp`
- `tests/test_bundle_chain_resolver.cpp`

---

### 阶段 6：模板定义重构 (4h)

**任务**:
- [ ] 6.1 更新 FullConfigTemplateItem 表（添加 BundleChain, TemplateType, BundleVersion 列）
- [ ] 6.2 更新 PipelineDefinition 结构（bundleChain 字段，优先级：bundleChain > components > steps）
- [ ] 6.3 实现模板状态指示（✅一致/⚠️已过期）
- [ ] 6.4 实现数据库迁移脚本
- [ ] 6.5 单元测试（模板加载 + 状态检查）

**交付物**:
- 更新 `include/FullConfigTemplateDAO.h`
- 更新 `src/FullConfigTemplateDAO.cpp`
- 更新 `tests/test_config_generator.cpp`

---

### 阶段 7：步骤→组件转换 (3h)

**任务**:
- [ ] 7.1 创建 StepToComponentMapping 表
- [ ] 7.2 实现 10 个步骤类型的转换逻辑
- [ ] 7.3 实现自动转换（loadPipeline 时检测旧格式）
- [ ] 7.4 单元测试（转换正确性）

**交付物**:
- `include/config/StepToComponentMapper.h`
- `src/config/StepToComponentMapper.cpp`
- `tests/test_step_to_component_mapper.cpp`

---

### 阶段 8：UI 组件列表视图 + BundleChain 编辑器 (10h)

**任务**:
- [ ] 8.1 创建 ComponentListPanel（组件列表视图）
- [ ] 8.2 创建 BundleChainEditor（Bundle 选择器 + 参数编辑）
- [ ] 8.3 创建 BundleSelectorDialog（Bundle 选择对话框）
- [ ] 8.4 实现 CoreType 适配（根据 CoreType 显示可用组件）
- [ ] 8.5 实现冲突提示（ERROR/WARN 显示）
- [ ] 8.6 实现模板状态指示（✅/⚠️）
- [ ] 8.7 UI 集成测试

**交付物**:
- `src/ui/ComponentListPanel.h/cpp`
- `src/ui/BundleChainEditor.h/cpp`
- `src/ui/BundleSelectorDialog.h/cpp`
- 更新 `src/ui/ConfigDialog.h/cpp`

---

### 阶段 9：配置生成机制 (6h)

**任务**:
- [ ] 9.1 实现 ConfigGenerator 接口
- [ ] 9.2 实现占位符替换（replaceJsonValue 递归）
- [ ] 9.3 实现 ConfigCache（线程安全，shared_mutex）
- [ ] 9.4 实现错误处理策略（编辑时 vs 运行时）
- [ ] 9.5 集成到 ConfigPipeline::execute()
- [ ] 9.6 单元测试（生成 + 缓存 + 错误处理）

**交付物**:
- `include/config/ConfigGenerator.h`
- `src/config/ConfigGenerator.cpp`
- 更新 `src/config/ConfigPipeline.cpp`

---

### 阶段 10：JSON Schema 校验 + 数据库迁移脚本 (3h)

**任务**:
- [ ] 10.1 创建 BundleChain JSON Schema 文件
- [ ] 10.2 创建 ComponentBundle JSON Schema 文件
- [ ] 10.3 实现 Schema 校验器
- [ ] 10.4 创建数据库迁移脚本（v1.5 → v1.6）
- [ ] 10.5 单元测试（Schema 校验）

**交付物**:
- `schemas/BundleChain.schema.json`
- `schemas/ComponentBundle.schema.json`
- `include/config/SchemaValidator.h`
- `src/config/SchemaValidator.cpp`
- `scripts/migrate_v1_6.sql`

---

### 阶段 11：单元测试 + 集成测试 (6h)

**任务**:
- [ ] 11.1 补充阶段 1-10 的单元测试
- [ ] 11.2 创建集成测试（端到端流程）
- [ ] 11.3 创建跨包冲突测试
- [ ] 11.4 创建性能测试（大 Bundle 组合）
- [ ] 11.5 运行全量测试（ctest --parallel 4）
- [ ] 11.6 修复 flaky tests

**交付物**:
- 更新 `tests/` 目录
- 更新 `CMakeLists.txt`（添加新测试目标）

---

## 实施顺序

```
阶段 1 ──→ 阶段 2 ──→ 阶段 3 ──→ 阶段 4 ──→ 阶段 5
                                              │
                                              ↓
阶段 10 ←── 阶段 9 ←── 阶段 8 ←── 阶段 7 ←── 阶段 6
                                              │
                                              ↓
                                         阶段 11（最终测试）
```

**可并行**:
- 阶段 2 和阶段 10（Schema）可并行
- 阶段 8 和阶段 9 可并行
- 阶段 11 最后执行

---

## 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 数据库迁移失败 | 现有配置丢失 | 备份 + 回滚脚本 |
| 旧配置不兼容 | 用户配置无法使用 | 双模式支持 + 自动转换 |
| UI 复杂度过高 | 开发时间超支 | 分阶段交付 MVP |
| 跨包冲突误报 | 阻止有效配置 | WARN 级别不阻止保存 |

---

## 验证标准

每个阶段完成后必须：
1. ✅ 编译通过（cmake --build build --parallel 8）
2. ✅ 单元测试通过（ctest -R <阶段名称>）
3. ✅ 无内存泄漏（AddressSanitizer）
4. ✅ 代码审查通过

---

## 变更记录

| 日期 | 版本 | 变更 |
|------|------|------|
| 2026-09-22 | v1.0 | 初始实施计划 |
