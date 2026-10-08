---
title: "ConfigPipeline 组件化重构 — 方案设计"
type: design
status: draft
date: 2026-09-22
updated: 2026-09-22
version: v1.6
---

# ConfigPipeline 组件化重构 — 方案设计（v1.5）

> 关联设计文档：`docs/design/2026-09-22-Design-ConfigPipeline-v1.0.md`（Pipeline 核心实现）
> 关联设计文档：`docs/design/2026-09-22-Design-ConfigPipeline-UI-Edit-v1.0.md`（UI 编辑功能）
> 关联组件：`ConfigPipeline` (`include/config/ConfigPipeline.h`)、`ConfigBuilder` (`include/config/ConfigBuilder.h`)

---

## 1. 背景与问题

### 1.1 现状

当前 ConfigPipeline 采用**命令式步骤（Step）**设计，用户需要配置有序步骤序列来构建最终配置。

**示例**：
```json
{
  "steps": [
    {"type": "add_api", "params": {"tag": "api", "listen": "127.0.0.1:10085"}},
    {"type": "add_stats", "params": {}},
    {"type": "add_policy", "params": {"levels": {...}}},
    {"type": "add_observatory", "params": {"subjectSelector": ["proxy"]}},
    {"type": "add_balancer", "params": {"tag": "balancer-out", "selector": ["proxy"]}},
    {"type": "add_outbound", "params": {"tag": "proxy", "source": "profile"}},
    {"type": "add_inbound", "params": {"tag": "socks", "port": 10809}},
    {"type": "merge_routing", "params": {"inboundTag": "socks", "outboundTag": "balancer-out"}}
  ]
}
```

### 1.2 问题分析

| 问题 | 说明 |
|------|------|
| **过于底层** | 用户需要理解每个步骤的操作语义（add/merge/modify） |
| **无冲突检测** | 两个 `add_api` 步骤会静默覆盖，不报错 |
| **依赖关系隐式** | `merge_routing` 依赖 `add_inbound` 和 `add_outbound`，但无校验 |
| **重复配置** | 相同类型的步骤需要重复配置（如多个 outbound 需要多次写 `add_outbound`） |
| **预览困难** | 步骤序列执行后才能看到最终配置，中间状态不可见 |

### 1.3 目标

1. **组件化** — 组件类型对应配置节类型（inbounds, outbounds, api, stats, policy, dns, routing, observatory, balancer）
2. **组合方式** — 相同类型组件采用组合/合并策略（数组追加、对象合并、单例覆盖）
3. **冲突校验** — 执行前检测组件冲突，提供清晰的错误信息
4. **声明式配置** — 用户声明"需要什么"，而非"如何操作"

---

## 2. 新架构设计

### 2.1 核心概念（v1.4 修订）

**新架构：模板 = 类型 + 组件组合链（BundleChain）**

```
┌─────────────────────────────────────────────────────────────┐
│                     Template Definition                       │
├─────────────────────────────────────────────────────────────┤
│  type: "test" | "standalone" | "pool"                        │
│  coreType: "xray" | "singbox"                                │
│  bundleChain: [                                              │
│    "base_xray_bundle_001",      // 基础配置（log/dns）        │
│    "test_xray_bundle_001"       // 测试配置（api/inbound）    │
│  ]                                                           │
│                                                              │
│  Config: { ... }  ← BundleChain 生成的完整配置（带占位符）      │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│              ComponentBundle Chain                            │
├─────────────────────────────────────────────────────────────┤
│  1. base_xray_bundle_001 (Parent)                            │
│     - log, dns, policy                                      │
│                                                              │
│  2. test_xray_bundle_001 (Child)                             │
│     - api, inbound, outbound, routing                        │
│                                                              │
│  组合结果：                                                   │
│  - 单例组件（log/dns/policy）：子组件覆盖父组件                  │
│  - 数组组件（inbound/outbound）：子组件追加到父组件               │
│  - 冲突检测：同类型单例组件冲突 → ERROR                          │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│                      Final Config                            │
├─────────────────────────────────────────────────────────────┤
│  {                                                           │
│    "log": {...},        ← 从 base_xray_bundle_001             │
│    "dns": {...},        ← 从 base_xray_bundle_001             │
│    "api": {...},        ← 从 test_xray_bundle_001              │
│    "inbounds": [...],   ← 从 test_xray_bundle_001              │
│    "outbounds": [...],  ← 从 test_xray_bundle_001              │
│    "routing": { ... }   ← 从 test_xray_bundle_001              │
│  }                                                           │
└─────────────────────────────────────────────────────────────┘
```

**关键变化（v1.5）**：
- **BundleChain**：支持多个组件包组合，形成继承链
- **模板 Config 字段**：保存 BundleChain 生成的完整配置（带占位符）
- **占位符替换**：生成配置时只用参数或 JSON 替换占位符
- **组件包继承**：子组件包可覆盖父组件包的单例组件，追加数组组件

### 2.1.1 数据源策略（v1.5 新增 — P0-1 修复）

**核心决策**：采用**方案C — 快照模式**

| 数据源 | 用途 | 修改时机 | 同步机制 |
|--------|------|----------|----------|
| **BundleChain** | 配置编辑（声明组件组合） | 用户编辑组件包或调整 BundleChain 时 | 手动触发"生成 Config" |
| **Template.Config** | 运行时生成（最终配置快照） | BundleChain 修改后手动生成 | BundleChain → Config 单向同步 |

**关键约束**：
1. **Config 是 BundleChain 的快照**，不是独立数据源
2. **BundleChain 修改后**，Config 不会自动更新，需用户手动"生成 Config"
3. **Config 手动修改后**，BundleChain 不会自动更新，标记为"不一致"
4. **生成实例时**，只读取 Config 并替换占位符，不读取 BundleChain

**不一致处理**：
```
场景1：BundleChain 修改 → Config 未更新 → 系统提示"配置已过期，请重新生成"
场景2：Config 手动修改 → BundleChain 未同步 → 标记为"已手动编辑"
场景3：Bundle 版本更新 → 所有引用模板 Config 过期 → 批量提示重新生成
```

**UI 状态指示**：
- ✅ 一致：Config 与 BundleChain 匹配
- ⚠️ 已过期：BundleChain 修改后未生成 Config
- 📝 已手动编辑：Config 被手动修改

### 2.2 组件类型定义

**参照 Xray 官方配置结构**（https://xtls.github.io/config）：

| 组件类型 | 配置节 | 官方类型 | 组合策略 | 单例 | 冲突规则 |
|----------|--------|----------|----------|------|----------|
| `env` | `env` | EnvObject | **override** | 是 | 同类型冲突 |
| `log` | `log` | LogObject | **override** | 是 | 同类型冲突 |
| `api` | `api` | ApiObject | **override** | 是 | 同类型冲突 |
| `dns` | `dns` | DnsObject | **override** | 是 | 同类型冲突 |
| `routing` | `routing` | RoutingObject | **merge**（规则追加） | 是 | 同类型冲突 |
| `policy` | `policy` | PolicyObject | **override** | 是 | 同类型冲突 |
| `inbound` | `inbounds[]` | InboundObject[] | **append**（按 tag 去重） | 否 | 同 tag 冲突 |
| `outbound` | `outbounds[]` | OutboundObject[] | **append**（按 tag 去重） | 否 | 同 tag 冲突 |
| `stats` | `stats` | StatsObject | **override** | 是 | 同类型冲突 |
| `fakedns` | `fakedns` | FakeDnsObject | **override** | 是 | 同类型冲突 |
| `metrics` | `metrics` | MetricsObject | **override** | 是 | 同类型冲突 |
| `observatory` | `observatory` | ObservatoryObject | **override** | 是 | 同类型冲突 |
| `burstObservatory` | `burstObservatory` | BurstObservatoryObject | **override** | 是 | 同类型冲突 |
| `geodata` | `geodata` | GeodataObject | **override** | 是 | 同类型冲突 |
| `version` | `version` | VersionObject | **override** | 是 | 同类型冲突 |

