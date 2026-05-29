# 订阅去重与代理黑名单设计方案

## 一、变更概述

| 功能 | 核心变更 | 影响文件 |
|------|----------|----------|
| 订阅更新去重 | 按 `lower(Address)+Port+ConfigType+lower(Id)` 检测重复，跳过已存在记录 | `SubitemUpdaterV2.cpp/.h`, `Profileitem.h` |
| 代理测试黑名单 | ProfileExItem 新增2字段，跟踪连续失败，超5次拉黑，测试时过滤 | `Profileexitem.h`, `ProxyBatchTester.cpp/.h`, `ConfigReader.h/.cpp`, `create_test_db.sql` |
| **无效代理预过滤（新增）** | **去重前预过滤字段不完整的代理：REALITY 缺 PublicKey/Sni、Network 非法、Address/Port 无效** | `SubitemUpdaterV2.cpp`, `ConfigGenerator.cpp` |

---

## 二、数据库路径说明

本方案验证使用的数据库路径：

| 类型 | 路径 | 说明 |
|------|------|------|
| 测试数据库 | `test/guindb.db` | 开发和测试参考，完整路径: `E:\eclipse_workspace\multiple_thread_validproxy\test\guindb.db` |
| 生产数据库 | `bin/worker/guindb.db` | 实际运行使用的数据库 |

**注意**: 所有 SQL 验证命令使用测试数据库 `test/guindb.db`，生产部署时使用 `bin/worker/guindb.db`。

---

## 三、无效代理预过滤规则（新增）

### 3.1 背景

根据 `bin/worker/log/` 日志扫描（2026-05-14 错误分析报告），大量代理在测试阶段因字段缺失而报错 `CONFIG_ERROR`，造成测试资源浪费。问题根源在于**订阅导入阶段未对关键字段做有效性校验**，不完整数据直接写入数据库。

### 3.2 过滤规则定义

在 `SubitemUpdaterV2::updateProfileItems()` 中，于去重检查**之前**增加预过滤步骤。一条代理记录满足**任意一条**即视为无效并跳过：

| 规则编号 | 条件 | 错误日志 |
|---------|------|---------|
| R1 | `StreamSecurity = 'reality'` 且 (`PublicKey` 为空 或 `PublicKey IS NULL`) | `WARN: REALITY proxy missing publicKey: {address}:{port}` |
| R2 | `StreamSecurity = 'reality'` 且 (`Sni` 为空 或 `Sni IS NULL`) | `WARN: REALITY proxy missing sni: {address}:{port}` |
| R3 | `Network` 非空且不在 `isValidNetwork()` 白名单中 | `WARN: Invalid network '{network}' for {address}:{port}` |
| R4 | `Address` 为空 或 `Address IS NULL` | `WARN: Empty address, skipped` |
| R5 | `Port` 为空 或 非数字 或 超出 1-65535 范围 | `WARN: Invalid port '{port}' for {address}` |

### 3.3 SQL 实现方案（推荐）

在 `updateProfileItems()` 开始处执行预处理 SQL 标记：

```sql
-- 标记无效代理（设置 FilterReason 字段）
UPDATE ProfileItem 
SET FilterReason = 'REALITY missing publicKey'
WHERE StreamSecurity = 'reality' 
  AND (PublicKey IS NULL OR PublicKey = '');

UPDATE ProfileItem 
SET FilterReason = 'REALITY missing sni'
WHERE StreamSecurity = 'reality' 
  AND (Sni IS NULL OR Sni = '');

-- 可选：直接删除而非标记
DELETE FROM ProfileItem 
WHERE StreamSecurity = 'reality' 
  AND (PublicKey IS NULL OR PublicKey = '');
```

### 3.4 代码实现方案

在 `updateProfileItems()` 遍历 `newProfiles` 时，在去重检查之前新增校验逻辑：

