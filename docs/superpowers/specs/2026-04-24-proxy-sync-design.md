# 代理同步功能设计文档

**日期**: 2026-04-24  
**状态**: 待实现  
**作者**: opencode

## 1. 概述

### 1.1 功能描述
通过命令行参数或配置文件，将有效代理从一个 SQLite 数据库同步（迁移）到另一个数据库。

### 1.2 背景
当前 `validproxy` 工具支持测试代理、导出分享链接等功能，但缺少将有效代理在不同数据库之间迁移的能力。这在以下场景中很有用：
- 将测试通过的代理迁移到生产环境数据库
- 合并多个数据库的代理
- 备份有效代理到独立数据库

### 1.3 范围
本功能仅处理：
- 静态筛选（`delay > 0`）的有效代理
- `profile_items`、`profile_exitems`、`subitems` 三表的关联数据迁移
- 覆盖模式更新（相同 `indexid` 时覆盖目标库数据）

不包括：
- 动态连接测试（类似 `-FMIN`）
- 增量同步（只全量迁移有效代理）
- 数据转换或格式修改

## 2. 命令行接口

### 2.1 新增参数
- `-S` 或 `-sync` 或 `--sync` - 触发代理同步功能

### 2.2 参数格式
```bash
# 方式1：仅指定源数据库（目标库从配置读取）
./validproxy -S /path/to/source.db

# 方式2：同时指定源和目标（命令行优先）
./validproxy -S /path/to/source.db:/path/to/target.db

# 方式3：仅通过配置文件（当命令行未指定时）
./validproxy -S
```

### 2.3 配置文件
在 `config.json` 中添加 `sync` 配置节：
```json
{
  "sync": {
    "source_db": "/path/to/source.db",
    "target_db": "/path/to/target.db"
  }
}
```

### 2.4 优先级
1. 命令行 `-S source:target`（最高优先级）
2. 命令行 `-S source`（target 从配置读取）
3. 配置文件 `sync.source_db` + `sync.target_db`
4. 若均未指定，报错并退出

## 3. 数据迁移逻辑

### 3.1 筛选条件
从源数据库查询有效代理：
```sql
SELECT p.*, pe.*
FROM ProfileItem p
LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
ORDER BY CAST(pe.Delay AS INTEGER) ASC
```

### 3.2 迁移流程
```
1. 打开源数据库 (source_db)
   ↓
2. 查询源库中 delay > 0 的有效代理
   ↓
3. 打开目标数据库 (target_db)
   ↓
4. 对每条有效代理：
   a. 检查代理对应的订阅 (p.subid) 在目标库是否存在
      - 若不存在：先插入订阅到目标库 subitems 表
   b. 检查代理 indexid 在目标库是否存在
      - 若存在：执行 UPDATE（覆盖模式）
      - 若不存在：执行 INSERT
   c. 迁移 profile_exitems 中的扩展信息（delay, speed等）
   ↓
5. 输出迁移结果统计
   - 总代理数
   - 成功迁移数
   - 跳过数（如有）
   - 失败数（如有）
```

### 3.3 冲突处理策略
- **相同 indexid**：覆盖更新（UPDATE）
- **相同 subid**：跳过（不重复插入订阅，保留目标库原有订阅信息）
- **数据完整性**：使用事务保证每条代理的迁移原子性

## 4. 实现方案

### 4.1 架构选择
**方案 C（扩展 SubitemUpdaterV2）**，在 `SubitemUpdaterV2` 中添加 `syncDatabases()` 方法。

**理由**：
- `SubitemUpdaterV2` 已处理数据库和订阅更新逻辑，便于复用
- 虽然会增加类的职责，但符合当前项目"功能集中"的模式
- 易于在 `main.cpp` 中调用

### 4.2 最大化复用原则

**核心思路**：尽量调用现有方法和类，避免重新实现已有逻辑。

**复用清单**：
1. **查询有效代理** - 复用 `ProfileitemDAO::getAll(sql)` 方法，传入带 `delay > 0` 过滤的 SQL
2. **数据模型** - 复用 `db::models::Profileitem` 和 `db::models::Profileexitem` 结构体
3. **订阅操作** - 复用 `SubitemDAO` 的 `getById()`、`insert()` 方法
4. **代理操作** - 复用 `ProfileitemDAO` 的 `getByIndexId()`、`insert()`、`update()` 方法
5. **扩展信息操作** - 复用 `ProfileexitemDAO` 的 `getByIndexId()`、`insert()`、`update()` 方法
6. **配置读取** - 复用 `config::ConfigReader::load()` 读取 `sync` 配置节
7. **数据转换** - 复用 `Profileitem::fromStmt()` 和 `Profileexitem::fromStmt()` 静态方法

