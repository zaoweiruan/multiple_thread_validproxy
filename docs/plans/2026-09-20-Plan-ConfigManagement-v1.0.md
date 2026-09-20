---
title: "plan: Xray/Sing-box 配置管理统一方案（含独立评审结论）"
type: plan
status: draft
date: 2026-09-20
version: 1.1
---

# Xray/Sing-box 配置管理统一方案

> 独立评审后修订版。原方案（EAV + JSONPath + DB 三表）被否决，改为推荐方案 F（统一工厂）+ 可选方案 C'（DB 单 BLOB）。
> **v1.1 新增**：配置所属代理类型字段（xray / sing-box），方案 F 天然支持双后端。

---

## 1. 背景与问题

### 1.1 现状

| 配置类型 | 生成位置 | 当前存储 | 问题 |
|---------|---------|---------|------|
| 测试配置 | `XrayInstance::createConfigFile()` | 硬编码 C++ 模板 | 无法修改、无法版本化 |
| 池启动配置 | `ConfigGenerator::buildPoolConfig()` | 硬编码 + `kPoolDnsJson` 常量 | DNS 双份维护、模板分叉 |
| 独立代理配置 | `ConfigGenerator::generateConfig()` → `XrayConfigAssembler` | 由 ProfileItem 字段生成 | 仅生成 outbound，无完整模板 |

### 1.2 已有外部模板

- `bin/xray-config-template.json` (6649B)
- `bin/singbox-config-template.json` (7886B)
- UI 已支持指定模板路径（3 个 `wxFileProperty`）
- `AppController::startStandaloneProxy` 已走"读外部模板 → 改写"路径

### 1.3 已归档规范

`docs/specs/2026-08-31-Spec-ConfigGenerationPathways-v1.0.md` 明确建议：
> "收敛为一个统一配置构造器（单点工厂），输入业务模式（test / standalone / pool），输出完整配置 JSON"

**不是**数据库化。

---

## 2. 独立评审结论

### 2.1 原方案否决原因

原方案（EAV + JSONPath + DB 三表）存在 **5 个阻断风险**：

| # | 风险 | 严重程度 | 说明 |
|---|------|---------|------|
| R1 | **违背已归档规范方向** | 🔴 阻断 | `2026-08-31-Spec-ConfigGenerationPathways` 明确推荐"统一工厂"，原方案走反方向 |
| R2 | **JSONPath 装配器 = 隐性自造语言** | 🔴 阻断 | 需自造 JSONPath 解析器 + 装配状态机 + 变量替换引擎 + Xray schema 校验器（300-500 行生产代码 + 500 行测试） |
| R3 | **UI 暴露 400+ 行 config 给终端用户 = UX 灾难** | 🔴 阻断 | 用户搞坏 DB → 应用起不来 → 无回退，可用性回归 |
| R4 | **模板变量系统不足以表达真实需求** | 🟡 高 | `${var}` 无法表达 observatory 块 / api.services 的耦合开关、数组插入、类型敏感替换 |
| R5 | **工时严重低估 + sing-box 场景遗漏** | 🟡 高 | 原估 21h，实际 30-41h；且只覆盖 Xray，未提 sing-box |

### 2.2 替代方案对比

| 方案 | 实现量 | 版本管理 | UI 编辑 | 结构校验 | 迁移风险 | 备注 |
|------|--------|---------|---------|---------|---------|------|
| A. 保留当前外部 JSON 文件 | 0 | ✅ 天然 | ⚠️ 需手动 | ⚠️ 靠 ConfigValidator | 无 | **已经在工作** |
| **B. JSON 文件 + `${var}` 占位符** | **3-5h** | ✅ | ⚠️ 简单 | ✅ 保留原校验 | 低 | **推荐（快速）** |
| C. DB 单 BLOB per profile | 6-10h | ❌ | ✅ 编辑 JSON | ✅ boost::json | 中 | 次优 |
| D. EAV + JSONPath + `${var}` | 25-45h | ❌ | ✅ 但复杂 | ❌ 需自造 schema | **高** | **否决** |
| **F. 统一工厂（已归档规范）** | **10-15h** | ✅ 代码即配置 | ❌ 无 UI 编辑 | ✅ | 低 | **推荐（长期正解）** |

