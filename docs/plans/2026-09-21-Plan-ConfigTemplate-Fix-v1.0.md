# 配置模板问题修复计划

**版本**: v1.1  
**日期**: 2026-09-21  
**作者**: AI Assistant  
**状态**: 评审修订  
**关联设计文档**: `docs/design/2026-09-21-Design-ConfigTemplate-Inheritance-v1.0.md`

**v1.1 评审修订**: 根据评审意见，将流量监听端口和 API 监听端口纳入模板占位符机制，使用统一工具函数替换

---

## 1. 问题总结

### 1.1 核心问题

经过代码审查，发现配置模板系统存在以下问题：

| # | 问题 | 严重性 | 影响范围 | 当前状态 |
|---|------|--------|----------|----------|
| 1 | 模板间无继承关系，配置重复定义 | 高 | 全部 6 个模板 | 未修复 |
| 2 | Test 模式 API 缺少 listen 地址 | 高 | Test 配置 | 未修复 |
| 3 | routing 规则被代码覆盖，模板配置失效 | 中 | 全部 3 种模式 | 未修复 |
| 4 | Test 模式协议与 Standalone/Pool 不一致 | 低 | Test 配置 | 未修复 |
| 5 | 代理池模板缺少 CN 路由规则 | 中 | Pool 配置 | 已修复 |
| 6 | 代理池模板 DNS 配置过于简化 | 低 | Pool 配置 | 已修复 |
| **7** | **流量/API 监听端口未在模板中定义（代码硬编码）** | **高** | **全部模式** | **未修复（评审新增）** |

### 1.2 问题详情

#### 问题 1: 模板间无继承关系

**现状**:
```
xray_default_001 (独立代理)  - 无继承
xray_test_001 (测试)         - 无继承
pool_xray_001 (代理池)       - 无继承
singbox_default_001 (独立)   - 无继承
singbox_test_001 (测试)      - 无继承
pool_singbox_001 (代理池)    - 无继承
```

**影响**:
- log、dns 配置在多个模板中重复
- 修改基础配置需更新多个模板
- 配置不一致（如 DNS 配置差异）

#### 问题 2: Test 模式 API 缺少 listen 地址

**模板** (`xray_test_001`):
```json
{
  "api": {
    "tag": "api",
    "services": [...]
    // ❌ 缺少 "listen" 字段
  }
}
```

**代码** (`buildTestConfig`):
```cpp
// ❌ 没有添加 API listen 地址
// 应该添加：
// root["api"].as_object()["listen"] = "127.0.0.1:" + std::to_string(vars.apiPort);
```

**对比 Pool 模式** (`buildPoolConfig:571`):
```cpp
// ✅ Pool 模式正确添加了 API listen 地址
root["api"].as_object()["listen"] = std::string("127.0.0.1:") + std::to_string(vars.apiPort);
```

**影响**: Xray API 可能无法正常工作，gRPC 注入失败

#### 问题 3: routing 规则被代码覆盖

**模板** (`xray_test_001`):
```json
{
  "routing": {
    "rules": [
      {"inboundTag": ["api"], "outboundTag": "api"},
      {"outboundTag": "proxy", "network": "tcp"}
    ]
  }
}
```

**代码** (`buildTestConfig:388-400`):
```cpp
// ❌ 完全覆盖模板中的 routing
routing["rules"] = rules;  // 只有 1 条规则：socks-in -> proxy
root["routing"] = routing;
```

**影响**: 模板中的路由规则失效

#### 问题 4: 协议不一致

| 模式 | 协议 | Tag | Routing |
|------|------|-----|---------|
| **Test** | `socks` | `socks-in` | `inboundTag: ["socks-in"]` |
| **Standalone** | `mixed` | `socks` | `inboundTag: ["socks"]` |
| **Pool** | `mixed` | `socks` | `inboundTag: ["socks"]` |

**影响**: Test 模式功能受限（无 sniffing 支持）

#### 问题 5 & 6: 已修复

- ✅ Pool 模板添加了 CN 路由规则
- ✅ Pool 模板添加了 DNS 配置

#### 问题 7: 流量/API 监听端口未在模板中定义