**不重新实现**：
- ❌ 不重新写 SQL 查询逻辑（复用 DAO 的 `getAll`）
- ❌ 不重新解析命令行参数（按现有模式在 main.cpp 中添加）
- ❌ 不重新实现数据模型转换（复用 `fromStmt` 方法）
- ❌ 不重新处理订阅逻辑（复用 `SubitemDAO`）

### 4.2 代码结构修改

#### 4.2.1 `include/SubitemUpdaterV2.h`
添加新方法声明：
```cpp
namespace update {

class SubitemUpdaterV2 {
public:
    // 现有方法...
    
    // 新增：同步数据库
    bool syncDatabases(const std::string& sourceDbPath, 
                       const std::string& targetDbPath);
    
private:
    // 新增：辅助方法
    bool migrateSubscription(sqlite3* srcDb, sqlite3* dstDb, 
                            const std::string& subid);
    bool migrateProxy(sqlite3* srcDb, sqlite3* dstDb, 
                     const db::models::Profileitem& proxy, 
                     const db::models::Profileexitem& exItem);
};

} // namespace update
```

#### 4.2.2 `src/SubitemUpdaterV2.cpp`
实现新增方法：
```cpp
bool SubitemUpdaterV2::syncDatabases(const std::string& sourceDbPath,
                                      const std::string& targetDbPath) {
    sqlite3* srcDb = nullptr;
    sqlite3* dstDb = nullptr;
    
    // 1. 打开源数据库
    if (sqlite3_open(sourceDbPath.c_str(), &srcDb) != SQLITE_OK) {
        logError("Failed to open source database: " + std::string(sqlite3_errmsg(srcDb)));
        return false;
    }
    
    // 2. 打开目标数据库
    if (sqlite3_open(targetDbPath.c_str(), &dstDb) != SQLITE_OK) {
        logError("Failed to open target database: " + std::string(sqlite3_errmsg(dstDb)));
        sqlite3_close(srcDb);
        return false;
    }
    
    // 3. 查询有效代理
    std::string sql = R"(
        SELECT p.*, COALESCE(pe.Delay, 0) as ExDelay
        FROM ProfileItem p
        LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
        WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
        ORDER BY CAST(pe.Delay AS INTEGER) ASC
    )";
    
    auto profiles = db::models::ProfileitemDAO(srcDb).getAll(sql);
    
    int successCount = 0;
    int failCount = 0;
    
    // 4. 逐条迁移
    for (const auto& profile : profiles) {
        // 获取扩展信息
        auto exItem = db::models::ProfileexitemDAO(srcDb).getByIndexId(profile.indexid);
        
        // 迁移订阅
        if (!profile.subid.empty()) {
            migrateSubscription(srcDb, dstDb, profile.subid);
        }
        
        // 迁移代理
        if (migrateProxy(srcDb, dstDb, profile, exItem)) {
            successCount++;
        } else {
            failCount++;
        }
    }
    
    // 5. 输出统计
    std::cout << "Migration complete:" << std::endl;
    std::cout << "  Total proxies: " << profiles.size() << std::endl;
    std::cout << "  Success: " << successCount << std::endl;
    std::cout << "  Failed: " << failCount << std::endl;
    
    sqlite3_close(srcDb);
    sqlite3_close(dstDb);
    
    return failCount == 0;
}

bool SubitemUpdaterV2::migrateSubscription(sqlite3* srcDb, sqlite3* dstDb,
                                            const std::string& subid) {
    // 检查目标库是否存在该订阅
    std::string checkSql = "SELECT COUNT(*) FROM subitems WHERE id = ?";
    sqlite3_stmt* checkStmt = nullptr;
    bool exists = false;
    
    if (sqlite3_prepare_v2(dstDb, checkSql.c_str(), -1, &checkStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(checkStmt, 1, subid.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            exists = sqlite3_column_int(checkStmt, 0) > 0;
        }
        sqlite3_finalize(checkStmt);
    }
    
    if (exists) {
        return true; // 已存在，跳过
    }
    
    // 从源库获取订阅信息并插入目标库
    // ... (实现细节)
    
    return true;
}

bool SubitemUpdaterV2::migrateProxy(sqlite3* srcDb, sqlite3* dstDb,
                                     const db::models::Profileitem& proxy,
                                     const db::models::Profileexitem& exItem) {
    // 检查目标库是否存在该代理
    std::string checkSql = "SELECT COUNT(*) FROM ProfileItem WHERE IndexId = ?";
    sqlite3_stmt* checkStmt = nullptr;
    bool exists = false;
    
    if (sqlite3_prepare_v2(dstDb, checkSql.c_str(), -1, &checkStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(checkStmt, 1, proxy.indexid.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            exists = sqlite3_column_int(checkStmt, 0) > 0;
        }
        sqlite3_finalize(checkStmt);
    }
    
    // 执行 INSERT 或 UPDATE
    // ... (实现细节)
    
    // 同时迁移 profile_exitems
    // ... (实现细节)
    
    return true;
}
```