---

## 3. 推荐方案

### 3.1 方案 F：统一工厂（长期正解）

实施 `Spec-ConfigGenerationPathways-v1.0.md` 已归档的建议：

```
┌─────────────────────────────────────────────────────────────────┐
│                    统一配置工厂架构                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │              ConfigBuilder (单点工厂)                      │  │
│  │                                                          │  │
│  │  build(mode, proxyType, profile, variables)              │  │
│  │           → boost::json::value                           │  │
│  │                                                          │  │
│  │  mode = Test | Standalone | Pool                          │  │
│  │  proxyType = Xray | SingBox                               │  │
│  └─────────────────────────────────────────────────────────┘  │
│                              │                                   │
│              ┌───────────────┼───────────────┐                  │
│              ▼               ▼               ▼                  │
│        ┌─────────┐     ┌─────────┐     ┌─────────┐             │
│        │  Test   │     │Standalone│     │  Pool   │             │
│        │  Config │     │  Config  │     │  Config  │             │
│        └─────────┘     └─────────┘     └─────────┘             │
│              │               │               │                  │
│              └───────────────┼───────────────┘                  │
│                              ▼                                   │
│                   ┌─────────────────────┐                       │
│                   │  ProxyType 分支       │                       │
│                   │  ┌─────┐  ┌───────┐ │                       │
│                   │  │Xray │  │SingBox│ │                       │
│                   │  └─────┘  └───────┘ │                       │
│                   └─────────────────────┘                       │
│                                                                  │
│  输入：ProfileItem + 运行时变量（socksPort, apiPort, probeUrl...） │
│  输出：完整 JSON 配置（含 DNS、routing、observatory 等）          │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**核心设计**：

```cpp
// include/config/ConfigBuilder.h
enum class ConfigMode { Test, Standalone, Pool };

// 代理后端类型（xray / sing-box）
enum class ProxyType { Xray, SingBox };

struct ConfigVariables {
    int socksPort;
    int apiPort;
    std::string probeUrl;
    int observatoryInterval;
    std::string balancerStrategy;
    // ... 其他变量
};

class ConfigBuilder {
public:
    explicit ConfigBuilder(sqlite3* db);
    
    // 主入口：根据模式 + 代理类型生成配置
    boost::json::value build(
        ConfigMode mode,
        ProxyType proxyType,
        const db::models::Profileitem& profile,
        const ConfigVariables& vars);
    
    // 序列化
    std::string serialize(const boost::json::value& config) const;
};
```

**代理类型字段说明**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ProxyType::Xray` | 枚举 | Xray-core 后端，生成 xray 配置格式 |
| `ProxyType::SingBox` | 枚举 | Sing-box 后端，生成 sing-box 配置格式 |

**配置模板对应关系**：

| 配置模式 | Xray 模板 | Sing-box 模板 |
|---------|----------|--------------|
| Test | `xray-config-test.json` | `singbox-config-test.json` |
| Standalone | `xray-config-standalone.json` | `singbox-config-standalone.json` |
| Pool | `xray-config-pool.json` | `singbox-config-pool.json` |

**优势**：
- 三路径 × 双后端统一，兼容性修复只需改一处
- `observatory` / `api.services` 配对通过工厂内部约束保证（Xray 特有）
- 代码即配置，版本管理天然
- 无 UI 编辑器（避免 UX 灾难）
- **天然支持 sing-box**（原方案遗漏场景已覆盖）