**说明**：
- `inbound`/`outbound` 是数组型组件，支持多个实例（按 tag 去重）
- `balancer` 是 `routing.balancers[]` 的子组件，通过 `routing` 组件的 params 配置
- 其余均为单例组件，同类型只能出现一次

**v1.6 新增：SingBox 配置差异说明（P1-3）**

| 组件类型 | Xray | SingBox | 说明 |
|----------|------|---------|------|
| `api` | ✅ 支持 | ❌ 不支持 | SingBox 无 API 组件 |
| `stats` | ✅ 支持 | ❌ 不支持 | SingBox 无 stats 组件 |
| `fakedns` | ✅ 支持 | ❌ 不支持 | SingBox 使用不同 DNS 机制 |
| `metrics` | ✅ 支持 | ❌ 不支持 | SingBox 无 metrics 组件 |
| `observatory` | ✅ 支持 | ❌ 不支持 | SingBox 无 observatory |
| `burstObservatory` | ✅ 支持 | ❌ 不支持 | SingBox 无 burstObservatory |
| `geodata` | ✅ 支持 | ❌ 不支持 | SingBox 无 geodata |
| `version` | ✅ 支持 | ❌ 不支持 | SingBox 无 version 组件 |
| `balancer` | `routing.balancers` | `balancers[]` | SingBox 顶层 balancers |
| `routing` | `routing` | `route` | SingBox 使用 `route` 替代 |

**处理策略**：
- 组件解析器根据 `CoreType` 过滤不支持的组件
- SingBox 配置生成时自动跳过不支持的组件类型
- UI 编辑时根据 CoreType 显示可用组件列表

### 2.2.1 ComponentBundle 表结构（v1.5 更新 — P0-3 修复）

**表用途**：存储预定义的组件组合，支持版本管理和共享

**重要变更**：移除 `ParentId` 字段，统一使用 BundleChain 进行组件组合

```sql
CREATE TABLE ComponentBundle (
    Id INTEGER PRIMARY KEY AUTOINCREMENT,
    Name varchar(100) NOT NULL UNIQUE,      -- 组件包名称（如 "test_xray_bundle_001"）
    Description varchar(500),               -- 描述
    CoreType INTEGER NOT NULL,              -- 核心类型：2=Xray, 24=SingBox
    TemplateType varchar(20) NOT NULL,      -- 模板类型：test, standalone, pool
    Version INTEGER DEFAULT 1,              -- 版本号
    Components varchar NOT NULL,            -- JSON 格式组件列表
    Enabled INTEGER DEFAULT 1,              -- 是否启用
    CreatedAt datetime DEFAULT CURRENT_TIMESTAMP,
    UpdatedAt datetime DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_bundle_core_type ON ComponentBundle(CoreType);
CREATE INDEX idx_bundle_template_type ON ComponentBundle(TemplateType);
CREATE INDEX idx_bundle_name ON ComponentBundle(Name);
CREATE INDEX idx_bundle_enabled ON ComponentBundle(Enabled);
```

**组件列表 JSON 格式**：
```json
{
  "components": [
    {
      "type": "log",
      "params": {
        "loglevel": "warning"
      }
    },
    {
      "type": "dns",
      "params": {
        "servers": ["1.1.1.1", "8.8.8.8"]
      }
    },
    {
      "type": "api",
      "params": {
        "tag": "api",
        "listen": "127.0.0.1:${apiPort}",
        "services": ["HandlerService"]
      }
    },
    {
      "type": "inbound",
      "params": {
        "tag": "socks",
        "protocol": "mixed",
        "port": "${socksPort}",
        "settings": {
          "udp": true
        }
      }
    },
    {
      "type": "outbound",
      "params": {
        "tag": "proxy",
        "source": "profile"
      }
    },
    {
      "type": "routing",
      "params": {
        "rules": [
          {
            "inboundTag": "socks",
            "outboundTag": "proxy"
          }
        ]
      }
    }
  ]
}
```

**TemplateType 与 CoreType 组合**（v1.6 更新 — P0-4 修复）：

| TemplateType | CoreType | 说明 |
|--------------|----------|------|
| **base** | 2 (Xray) | Xray 基础组件包（log/dns/policy，共享） |
| **base** | 24 (SingBox) | SingBox 基础组件包（log/dns，共享） |
| test | 2 (Xray) | Xray 批量测试配置 |
| test | 24 (SingBox) | SingBox 批量测试配置 |
| standalone | 2 (Xray) | Xray 独立代理配置 |
| standalone | 24 (SingBox) | SingBox 独立代理配置 |
| pool | 2 (Xray) | Xray 代理池配置 |
| pool | 24 (SingBox) | SingBox 代理池配置 |

**说明**：
- **base**：基础组件包，包含所有模板共享的组件（log/dns/policy），可被其他 TemplateType 引用
- **test/standalone/pool**：特定模板类型，可引用 base 组件包

**v1.6 新增：Bundle 版本同步机制**

| 场景 | 行为 |
|------|------|
| Bundle 更新版本 | Version + 1，所有引用该 Bundle 的模板标记为"⚠️ 已过期" |
| 模板重新生成 Config | 加载最新 Bundle 版本，更新 Config 快照，状态变为"✅ 一致" |
| Bundle 被引用计数 | ComponentBundle 表添加 `ReferencedBy` 计数（可选） |

**版本更新流程**：
1. 用户修改 Bundle → Version + 1
2. 系统扫描所有引用该 Bundle 的模板
3. 将模板状态标记为"⚠️ 已过期"
4. 用户手动触发"生成 Config" → 状态变为"✅ 一致"

### 2.2.2 BundleChain 格式（v1.4 更新）

**用途**：保存组件包 ID 组合链，用于配置编辑和生成

**格式**：
```json
{
  "bundleIds": [
    "base_xray_bundle_001",
    "test_xray_bundle_001"
  ],
  "params": {
    "socksPort": 10809,
    "apiPort": 10085,
    "probeUrl": "https://www.google.com/generate_204"
  }
}
```

**说明**：
- `bundleIds`: BundleId 数组，按顺序组合多个组件包
- `params`: 占位符参数，用于替换组件中的 `${...}` 占位符

**组合规则**：
1. **顺序执行**：按 bundleIds 数组顺序依次加载组件包
2. **单例组件**：后面的组件包覆盖前面的（子覆盖父）
3. **数组组件**：后面的组件包追加到前面的（子追加父）
4. **冲突检测**：组合后检测冲突（单例冲突、tag 冲突）

**示例**：
```json
{
  "bundleIds": [
    "base_xray_bundle_001",      // 基础配置：log, dns, policy
    "standalone_xray_bundle_001"  // 独立代理配置：api, inbound, outbound, routing
  ],
  "params": {
    "socksPort": 10809,
    "apiPort": 10085
  }
}
```

### 2.3 组件配置格式

#### 2.3.1 数组型组件（inbound, outbound, balancer）

```json
{
  "type": "inbound",
  "params": {
    "tag": "socks",
    "protocol": "mixed",
    "port": 10809,
    "settings": {
      "udp": true
    }
  }
}
```

**去重规则**：按 `tag` 去重，相同 tag 的组件只保留最后一个。

#### 2.3.2 单例组件（env, log, api, stats, policy, dns, fakedns, metrics, observatory, burstObservatory, geodata, version, routing）

```json
{
  "type": "log",
  "params": {
    "loglevel": "warning",
    "access": "/var/log/xray/access.log",
    "error": "/var/log/xray/error.log",
    "dnsLog": false
  }
}
```

```json
{
  "type": "dns",
  "params": {
    "servers": [
      "1.1.1.1",
      "8.8.8.8",
      {
        "address": "https://doh.pub/dns-query",
        "clientIp": "123.14.127.142",
        "tag": "google-doh"
      }
    ],
    "queryStrategy": "UseIPv4",
    "cache": {
      "enabled": true,
      "expiration": 60
    }
  }
}
```

```json
{
  "type": "fakedns",
  "params": {
    "ipPool": "198.18.0.0/16"
  }
}
```