#### 4.2.3 `src/main.cpp`
添加参数解析：
```cpp
std::string syncSourceDb;
std::string syncTargetDb;

// 在参数解析循环中添加：
} else if (arg == "-S" || arg == "-sync" || arg == "--sync") {
    if (i + 1 < argc) {
        std::string syncParam = argv[++i];
        commandMode = "sync";
        // 解析 "source:target" 或仅 "source"
        size_t colonPos = syncParam.find(':');
        if (colonPos != std::string::npos) {
            syncSourceDb = syncParam.substr(0, colonPos);
            syncTargetDb = syncParam.substr(colonPos + 1);
        } else {
            syncSourceDb = syncParam;
            // target 从配置读取
        }
    }
}
```

在执行部分添加：
```cpp
if (commandMode == "sync") {
    Logger::init(logDir.string(), commandMode);
    logInfo("validproxy starting sync...");
    
    auto appConfig = config::ConfigReader::load(configPath);
    if (!appConfig) {
        logError("Failed to load config from: " + configPath);
        return 1;
    }
    
    // 确定源库和目标库
    std::string sourceDb = !syncSourceDb.empty() ? syncSourceDb : appConfig->sync.source_db;
    std::string targetDb = !syncTargetDb.empty() ? syncTargetDb : appConfig->sync.target_db;
    
    if (sourceDb.empty() || targetDb.empty()) {
        logError("Source or target database not specified");
        return 1;
    }
    
    update::SubitemUpdaterV2 updater(nullptr, "", *appConfig, nullptr, exeDir);
    bool result = updater.syncDatabases(sourceDb, targetDb);
    
    logInfo(result ? "sync completed" : "sync failed");
    return result ? 0 : 1;
}
```

#### 4.2.4 `include/ConfigReader.h`
在 `AppConfig` 结构中添加：
```cpp
struct AppConfig {
    // ... 现有字段
    
    struct {
        std::string source_db;
        std::string target_db;
    } sync;
};
```

在 `ConfigReader::load()` 中添加配置读取：
```cpp
// 读取 sync 配置
if (json.contains("sync")) {
    auto syncJson = json["sync"];
    if (syncJson.contains("source_db")) {
        config.sync.source_db = syncJson["source_db"].get<std::string>();
    }
    if (syncJson.contains("target_db")) {
        config.sync.target_db = syncJson["target_db"].get<std::string>();
    }
}
```

## 5. 错误处理

### 5.1 错误场景
| 错误场景 | 处理方式 | 用户提示 |
|----------|----------|-----------|
| 源数据库不存在 | 终止并返回错误 | "Source database not found: {path}" |
| 目标数据库无法创建/打开 | 终止并返回错误 | "Failed to open target database: {error}" |
| 源库表结构不匹配 | 跳过该代理，继续处理 | "Warning: Invalid proxy data, skipped" |
| 目标库写入失败 | 回滚当前代理事务，继续处理 | "Failed to migrate proxy {indexid}" |
| 订阅信息缺失 | 仅迁移代理，记录警告 | "Warning: Subscription {subid} not found in source" |

### 5.2 事务处理
- 每条代理的迁移使用独立事务（BEGIN/COMMIT/ROLLBACK）
- 不使用全局事务（避免单点失败导致全部回滚）

## 6. 测试计划

### 6.1 单元测试
```cpp
TEST(SyncTest, MigrateSingleProxy) {
    // 创建临时源库和目标库
    // 在源库插入有效代理（delay>0）
    // 执行 syncDatabases()
    // 验证目标库中存在该代理
}

TEST(SyncTest, OverwriteExistingProxy) {
    // 目标库已存在相同 indexid 的代理
    // 执行同步
    // 验证目标库数据被覆盖
}

TEST(SyncTest, MigrateSubscription) {
    // 源库代理有关联订阅
    // 目标库不存在该订阅
    // 执行同步
    // 验证订阅被创建
}
```

### 6.2 集成测试
```bash
# 测试1：基本同步
./validproxy -S source.db:target.db

# 测试2：仅指定源（目标从配置读取）
./validproxy -S source.db

# 测试3：错误处理（源库不存在）
./validproxy -S nonexistent.db:target.db
```

## 7. 成功标准

- [ ] 源库中 delay>0 的代理成功复制到目标库
- [ ] 关联订阅信息同步创建（若目标库不存在）
- [ ] 扩展信息（profile_exitems）完整迁移
- [ ] 相同 indexid 的代理在目标库中覆盖更新
- [ ] 迁移过程输出清晰的统计信息
- [ ] 错误处理正确，不会因单个代理失败而中断整个迁移
- [ ] 通过单元测试和集成测试

## 8. 后续优化（不在此版本范围）

- 支持增量同步（只同步变更的代理）
- 支持双向同步
- 添加同步前预览模式（dry-run）
- 支持同步进度显示
- 支持批量事务提升性能
