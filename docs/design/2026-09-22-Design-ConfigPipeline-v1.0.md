---
title: "配置生成管线（ConfigPipeline）设计方案"
type: design
status: draft
date: 2026-09-22
version: "1.0"
---

# 配置生成管线（ConfigPipeline）设计方案

## 1. 背景与动机

### 1.1 现状问题

当前 `ConfigBuilder` 将配置生成逻辑硬编码在 C++ 代码中，存在以下问题：

| 问题 | 描述 | 影响 |
|------|------|------|
| 新增模式需改代码 | 新增 Test/Standalone/Pool 变体必须修改 `loadTemplate()` 的 if-else 映射 | 编译周期长，无法运行时切换 |
| 标签名硬编码 | `proxy`/`direct`/`block`/`socks`/`balancer-out`/`px-` 等标签散落在代码中 | 用户无法自定义标签命名 |
| 步骤顺序固定 | 每个模式的步骤顺序写死在方法体内 | 无法按需调整执行顺序 |
| 步骤类型封闭 | 新增步骤类型需修改 `ConfigBuilder` 类 | 违反开闭原则 |
| 无 UI 可视化 | 配置生成逻辑不可见 | 用户无法理解/调整生成行为 |

### 1.2 设计目标

参照项目已有的**自动任务步骤编排**（`auto_task.steps` 数组）模式，将配置生成重构为**可编排的步骤管线**：

- **G1**: 零代码新增配置模式变体（仅添加 JSON 配置）
- **G2**: 所有标签名、协议类型、步骤参数可配置
- **G3**: 步骤类型可扩展（新增 handler 无需修改管线引擎）
- **G4**: 保留向后兼容（现有模板/占位符机制不变）
- **G5**: 可在 UI 配置界面中可视化编排（后续迭代）

---

## 2. 架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────┐
│                    ConfigPipeline                        │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ PipelineCfg │  │  StepRegistry │  │  PipelineExec │  │
│  │ (JSON解析)   │  │  (步骤注册表) │  │  (步骤执行器)  │  │
│  └─────────────┘  └──────────────┘  └───────────────┘  │
│         │                  │                │            │
│         ▼                  ▼                ▼            │
│  ┌─────────────────────────────────────────────────┐    │
│  │              StepHandler 接口                     │    │
│  │  + execute(config, params, ctx) → void           │    │
│  └─────────────────────────────────────────────────┘    │
│         │                  │                │            │
│         ▼                  ▼                ▼            │
│  ┌──────────┐     ┌──────────────┐  ┌──────────────┐   │
│  │ Replace  │     │  AddOutbound │  │  MergeRoute  │   │
│  │Variables │     │              │  │              │   │
│  └──────────┘     └──────────────┘  └──────────────┘   │
└─────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│                   ConfigBuilder (现有)                     │
│  build() → 委托给 ConfigPipeline.execute()               │
│  loadTemplate() / mergeConfig() → 保持不变                │
└─────────────────────────────────────────────────────────┘
```

### 2.2 核心组件

#### 2.2.1 StepHandler 接口

```cpp
// include/config/StepHandler.h
class StepHandler {
public:
    virtual ~StepHandler() = default;
    virtual std::string type() const = 0;  // 步骤类型标识
    virtual void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const = 0;
};
```

#### 2.2.2 PipelineContext

```cpp
// include/config/PipelineContext.h
struct PipelineContext {
    ProxyType proxyType;
    const db::models::Profileitem* profile;  // nullable
    const ConfigVariables& vars;
    sqlite3* db;
    std::string clientName;
};
```

#### 2.2.3 StepRegistry（步骤注册表）

```cpp
// include/config/StepRegistry.h
class StepRegistry {
public:
    static StepRegistry& instance();
    void registerHandler(const std::string& type, std::unique_ptr<StepHandler> handler);
    const std::shared_ptr<const StepHandler> getHandler(const std::string& type) const;
    std::vector<std::string> getRegisteredTypes() const;
private:
    std::unordered_map<std::string, std::shared_ptr<const StepHandler>> handlers_;
};
```

#### 2.2.4 PipelineConfig（管线配置）

```cpp
// include/config/PipelineConfig.h
struct StepConfig {
    std::string type;              // 步骤类型 (replace_variables, add_outbound, ...)
    boost::json::object params;    // 步骤参数
};

