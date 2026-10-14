# 配置模板继承机制设计文档

**版本**: v1.0  
**日期**: 2026-09-21  
**作者**: AI Assistant  
**状态**: 草案

---

## 1. 背景与问题

### 1.1 当前问题

当前配置模板系统存在以下问题：

1. **模板独立无继承**：6 个模板（xray/singbox × default/test/pool）各自独立，没有基类模板
2. **配置重复定义**：log、dns、routing 等基础配置在多个模板中重复
3. **内容不一致**：
   - 测试模板缺少 DNS 配置
   - 代理池模板缺少 CN 路由规则
   - 独立代理模板缺少 API/Stats/Policy
4. **维护困难**：修改基础配置需要更新多个模板

### 1.2 当前模板内容对比

| 模板 | ID | 包含内容 | 缺少内容 |
|------|-----|----------|----------|
| **独立代理** | `xray_default_001` | log, dns(完整), routing(7规则) | API, Stats, Policy |
| **测试** | `xray_test_001` | log, api, stats, policy, routing(2规则) | dns, 完整 routing |
| **代理池** | `pool_xray_001` | log, dns(简单), api, stats, policy, routing(balancer) | 完整 routing(缺 CN 规则) |

### 1.3 设计目标

1. **支持模板继承**：子模板可以继承父模板的配置
2. **减少重复**：基础配置只定义一次
3. **保持一致**：所有模板共享基础配置，确保一致性
4. **易于维护**：修改基础配置只需更新一个地方
5. **向后兼容**：现有模板无需修改即可使用

---

## 2. 设计方案

### 2.1 数据库表结构变更

#### 当前结构

```sql
CREATE TABLE FullConfigTemplate (
    id TEXT PRIMARY KEY,
    remarks TEXT,
    enabled INTEGER,
    coreType INTEGER,
    config TEXT,           -- 完整 JSON 配置
    tunConfig TEXT,
    addProxyOnly INTEGER,
    proxyDetour TEXT,
    createdAt TEXT,
    updatedAt TEXT
);
```

#### 建议结构（新增字段）

```sql
CREATE TABLE FullConfigTemplate (
    id TEXT PRIMARY KEY,
    parent_id TEXT,        -- 父模板 ID（NULL 表示基础模板）
    remarks TEXT,
    enabled INTEGER,
    coreType INTEGER,
    config TEXT,           -- 增量 JSON 配置（只存差异）
    tunConfig TEXT,
    addProxyOnly INTEGER,
    proxyDetour TEXT,
    createdAt TEXT,
    updatedAt TEXT,
    
    FOREIGN KEY (parent_id) REFERENCES FullConfigTemplate(id)
);
```

### 2.2 模板层级设计