**冲突规则**：同类型组件只能有一个，重复定义视为冲突。

#### 2.3.3 特殊组件：outbound（支持 profile 源）

```json
{
  "type": "outbound",
  "params": {
    "tag": "proxy",
    "source": "profile"
  }
}
```

`source: "profile"` 表示从运行时 Profileitem 生成 outbound 配置。

#### 2.3.4 占位符支持（变量替换）

**所有组件的 `params` 都支持占位符**，在组件应用时自动解析。

**支持的占位符**：
| 占位符 | 说明 | 示例值 |
|--------|------|--------|
| `${socksPort}` | SOCKS 入站端口 | 10809 |
| `${apiPort}` | API 端口 | 10085 |
| `${probeUrl}` | 探测 URL | https://www.google.com/generate_204 |
| `${balancerStrategy}` | 负载均衡策略 | leastPing |
| `${observatoryInterval}` | 观测间隔（秒） | 5 |

**示例**：
```json
{
  "type": "api",
  "params": {
    "tag": "api",
    "listen": "127.0.0.1:${apiPort}",
    "services": ["HandlerService", "StatsService"]
  }
}
```

```json
{
  "type": "inbound",
  "params": {
    "tag": "socks",
    "protocol": "mixed",
    "port": ${socksPort},
    "settings": {
      "udp": true
    }
  }
}
```

**替换时机**：
1. 组件加载时 - 解析 params 中的占位符
2. 使用 `ConfigVariables` 中的运行时值替换
3. 替换后应用组件到配置

**实现方式**：复用现有的 `ConfigBuilder::replaceVariables()` 方法。

---

## 3. 冲突检测（v1.6 更新 — P1-1 修复）

### 3.1 冲突类型

| 冲突类型 | 说明 | 严重级别 |
|----------|------|----------|
| `SINGLETON_CONFLICT` | 单例组件重复定义 | 同包 ERROR / 跨包 WARN |
| `TAG_CONFLICT` | 数组组件 tag 重复 | ERROR |
| `DEPENDENCY_MISSING` | routing 引用不存在的 inbound/outbound | WARN |
| `INVALID_PARAMS` | 组件参数格式错误 | ERROR |

**v1.6 更新**：冲突检测区分同包 vs 跨包场景

| 场景 | 单例组件 | 数组组件 |
|------|----------|----------|
| 同包内重复 | ERROR | ERROR（tag 冲突） |
| 跨包覆盖 | WARN（允许，后者覆盖前者） | ERROR（tag 冲突） |

### 3.2 冲突检测算法

```cpp
struct Conflict {
    enum class Severity { ERROR, WARN };
    Severity severity;
    std::string component1;      // 冲突组件1 类型
    std::string component2;      // 冲突组件2 类型（可选）
    std::string message;         // 描述
    int index1;                  // 组件索引
    int index2;                  // 组件索引（可选）
    
    // v1.6 新增：来源包信息
    std::string sourceBundle1;   // 组件1 的来源包 ID
    std::string sourceBundle2;   // 组件2 的来源包 ID
    bool isCrossBundle;          // 是否跨包冲突
};

std::vector<Conflict> ConfigPipeline::detectConflicts(
    const std::vector<ComponentConfig>& components) const {
    
    std::vector<Conflict> conflicts;
    
    // 1. 单例冲突检测
    std::unordered_set<std::string> singletonTypes;
    for (const auto& comp : components) {
        if (isSingletonType(comp.type)) {
            if (singletonTypes.count(comp.type)) {
                conflicts.push_back({
                    Conflict::Severity::ERROR,
                    comp.type, comp.type,
                    "单例组件重复定义: " + comp.type,
                    -1, -1
                });
            }
            singletonTypes.insert(comp.type);
        }
    }
    
    // 2. Tag 冲突检测（数组型组件）
    std::unordered_map<std::string, int> tagMap;
    for (int i = 0; i < components.size(); ++i) {
        const auto& comp = components[i];
        if (isArrayType(comp.type)) {
            std::string tag = extractTag(comp);
            if (!tag.empty()) {
                if (tagMap.count(tag)) {
                    conflicts.push_back({
                        Conflict::Severity::ERROR,
                        comp.type, comp.type,
                        "Tag 重复: " + tag + " (组件 " + 
                            std::to_string(tagMap[tag]) + " 和 " + 
                            std::to_string(i) + ")",
                        tagMap[tag], i
                    });
                } else {
                    tagMap[tag] = i;
                }
            }
        }
    }
    
    // 3. 依赖检测（routing 引用 inbound/outbound）
    std::unordered_set<std::string> inboundTags;
    std::unordered_set<std::string> outboundTags;
    for (const auto& comp : components) {
        if (comp.type == "inbound") inboundTags.insert(extractTag(comp));
        if (comp.type == "outbound") outboundTags.insert(extractTag(comp));
    }
    
    for (const auto& comp : components) {
        if (comp.type == "routing" && comp.params.contains("rules")) {
            for (const auto& rule : comp.params["rules"].as_array()) {
                std::string inboundTag = rule["inboundTag"].get_string();
                std::string outboundTag = rule["outboundTag"].get_string();
                
                if (inboundTags.count(inboundTag) == 0) {
                    conflicts.push_back({
                        Conflict::Severity::WARN,
                        "routing", "inbound",
                        "Routing 规则引用不存在的 inbound: " + inboundTag,
                        -1, -1
                    });
                }
                if (outboundTags.count(outboundTag) == 0 && 
                    outboundTag != "direct" && outboundTag != "block") {
                    conflicts.push_back({
                        Conflict::Severity::WARN,
                        "routing", "outbound",
                        "Routing 规则引用不存在的 outbound: " + outboundTag,
                        -1, -1
                    });
                }
            }
        }
    }
    
    return conflicts;
}
```

### 3.3 冲突处理策略

| 策略 | 说明 | 适用场景 |
|------|------|----------|
| `fail_fast` | 有 ERROR 级别冲突则抛异常 | 生产环境 |
| `warn_continue` | 有 WARN 级别冲突则记录日志继续执行 | 开发环境 |
| `ignore` | 忽略所有冲突 | 测试环境 |

**默认策略**：`fail_fast`（UI 保存时自动检测）

---

## 4. 组件解析器

### 4.1 核心接口

```cpp
/// 组件配置
struct ComponentConfig {
    std::string type;              // 组件类型（inbound/outbound/api/stats/...）
    boost::json::object params;    // 组件参数（支持占位符）
    std::string id;                // 可选：组件唯一标识（用于 UI 引用）
};

/// 组件解析结果
struct ResolveResult {
    boost::json::value config;     // 解析后的配置
    std::vector<Conflict> conflicts; // 检测到的冲突
    bool success;                  // 是否成功
};

/// 组件解析器
class ComponentResolver {
public:
    explicit ComponentResolver(sqlite3* db);
    
    /// 解析组件列表
    /// 1. 检测冲突
    /// 2. 替换占位符（使用 ctx.vars）
    /// 3. 应用组件到配置
    ResolveResult resolve(
        const std::vector<ComponentConfig>& components,
        const PipelineContext& ctx) const;
    
private:
    /// 替换组件参数中的占位符
    /// 复用 ConfigBuilder::replaceVariables()
    boost::json::object replacePlaceholders(
        const boost::json::object& params,
        const ConfigVariables& vars) const;
    
    /// 按类型分组组件
    std::unordered_map<std::string, std::vector<ComponentConfig>> 
        groupByType(const std::vector<ComponentConfig>& components) const;
    
    /// 应用数组型组件（append）
    void applyArrayComponent(
        boost::json::object& config,
        const std::string& section,
        const std::vector<ComponentConfig>& components) const;
    
    /// 应用单例组件（override）
    void applySingletonComponent(
        boost::json::object& config,
        const std::string& section,
        const ComponentConfig& component) const;
    
    /// 应用路由组件（merge）
    void applyRoutingComponent(
        boost::json::object& config,
        const ComponentConfig& component) const;
    
    /// 检测冲突
    std::vector<Conflict> detectConflicts(
        const std::vector<ComponentConfig>& components) const;
    
    sqlite3* db_;
};
```

