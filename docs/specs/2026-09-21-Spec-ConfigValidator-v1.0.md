# 配置校验与错误处理增强方案

**版本**: v1.0  
**日期**: 2026-09-21  
**模块**: ConfigBuilder / XrayInstance / ConfigGenerator  
**状态**: ✅ 已完成（2026-09-21）

> **⚠️ 后续变更（2026-09-21）**: 本规格中提到的 `use_new_config_builder` 配置开关已删除。ConfigBuilder 已成为唯一配置生成路径，`isEnableNewConfigBuilder()` 始终返回 `true`。相关代码和文档已同步更新。

---

## 0. 实施完成摘要

### 0.1 已完成步骤

| 步骤 | 描述 | 状态 |
|------|------|------|
| 2.1 | 增强错误日志（isEnableNewConfigBuilder 添加 WARN/ERR 日志） | ✅ 完成 |
| 2.2 | 模板缺失时抛出异常（loadTemplate 改用 getById + 异常传播） | ✅ 完成 |
| 2.3 | 添加配置完整性校验（validateConfig 方法） | ✅ 完成 |
| 2.4 | 修改 build 方法调用校验（3 个 buildXxxConfig 删除硬编码回退） | ✅ 完成 |
| 2.5 | 删除硬编码回退（XrayInstance 362-389 行 + ConfigGenerator::buildPoolConfig） | ✅ 完成 |

### 0.2 额外改进

| 改进项 | 描述 |
|--------|------|
| 种子数据补充 | seedDefaultTemplates 新增 pool_xray_001 + pool_singbox_001（共 6 条模板） |
| loadTemplate 精确查找 | 改用 getById() 方法，templateKey 映射到模板 ID（修复 CoreType 冲突） |
| buildPoolConfig 动态字段 | 加载模板后动态添加 api.listen 字段 |
| XrayInstance 无 DB 回退 | 无数据库连接时生成最小化配置（minimal config） |
| 测试夹具创建 | InMemoryDBWithTemplates 夹具（17 个 ConfigBuilder 测试） |
| 测试文件更新 | 删除 5 个 buildPoolConfig 测试 + 重写 1 个回退测试 + 新增 4 个 validateConfig 测试 |

### 0.3 验证结果

- **构建**: `cmake --build build --parallel 8` → 0 error
- **ConfigGeneratorTest**: 47 测试（46 passed + 1 skipped）
- **全量 ctest**: 48/48 测试通过（32.04s）
- **CMakeLists.txt**: 6 个测试目标链接修复

### 0.4 相关文件变更

**源文件**:
- `src/config/ConfigBuilder.cpp` - isEnableNewConfigBuilder 日志 + loadTemplate getById + 3 个 buildXxxConfig 删除回退 + buildPoolConfig api.listen + validateConfig 方法
- `include/config/ConfigBuilder.h` - 添加 validateConfig() 声明
- `src/XrayInstance.cpp` - 删除硬编码回退（362-389 行）+ 无 DB 时生成最小化配置
- `src/ConfigGenerator.cpp` - 删除 buildPoolConfig() 方法（116-362 行）+ 匿名命名空间
- `include/ConfigGenerator.h` - 删除 buildPoolConfig() 声明
- `src/StandaloneProxyPool.cpp` - 移除 ConfigGenerator fallback + try-catch 处理

**测试文件**:
- `tests/test_config_builder.cpp` - 新增 InMemoryDBWithTemplates 夹具 + 4 个 validateConfig 测试
- `tests/test_config_generator.cpp` - 删除 3 个 BuildPoolConfig 测试
- `tests/TestStandaloneProxyPool.cpp` - 删除 2 个 BuildPoolConfig 测试
- `tests/test_xray_instance_stderr.cpp` - 更新 ConfigTemplateOmitsObservatoryService 测试使用更灵活的模式匹配

**文档**:
- `docs/specs/2026-09-21-Spec-ConfigValidator-v1.0.md` - 本文件（规格文档）
- `docs/INDEX.md` - 已更新（specs 第 49 项）
- `CMakeLists.txt` - 6 个测试目标链接修复

---

## 1. 背景与问题

### 1.1 当前状态