```
┌─────────────────────────────────────────────────────────────────┐
│                      模板继承体系                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  Level 0: 基础模板（parent_id = NULL）                            │
│  ┌─────────────────────┐      ┌─────────────────────┐           │
│  │  xray_base_001      │      │  singbox_base_001    │           │
│  │  基础配置            │      │  基础配置            │           │
│  │  - log              │      │  - log              │           │
│  │  - dns              │      │  - dns              │           │
│  └──────────┬──────────┘      └──────────┬──────────┘           │
│             │                             │                       │
│  Level 1: 功能模板（parent_id = xray_base_001/singbox_base_001）  │
│  ┌─────────────────────┐      ┌─────────────────────┐           │
│  │  xray_test_001      │      │  singbox_test_001    │           │
│  │  测试模板            │      │  测试模板            │           │
│  │  + api              │      │  + api              │           │
│  │  + stats            │      │  + stats            │           │
│  │  + policy           │      │  + policy           │           │
│  │  + routing(1规则)   │      │  + routing          │           │
│  └─────────────────────┘      └─────────────────────┘           │
│                                                                   │
│  ┌─────────────────────┐      ┌─────────────────────┐           │
│  │  pool_xray_001      │      │  pool_singbox_001    │           │
│  │  代理池模板          │      │  代理池模板          │           │
│  │  + api              │      │  (无 api)           │           │
│  │  + stats            │      │  + balancer         │           │
│  │  + policy           │      │  + observatory      │           │
│  │  + balancers        │      │  + routing(5规则)   │           │
│  │  + routing(5规则)   │      │                      │           │
│  └─────────────────────┘      └─────────────────────┘           │
│                                                                   │
│  Level 2: 独立代理模板（parent_id = xray_base_001）               │
│  ┌─────────────────────┐                                        │
│  │  xray_default_001   │                                        │
│  │  独立代理模板        │                                        │
│  │  + routing(7规则)   │                                        │
│  └─────────────────────┘                                        │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

### 2.3 模板内容定义

#### 2.3.1 基础模板（Level 0）

**xray_base_001**:
```json
{
  "log": {"loglevel": "warning"},
  "dns": {
    "hosts": {
      "dns.google": ["8.8.8.8", "8.8.4.4"],
      "cloudflare-dns.com": ["104.16.249.249", "104.16.248.249"],
      "dns.alidns.com": ["223.5.5.5", "223.6.6.6"]
    },
    "servers": [
      {"address": "https://cloudflare-dns.com/dns-query", "skipFallback": true},
      {"address": "https://dns.alidns.com/dns-query", "skipFallback": true},
      {"address": "223.5.5.5", "skipFallback": true}
    ]
  }
}
```

**singbox_base_001**:
```json
{
  "log": {"level": "warn", "timestamp": true},
  "dns": {
    "servers": [
      {"server": "223.5.5.5", "type": "udp", "tag": "local_dns"},
      {"server": "cloudflare-dns.com", "type": "https", "tag": "remote_dns"}
    ],
    "rules": [],
    "final": "remote_dns"
  }
}
```

#### 2.3.2 独立代理模板（Level 2）

**xray_default_001** (parent_id: xray_base_001):
```json
{
  "routing": {
    "domainStrategy": "AsIs",
    "rules": [
      {"type": "field", "port": "443", "network": "udp", "outboundTag": "block"},
      {"type": "field", "outboundTag": "proxy", "domain": ["geosite:google"]},
      {"type": "field", "outboundTag": "direct", "ip": ["geoip:private"]},
      {"type": "field", "outboundTag": "direct", "domain": ["geosite:private"]},
      {"type": "field", "outboundTag": "direct", "ip": ["geoip:cn"]},
      {"type": "field", "outboundTag": "direct", "domain": ["geosite:cn"]},
      {"type": "field", "inboundTag": ["socks"], "outboundTag": "proxy"}
    ]
  }
}
```

#### 2.3.3 测试模板（Level 1）

**xray_test_001** (parent_id: xray_base_001):
```json
{
  "api": {
    "tag": "api",
    "listen": "127.0.0.1:10085",
    "services": ["HandlerService", "LoggerService", "StatsService", "RoutingService"]
  },
  "stats": {},
  "policy": {
    "levels": {"0": {"statsUserUplink": true, "statsUserDownlink": true}},
    "system": {"statsInboundUplink": true, "statsInboundDownlink": true}
  },
  "routing": {
    "domainStrategy": "AsIs",
    "rules": [
      {"type": "field", "inboundTag": ["socks-in"], "outboundTag": "proxy"}
    ]
  }
}
```

#### 2.3.4 代理池模板（Level 1）

**pool_xray_001** (parent_id: xray_base_001):
```json
{
  "api": {
    "tag": "api",
    "services": ["HandlerService", "LoggerService", "StatsService", "RoutingService"]
  },
  "stats": {},
  "policy": {
    "levels": {"0": {"statsUserUplink": true, "statsUserDownlink": true}},
    "system": {"statsInboundUplink": true, "statsInboundDownlink": true}
  },
  "routing": {
    "domainStrategy": "AsIs",
    "balancers": [
      {
        "tag": "px-",
        "strategy": {"type": "leastPing", "fallbackProxy": "api"},
        "observation": {
          "probeUrl": "https://www.google.com/generate_204",
          "probeInterval": "1m"
        }
      }
    ],
    "rules": [
      {"type": "field", "inboundTag": ["api"], "outboundTag": "api"},
      {"type": "field", "network": "udp", "port": "443", "outboundTag": "block"},
      {"type": "field", "outboundTag": "direct", "ip": ["geoip:private"]},
      {"type": "field", "outboundTag": "direct", "domain": ["geosite:private"]},
      {"type": "field", "outboundTag": "direct", "ip": ["geoip:cn"]},
      {"type": "field", "outboundTag": "direct", "domain": ["geosite:cn"]},
      {"type": "field", "inboundTag": ["socks"], "balancerTag": "balancer-out"}
    ]
  }
}
```

---

## 3. 实现方案

### 3.1 配置合并算法

```cpp
// 伪代码
boost::json::value mergeConfig(
    const boost::json::value& parentConfig,
    const boost::json::value& childConfig) {
    
    // 深拷贝父配置
    boost::json::object result = parentConfig.as_object();
    
    // 遍历子配置的每个键
    for (auto& [key, value] : childConfig.as_object()) {
        if (result.contains(key)) {
            // 键已存在：递归合并对象，或覆盖其他类型
            if (value.is_object() && result[key].is_object()) {
                result[key] = mergeConfig(result[key], value);
            } else {
                result[key] = value;  // 覆盖
            }
        } else {
            // 键不存在：直接添加
            result[key] = value;
        }
    }
    
    return boost::json::value(result);
}
```

### 3.2 ConfigBuilder 修改

```cpp
// ConfigBuilder::loadTemplate() 修改
boost::json::value ConfigBuilder::loadTemplate(const std::string& templateId) const {
    // 1. 从数据库加载模板（包含 parent_id）
    db::FullConfigTemplateDAO dao(db_);
    auto templateItem = dao.getById(templateId);
    
    if (!templateItem.has_value()) {
        throw std::runtime_error("Template not found: " + templateId);
    }
    
    // 2. 解析子配置
    boost::json::value childConfig = boost::json::parse(templateItem->config);
    
    // 3. 如果有父模板，递归加载并合并
    if (!templateItem->parent_id.empty()) {
        boost::json::value parentConfig = loadTemplate(templateItem->parent_id);
        return mergeConfig(parentConfig, childConfig);
    }
    
    // 4. 无父模板，直接返回
    return childConfig;
}
```

### 3.3 数据库迁移脚本

```sql
-- 1. 添加 parent_id 字段
ALTER TABLE FullConfigTemplate ADD COLUMN parent_id TEXT;

