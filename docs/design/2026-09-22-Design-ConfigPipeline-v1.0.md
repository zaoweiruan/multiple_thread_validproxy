---
title: "配置生成管线（ConfigPipeline）设计方案"
type: design
status: approved
date: 2026-09-22
version: "1.2"
---

# 配置生成管线（ConfigPipeline）设计方案

> **实施状态**: 全部 6 个阶段完成（Phase 1-6 ✅）
> **验证结果**: 构建 0 errors + ConfigGeneratorTest 98/98 passed + 全量 ctest 48/48（100% pass rate）

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

#### 2.2.2 PipelineContext（值类型，消除引用安全风险）

> **评审 R1 修复**：profile/vars 改为值传递，消除悬垂引用风险。

```cpp
// include/config/PipelineContext.h
struct PipelineContext {
    ProxyType proxyType;
    db::models::Profileitem profile;   // 值传递，可为空（无 profile 时各字段为空）
    bool hasProfile = false;           // profile 是否有效（address 非空）
    ConfigVariables vars;              // 值传递
    sqlite3* db = nullptr;             // 非拥有指针，仅用于 DB 查询
    std::string clientName;
};
```

**说明**：`ConfigVariables` 含 `socksPort`/`apiPort`/`probeUrl`/`observatoryInterval`/`balancerStrategy`，值拷贝开销可忽略。`Profileitem` 同样为轻量结构体。

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
| `add_outbound` | `{tag, protocol, settings?, source?}` | 添加出站；`source:"profile"` 表示从 `ctx.profile` 生成（`hasProfile==false` 或 `address` 为空时跳过此步骤，不添加出站也不报错） |
| `add_inbound` | `{tag, protocol, settings, ...}` | 添加/修改入站 |
| `merge_routing` | `{inboundTag, outboundTag?, balancerTag?}` | 合并路由规则（复用现有 `mergeSocksInboundRule()`） |
| `add_observatory` | `{subjectSelector, probeUrl, probeInterval}` | 添加 observatory 配置 |
| `add_balancer` | `{tag, selector, strategy, observation?}` | 添加 balancer 声明 |
| `add_stats` | `{}` | 添加 stats 配置（空对象） |
| `add_policy` | `{levels, system}` | 添加 policy 配置 |
| `add_api` | `{tag, listen, services}` | 添加 API 入站配置 |
| `modify_field` | `{path, value}` | 修改任意 JSON 字段；`path` 语法：`.` 分隔层级 + `[n]` 数组索引（如 `routing.rules[0].inboundTag`、`inbounds[0].port`、`log.access`），不存在时逐级创建对象/数组 |

### 2.3.3 模板继承与 Pipeline 交互（评审 R11 补充）

Pipeline 在 `loadTemplateById()` **之后**执行，仅修改运行时字段：

```
loadTemplateById("xray_test_001")  // 递归加载父模板 + 深合并 → 完整基础配置
  │
  └── ConfigPipeline::execute()     // 按步骤顺序修改配置
        ├── replace_variables       // 替换 ${socksPort} 等占位符
        ├── add_outbound            // 添加代理出站（source:"profile"）
        ├── merge_routing           // 合并 socks 路由规则
        └── ...
```

**设计原则**：
- 模板提供**静态基础结构**（log/dns/routing/api/stats/policy/balancers）
- Pipeline 步骤仅添加**运行时动态字段**（outbounds、占位符替换、routing 合并）
- 两者职责分离，互不干扰
- Pipeline 不修改模板的 log/dns/api/stats/policy/balancers 等静态配置

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

### 2.5 错误处理策略（评审 R5 补充）

每步独立 try-catch，错误策略由步骤参数 `onError` 控制（默认 `"continue"`）：

| 策略 | 行为 | 适用场景 |
|------|------|---------|
| `continue` | 记录 WARN 日志，跳过该步继续执行后续步骤 | 默认，大多数步骤 |
| `fail_fast` | 记录 ERR 日志，抛出异常终止整个管线 | 关键步骤（如 `replace_variables`） |
| `fallback` | 记录 ERR 日志，整个管线回退到硬编码路径重新执行 | 需要保障配置完整性的场景 |

```json
{"type": "replace_variables", "params": {}, "onError": "fail_fast"},
{"type": "add_outbound", "params": {"tag": "proxy", "source": "profile"}, "onError": "continue"},
{"type": "modify_field", "params": {"path": "log.access", "value": null}, "onError": "continue"}
```

### 2.6 SingBox 管线定义（评审 R3 补充）

SingBox 与 Xray 的 JSON 结构差异较大（`outbounds`→`outbounds`、`inbounds`→`inbounds`、`routing`→`route`），通过 `modify_field` 步骤适配：