**现状**:
```
模板中:
- inbounds[0].listen = "127.0.0.1"  (无端口，代码动态添加)
- api.listen = 不存在 (代码动态添加)

代码中:
- buildTestConfig:  手动设置 inbounds[0].listen = "127.0.0.1:SOCKS_PORT"
- buildPoolConfig:  手动设置 inbounds[0].listen + api.listen
- buildStandaloneConfig: 手动设置 inbounds[0].listen
```

**影响**:
- 端口配置分散在代码中，难以统一维护
- 模板无法完整描述配置结构
- 修改端口分配逻辑需修改多处代码

**评审要求**:
- 将流量监听端口配置纳入基础模板，监听端口用占位符
- 将 API 监听端口纳入除独立代理外模板（Test/Pool）
- 用统一工具函数修改占位符

---

## 2. 修复方案

### 2.1 短期修复（1-2 天）

#### 修复 A: 统一占位符替换工具函数

**文件**: `include/config/ConfigBuilder.h` + `src/config/ConfigBuilder.cpp`  
**位置**: 新增静态方法 `replacePlaceholders()`

**修改内容**:
```cpp
// 在 ConfigBuilder 类中添加静态方法
static void replacePlaceholders(boost::json::value& config, const ConfigVariables& vars);

// 实现：
void ConfigBuilder::replacePlaceholders(boost::json::value& config, const ConfigVariables& vars) {
    // 递归遍历 JSON 树，替换字符串中的占位符
    // ${SOCKS_PORT} -> std::to_string(vars.socksPort)
    // ${API_PORT} -> std::to_string(vars.apiPort)
}
```

**预计工作量**: 2 小时

#### 修复 B: 模板中添加端口占位符

**文件**: `src/FullConfigTemplateDAO.cpp`  
**位置**: `seedDefaultTemplates()` 方法

**修改内容**:
```cpp
// 所有模板的 inbounds[0].listen 添加端口占位符
// 修改前: "listen": "127.0.0.1"
// 修改后: "listen": "127.0.0.1:${SOCKS_PORT}"

// Test/Pool 模板的 api.listen 添加占位符
// Test 模板: "listen": "127.0.0.1:${API_PORT}"
// Pool 模板: "listen": "127.0.0.1:${API_PORT}"
// Standalone 模板: 无 api.listen（不需要）
```

**预计工作量**: 1 小时

#### 修复 C: buildXxxConfig 使用统一工具函数

**文件**: `src/config/ConfigBuilder.cpp`  
**位置**: `buildTestConfig()`, `buildStandaloneConfig()`, `buildPoolConfig()`

**修改内容**:
```cpp
// 删除各方法中手动设置 listen 地址的代码
// 统一在最后调用 replacePlaceholders()

// 示例 (buildTestConfig):
// 删除: socksIn["listen"] = "127.0.0.1:" + std::to_string(vars.socksPort);
// 删除: root["api"].as_object()["listen"] = ...
// 添加: replacePlaceholders(root, vars);
```

**预计工作量**: 1 小时

#### 修复 D: 统一 Test 模式协议为 mixed

**文件**: `src/config/ConfigBuilder.cpp`  
**位置**: `buildTestConfig()` 方法，line 350-362

**修改内容**:
```cpp
// 修改前
socksIn["tag"] = "socks-in";
socksIn["protocol"] = "socks";

// 修改后
socksIn["tag"] = "socks";
socksIn["protocol"] = "mixed";
// 添加 sniffing 配置
boost::json::object sniffing;
sniffing["enabled"] = true;
boost::json::array destOverride;
destOverride.push_back(boost::json::value("http"));
destOverride.push_back(boost::json::value("tls"));
destOverride.push_back(boost::json::value("quic"));
sniffing["destOverride"] = destOverride;
sniffing["routeOnly"] = false;
socksIn["sniffing"] = sniffing;
```

**同步修改 routing 规则**:
```cpp
// 修改前
inTag.push_back(boost::json::value("socks-in"));

// 修改后
inTag.push_back(boost::json::value("socks"));
```

**预计工作量**: 1 小时

#### 修复 E: 保留模板 routing 规则（合并而非覆盖）

**文件**: `src/config/ConfigBuilder.cpp`  
**位置**: `buildTestConfig()` 方法，line 388-400