-- 2. 添加外键约束
ALTER TABLE FullConfigTemplate 
ADD CONSTRAINT fk_template_parent 
FOREIGN KEY (parent_id) REFERENCES FullConfigTemplate(id);

-- 3. 创建基础模板
INSERT OR IGNORE INTO FullConfigTemplate (id, parent_id, remarks, enabled, coreType, config)
VALUES 
('xray_base_001', NULL, 'Xray 基础模板', 1, 2, 
 '{"log":{"loglevel":"warning"},"dns":{...}}'),
('singbox_base_001', NULL, 'SingBox 基础模板', 1, 24, 
 '{"log":{"level":"warn"},"dns":{...}}');

-- 4. 更新现有模板的 parent_id
UPDATE FullConfigTemplate SET parent_id = 'xray_base_001' 
WHERE id IN ('xray_default_001', 'xray_test_001', 'pool_xray_001');

UPDATE FullConfigTemplate SET parent_id = 'singbox_base_001' 
WHERE id IN ('singbox_default_001', 'singbox_test_001', 'pool_singbox_001');

-- 5. 更新现有模板的 config 为增量配置
UPDATE FullConfigTemplate SET config = '{...增量配置...}' 
WHERE id = 'xray_default_001';
```

---

## 4. 实施步骤

### 4.1 阶段一：数据库变更（1天）

1. 修改 `FullConfigTemplate` 表结构，添加 `parent_id` 字段
2. 编写迁移脚本，创建基础模板
3. 更新现有模板的 `parent_id` 和 `config`

### 4.2 阶段二：代码修改（2天）

1. 修改 `FullConfigTemplateDAO`：
   - 添加 `parent_id` 字段支持
   - 修改 `getById()` 返回包含 `parent_id` 的完整记录
2. 修改 `ConfigBuilder`：
   - 实现 `mergeConfig()` 函数
   - 修改 `loadTemplate()` 支持递归加载
3. 修改 `seedDefaultTemplates()`：
   - 创建基础模板
   - 更新现有模板为增量配置

### 4.3 阶段三：测试验证（1天）

1. 单元测试：`mergeConfig()` 函数
2. 集成测试：模板加载和合并
3. 回归测试：三种配置生成（test/standalone/pool）
4. 端到端测试：代理启动验证

### 4.4 阶段四：文档更新（0.5天）

1. 更新 `docs/INDEX.md`
2. 更新 API 文档
3. 更新模板管理文档

---

## 5. 风险评估

### 5.1 风险点

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 合并算法错误 | 配置错误 | 中 | 充分的单元测试 |
| 循环继承 | 无限递归 | 低 | 添加深度限制检查 |
| 向后兼容 | 旧模板失效 | 中 | 支持无 parent_id 的模板 |
| 性能下降 | 加载变慢 | 低 | 缓存合并结果 |

### 5.2 回滚方案

1. 数据库迁移前备份
2. 保留旧模板数据
3. 代码可回滚到无继承版本

---

## 6. 收益分析

### 6.1 维护收益

- **减少 60% 重复配置**：log、dns 只定义一次
- **一致性保证**：所有模板共享基础配置
- **快速更新**：修改基础配置只需更新一个模板

### 6.2 功能收益

- **支持更复杂的模板层级**：可扩展到 3-4 层
- **易于添加新模板类型**：只需继承基础模板
- **配置验证**：可以验证继承链的完整性

### 6.3 技术收益

- **代码复用**：减少重复代码
- **设计模式**：应用模板方法模式
- **可扩展性**：易于添加新的配置类型

---

## 7. 总结

本设计文档提出了配置模板继承机制，通过以下改进：

1. **数据库层面**：添加 `parent_id` 字段，支持模板层级
2. **代码层面**：实现递归加载和深合并算法
3. **配置层面**：基础配置只定义一次，子模板只存增量

**预计工作量**：4.5 天  
**风险等级**：中低  
**收益**：高（维护性、一致性、可扩展性）

---

## 附录

### A. 相关文件

- `include/config/ConfigBuilder.h`
- `src/config/ConfigBuilder.cpp`
- `include/FullConfigTemplateDAO.h`
- `src/FullConfigTemplateDAO.cpp`
- `docs/INDEX.md`

### B. 参考文档

- Xray 配置文档: https://xtls.github.io/config/
- Sing-Box 配置文档: https://sing-box.sagernet.org/config/

### C. 变更记录

| 版本 | 日期 | 变更内容 |
|------|------|----------|
| v1.0 | 2026-09-21 | 初始版本 |