struct PipelineDefinition {
    std::string templateId;               // 基础模板 ID
    std::vector<StepConfig> steps;        // 步骤列表（按顺序执行）
};

// 配置结构: config.json → "config_pipeline" → {mode} → {template, steps[]}
// 模式 key: "test" / "standalone" / "pool"
```

#### 2.2.5 ConfigPipeline（管线执行器）

```cpp
// include/config/ConfigPipeline.h
class ConfigPipeline {
public:
    ConfigPipeline(sqlite3* db);
    void loadConfig(const boost::json::value& pipelineConfig);
    boost::json::value execute(
        ConfigMode mode,
        const db::models::Profileitem& profile,
        const ConfigVariables& vars) const;
private:
    PipelineContext makeContext(ConfigMode mode, ...) const;
    void loadPipelines();  // 从 config.json 读取
    std::unordered_map<std::string, PipelineDefinition> pipelines_;  // mode → definition
    sqlite3* db_;
};
```

### 2.3 步骤类型定义

#### 2.3.1 内置步骤类型

| 类型 | 参数 | 说明 |
|------|------|------|
| `replace_variables` | `{socksPort, apiPort, probeUrl, ...}` | 占位符替换（复用现有 `replacePlaceholders()`） |
| `add_outbound` | `{tag, protocol, settings?, source?}` | 添加出站；`source:"profile"` 表示从 profile 生成 |
| `add_inbound` | `{tag, protocol, settings, ...}` | 添加/修改入站 |
| `merge_routing` | `{inboundTag, outboundTag?, balancerTag?}` | 合并路由规则（复用现有 `mergeSocksInboundRule()`） |
| `add_observatory` | `{subjectSelector, probeUrl, probeInterval}` | 添加 observatory 配置 |
| `add_balancer` | `{tag, selector, strategy, observation?}` | 添加 balancer 声明 |
| `add_stats` | `{}` | 添加 stats 配置（空对象） |
| `add_policy` | `{levels, system}` | 添加 policy 配置 |
| `add_api` | `{tag, listen, services}` | 添加 API 入站配置 |
| `modify_field` | `{path, value}` | 修改任意 JSON 字段（JSONPath 简化版） |

#### 2.3.2 步骤参数占位符

步骤参数中支持 `${variable}` 占位符，在执行前由 `ConfigVariables` 替换：
- `${socksPort}` / `${apiPort}` — 运行时端口
- `${probeUrl}` — 探测 URL
- `${observatoryInterval}` — 观测间隔
- `${balancerStrategy}` — 均衡策略

### 2.4 配置文件结构

```json
{
  "config_pipeline": {
    "test": {
      "template": "xray_test_001",
      "steps": [
        {"type": "replace_variables", "params": {}},
        {"type": "add_outbound", "params": {"tag": "proxy", "source": "profile"}},
        {"type": "add_outbound", "params": {"tag": "direct", "protocol": "freedom", "settings": {"domainStrategy": "AsIs"}}},
        {"type": "merge_routing", "params": {"inboundTag": "socks", "outboundTag": "proxy"}}
      ]
    },
    "standalone": {
      "template": "xray_default_001",
      "steps": [
        {"type": "replace_variables", "params": {}},
        {"type": "add_outbound", "params": {"tag": "proxy", "source": "profile"}},
        {"type": "add_outbound", "params": {"tag": "direct", "protocol": "freedom"}},
        {"type": "add_outbound", "params": {"tag": "block", "protocol": "blackhole"}},
        {"type": "merge_routing", "params": {"inboundTag": "socks", "outboundTag": "proxy"}}
      ]
    },
    "pool": {
      "template": "pool_xray_001",
      "steps": [
        {"type": "replace_variables", "params": {}},
        {"type": "add_outbound", "params": {"tag": "direct", "protocol": "freedom", "settings": {"domainStrategy": "AsIs"}}},
        {"type": "add_outbound", "params": {"tag": "block", "protocol": "blackhole"}},
        {"type": "add_observatory", "params": {"subjectSelector": ["px-"], "probeUrl": "${probeUrl}", "probeInterval": "${observatoryInterval}"}},
        {"type": "add_balancer", "params": {"tag": "balancer-out", "selector": ["px-"], "strategy": "${balancerStrategy}"}},
        {"type": "merge_routing", "params": {"inboundTag": "socks", "balancerTag": "balancer-out"}}
      ]
    }
  }
}
```

---

## 3. 迁移策略

### 3.1 分阶段实施

| 阶段 | 内容 | 工时 | 风险 |
|------|------|------|------|
| **Phase 1** | 基础设施：StepHandler 接口 + StepRegistry + PipelineContext + PipelineConfig 解析 | 1d | 低 |
| **Phase 2** | 核心步骤实现：replace_variables + add_outbound + merge_routing | 1d | 低 |
| **Phase 3** | 扩展步骤：add_observatory + add_balancer + modify_field + 剩余步骤 | 0.5d | 低 |
| **Phase 4** | ConfigBuilder 集成：build() 委托给 Pipeline + 回退到硬编码路径 | 0.5d | 中 |
| **Phase 5** | 单元测试：每个步骤类型 + 完整管线 + 回退路径 | 1d | 中 |
| **Phase 6** | 文档 + 配置示例 + 迁移指南 | 0.5d | 低 |
| **总计** | | **4.5d** | |

### 3.2 向后兼容

```
ConfigBuilder::build()
  │
  ├── 检查 config.json 是否有 "config_pipeline" 配置
  │     ├── 有 → ConfigPipeline::execute()（新路径）
  │     └── 无 → 硬编码路径（现有逻辑，保持不变）
  │
  └── 返回配置