**修改内容**:
```cpp
// 修改前：覆盖 routing
root["routing"] = routing;

// 修改后：合并 routing 规则
boost::json::object rootRouting = root["routing"].as_object();
boost::json::array existingRules = rootRouting["rules"].as_array();
// 添加新规则
existingRules.push_back(rule);
rootRouting["rules"] = existingRules;
root["routing"] = rootRouting;
```

**预计工作量**: 2 小时

### 2.2 长期修复（4-5 天）

#### 修复 D: 实现模板继承机制

**设计文档**: `docs/design/2026-09-21-Design-ConfigTemplate-Inheritance-v1.0.md`

**实施步骤**:

1. **数据库变更** (1 天)
   - 添加 `parent_id` 字段到 `FullConfigTemplate` 表
   - 创建基础模板 (`xray_base_001`, `singbox_base_001`)
   - 更新现有模板的 `parent_id` 和增量配置

2. **代码修改** (2 天)
   - 修改 `FullConfigTemplateDAO` 支持 `parent_id`
   - 实现 `mergeConfig()` 深合并算法
   - 修改 `ConfigBuilder::loadTemplate()` 支持递归加载

3. **测试验证** (1 天)
   - 单元测试：`mergeConfig()` 函数
   - 集成测试：模板加载和合并
   - 回归测试：三种配置生成

4. **文档更新** (0.5 天)
   - 更新 `docs/INDEX.md`
   - 更新 API 文档
   - 更新模板管理文档

**预计工作量**: 4.5 天

---

## 3. 实施计划

### 3.1 阶段一：占位符机制（1 天）

**目标**: 实现统一的端口占位符替换机制

**任务**:
1. [ ] 修复 A: 新增 `replacePlaceholders()` 静态方法
2. [ ] 修复 B: 在模板中添加 `${SOCKS_PORT}` 和 `${API_PORT}` 占位符
3. [ ] 修复 C: `buildXxxConfig()` 删除手动设置端口代码，调用 `replacePlaceholders()`
4. [ ] 验证端口替换正确

**验收标准**:
- [ ] 所有模板包含 `${SOCKS_PORT}` 占位符
- [ ] Test/Pool 模板包含 `${API_PORT}` 占位符
- [ ] Standalone 模板无 API 占位符
- [ ] 生成的配置中端口被正确替换
- [ ] 所有测试通过

### 3.2 阶段二：协议统一（0.5 天）

**目标**: 统一三种模式的 SOCKS 协议

**任务**:
1. [ ] 修复 D: Test 模式协议改为 mixed
2. [ ] 同步修改 routing 规则中的 inboundTag
3. [ ] 验证协议一致性

**验收标准**:
- [ ] Test/Standalone/Pool 三种模式使用相同的协议和 tag
- [ ] routing 规则与 inbounds tag 匹配
- [ ] 所有测试通过

### 3.3 阶段三：routing 合并（1 天）

**目标**: 保留模板中的 routing 规则

**任务**:
1. [ ] 修复 E: 实现 routing 规则合并
2. [ ] 测试合并逻辑
3. [ ] 验证模板规则生效

**验收标准**:
- [ ] 模板中的 routing 规则被保留
- [ ] 代码添加的规则被追加
- [ ] 所有测试通过

### 3.4 阶段四：模板继承（4 天）

**目标**: 实现完整的模板继承机制

**任务**:
1. [ ] 数据库变更：添加 `parent_id` 字段
2. [ ] 创建基础模板
3. [ ] 实现合并算法
4. [ ] 修改 `loadTemplate()`
5. [ ] 更新现有模板为增量配置
6. [ ] 完整测试验证

**验收标准**:
- [ ] 数据库支持模板继承
- [ ] 代码支持递归加载
- [ ] 配置合并正确
- [ ] 所有测试通过
- [ ] 文档已更新

---

## 4. 风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| API 端口冲突 | 低 | 高 | PortManager 已处理 |
| routing 合并错误 | 中 | 中 | 充分的单元测试 |
| 模板继承循环 | 低 | 高 | 添加深度限制检查 |
| 向后兼容问题 | 中 | 中 | 支持无 parent_id 的模板 |

---

## 5. 测试计划

### 5.1 单元测试