```cpp
bool SubitemUpdaterV2::isValidProxy(const db::models::Profileitem& p) {
    // R4: Address 不能为空
    if (p.address.empty()) {
        Logger::write("WARN: Empty address, skipped", LogLevel::WARN);
        return false;
    }
    
    // R5: Port 有效性
    if (p.port.empty()) {
        Logger::write("WARN: Empty port for " + p.address, LogLevel::WARN);
        return false;
    }
    try {
        int portVal = std::stoi(p.port);
        if (portVal <= 0 || portVal > 65535) {
            Logger::write("WARN: Invalid port '" + p.port + "' for " + p.address, LogLevel::WARN);
            return false;
        }
    } catch (...) {
        Logger::write("WARN: Non-numeric port '" + p.port + "' for " + p.address, LogLevel::WARN);
        return false;
    }
    
    // R1: REALITY 必须包含 PublicKey
    if (p.streamsecurity == "reality" && p.publickey.empty()) {
        Logger::write("WARN: REALITY proxy missing publicKey: " + p.address + ":" + p.port, LogLevel::WARN);
        return false;
    }
    
    // R2: REALITY 必须包含 Sni
    if (p.streamsecurity == "reality" && p.sni.empty()) {
        Logger::write("WARN: REALITY proxy missing sni: " + p.address + ":" + p.port, LogLevel::WARN);
        return false;
    }
    
    // R3: Network 白名单校验
    if (!p.network.empty()) {
        std::string lowerNet = p.network;
        std::transform(lowerNet.begin(), lowerNet.end(), lowerNet.begin(), ::tolower);
        static const std::set<std::string> valid = {"tcp","ws","grpc","h2","httpupgrade","kcp","xhttp","http","quic","raw","tcp,udp"};
        if (valid.count(lowerNet) == 0) {
            Logger::write("WARN: Invalid network '" + p.network + "' for " + p.address + ":" + p.port, LogLevel::WARN);
            return false;
        }
    }
    
    return true;
}
```

### 3.5 处理流程

```
订阅数据 (raw profiles from subscription)
    │
    ▼
[新增] 3.5 预过滤 (isValidProxy)
    │  ├── 无效 → 记录 WARN 日志 → 跳过（不写入数据库）
    │  └── 有效 ↓
    ▼
[原有] 3.1 去重检查 (dedup check)
    │  ├── 已存在 → 跳过（保留原 IndexId 和测试结果）
    │  └── 新记录 ↓
    ▼
[原有] 3.2 插入 ProfileItem + ProfileExItem
```

### 3.6 日志与统计

- 每次过滤操作记录 `LogLevel::WARN`，包含被过滤原因和代理地址
- 在 `dedup_*.log` 末尾新增汇总行：
  ```
  [REPORT] Pre-filtered invalid proxies: N (R1: X, R2: Y, R3: Z, R4: A, R5: B)
  ```
- `SubitemUpdaterV2` 返回值中增加 `filteredCount` 字段

---

## 四、订阅更新去重详细方案

### 4.1 去重键定义

```sql
-- 最终去重键（需标准化）
GROUP BY lower(Address), Port, ConfigType, lower(Id)
```

基于数据库探索结果（`test/guindb.db`）：

| 字段 | 稳定性 | 说明 |
|------|--------|------|
| Address | ✅ 稳定 | 标准化为小写（存在大小写不一致） |
| Port | ✅ 稳定 | ~50条记录为空，需统一处理为空值 |
| ConfigType | ✅ 稳定 | 协议类型固定（1=VMess, 3=SS, 5=VLESS, 6=Trojan） |
| Id | ✅ **需小写** | 2.5%记录有大小写混合，同一逻辑Id有8种大小写变体 |
| Network | ❌ 不稳定 | 15%的重复记录中Network不同（ws/tcp/grpc），不使用 |
| Sni | ❌ 不稳定 | 46%为空，20%的重复记录中SNI不一致，不使用 |

### 4.2 去重逻辑

在 `SubitemUpdaterV2::updateProfileItems()` 中：

1. **预过滤**（新增）：调用 `isValidProxy()` 过滤无效代理（详见 §三）
2. 对每条有效记录，查询是否存在相同去重键的记录：
   ```sql
   SELECT IndexId FROM ProfileItem 
   WHERE lower(Address) = lower(?) 
     AND (Port IS NULL OR Port = '' OR Port = ?) 
     AND ConfigType = ? 
     AND lower(Id) = lower(?)
     AND (Network IS NULL OR Network = '' OR lower(Network) = lower(?))
   LIMIT 1
   ```
3. **存在**：跳过插入（保留原 `IndexId` 和 test results）
4. **不存在**：生成新 `IndexId`，插入记录，同时插入 `ProfileExItem` 初始状态

### 4.3 代码变更

#### `src/SubitemUpdaterV2.cpp` - 修改 `updateProfileItems()`