### 4.3 BundleChain 解析器（v1.5 新增 — P1-1）

**用途**：解析 BundleChain，组合多个组件包，生成扁平化组件列表或最终配置

```cpp
/// BundleChain 数据结构
struct BundleChain {
    std::vector<std::string> bundleIds;  // 组件包 ID 数组（按顺序）
    std::map<std::string, boost::json::value> params;  // 占位符参数
};

/// BundleChain 解析结果
struct BundleChainResolveResult {
    std::vector<ComponentConfig> components;  // 扁平化组件列表
    std::vector<Conflict> conflicts;          // 检测到的冲突
    boost::json::value config;                // 最终配置（如果成功）
    bool success;                             // 是否成功
};

/// BundleChain 解析器
class BundleChainResolver {
public:
    explicit BundleChainResolver(sqlite3* db);
    
    /// 解析 BundleChain，返回扁平化组件列表
    /// 1. 按 bundleIds 顺序加载组件包
    /// 2. 合并组件（单例覆盖、数组追加）
    /// 3. 检测冲突（区分同包 vs 跨包）
    BundleChainResolveResult resolveBundleChain(
        const BundleChain& chain) const;
    
    /// 解析 BundleChain，直接生成配置
    /// 1. 调用 resolveBundleChain 获取组件列表
    /// 2. 调用 ComponentResolver 生成配置
    BundleChainResolveResult resolveToConfig(
        const BundleChain& chain,
        const PipelineContext& ctx) const;
    
private:
    /// 加载组件包
    ComponentBundle loadBundle(const std::string& bundleId) const;
    
    /// 合并两个组件列表
    /// 单例组件：后者覆盖前者
    /// 数组组件：后者追加到前者（按 tag 去重）
    std::vector<ComponentConfig> mergeComponents(
        const std::vector<ComponentConfig>& base,
        const std::vector<ComponentConfig>& override) const;
    
    sqlite3* db_;
};
```

**解析流程**：
```
BundleChain {
  bundleIds: ["base_xray_bundle_001", "test_xray_bundle_001"],
  params: { "socksPort": 10809 }
}
    │
    ▼
1. 加载 base_xray_bundle_001 → [log, dns, policy]
    │
    ▼
2. 加载 test_xray_bundle_001 → [api, inbound, outbound, routing]
    │
    ▼
3. 合并组件：
   - log, dns, policy: 来自 base（无覆盖）
   - api, inbound, outbound, routing: 来自 test（追加）
   - 结果：[log, dns, policy, api, inbound, outbound, routing]
    │
    ▼
4. 替换占位符（使用 params）
    │
    ▼
5. 检测冲突（跨包覆盖允许，同包重复报错）
    │
    ▼
6. 生成最终配置
```

---

## 5. 数据流（v1.6 重写 — P0-2 修复）

### 5.1 配置生成流程（快照模式）

**核心原则**：运行时只读取 Config 快照并替换占位符，不读取 BundleChain。

```
┌─────────────────────────────────────────────────────────────┐
│ 1. 触发配置生成                                              │
│    - 启动代理实例 / 批量测试 / 代理池                          │
│    - 输入：templateId, profileId, runtimeParams               │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. 加载模板                                                  │
│    - 从 DB 查询 FullConfigTemplateItem                       │
│    - 读取 Config 字段（带占位符的完整配置）                    │
│    - 检查状态：✅一致 / ⚠️已过期 / 📝已手动编辑               │
│    - 如果过期 → 提示用户重新生成 Config                       │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. 构建运行时参数                                            │
│    - socksPort: 分配可用端口                                 │
│    - apiPort: 分配可用端口                                   │
│    - probeUrl: 从配置读取                                    │
│    - profileId: 当前代理 ID                                  │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. 占位符替换                                                │
│    - 遍历 Config JSON，替换所有 ${...} 占位符                 │
│    - 使用 ConfigVariables 中的运行时值                        │
│    - 复用 ConfigBuilder::replaceVariables()                   │
│    - 检查残留占位符 → ERROR                                   │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. 注入代理配置                                              │
│    - 从 Profileitem 生成 outbound 配置                        │
│    - 替换 Config 中的 outbound（tag="proxy"）                 │
│    - 保留其他组件不变                                         │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. 验证配置                                                  │
│    - JSON 格式合法性                                         │
│    - 必要组件完整性（根据 TemplateType）                      │
│    - 占位符全部替换（无残留 ${...}）                          │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. 写入配置文件                                              │
│    - 写入到 config/ 目录（如 config/xray_10809.json）         │
│    - 备份旧文件（如 xray_10809.json.bak）                     │
│    - 返回配置文件路径                                         │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 8. 启动代理实例                                              │
│    - 使用配置文件启动 xray/singbox 进程                        │
│    - 返回实例信息（PID, 端口等）                               │
└─────────────────────────────────────────────────────────────┘
```

### 5.2 BundleChain 生成 Config 流程（编辑模式）

**核心原则**：BundleChain 是配置编辑工具，生成 Config 快照后保存到模板。

```
┌─────────────────────────────────────────────────────────────┐
│ 1. 用户编辑 BundleChain                                      │
│    - 选择/添加/删除组件包（bundleIds 数组）                   │
│    - 调整占位符参数（params）                                 │
│    - 点击「生成 Config」按钮                                  │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. 按顺序加载组件包                                          │
│    - 遍历 bundleIds 数组                                      │
│    - 从 DB 查询 ComponentBundle 表                           │
│    - 解析 JSON 组件列表                                       │
│    - 按顺序组合（子覆盖父）                                   │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. 合并组件                                                  │
│    - 单例组件：后面的覆盖前面的                                │
│    - 数组组件：后面的追加到前面的（按 tag 去重）                │
│    - 生成扁平化组件列表                                       │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. 冲突检测                                                  │
│    - 单例组件重复检测 → ERROR                                │
│    - 数组组件 tag 冲突检测 → ERROR                            │
│    - routing 引用依赖检测 → WARN                              │
│    - 跨包覆盖允许（WARN 级别）                                │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. 应用组件到配置                                            │
│    - 单例组件（log/dns/api）：直接覆盖                       │
│    - 数组组件（inbound/outbound）：追加到数组                  │
│    - routing：合并规则                                       │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. 保留占位符                                                │
│    - 不替换占位符（如 ${socksPort}）                          │
│    - 生成带占位符的完整配置                                   │
│    - 保存到 Template.Config                                   │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. 更新模板状态                                              │
│    - Config 已更新 → 状态：✅一致                             │
│    - 如果有冲突（WARN）→ 允许保存但提示                       │
│    - 如果有错误（ERROR）→ 阻止保存，显示错误                  │
└─────────────────────────────────────────────────────────────┘
```

### 5.3 组件包管理流程

```
┌─────────────────────────────────────────────────────────────┐
│ 创建组件包                                                    │
├─────────────────────────────────────────────────────────────┤
│ 1. 定义组件列表（UI 或 JSON）                                │
│ 2. 选择模板类型（base/test/standalone/pool）                  │
│ 3. 选择核心类型（xray/singbox）                               │
│ 4. 保存到 ComponentBundle 表                                 │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ 版本管理                                                      │
├─────────────────────────────────────────────────────────────┤
│ 1. 修改组件列表                                              │
│ 2. Version + 1                                              │
│ 3. 保存新版本（保留历史版本）                                │
│ 4. 标记所有引用此组件包的模板为「已过期」                      │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ BundleChain 组合（非继承）                                    │
├─────────────────────────────────────────────────────────────┤
│ 1. BundleChain 是组件包 ID 数组                              │
│ 2. 按顺序组合多个组件包                                      │
│ 3. 后加载的组件包覆盖先加载的（单例）/追加（数组）              │
│ 4. 不使用 ParentId 继承机制                                  │
└─────────────────────────────────────────────────────────────┘
```

