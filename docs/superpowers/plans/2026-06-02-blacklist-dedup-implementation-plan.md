# 黑名单去重功能实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add blacklist functionality during deduplication phase - proxies with consecutive_failures >= threshold will be moved to a configured blacklist subscription ID

**Architecture:** Extend existing deduplication flow in SubitemUpdaterV2 with blacklist phase that updates ProxyItem.SubId for failed proxies based on consecutive_failures count

**Tech Stack:** C++17, SQLite3, Boost.JSON, wxWidgets

---

## 当前状态分析

**现有实现:**
- `ProfileExItem` 已有 `consecutive_failures` 字段 (int, 默认0)
- `ConfigReader` 已有 `blacklist_threshold` 配置 (默认5)
- `SubitemUpdaterV2::deduplicate()` 有3个阶段：Phase0(标记有效代理) → Phase1(删除无效地址) → MergedPhase(删除重复)
- 缺失：黑名单逻辑、`blacklist_id` 配置、`blacklist_enabled` 配置

**参考设计:** `docs/design/dedup-blacklist-design.md`

---

## 详细变更

### U1: include/ConfigReader.h - 添加黑名单配置字段

```cpp
struct AppConfig {
    // ... 现有字段 ...
    int blacklist_threshold;  // 已存在
    bool blacklist_enabled = true;  // 新增：是否启用黑名单功能
    std::string blacklist_subid;    // 新增：黑名单订阅ID
};
```

### U2: src/ConfigReader.cpp - 解析黑名单配置

```cpp
// 在 dedup 配置解析后添加 (约192行后)
if (dedup.contains("blacklist_enabled") && dedup["blacklist_enabled"].is_bool()) {
    config.blacklist_enabled = dedup["blacklist_enabled"].as_bool();
} else {
    config.blacklist_enabled = true;
}
if (dedup.contains("blacklist_subid") && dedup["blacklist_subid"].is_string()) {
    config.blacklist_subid = dedup["blacklist_subid"].as_string().c_str();
}
```

### U3: src/ConfigReader.cpp - save() 方法添加黑名单配置

```cpp
dedupObj["blacklist_enabled"] = config.blacklist_enabled;
dedupObj["blacklist_subid"] = config.blacklist_subid;
```

### U4: include/Profileexitem.h - 添加 blacklisted 字段 (可选)

```cpp
struct ProfileExItem {
    // ... 现有字段 ...
    int consecutive_failures = 0;
    int blacklisted = 0;  // 新增：明确标记是否黑名单(0=否,1=是)
};
```

### U5: src/Profileexitem.cpp - migrateTable 添加 blacklisted 列 (若有cpp文件)

或修改 `ProfileExItemDAO::migrateTable()` 添加列迁移

### U6: src/SubitemUpdaterV2.cpp - 添加 deduplicateBlacklistPhase() 方法

```cpp
int SubitemUpdaterV2::deduplicateBlacklistPhase() {
    if (!config_.blacklist_enabled) {
        Logger::write("INFO: Blacklist phase skipped - blacklist_enabled=false", LogLevel::INFO);
        return 0;
    }
    
    if (config_.blacklist_subid.empty()) {
        Logger::write("WARN: Blacklist phase skipped - blacklist_subid not configured", LogLevel::WARN);
        return 0;
    }
    
    int threshold = config_.blacklist_threshold;
    std::string blacklistSubid = config_.blacklist_subid;
    
    std::string sql = "UPDATE ProfileItem SET SubId = '" + blacklistSubid + "' "
                      "WHERE IndexId IN (SELECT IndexId FROM ProfileExItem "
                      "WHERE consecutive_failures >= " + std::to_string(threshold) + ")";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Blacklist phase failed - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return 0;
    }
    
    int updated = sqlite3_changes(db_);
    Logger::write("INFO: Blacklist phase moved " + std::to_string(updated) + " proxies to blacklist subid: " + blacklistSubid, LogLevel::INFO);
    return updated;
}
```