1. **ConfigBuilderTest**
   - [ ] TestPlaceholderReplacement - 验证 `${SOCKS_PORT}` 和 `${API_PORT}` 被正确替换
   - [ ] TestTemplateHasPortPlaceholders - 验证模板包含端口占位符
   - [ ] TestConfigUsesMixedProtocol - 验证三种模式协议一致
   - [ ] TestConfigRoutingMerged - 验证 routing 规则合并

2. **ConfigTemplateTest**
   - [ ] MergeConfigDeepMerge
   - [ ] MergeConfigOverride
   - [ ] MergeConfigArrayMerge

### 5.2 集成测试

1. **TestConfigGeneration**
   - [ ] 生成 Test 配置并验证 API listen
   - [ ] 生成 Standalone 配置并验证 routing
   - [ ] 生成 Pool 配置并验证 balancer

2. **XrayInstanceTest**
   - [ ] 使用 Test 配置启动 Xray
   - [ ] 验证 API 端口可访问
   - [ ] 验证 gRPC 注入成功

### 5.3 回归测试

1. **全量测试**
   - [ ] `ctest --test-dir build --parallel 4`
   - [ ] 48/48 测试通过

---

## 6. 回滚方案

### 6.1 代码回滚

```bash
git revert <commit-hash>
```

### 6.2 数据库回滚

```sql
-- 删除 parent_id 字段
ALTER TABLE FullConfigTemplate DROP COLUMN parent_id;

-- 恢复模板配置
-- 从备份恢复或手动修改
```

### 6.3 配置回滚

```json
// config.json 无需修改（API 端口配置保持不变）
```

---

## 7. 验收标准

### 7.1 功能验收

- [ ] 所有模板包含 `${SOCKS_PORT}` 占位符
- [ ] Test/Pool 模板包含 `${API_PORT}` 占位符
- [ ] Standalone 模板无 API 占位符
- [ ] 统一工具函数正确替换端口占位符
- [ ] 三种模式协议一致（mixed）
- [ ] routing 规则正确合并
- [ ] 模板继承正常工作

### 7.2 性能验收

- [ ] 配置生成时间无显著增加（< 100ms）
- [ ] 模板加载时间可接受（< 50ms）

### 7.3 文档验收

- [ ] 设计文档已更新
- [ ] API 文档已更新
- [ ] 模板管理文档已更新

---

## 8. 附录

### 8.1 相关文件

| 文件 | 说明 |
|------|------|
| `include/config/ConfigBuilder.h` | ConfigBuilder 头文件 |
| `src/config/ConfigBuilder.cpp` | ConfigBuilder 实现 |
| `include/FullConfigTemplateDAO.h` | 模板 DAO 头文件 |
| `src/FullConfigTemplateDAO.cpp` | 模板 DAO 实现 |
| `docs/design/2026-09-21-Design-ConfigTemplate-Inheritance-v1.0.md` | 设计文档 |
| `docs/INDEX.md` | 文档索引 |

### 8.2 参考文档

- Xray 配置文档: https://xtls.github.io/config/
- Sing-Box 配置文档: https://sing-box.sagernet.org/config/

### 8.3 变更记录

| 版本 | 日期 | 变更内容 | 作者 |
|------|------|----------|------|
| v1.0 | 2026-09-21 | 初始版本 | AI Assistant |
| v1.1 | 2026-09-21 | 评审修订：①新增问题 7（端口未纳入模板）②修复 A 改为统一占位符替换工具函数 ③修复 B 改为模板添加占位符 ④修复 C 改为代码使用工具函数 ⑤更新实施计划、测试计划、验收标准 | AI Assistant |

---

## 9. 评审清单

### 9.1 设计评审

- [ ] 问题定义清晰
- [ ] 解决方案合理
- [ ] 风险评估充分
- [ ] 实施计划可行

### 9.2 技术评审

- [ ] 代码修改正确
- [ ] 测试覆盖充分
- [ ] 向后兼容
- [ ] 性能影响可接受

### 9.3 文档评审

- [ ] 设计文档完整
- [ ] 实施计划详细
- [ ] 测试计划明确
- [ ] 回滚方案可行

---

## 10. 下一步

1. **再次评审此计划文档** - 确认占位符机制方案是否合理
2. **确认修复优先级**（阶段一占位符机制 vs 阶段四模板继承）
3. **分配实施资源**
4. **开始阶段一实施** - 实现 `replacePlaceholders()` 统一工具函数