### 5.3 Template 与 BundleChain 关系（v1.5 更新 — P0-2 修复）

**FullConfigTemplateItem 表结构变更**：
```sql
ALTER TABLE FullConfigTemplateItem ADD COLUMN TemplateType varchar(20);
ALTER TABLE FullConfigTemplateItem ADD COLUMN BundleChain varchar;
```

```
┌─────────────────────────────────────────────────────────────┐
│ FullConfigTemplateItem (模板)                                │
├─────────────────────────────────────────────────────────────┤
│ Id: "xray_test_001"                                         │
│ CoreType: 2 (Xray)                                          │
│ TemplateType: "test"        ← 模板类型（新增列）             │
│ Remarks: "Xray 批量测试模板"                                 │
│                                                              │
│ BundleChain: {                   ← 组件组合链（新增列）      │
│   "bundleIds": [                       │
│     "base_xray_bundle_001",              │
│     "test_xray_bundle_001"                │
│   ],                                     │
│   "params": {                               │
│     "socksPort": 10809,                     │
│     "apiPort": 10085                        │
│   }                                          │
│ }                                            │
│                                                              │
│ Config: { "log": {...}, "dns": {...}, ... }  ← 生成快照      │
│                                                              │
│ 状态指示：✅一致 / ⚠️已过期 / 📝已手动编辑                    │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ BundleChain 生成流程                                           │
├─────────────────────────────────────────────────────────────┤
│ 1. 加载 bundleChain.bundleIds 中的所有组件包                  │
│ 2. 按顺序合并组件（单例覆盖、数组追加）                           │
│ 3. 检测冲突（单例冲突、tag 冲突）                               │
│ 4. 替换占位符（使用 bundleChain.params 或运行时值）              │
│ 5. 生成最终配置 → 保存到 Template.Config                       │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ ComponentBundle (组件包)                                     │
├─────────────────────────────────────────────────────────────┤
│ Name: "base_xray_bundle_001"                                │
│ TemplateType: "base"                                        │
│ CoreType: 2 (Xray)                                          │
│ Version: 1                                                  │
│ Components: [                                               │
│   { "type": "log", "params": {...} },                       │
│   { "type": "dns", "params": {...} },                       │
│   { "type": "policy", "params": {...} }                     │
│ ]                                                           │
│                                                              │
│ Name: "test_xray_bundle_001"                                │
│ TemplateType: "test"                                        │
│ CoreType: 2 (Xray)                                          │
│ Version: 1                                                  │
│ Components: [                                               │
│   { "type": "api", "params": {...} },                       │
│   { "type": "inbound", "params": {...} },                   │
│   { "type": "outbound", "params": {...} },                  │
│   { "type": "routing", "params": {...} }                    │
│ ]                                                           │
└─────────────────────────────────────────────────────────────┘
```

---

## 6. 新旧架构对比

### 6.1 配置对比

**旧架构（命令式步骤）**：
```json
{
  "steps": [
    {"type": "add_api", "params": {"tag": "api", "listen": "127.0.0.1:10085"}},
    {"type": "add_stats", "params": {}},
    {"type": "add_observatory", "params": {"subjectSelector": ["proxy"]}},
    {"type": "add_balancer", "params": {"tag": "balancer-out", "selector": ["proxy"]}},
    {"type": "add_outbound", "params": {"tag": "proxy", "source": "profile"}},
    {"type": "add_inbound", "params": {"tag": "socks", "port": 10809}},
    {"type": "merge_routing", "params": {"inboundTag": "socks", "outboundTag": "balancer-out"}}
  ]
}
```

**新架构（组件声明式）**：
```json
{
  "components": [
    {"type": "api",         "params": {"tag": "api", "listen": "127.0.0.1:10085"}},
    {"type": "stats",       "params": {}},
    {"type": "observatory", "params": {"subjectSelector": ["proxy"]}},
    {"type": "balancer",    "params": {"tag": "balancer-out", "selector": ["proxy"]}},
    {"type": "outbound",    "params": {"tag": "proxy", "source": "profile"}},
    {"type": "inbound",     "params": {"tag": "socks", "port": 10809}},
    {"type": "routing",     "params": {"rules": [{"inboundTag": "socks", "outboundTag": "balancer-out"}]}}
  ]
}
```

### 6.2 完整组件类型列表

**Xray 官方配置节**（https://xtls.github.io/config）：

```
┌─────────────────────────────────────────────────────────────┐
│                     Xray 配置文件结构                         │
├─────────────────────────────────────────────────────────────┤
│  {                                                           │
│    "env": {},                  ← env 组件                    │
│    "log": {},                  ← log 组件                    │
│    "api": {},                  ← api 组件                    │
│    "dns": {},                  ← dns 组件                    │
│    "routing": {},              ← routing 组件                │
│    "policy": {},               ← policy 组件                 │
│    "inbounds": [],             ← inbound 组件（数组）         │
│    "outbounds": [],            ← outbound 组件（数组）        │
│    "stats": {},                ← stats 组件                  │
│    "fakedns": {},              ← fakedns 组件                │
│    "metrics": {},              ← metrics 组件                │
│    "observatory": {},          ← observatory 组件            │
│    "burstObservatory": {},     ← burstObservatory 组件       │
│    "geodata": {},              ← geodata 组件                │
│    "version": {}               ← version 组件                │
│  }                                                           │
└─────────────────────────────────────────────────────────────┘
```

**组件类型与配置节映射**：

| 组件类型 | 配置节 | 类型 |
|----------|--------|------|
| `env` | `env` | EnvObject |
| `log` | `log` | LogObject |
| `api` | `api` | ApiObject |
| `dns` | `dns` | DnsObject |
| `routing` | `routing` | RoutingObject |
| `policy` | `policy` | PolicyObject |
| `inbound` | `inbounds[]` | InboundObject[] |
| `outbound` | `outbounds[]` | OutboundObject[] |
| `stats` | `stats` | StatsObject |
| `fakedns` | `fakedns` | FakeDnsObject |
| `metrics` | `metrics` | MetricsObject |
| `observatory` | `observatory` | ObservatoryObject |
| `burstObservatory` | `burstObservatory` | BurstObservatoryObject |
| `geodata` | `geodata` | GeodataObject |
| `version` | `version` | VersionObject |

| 维度 | 旧架构（步骤） | 新架构（组件） |
|------|----------------|----------------|
| **学习曲线** | 需要理解操作步骤顺序 | 只需声明组件类型 |
| **冲突检测** | 无（静默覆盖） | 自动检测并报错 |
| **依赖关系** | 隐式（执行顺序依赖） | 显式（routing 规则引用检测） |
| **配置复用** | 步骤序列复制粘贴 | 组件引用复用 |
| **预览** | 执行后才能看到结果 | 可静态分析组件依赖 |
| **扩展性** | 新增步骤类型 | 新增组件类型（相同模式） |

---

## 7. 校验规则（v1.4 新增）

### 6.1 模板完整性校验

**核心原则**：生成配置实例时**只替换占位符，不新增组件**。

**校验规则**：
1. **模板必须包含所有必要的组件类型**（完整配置结构）
2. **组件内容可以是占位符**（如 `${socksPort}`、`${apiPort}`）
3. **生成实例时只替换占位符，不新增/删除组件**
4. **BundleChain 生成 Config 后保存到模板**，生成实例时不再修改结构

**必要组件类型**（根据 TemplateType）：

| TemplateType | 必要组件 | 说明 |
|--------------|----------|------|
| test | log, dns, inbounds, outbounds | 批量测试最小配置 |
| standalone | log, dns, api, inbounds, outbounds, routing | 独立代理完整配置 |
| pool | log, dns, api, inbounds, outbounds, routing, observatory | 代理池配置 |

**示例**：

```json
{
  "log": {
    "loglevel": "warning"
  },
  "dns": {
    "servers": ["1.1.1.1"]
  },
  "inbounds": [
    {
      "tag": "socks",
      "protocol": "mixed",
      "port": "${socksPort}"
    }
  ],
  "outbounds": [
    {
      "tag": "proxy",
      "type": "vmess"
    }
  ]
}
```