**UI 扩展**：仅在 ConfigDialog 增加"高级选项"面板暴露 6-8 个高频变量：
- socksPort 基础值、apiPort 基础值
- test.url（探测 URL）
- 是否启用 observatory（仅 Xray）
- balancer 策略（仅 Xray 池模式）
- 是否启用 CN/private 直连分流
- **代理后端选择**（Xray / Sing-box，已有 `proxy.singbox.enabled` 字段）

### 3.2 方案 C'：DB 单 BLOB per template（备选）

如果必须要 UI 编辑 + DB 存储：

```sql
CREATE TABLE IF NOT EXISTS xray_templates (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    kind        TEXT NOT NULL UNIQUE CHECK(kind IN 
                ('test','standalone_xray','standalone_singbox','pool')),
    content     TEXT NOT NULL CHECK(json_valid(content)),
    version     INTEGER NOT NULL DEFAULT 1,
    updated_at  TEXT NOT NULL DEFAULT (datetime('now')),
    notes       TEXT
);
```

**特点**：
- 变量用 `${var}` 在 content 文本里做占位符
- UI 打开 JSON 树形编辑器，直接编辑 BLOB
- 无 JSONPath 装配器
- 工时：10-14h

### 3.3 方案 B：外部 JSON 文件 + `${var}` 占位符（快速方案）

保留外部 JSON 文件，加变量替换：

```json
{
    "inbounds": [
        {"port": ${socksPort}, "protocol": "mixed", ...}
    ]
}
```

- 工时：3-5h
- 收益：几乎相同，成本低一个数量级

---

## 4. 实施计划（方案 F）

### 4.1 阶段划分

| 阶段 | 任务 | 文件 | 预估工时 |
|------|------|------|---------|
| **P0** | 设计 ConfigBuilder 接口（含 ProxyType 枚举） | `include/config/ConfigBuilder.h` | 2h |
| **P0** | 实现 Test 模式配置生成（Xray + SingBox） | `src/config/ConfigBuilder.cpp` | 4h |
| **P0** | 实现 Standalone 模式配置生成（Xray + SingBox） | `src/config/ConfigBuilder.cpp` | 4h |
| **P0** | 实现 Pool 模式配置生成（Xray + SingBox） | `src/config/ConfigBuilder.cpp` | 4h |
| **P0** | 实现变量解析与替换 | `src/config/ConfigBuilder.cpp` | 2h |
| **P1** | 重构 ConfigGenerator::buildPoolConfig | `src/ConfigGenerator.cpp` | 2h |
| **P1** | 重构 XrayInstance::createConfigFile | `src/XrayInstance.cpp` | 1h |
| **P1** | 重构 AppController::startStandaloneProxy | `src/ui/AppController.cpp` | 1h |
| **P1** | 新增 SingBoxConfigBuilder 适配层 | `src/config/SingBoxConfigBuilder.cpp` | 2h |
| **P2** | ConfigDialog UI 扩展（高级选项 + 代理类型） | `src/ui/ConfigDialog.cpp` | 3h |
| **P2** | 配置变量持久化（config.json 扩展） | `include/ConfigReader.h` | 1h |
| **P3** | 快照测试（3 路径 × 2 后端 = 6 组 byte-identical） | `tests/test_config_builder.cpp` | 4h |
| **P3** | Xray 26.x 加载测试 | `tests/test_config_builder.cpp` | 2h |
| **P3** | Sing-box 加载测试 | `tests/test_config_builder.cpp` | 2h |
| **P3** | 文档 + 登记 | `docs/specs/` + tracker | 1h |
| **总计** | | | **32h** |

### 4.2 关键设计决策

| 决策 | 选项 | 选择 | 理由 |
|------|------|------|------|
| DNS 来源 | 硬编码 / 外部文件 / DB | 外部文件 `xray-config-template.json` + `singbox-config-template.json` | 已存在、版本管理、人工编辑复杂规则 |
| 变量存储 | config.json / DB / 代码 | config.json | 已有体系、用户可修改、版本管理 |
| UI 编辑范围 | 全部配置项 / 高频变量 / 无 | 高频变量（6-8 个） | 避免 UX 灾难，开发者改代码即可 |
| sing-box 支持 | 暂不支持 / 同步支持 | 同步支持 | 项目双后端，方案 F 天然支持 |
| 旧代码路径 | 删除 / 保留 / 配置开关 | 配置开关 | 观察一个发布周期后删除 |