ConfigBuilder 已集成到所有配置生成路径，但存在以下问题：

| 场景 | 当前行为 | 问题 |
|------|---------|------|
| config.json 缺失 | 静默返回 `false` | 用户不知道新配置生成器未启用 |
| DB 模板缺失 | 返回空对象 `{}` | DEBUG 级别日志，生产环境不可见 |
| 模板 JSON 无效 | 返回空对象 `{}` + ERR 日志 | 调用者无法区分失败原因 |
| 配置不完整 | 无校验 | 可能生成无效配置，Xray 启动失败 |

### 1.2 硬编码回退路径

当前存在 3 处硬编码回退，已迁移到 ConfigBuilder 但未删除：

1. **XrayInstance.cpp:362-389** - Xray 实例配置回退
2. **ConfigGenerator.cpp:116-364** - 池配置回退
3. **ConfigGenerator.cpp:42-69** - 单代理配置旧路径（feature flag 回退）

### 1.3 风险评估

| 风险 | 严重性 | 场景 |
|------|--------|------|
| 功能未启用但用户不知情 | 🔴 高 | config.json 缺失时静默回退 |
| 配置不完整导致运行时错误 | 🔴 高 | 模板缺失时生成最小化配置 |
| 难以排查配置问题 | 🟡 中 | DEBUG 级别日志在生产不可见 |
| 安全网变成隐患 | 🟡 中 | 硬编码回退已过时 |

---

## 2. 改进方案

### 2.1 增强错误日志

**文件**: `src/config/ConfigBuilder.cpp`  
**方法**: `isEnableNewConfigBuilder()`

```cpp
// 当前实现（问题：静默失败）
bool ConfigBuilder::isEnableNewConfigBuilder(const std::string& configPath) {
    std::string path = configPath.empty() ? ConfigReader::getDefaultConfigPath() : configPath;
    std::filesystem::path filePath(path);
    if (!std::filesystem::exists(filePath)) {
        return false;  // ← 无日志
    }
    std::optional<AppConfig> config = ConfigReader::load(path);
    if (!config) {
        return false;  // ← 无日志
    }
    return config->proxy.use_new_config_builder;
}

// 改进后（添加日志）
bool ConfigBuilder::isEnableNewConfigBuilder(const std::string& configPath) {
    std::string path = configPath.empty() ? ConfigReader::getDefaultConfigPath() : configPath;
    
    std::filesystem::path filePath(path);
    if (!std::filesystem::exists(filePath)) {
        Logger::write("[ConfigBuilder] Config file not found: " + path + 
                      " - new config builder will be DISABLED", LogLevel::WARN);
        return false;
    }
    
    std::optional<AppConfig> config = ConfigReader::load(path);
    if (!config) {
        Logger::write("[ConfigBuilder] Failed to load config: " + path + 
                      " - new config builder will be DISABLED", LogLevel::ERR);
        return false;
    }
    
    return config->proxy.use_new_config_builder;
}
```

### 2.2 模板缺失时抛出异常

**文件**: `src/config/ConfigBuilder.cpp`  
**方法**: `loadTemplate()`

```cpp
// 当前实现（问题：返回空对象，调用者无法区分失败原因）
boost::json::value ConfigBuilder::loadTemplate(const std::string& templateKey) const {
    // ... 解析 templateKey ...
    auto templateItem = dao.getByCoreType(coreType);
    
    if (!templateItem.has_value()) {
        return boost::json::object{};  // ← 静默失败
    }
    // ...
}

// 改进后（抛出异常）
boost::json::value ConfigBuilder::loadTemplate(const std::string& templateKey) const {
    // ... 解析 templateKey ...
    auto templateItem = dao.getByCoreType(coreType);
    
    if (!templateItem.has_value()) {
        throw std::runtime_error(
            "[ConfigBuilder] Template not found for key: " + templateKey +
            " (coreType=" + std::to_string(coreType) + "). " +
            "Please ensure the template is seeded in the database.");
    }
    
    if (templateItem->config.empty()) {
        throw std::runtime_error(
            "[ConfigBuilder] Template config is empty for key: " + templateKey +
            " (id=" + templateItem->id + ").");
    }
    
    try {
        return boost::json::parse(templateItem->config);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "[ConfigBuilder] Failed to parse template JSON for " + templateKey +
            ": " + std::string(e.what()));
    }
}
```