**校验时机**：
1. **BundleChain 生成时**：检测组件完整性，缺少必要组件则报错
2. **模板保存时**：检测 Config 字段是否包含必要组件
3. **生成实例时**：检测占位符是否全部替换（无残留 `${...}`）

### 6.2 占位符替换规则

**替换范围**：
- 字符串值中的占位符（如 `"port": "${socksPort}"`）
- 数值占位符（如 `"port": ${socksPort}`，不加引号）
- 数组中的占位符（如 `"servers": ["${dnsServer}"]`）
- 嵌套对象中的占位符（如 `"settings": {"tag": "${tag}"}`）

**替换顺序**：
1. 先替换所有占位符（使用 bundleChain.params 或运行时值）
2. 再验证 JSON 格式合法性
3. 最后保存到配置文件

**未替换占位符处理**：
- 如果替换后仍有 `${...}` 残留 → **ERROR**
- 错误消息：`"未替换的占位符: ${socksPort}"`

### 6.3 新旧架构对比

| 维度 | 旧架构（步骤） | 新架构（组件 + BundleChain） |
|------|----------------|-----------------------------|
| **配置生成** | 步骤序列执行 | BundleChain 生成 + 占位符替换 |
| **模板内容** | 完整配置 | 完整配置（带占位符） |
| **实例生成** | 重新执行步骤 | 只替换占位符 |
| **组件新增** | 步骤可动态添加 | 不允许（只替换） |
| **校验时机** | 无 | BundleChain 生成时 + 实例生成时 |

---

## 8. 向后兼容

### 8.1 双模式支持（v1.6 更新 — P0-3 修复）

```cpp
struct PipelineDefinition {
    std::string templateId;
    
    // 新模式：BundleChain（v1.5+）
    std::optional<BundleChain> bundleChain;
    
    // 中间模式：组件列表（v1.0-v1.3）
    std::optional<std::vector<ComponentConfig>> components;
    
    // 旧模式：步骤列表（向后兼容）
    std::optional<std::vector<StepConfig>> steps;
};
```

**加载逻辑**：
```cpp
if (defObj.contains("bundleChain")) {
    def.bundleChain = parseBundleChain(defObj);  // 新模式
} else if (defObj.contains("components")) {
    def.components = parseComponents(defObj);    // 中间模式
} else if (defObj.contains("steps")) {
    def.steps = parseSteps(defObj);              // 旧模式，转换为组件
}
```

**优先级**：bundleChain > components > steps

### 7.2 步骤到组件的转换（v1.5 更新 — P1-4）

**完整转换表**（10 个步骤类型）：

| 步骤类型 | 组件类型 | 特殊处理 | 说明 |
|----------|----------|----------|------|
| `add_inbound` | `inbound` | 否 | 直接映射 |
| `add_outbound` | `outbound` | 否 | 直接映射 |
| `add_api` | `api` | 否 | 直接映射 |
| `add_stats` | `stats` | 否 | 直接映射 |
| `add_policy` | `policy` | 否 | 直接映射 |
| `add_observatory` | `observatory` | 否 | 直接映射 |
| `add_balancer` | `routing` | 是 | balancer 是 routing 的子组件，需合并到 routing.balancers[] |
| `merge_routing` | `routing` | 是 | 合并而非覆盖，需特殊处理 rules 数组 |
| `modify_field` | `modify_field` | 是 | 特殊组件，保留为特殊类型 |
| `replace_variables` | N/A | 是 | 不需要转换，在占位符替换阶段处理 |

```cpp
struct StepToComponentMapping {
    std::string stepType;
    std::string componentType;
    bool specialHandling;  // 是否需要特殊处理
};

const std::vector<StepToComponentMapping> STEP_COMPONENT_MAPPINGS = {
    {"add_inbound", "inbound", false},
    {"add_outbound", "outbound", false},
    {"add_api", "api", false},
    {"add_stats", "stats", false},
    {"add_policy", "policy", false},
    {"add_observatory", "observatory", false},
    {"add_balancer", "routing", true},    // balancer 是 routing 子组件
    {"merge_routing", "routing", true},    // 合并而非覆盖
    {"modify_field", "modify_field", true}, // 特殊组件
    {"replace_variables", "N/A", true}      // 不需要转换
};

std::vector<ComponentConfig> convertStepsToComponents(
    const std::vector<StepConfig>& steps) {
    
    std::vector<ComponentConfig> components;
    
    for (const auto& step : steps) {
        const StepToComponentMapping* mapping = findMapping(step.type);
        if (!mapping || mapping->componentType == "N/A") {
            continue;  // 跳过不需要转换的步骤
        }
        
        ComponentConfig comp;
        comp.type = mapping->componentType;
        comp.params = step.params;
        
        if (mapping->specialHandling) {
            if (step.type == "add_balancer") {
                // balancer 是 routing 的子组件，合并到 routing.balancers[]
                mergeBalancerToRouting(components, comp);
            } else if (step.type == "merge_routing") {
                // 合并 routing rules
                mergeRoutingRules(components, comp);
            } else if (step.type == "modify_field") {
                // 特殊组件，直接添加
                components.push_back(comp);
            }
        } else {
            components.push_back(comp);
        }
    }
    
    return components;
}
```

---

## 9. UI 变更

### 8.1 组件列表视图

```
┌─────────────────────────────────────────────────────────┐
│  组件列表                                                 │
├──────┬──────────────┬────────┬──────────┬───────────────┤
│  #   │ 组件类型      │ 配置节 │ 组合策略 │ 状态          │
├──────┼──────────────┼────────┼──────────┼───────────────┤
│  1   │ api          │ api    │ override │ ✅ 正常        │
│  2   │ stats        │ stats  │ override │ ✅ 正常        │
│  3   │ policy       │ policy │ override │ ✅ 正常        │
│  4   │ observatory  │ obs    │ override │ ✅ 正常        │
│  5   │ balancer     │ balanc │ append   │ ✅ 正常        │
│  6   │ outbound     │ out    │ append   │ ✅ 正常        │
│  7   │ inbound      │ in     │ append   │ ✅ 正常        │
│  8   │ routing      │ rout   │ merge    │ ⚠️ 依赖缺失   │
└──────┴──────────────┴────────┴──────────┴───────────────┘
```

### 8.2 冲突高亮

- **ERROR**（红色）：单例冲突、Tag 冲突 → 阻止保存
- **WARN**（黄色）：依赖缺失 → 允许保存但提示

---

## 10. 配置生成机制设计（v1.5 新增）

### 9.1 运行时配置生成流程

```
┌─────────────────────────────────────────────────────────────┐
│ 1. 触发配置生成                                              │
│    - 启动代理实例 / 批量测试 / 代理池                          │
│    - 输入：templateId, profileId, runtimeParams               │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. 加载模板                                                  │
│    - 从 DB 查询 FullConfigTemplateItem                       │
│    - 读取 Config 字段（带占位符的完整配置）                    │
│    - 检查状态：✅一致 / ⚠️已过期 / 📝已手动编辑               │
│    - 如果过期 → 提示用户重新生成 Config                       │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. 构建运行时参数                                            │
│    - socksPort: 分配可用端口                                 │
│    - apiPort: 分配可用端口                                   │
│    - probeUrl: 从配置读取                                    │
│    - profileId: 当前代理 ID                                  │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. 占位符替换                                                │
│    - 遍历 Config JSON，替换所有 ${...} 占位符                 │
│    - 使用 ConfigVariables 中的运行时值                        │
│    - 复用 ConfigBuilder::replaceVariables()                   │
│    - 检查残留占位符 → ERROR                                   │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. 注入代理配置                                              │
│    - 从 Profileitem 生成 outbound 配置                        │
│    - 替换 Config 中的 outbound（tag="proxy"）                 │
│    - 保留其他组件不变                                         │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. 验证配置                                                  │
│    - JSON 格式合法性                                         │
│    - 必要组件完整性（根据 TemplateType）                      │
│    - 占位符全部替换（无残留 ${...}）                          │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. 写入配置文件                                              │
│    - 写入到 config/ 目录（如 config/xray_10809.json）         │
│    - 备份旧文件（如 xray_10809.json.bak）                     │
│    - 返回配置文件路径                                         │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│ 8. 启动代理实例                                              │
│    - 使用配置文件启动 xray/singbox 进程                        │
│    - 返回实例信息（PID, 端口等）                               │
└─────────────────────────────────────────────────────────────┘
```