```cpp
bool SubitemUpdaterV2::updateProfileItems(const std::string& subid, 
                                            const std::vector<db::models::Profileitem>& profiles) {
    // 新增：预过滤无效代理
    std::vector<db::models::Profileitem> validProfiles;
    int filteredCount = 0;
    for (const auto& p : profiles) {
        if (isValidProxy(p)) {
            validProfiles.push_back(p);
        } else {
            filteredCount++;
        }
    }
    
    // 原有去重逻辑（仅对 validProfiles 执行）
    sqlite3_stmt* checkStmt = nullptr;
    const char* checkSql = 
        "SELECT IndexId FROM ProfileItem "
        "WHERE lower(Address) = lower(?) AND (Port IS NULL OR Port = '' OR Port = ?) "
        "AND ConfigType = ? AND lower(Id) = lower(?) "
        "AND (Network IS NULL OR Network = '' OR lower(Network) = lower(?)) LIMIT 1";
    // ... 后续逻辑不变
}
```

**注意**：不再删除现有记录，改为增量更新模式。去重键包含 Network 字段，确保不同传输协议的代理配置都被保留。

---

## 五、代理测试失败黑名单详细方案

### 5.1 数据库变更

#### `create_test_db.sql` - 修改 ProfileExItem 表

```sql
CREATE TABLE IF NOT EXISTS ProfileExItem (
    IndexId TEXT PRIMARY KEY,
    Delay TEXT,
    Speed TEXT,
    Sort TEXT,
    Message TEXT,
    consecutive_failures INTEGER DEFAULT 0,  -- 新增：连续失败次数
    blacklisted INTEGER DEFAULT 0             -- 新增：是否黑名单（0=否，1=是）
);
```

#### 数据库迁移（在初始化时执行）

```cpp
void migrateProfileExItemTable(sqlite3* db) {
    const char* addCols[] = {
        "ALTER TABLE ProfileExItem ADD COLUMN consecutive_failures INTEGER DEFAULT 0",
        "ALTER TABLE ProfileExItem ADD COLUMN blacklisted INTEGER DEFAULT 0"
    };
    for (const auto& sql : addCols) {
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }
}
```

### 5.2 模型变更

#### `include/Profileexitem.h` - 修改结构体

```cpp
struct Profileexitem {
    std::string indexid;
    std::string delay;
    std::string speed;
    std::string sort;
    std::string message;
    int consecutive_failures = 0;
    static Profileexitem fromStmt(sqlite3_stmt* stmt);
};
```

### 5.3 测试结果跟踪

#### `src/ProxyBatchTester.cpp` - 修改 `updateTestResult()`

```cpp
bool ProxyBatchTester::updateTestResult(const std::string& indexId, int latencyMs, bool success) {
    ProfileExItemDAO exItemDao(db_);
    auto exItem = exItemDao.getById(indexId);
    
    int currentFailures = exItem ? exItem->consecutive_failures : 0;
    int newFailures = success ? 0 : currentFailures + 1;
    
    std::string message = success ? "OK" : "FAILED";
    
    std::ostringstream oss;
    oss << "INSERT OR REPLACE INTO ProfileExItem "
        << "(indexid, delay, speed, sort, message, consecutive_failures) VALUES ("
        << "'" << indexId << "', "
        << "'" << (success ? std::to_string(latencyMs / 10) : "-1") << "', "
        << "'0', '0', '" << message << "', "
        << newFailures << ")";
    
    return sqlite3_exec(db_, oss.str().c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
}
```

### 5.4 过滤黑名单代理

#### `src/ProxyBatchTester.cpp` - 修改 `loadProxies()`

```cpp
std::vector<db::models::Profileitem> ProxyBatchTester::loadProxies(const std::string& subId) {
    config::ConfigGenerator configGen(db_);
    std::string sql;
    
    std::string blacklistFilter = 
        " AND IndexId NOT IN (SELECT IndexId FROM ProfileExItem WHERE blacklisted = 1)";
    
    if (!subId.empty() && !config_.sql_by_subid.empty()) {
        sql = config_.sql_by_subid + blacklistFilter;
    } else {
        sql = config_.sql_query + blacklistFilter;
    }
    
    return configGen.loadProfiles(sql);
}
```

### 5.5 配置项

#### `include/ConfigReader.h` - 修改 `AppConfig`

```cpp
struct AppConfig {
    int blacklist_threshold = 5;
    bool blacklist_enabled = true;
};
```

#### `src/ConfigReader.cpp` - 解析配置