```

**关键保障**：
- 不修改 `loadTemplate()` / `mergeConfig()` / `validateConfig()` 等现有方法
- `ConfigPipeline` 复用现有 `replacePlaceholders()` / `mergeSocksInboundRule()` 等工具函数
- `ConfigBuilder::build()` 作为统一入口，内部路由到 Pipeline 或硬编码路径
- 用户未配置 `config_pipeline` 时，行为与当前完全一致

### 3.3 文件变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/config/StepHandler.h` | 新增 | StepHandler 接口 + PipelineContext |
| `include/config/StepRegistry.h` | 新增 | 步骤注册表 |
| `include/config/PipelineConfig.h` | 新增 | PipelineDefinition / StepConfig 结构 |
| `include/config/ConfigPipeline.h` | 新增 | 管线执行器 |
| `src/config/StepHandlers.cpp` | 新增 | 所有内置步骤实现 |
| `src/config/ConfigPipeline.cpp` | 新增 | 管线执行逻辑 |
| `src/config/ConfigBuilder.cpp` | 修改 | build() 增加 pipeline 路由 |
| `src/config/StepRegistry.cpp` | 新增 | 注册表实现 |
| `tests/test_config_pipeline.cpp` | 新增 | 管线单元测试 |
| `docs/design/2026-09-22-Design-ConfigPipeline-v1.0.md` | 新增 | 本设计文档 |

---

## 4. 风险与缓解

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| Pipeline 配置错误导致配置生成失败 | 中 | 加载时校验步骤类型 + 参数 schema；执行失败回退到硬编码路径 |
| 步骤执行顺序错误导致配置不完整 | 低 | 步骤按配置数组顺序执行；模板提供基础结构，步骤只增不改 |
| JSON 参数解析异常 | 低 | 每步独立 try-catch，记录日志后继续执行 |
| 与现有模板继承机制冲突 | 低 | Pipeline 在 `loadTemplate()` 之后执行，仅修改 outbounds/routing 等运行时字段 |
| SingBox 协议差异 | 中 | `add_outbound` 步骤根据 `ctx.proxyType` 分派到不同 factory；`modify_field` 步骤支持后端差异配置 |

---

## 5. 未来扩展

- **UI 可视化编排**：在配置对话框的"模板种子" tab 旁增加"管线编排" tab，列表显示步骤，支持拖拽排序
- **步骤条件执行**：步骤支持 `if` 条件表达式（如 `if: "${enableObservatory}"`）
- **步骤模板复用**：步骤组可提取为命名模板，多个模式引用同一组步骤
- **步骤日志**：每步执行前后输出 TRACE 日志，便于调试
- **SingBox 管线**：独立配置 `test_singbox` / `standalone_singbox` / `pool_singbox` 管线