### 9.2 占位符替换机制

**占位符格式**：
```
${variableName}                    // 基本格式
${variableName:defaultValue}       // 带默认值
```

**替换规则**：
| 场景 | 行为 | 示例 |
|------|------|------|
| 变量存在 | 替换为变量值 | `${socksPort}` → `10809` |
| 变量不存在，有默认值 | 使用默认值 | `${port:10809}` → `10809` |
| 变量不存在，无默认值 | ERROR | `${undefinedVar}` → 报错 |
| 数值类型 | 不加引号 | `"port": ${socksPort}` → `10809` |
| 字符串类型 | 加引号 | `"tag": "${tag}"` → `"socks"` |

**替换实现**：
```cpp
boost::json::object replacePlaceholders(
    const boost::json::object& config,
    const ConfigVariables& vars) {
    
    boost::json::object result;
    
    for (const auto& [key, value] : config) {
        if (value.is_string()) {
            std::string str = value.as_string();
            std::string replaced = replaceInString(str, vars);
            // 检查是否为数值
            if (isNumeric(replaced)) {
                result[key] = std::stoll(replaced);
            } else {
                result[key] = replaced;
            }
        } else if (value.is_object()) {
            result[key] = replacePlaceholders(value.as_object(), vars);
        } else if (value.is_array()) {
            boost::json::array newArr;
            for (const auto& item : value.as_array()) {
                newArr.push_back(replaceJsonValue(item, vars));
            }
            result[key] = newArr;
        } else {
            result[key] = value;
        }
    }
    
    return result;
}

std::string replaceInString(const std::string& str, const ConfigVariables& vars) {
    std::string result = str;
    std::regex pattern(R"(\$\{(\w+)(?::([^}]*))?\})");
    
    result = std::regex_replace(result, pattern, 
        [&](const std::smatch& match) {
            const std::string& varName = match[1];
            const std::string& defaultValue = match[2];
            
            if (vars.contains(varName)) {
                return vars.at(varName);
            } else if (!defaultValue.empty()) {
                return defaultValue;
            } else {
                throw ConfigGenerationError("未定义的变量: " + varName);
            }
        });
    
    return result;
}

// v1.6 新增：递归替换 JSON 值中的占位符
boost::json::value replaceJsonValue(
    const boost::json::value& value,
    const ConfigVariables& vars) {
    if (value.is_string()) {
        std::string str = value.as_string();
        std::string replaced = replaceInString(str, vars);
        return isNumeric(replaced) ? boost::json::value(std::stoll(replaced)) 
                                    : boost::json::value(replaced);
    } else if (value.is_object()) {
        return replacePlaceholders(value.as_object(), vars);
    } else if (value.is_array()) {
        boost::json::array newArr;
        for (const auto& item : value.as_array()) {
            newArr.push_back(replaceJsonValue(item, vars));
        }
        return boost::json::value(newArr);
    }
    return value;
}
```

### 9.3 配置缓存策略

**缓存层级**：
| 层级 | 内容 | 缓存时机 | 失效条件 |
|------|------|----------|----------|
| L1 | 模板 Config（带占位符） | 模板加载时 | 模板修改 |
| L2 | 替换后配置（实例级） | 生成实例时 | 端口变化、profile 变化 |

**缓存实现**：
```cpp
class ConfigCache {
public:
    /// 获取模板配置（L1）
    boost::json::value getTemplateConfig(const std::string& templateId);
    
    /// 生成实例配置（L2）
    boost::json::value getInstanceConfig(
        const std::string& templateId,
        const ConfigVariables& vars,
        const Profileitem& profile);
    
    /// 使缓存失效
    void invalidateTemplate(const std::string& templateId);
    void invalidateInstance(const std::string& templateId, int port);
    
private:
    mutable std::shared_mutex mutex_;  // v1.6 新增：线程安全
    std::unordered_map<std::string, CacheEntry> templateCache_;
    std::unordered_map<std::string, CacheEntry> instanceCache_;
};
```

**缓存策略**：
- **模板配置**：长期缓存，直到模板修改
- **实例配置**：短期缓存，端口变化时失效
- **LRU 淘汰**：最多缓存 100 个实例配置

### 9.4 错误处理策略

**编辑时（保存模板）**：
| 错误类型 | 行为 | 示例 |
|----------|------|------|
| ERROR | 阻止保存，显示错误信息 | 缺少必要组件 |
| WARN | 允许保存，显示警告信息 | routing 引用不存在的 tag |

**运行时（生成配置）**：
| 错误类型 | 行为 | 示例 |
|----------|------|------|
| ERROR | 抛出异常，配置生成失败 | 占位符未替换 |
| WARN | 记录日志，继续执行 | 端口被占用 |

**可配置策略**：
```json
{
  "config_generation": {
    "on_error": "fail_fast",      // fail_fast | warn_continue
    "on_warn": "log_only",        // log_only | show_dialog
    "cache_enabled": true,
    "cache_max_size": 100
  }
}
```

**错误类型定义**：
```cpp
enum class ConfigGenerationErrorType {
    MISSING_COMPONENT,        // 缺少必要组件
    INVALID_JSON,             // JSON 格式错误
    UNREPLACED_PLACEHOLDER,   // 占位符未替换
    INVALID_PARAMS,           // 参数格式错误
    BUNDLE_NOT_FOUND,         // 组件包不存在
    TEMPLATE_NOT_FOUND,       // 模板不存在
    PORT_IN_USE               // 端口被占用
};

class ConfigGenerationError : public std::runtime_error {
public:
    ConfigGenerationError(ConfigGenerationErrorType type, const std::string& message);
    ConfigGenerationErrorType getType() const;
private:
    ConfigGenerationErrorType type_;
};
```

---

## 11. 迁移策略（v1.5 新增）

### 10.1 三阶段迁移计划

**阶段1：双模式并行（1-2 周）**
```
- 新模板使用 BundleChain 模式
- 旧模板使用步骤模式
- 系统自动检测并路由到对应模式
- UI 支持两种模式切换
```

**阶段2：迁移工具（1 周）**
```
- 提供步骤→组件转换工具
- 批量转换现有模板
- 验证转换后的配置正确性
- 用户确认迁移结果
```

**阶段3：废弃步骤模式（2 周后）**
```
- 删除步骤相关代码
- 只保留组件模式
- 更新文档和示例
```

### 10.2 迁移工具设计

```cpp
class TemplateMigrationTool {
public:
    /// 检测模板模式（步骤 vs 组件）
    TemplateMode detectMode(const FullConfigTemplateItem& templateItem) const;
    
    /// 转换步骤模式为组件模式
    bool convertToComponentMode(
        const FullConfigTemplateItem& templateItem,
        FullConfigTemplateItem& result) const;
    
    /// 批量转换
    MigrationResult batchConvert(
        const std::vector<FullConfigTemplateItem>& templates) const;
    
private:
    StepToComponentMapping findMapping(const std::string& stepType) const;
};

struct MigrationResult {
    int total;
    int success;
    int failed;
    std::vector<std::string> errors;
};
```

---

## 12. JSON Schema（v1.6 更新 — P1-6 修复）