```cpp
if (json.contains("blacklist_threshold")) {
    config.blacklist_threshold = json["blacklist_threshold"];
}
if (json.contains("blacklist_enabled")) {
    config.blacklist_enabled = json["blacklist_enabled"];
}
```

---

## 六、实施顺序

1. **无效代理预过滤**（新增）：在 `SubitemUpdaterV2` 中实现 `isValidProxy()`
2. **数据库 Schema 变更**：更新 `create_test_db.sql`
3. **模型更新**：修改 `Profileexitem.h` 结构体
4. **配置支持**：修改 `ConfigReader` 支持黑名单配置
5. **黑名单逻辑**：修改 `updateTestResult()` 跟踪连续失败
6. **过滤逻辑**：修改 `ProxyBatchTester::loadProxies()` 排除黑名单
7. **去重逻辑**：修改 `SubitemUpdaterV2::updateProfileItems()` 实现去重
8. **构建验证**：使用 `cmake-build` skill 编译验证
9. **功能测试**：
   - 订阅更新：验证无效代理被预过滤
   - 去重：验证重复记录被跳过
   - 代理测试：验证失败5次后被拉黑，不再参与测试

---

## 七、验证方法

### 7.1 无效代理预过滤验证

```bash
# 在 test/guiNDB_empty.db 中插入测试数据
sqlite3 test/guiNDB_empty.db "
INSERT INTO ProfileItem (IndexId, ConfigType, Address, Port, StreamSecurity, PublicKey, Sni, Network) 
VALUES 
  ('test-invalid-1', '5', '1.2.3.4', '443', 'reality', '', 'sni.example.com', 'tcp'),
  ('test-invalid-2', '5', '1.2.3.5', '443', 'reality', 'pk123', '', 'tcp'),
  ('test-valid-1',   '5', '1.2.3.6', '443', 'reality', 'pk456', 'sni.example.com', 'tcp'),
  ('test-invalid-3', '5', '1.2.3.7', '443', 'tcp', '', '', 'ws@freev2configs');
"

# 执行订阅更新后验证无效记录被过滤
# 检查日志输出：应包含 WARN 级别的过滤记录
# 检查数据库：test-invalid-1/2/3 不应存在于 ProfileItem 中
```

### 7.2 去重验证

```bash
# 使用测试数据库进行验证
sqlite3 test/guindb.db "
SELECT lower(Address), Port, ConfigType, lower(Id), COUNT(*) as cnt
FROM ProfileItem 
GROUP BY lower(Address), Port, ConfigType, lower(Id)
HAVING cnt > 1;"
```

### 7.3 黑名单验证

```bash
# 使用测试数据库验证
sqlite3 test/guindb.db "
SELECT p.Address, p.Port, e.Delay, e.consecutive_failures, e.blacklisted
FROM ProfileItem p JOIN ProfileExItem e ON p.IndexId = e.IndexId
WHERE e.consecutive_failures > 0 OR e.blacklisted = 1
LIMIT 20;"
```

---

## 八、待确认事项（已全部确认 2026-05-14）

1. **去重时是否更新现有记录的字段**（如 Remarks、Network 等变化）？
   - ✅ 已确认：跳过不更新，仅保留原记录

2. **黑名单代理是否需要手动恢复机制**？
   - ✅ 已确认：无CLI标志，需手动修改数据库 `UPDATE ProfileExItem SET blacklisted=0, consecutive_failures=0 WHERE IndexId='...'`

3. **迁移逻辑位置**：是在 `Database` 初始化时执行，还是单独提供迁移工具？
   - ✅ 已确认：在初始化时自动执行，忽略字段已存在的错误

4. **预过滤的严格程度**：是否需要对 `Network` 为空的记录也过滤？
   - ✅ 已确认：`Network` 为空时由 `ConfigGenerator` fallback 为 `tcp`（现有行为），不主动过滤

5. **FilterReason 字段是否需要加入数据库**？
   - ✅ 已确认：仅在日志中记录，不修改数据库 schema；如需持久化可后续添加

---

## 九、参考资料

- 错误分析报告：`docs/reports/error-report_20260514.md`
- 数据库探索报告：`task_id: ses_2142eca0affe1cXJOmZntJNZLq`
- Id字段大小写分析：`task_id: ses_214249b5bffezz4UvxDHoD8aJs`
- 项目规范：`AGENTS.md`
- 开发流程：`docs/plans/DEV-PROCESS.md`
- 测试数据库：`test/guindb.db`