```json
{
  "config_pipeline": {
    "test_singbox": {
      "template": "singbox_test_001",
      "steps": [
        {"type": "replace_variables", "params": {}},
        {"type": "add_outbound", "params": {"tag": "proxy", "source": "profile"}, "onError": "continue"},
        {"type": "add_outbound", "params": {"tag": "direct", "protocol": "direct"}},
        {"type": "merge_routing", "params": {"inboundTag": "socks", "outboundTag": "proxy"}}
      ]
    }
  }
}
```

SingBox 步骤的 `add_outbound` handler 根据 `ctx.proxyType` 自动分派到 SingBox outbound 格式。

### 2.7 配置加载流程（评审 R6 补充）

```
应用启动
  │
  ├── ConfigReader::load() → 读取 config.json
  │     └── 解析 "config_pipeline" 段 → boost::json::value pipelineJson
  │
  └── AppController 构造
        └── ConfigPipeline pipeline(db)
              └── pipeline.loadConfig(pipelineJson)  // 解析 + 校验步骤类型
                    ├── 步骤类型不存在 → WARN 日志，跳过
                    ├── params JSON 解析失败 → WARN 日志，跳过
                    └── 全部合法 → 存入 pipelines_ map

每次 build() 调用
  │
  ├── 检查 pipelines_ 是否为空
  │     ├── 空 → 硬编码路径（现有 buildXxxConfig）
  │     └── 非空 → ConfigPipeline::execute()（管线路径）
  │                  ├── 管线执行成功 → validateConfig() → 返回
  │                  └── 管线执行失败 + onError="fallback" → 回退硬编码路径
  │
  └── 返回配置
```

### 2.8 步骤幂等性（评审 R7 补充）

所有步骤设计为**幂等**——重复执行同一配置产生相同结果：
- `replace_variables`：替换已完成占位符后再次执行无效果
- `add_outbound`：按 tag 去重，已存在同名 outbound 则更新 settings 而非追加
- `merge_routing`：先移除已有 socks 规则再添加，天然幂等
- `modify_field`：覆盖写入，幂等
- `add_stats`/`add_policy`/`add_api`：覆盖写入对应顶级键，幂等

### 2.9 StepRegistry 线程安全（评审 R8 补充）

```cpp
class StepRegistry {
public:
    static StepRegistry& instance();  // Meyer's Singleton
    void registerHandler(...);         // 初始化阶段调用，无需锁
    const std::shared_ptr<const StepHandler> getHandler(...) const;  // 只读，线程安全
private:
    std::unordered_map<...> handlers_;  // 初始化后只读
};
```

**约定**：`registerHandler()` 仅在应用启动阶段（`main()` → `AppController` 构造前）调用一次，之后只读。管线执行器读取 `getHandler()` 为 const 操作，天然线程安全。

### 2.10 validateConfig 集成（评审 R9 补充）

`ConfigPipeline::execute()` 执行完成后调用 `ConfigBuilder::validateConfig()` 校验结果。校验失败时：
- 若 `onError="fallback"` → 抛出异常触发回退
- 若 `onError="continue"` → 记录 WARN 日志，仍返回配置（与现有行为一致）

---

### 3.1 分阶段实施

| 阶段 | 内容 | 工时 | 风险 | 状态 |
|------|------|------|------|------|
| **Phase 1** | 基础设施：StepHandler 接口 + StepRegistry + PipelineContext + PipelineConfig 解析 | 1.5d | 低 | ✅ 完成 |
| **Phase 2** | 核心步骤实现：replace_variables + add_outbound + merge_routing | 1.5d | 低 | ✅ 完成 |
| **Phase 3** | 扩展步骤：add_inbound + add_observatory + add_balancer + add_stats + add_policy + add_api + modify_field | 1d | 低 | ✅ 完成 |
| **Phase 4** | ConfigBuilder 集成：build() 委托给 Pipeline + 回退到硬编码路径 | 0.5d | 中 | ✅ 完成 |
| **Phase 5** | 单元测试：每个步骤类型 + 完整管线 + 回退路径 | 1.5d | 中 | ✅ 完成 (98 tests) |
| **Phase 6** | 文档 + 配置示例 + 迁移指南 | 0.5d | 低 | ✅ 完成 |
| **总计** | | **6.5d** | | |

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

- **UI 可视化编排**：在配置对话框的"模板配置" tab 旁增加"管线编排" tab，列表显示步骤，支持拖拽排序
- **步骤条件执行**：步骤支持 `if` 条件表达式（如 `if: "${enableObservatory}"`）
- **步骤模板复用**：步骤组可提取为命名模板，多个模式引用同一组步骤
- **步骤日志**：每步执行前后输出 TRACE 日志，便于调试
- **SingBox 管线**：独立配置 `test_singbox` / `standalone_singbox` / `pool_singbox` 管线