### 12.1 BundleChain Schema

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "BundleChain",
  "type": "object",
  "required": ["bundleIds"],
  "properties": {
    "bundleIds": {
      "type": "array",
      "items": { "type": "string" },
      "minItems": 1,
      "description": "组件包 ID 数组，按顺序组合"
    },
    "params": {
      "type": "object",
      "additionalProperties": {
        "oneOf": [
          { "type": "string" },
          { "type": "number" },
          { "type": "boolean" },
          { "type": "object" },
          { "type": "array" }
        ]
      },
      "description": "占位符参数"
    }
  },
  "additionalProperties": false
}
```

### 12.2 ComponentBundle Schema

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "ComponentBundle",
  "type": "object",
  "required": ["id", "name", "coreType", "templateType", "components"],
  "properties": {
    "id": {
      "type": "string",
      "pattern": "^[a-z]+_[a-z]+_[0-9]{3}$",
      "description": "Bundle 唯一标识（如 base_xray_001）"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "description": "Bundle 显示名称"
    },
    "description": {
      "type": "string",
      "description": "Bundle 描述（可选）"
    },
    "coreType": {
      "type": "integer",
      "enum": [2, 24],
      "description": "核心类型：2=Xray, 24=SingBox"
    },
    "templateType": {
      "type": "string",
      "enum": ["base", "test", "standalone", "pool"],
      "description": "模板类型：base=基础, test=批量测试, standalone=独立代理, pool=代理池"
    },
    "version": {
      "type": "integer",
      "minimum": 1,
      "default": 1,
      "description": "版本号（更新时递增）"
    },
    "enabled": {
      "type": "boolean",
      "default": true,
      "description": "是否启用"
    },
    "components": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["type", "params"],
        "properties": {
          "type": {
            "type": "string",
            "enum": [
              "env", "log", "api", "dns", "routing", "policy",
              "inbound", "outbound", "stats", "fakedns", "metrics",
              "observatory", "burstObservatory", "geodata", "version"
            ]
          },
          "params": {
            "type": "object",
            "additionalProperties": true
          },
          "id": {
            "type": "string",
            "description": "可选：组件唯一标识"
          }
        },
        "additionalProperties": false
      }
    }
  },
  "additionalProperties": false
}
```

---

## 13. 实施计划（v1.6 更新 — 52h）

| 阶段 | 任务 | 工时 | 依赖 |
|------|------|------|------|
| 1 | ComponentBundle 表结构 + DAO + 数据库迁移 | 4h | — |
| 2 | 组件类型元数据结构 + 注册表（15个类型，含 CoreType 过滤） | 4h | — |
| 3 | 组件解析器核心逻辑（含 SingBox 差异处理） | 7h | 1, 2 |
| 4 | 冲突检测算法（含跨包冲突区分 + 边界测试） | 5h | 3 |
| 5 | BundleChain 解析器（组合多个组件包） | 5h | 3, 4 |
| 6 | 模板定义重构（BundleChain + Config 字段 + 状态指示 + 版本同步） | 4h | 1, 5 |
| 7 | 步骤→组件转换（10个类型） | 3h | 4 |
| 8 | UI 组件列表视图 + BundleChain 编辑器 + CoreType 适配 | 10h | 6, 7 |
| 9 | 配置生成机制（占位符替换 + 缓存 + 错误处理 + 线程安全） | 6h | 5, 6 |
| 10 | JSON Schema 校验 + 数据库迁移脚本 | 3h | 1, 2 |
| 11 | 单元测试 + 集成测试（含跨包冲突测试） | 6h | 8, 9 |
| **合计** | | **57h** | |

**关键变更（v1.6）**：
- 工时从 40h 调整为 57h（+17h）
- 阶段 2：增加 CoreType 过滤（+1h）
- 阶段 3：增加 SingBox 差异处理（+1h）
- 阶段 4：增加跨包冲突区分（+1h）
- 阶段 6：增加版本同步机制（+1h）
- 阶段 8：增加 CoreType 适配（+2h）
- 阶段 9：增加线程安全（+0h，含在缓存中）
- 新增阶段 10：JSON Schema 校验 + 迁移脚本（3h）
- 阶段 11：增加跨包冲突测试（+1h）

---

## 14. 风险与约束

### 14.1 风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 旧配置不兼容 | 用户配置无法使用 | 双模式支持 + 自动转换 |
| 组件类型遗漏 | 某些配置节无法配置 | 组件注册表可扩展 |
| 冲突检测误报 | 阻止有效配置 | WARN 级别不阻止保存 |

### 14.2 约束

- **不能破坏现有步骤机制** — 通过双模式支持实现兼容
- **冲突检测不能过于严格** — 允许 WARN 级别继续执行
- **UI 变更不能影响其他功能** — 独立组件列表视图

---

## 15. 变更记录

| 版本 | 日期 | 变更内容 | 作者 |
|------|------|----------|------|
| v1.6 | 2026-09-22 | **独立评审修订（P0+P1 全部修复）**：①P0-1 重新编号章节（消除重复§6/§10）；②P0-2 重写§5.1/§5.2 数据流（BundleChain 快照模式）；③P0-3 PipelineDefinition 添加 bundleChain 字段（优先级 bundleChain > components > steps）；④P0-4 添加 base TemplateType（共享基础组件包）；⑤P1-1 Conflict 结构体增加 sourceBundle1/2 + isCrossBundle（跨包单例 WARN，tag ERROR）；⑥P1-2 添加 Bundle 版本同步机制（Version+1 → 模板标记过期）；⑦P1-3 添加 SingBox 配置差异说明（8 个不支持组件 + balancer/routing 差异）；⑧P1-4 ConfigCache 添加 std::shared_mutex 线程安全；⑨P1-5 补充 replaceJsonValue 递归函数；⑩P1-6 ComponentBundle JSON Schema 补全（id/name/coreType/templateType/version/enabled）；⑪工时从 40h 调整为 57h（11 阶段） | AI Assistant |
| v1.5 | 2026-09-22 | **评审修订 + 配置生成机制**：①P0-1 明确单一数据源策略（快照模式：BundleChain=编辑，Config=快照）；②P0-2 FullConfigTemplateItem 表添加 BundleChain 列和 TemplateType 列；③P0-3 移除 ComponentBundle ParentId，统一用 BundleChain 组合；④P1-1 新增 BundleChainResolver 接口；⑤P1-4 补全 10 个步骤→组件转换表；⑥新增 §9 配置生成机制设计（运行时流程、占位符替换、缓存策略、错误处理）；⑦新增 §10 迁移策略（三阶段）；⑧新增 §11 JSON Schema（BundleChain + ComponentBundle）；⑨工时从 25h 调整为 40h | AI Assistant |
| v1.4 | 2026-09-22 | **BundleChain 重构 + 校验规则**：①BundleId → BundleChain（bundleIds 数组，组件包 ID 组合链）；②模板 Config 字段保存 BundleChain 生成结果（带占位符）；③生成配置时只用参数或 JSON 替换占位符；④新增 BundleChain 格式说明（bundleIds + params）；⑤新增校验规则（模板完整性校验、占位符替换规则）；⑥实施计划更新为 9 阶段 25h | AI Assistant |
| v1.3 | 2026-09-22 | **架构重构**：①模板 = 类型（test/standalone/pool）+ 组件组合管线；②新增 ComponentBundle 表（存储组件包，支持版本管理和继承）；③更新数据流（组件包从 DB 加载）；④实施计划更新为 8 阶段 22h；⑤明确 TemplateType 与 CoreType 组合（6 种组合） | AI Assistant |
| v1.2 | 2026-09-22 | 新增占位符支持：所有组件 params 支持 `${socksPort}`/`${apiPort}`/`${probeUrl}` 等占位符，在组件应用时自动解析；复用 ConfigBuilder::replaceVariables()；新增 replacePlaceholders() 私有方法 | AI Assistant |
| v1.1 | 2026-09-22 | 组件类型参照 Xray 官方配置结构（https://xtls.github.io/config）完整定义 15 个组件类型：env/log/api/dns/routing/policy/inbound/outbound/stats/fakedns/metrics/observatory/burstObservatory/geodata/version；新增完整组件类型列表和配置节映射表 | AI Assistant |
| v1.0 | 2026-09-22 | 初始版本 — 组件化重构方案设计 | AI Assistant |