### 2.3 添加配置完整性校验

**文件**: `include/config/ConfigBuilder.h` + `src/config/ConfigBuilder.cpp`  
**新增方法**: `validateConfig()`

```cpp
// 头文件声明
bool validateConfig(const boost::json::value& config, ConfigMode mode) const;

// 实现
bool ConfigBuilder::validateConfig(const boost::json::value& config, ConfigMode mode) const {
    if (!config.is_object()) {
        Logger::write("[ConfigBuilder] Config is not an object", LogLevel::ERR);
        return false;
    }
    
    const auto& obj = config.as_object();
    
    // 所有模式都需要的基本部分
    if (!obj.contains("log")) {
        Logger::write("[ConfigBuilder] Missing required section: log", LogLevel::ERR);
        return false;
    }
    
    if (!obj.contains("inbounds")) {
        Logger::write("[ConfigBuilder] Missing required section: inbounds", LogLevel::ERR);
        return false;
    }
    
    if (!obj.contains("outbounds")) {
        Logger::write("[ConfigBuilder] Missing required section: outbounds", LogLevel::ERR);
        return false;
    }
    
    if (!obj.contains("routing")) {
        Logger::write("[ConfigBuilder] Missing required section: routing", LogLevel::ERR);
        return false;
    }
    
    // 模式特定校验
    if (mode == ConfigMode::Pool && !obj.contains("api")) {
        Logger::write("[ConfigBuilder] Pool config missing API section", LogLevel::ERR);
        return false;
    }
    
    if (mode == ConfigMode::Test && proxyType_ == ProxyType::Xray && !obj.contains("api")) {
        Logger::write("[ConfigBuilder] Test config missing API section (required for gRPC)", LogLevel::ERR);
        return false;
    }
    
    return true;
}
```

### 2.4 修改 build 方法调用校验

**文件**: `src/config/ConfigBuilder.cpp`  
**方法**: `buildTestConfig()`, `buildStandaloneConfig()`, `buildPoolConfig()`

```cpp
// buildTestConfig() 改进
boost::json::value ConfigBuilder::buildTestConfig(...) {
    // ... 现有代码 ...
    
    // 返回前校验
    if (!validateConfig(root, ConfigMode::Test)) {
        Logger::write("[ConfigBuilder] Test config validation FAILED", LogLevel::ERR);
        return boost::json::object{};
    }
    
    return boost::json::value(root);
}
```

### 2.5 删除硬编码回退

**文件**: `src/XrayInstance.cpp`  
**位置**: 362-389 行

```cpp
// 当前实现（硬编码回退）
bool XrayInstance::createConfigFile() {
    // ... ConfigBuilder 路径 ...
    
    if (!configJson.is_object() || configJson.as_object().empty()) {
        Logger::write("[XrayInstance] ConfigBuilder failed, falling back to hardcoded", LogLevel::WARN);
        // 使用硬编码配置...
    }
    
    // 硬编码配置（删除此部分）
    std::string content = R"({
        "log": {"loglevel": "warning"},
        "api": {...},
        "inbounds": [...],
        "outbounds": [...],
        "routing": {...}
    })";
}

// 改进后（失败时返回错误）
bool XrayInstance::createConfigFile() {
    // ... ConfigBuilder 路径 ...
    
    if (!configJson.is_object() || configJson.as_object().empty()) {
        Logger::write("[XrayInstance] ConfigBuilder failed - no fallback available", LogLevel::ERR);
        return false;  // 直接返回错误，不生成硬编码配置
    }
    
    // 写入配置...
    return true;
}
```

**文件**: `src/ConfigGenerator.cpp`  
**位置**: 116-364 行（`buildPoolConfig()`）

```cpp
// 当前实现（硬编码回退）
std::string ConfigGenerator::buildPoolConfig(...) {
    // 硬编码池配置...
    return boost::json::serialize(root);
}

// 改进后（删除此方法，由 ConfigBuilder 处理）
// StandaloneProxyPool 已完全迁移到 ConfigBuilder
```