### 4.3 测试策略

| 测试类型 | 目标 | 工具 |
|---------|------|------|
| 快照测试 | 3 路径生成 JSON 与原硬编码 byte-identical | GTest + fixture |
| Xray 兼容性 | xray 26.x 加载生成的配置 | `BuildPoolConfig_Xray26Loads` 范式 |
| 变量替换边界 | `${var}` 未定义 / 空字符串 / 特殊字符 | GTest |
| UI 编辑回归 | 修改 → 保存 → 重启 → 读取 | Catch2 UI 测试 |
| 迁移幂等性 | 空 DB → 创建 → 有数据 DB → 迁移 | GTest |
| 旧路径回归 | config flag 关闭时，byte-identical 到今天 | GTest |

---

## 5. 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| 新工厂与原配置不一致 | 快照测试 byte-identical 断言 |
| 变量替换错误 | 类型敏感替换 + 未定义变量抛异常 |
| UI 编辑误导用户 | 仅暴露 6-8 个高频变量，非全部配置项 |
| sing-box 遗漏 | 方案 F 天然支持，同步实施 |
| 迁移失败 | 配置开关保留旧路径，观察一个发布周期 |
| 性能影响 | 配置项数量少（<50），查询无性能问题 |

---

## 6. 验收标准

### 6.1 功能验收

- [ ] 3 路径（Test / Standalone / Pool）生成配置与原硬编码 byte-identical
- [ ] xray 26.x 加载生成的配置成功
- [ ] sing-box 配置生成成功
- [ ] 变量替换正确（类型敏感、未定义抛异常）
- [ ] UI 高级选项面板可编辑 6-8 个高频变量
- [ ] 配置保存后重启生效

### 6.2 代码验收

- [ ] 构建 0 error
- [ ] ctest 全绿
- [ ] 快照测试 3 路径 byte-identical
- [ ] Xray 26.x 加载测试通过
- [ ] UI 编辑回归测试通过

### 6.3 文档验收

- [ ] 规格文档 `docs/specs/2026-09-20-Spec-ConfigBuilder-v1.0.md`
- [ ] 本计划文档登记至 `docs/plans/project-plans-tracker.md`
- [ ] `docs/INDEX.md` 同步更新

---

## 7. 附录

### 7.1 评审引用来源

1. `docs/specs/2026-08-31-Spec-ConfigGenerationPathways-v1.0.md` — 已归档规范
2. `src/ConfigGenerator.cpp:103-346` — `buildPoolConfig` 现有实现
3. `src/XrayInstance.cpp:300-353` — `createConfigFile` 现有硬编码模板
4. `src/ui/AppController.cpp:899-939` — 已有"读外部模板 → 改写"路径
5. `bin/xray-config-template.json` (6649B) — 现有外部模板
6. `src/ui/ConfigDialog.cpp:79,86,104` — 已有模板路径 UI 属性

### 7.2 原方案否决详情

详见独立评审报告（评审代理全文实证）：
- EAV 反模式：Xray 配置结构固定，不需要动态 schema
- JSONPath 复杂度：需自造解析器 + 装配状态机 + 变量替换引擎
- UI 复杂度：400+ 行 config 暴露给终端用户 = UX 灾难
- 工时低估：原估 21h，实际 30-41h
- sing-box 遗漏：方案只覆盖 Xray，未提 sing-box

---

## 8. 状态

- **评审状态**：✅ 独立评审完成，原方案否决，推荐方案 F
- **实施状态**：📝 draft（待用户确认方案后实施）
- **下一步**：用户确认方案 → 生成规格文档 → 实施