### U7: src/SubitemUpdaterV2.cpp - 修改 deduplicate() 方法

在 Phase 2 (Phase 1b后) 或 Phase 3 前插入黑名单阶段：

```cpp
bool SubitemUpdaterV2::deduplicate() {
    // ... 统计开始 ...
    
    Logger::write("Phase 1/4 - Marking working proxies with protected subid", LogLevel::REPORT);
    int p0 = deduplicatePhase0();
    
    Logger::write("Phase 2/4 - Removing invalid addresses (private IPs)", LogLevel::REPORT);
    int p1 = deduplicatePhase1();
    
    // 新增：黑名单阶段
    Logger::write("Phase 3/4 - Moving blacklisted proxies to blacklist subid", LogLevel::REPORT);
    int pBlacklist = deduplicateBlacklistPhase();
    
    Logger::write("Phase 4/4 - Removing duplicates (merged CTE)", LogLevel::REPORT);
    int pMerged = deduplicateMergedPhase();
    
    // ... 后续逻辑 ...
}
```

### U8: bin/config.json - 添加黑名单配置示例

```json
"dedup": {
    "enabled": true,
    "dedup_after_update": true,
    "blacklist_threshold": 5,
    "blacklist_enabled": true,
    "blacklist_subid": "your-blacklist-subid-here",
    "subids": ["5544178410297751350", "5126987..."]
}
```

---

## 验证步骤

### V1: 编译验证
```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
```

### V2: 单元测试
```bash
ctest -R DedupTest -V
```

### V3: 手动测试
```bash
# 插入测试数据
sqlite3 bin/worker/guindb.db "
INSERT INTO ProfileItem (IndexId, ConfigType, Address, Port) VALUES 
('test-banned-1', '5', '1.2.3.4', '443'),
('test-banned-2', '1', '5.6.7.8', '80');

INSERT INTO ProfileExItem (IndexId, consecutive_failures, blacklisted) VALUES 
('test-banned-1', 5, 0),
('test-banned-2', 6, 0);
"

# 执行去重
./build/validproxy.exe -D

# 验证黑名单移动
sqlite3 bin/worker/guindb.db "SELECT IndexId, SubId FROM ProfileItem WHERE IndexId IN ('test-banned-1', 'test-banned-2')"
```

---

## 文件变更列表

| 文件 | 操作 | 说明 |
|------|------|------|
| include/ConfigReader.h | 修改 | 添加 `blacklist_enabled`, `blacklist_subid` 字段 |
| src/ConfigReader.cpp | 修改 | 解析和保存黑名单配置 |
| include/Profileexitem.h | 修改 | 添加 `blacklisted` 字段 |
| src/SubitemUpdaterV2.cpp | 修改 | 添加 `deduplicateBlacklistPhase()` 方法和调用 |
| bin/config.json | 修改 | 添加配置示例 |

---

## 风险

1. **现有数据兼容**: 新增字段需在DAO中处理迁移，默认值为0/空字符串
2. **黑名单subid不存在**: 若配置的blacklist_subid在SubItem表中不存在，代理将指向无效订阅（可接受，后续显示时过滤）
3. **性能影响**: SQL UPDATE 操作在大数据量时可能耗时，可考虑添加索引

---

## 状态: completed

日期: 2026-06-02

## 验证结果

- 编译: 0 errors ✓
- 单元测试: 13/13 通过 ✓
- 新增测试: BlacklistPhaseMovesProxiesAboveThreshold, BlacklistPhaseSkipsWhenThresholdNotMet

## 实施备注

- U4 (blacklisted 字段) 已移除，判定逻辑完全基于 `consecutive_failures >= threshold`
- 实际文件变更：include/ConfigReader.h, src/ConfigReader.cpp, include/SubitemUpdaterV2.h, src/SubitemUpdaterV2.cpp, tests/test_dedup.cpp