---

## 3. 测试计划

### 3.1 新增单元测试

**文件**: `tests/test_config_builder.cpp`

```cpp
TEST_F(InMemoryDB, ConfigBuilderIsEnableNewConfigBuilder_MissingFile) {
    // config.json 不存在时返回 false，并记录日志
    bool enabled = ConfigBuilder::isEnableNewConfigBuilder("nonexistent.json");
    EXPECT_FALSE(enabled);
}

TEST_F(InMemoryDB, ConfigBuilderLoadTemplate_TemplateNotFound_ThrowsException) {
    // 模板不存在时抛出异常
    ConfigBuilder builder(db_);
    EXPECT_THROW(
        builder.build(ConfigMode::Test, "unknown_backend", dummyProfile, vars),
        std::runtime_error
    );
}

TEST_F(InMemoryDB, ConfigBuilderValidateConfig_MissingRequiredSections) {
    // 缺少必要部分时校验失败
    ConfigBuilder builder(db_);
    boost::json::object incompleteConfig;
    incompleteConfig["log"] = boost::json::object{};  // 缺少 inbounds, outbounds, routing
    EXPECT_FALSE(builder.validateConfig(incompleteConfig, ConfigMode::Test));
}

TEST_F(InMemoryDB, ConfigBuilderValidateConfig_PoolMissingAPI) {
    // 池配置缺少 API 时校验失败
    ConfigBuilder builder(db_);
    boost::json::object poolConfig;
    poolConfig["log"] = boost::json::object{};
    poolConfig["inbounds"] = boost::json::array{};
    poolConfig["outbounds"] = boost::json::array{};
    poolConfig["routing"] = boost::json::object{};
    // 缺少 api
    EXPECT_FALSE(builder.validateConfig(poolConfig, ConfigMode::Pool));
}
```

### 3.2 回归测试

```bash
# 运行全量测试
ctest --test-dir build --parallel 4

# 运行配置生成测试
ctest --test-dir build -R ConfigGeneratorTest -V

# 运行代理池测试
ctest --test-dir build -R StandaloneProxyPoolTest -V

# 运行批量测试组件测试
ctest --test-dir build -R ProxyBatchComponentsTest -V
```

---

## 4. 实施步骤

| 步骤 | 任务 | 预计耗时 | 验证方式 |
|------|------|---------|---------|
| 1 | 增强 `isEnableNewConfigBuilder()` 日志 | 15 min | 运行测试，检查日志 |
| 2 | 修改 `loadTemplate()` 抛出异常 | 30 min | 运行测试，验证异常 |
| 3 | 新增 `validateConfig()` 方法 | 45 min | 运行新测试 |
| 4 | 修改 `buildXxxConfig()` 调用校验 | 30 min | 运行测试 |
| 5 | 删除 `XrayInstance.cpp` 硬编码回退 | 15 min | 运行测试 |
| 6 | 删除 `ConfigGenerator::buildPoolConfig()` | 30 min | 运行测试 |
| 7 | 更新单元测试 | 45 min | 运行全量测试 |
| 8 | 更新文档索引 | 15 min | 检查 INDEX.md |

**总耗时**: 约 4 小时

---

## 5. 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| 删除回退后配置生成失败 | 先添加校验和日志，确认所有模板存在 |
| 异常抛出影响现有调用者 | 检查所有调用者，确保已处理异常 |
| 模板数据未迁移 | 运行 `seedDefaultTemplates()` 确保 6 条模板存在 |

---

## 6. 参考文件

- `src/config/ConfigBuilder.cpp` - 主实现文件
- `include/config/ConfigBuilder.h` - 头文件
- `src/XrayInstance.cpp` - Xray 实例配置
- `src/ConfigGenerator.cpp` - 配置生成器
- `src/StandaloneProxyPool.cpp` - 代理池
- `tests/test_config_builder.cpp` - 单元测试
- `bin/config.json` - 配置文件
- `bin/worker/guiNDB.db` - 数据库

---

## 7. 变更记录

| 版本 | 日期 | 变更内容 |
|------|------|---------|
| v1.0 | 2026-09-21 | 初始版本，记录问题和改进方案 |
