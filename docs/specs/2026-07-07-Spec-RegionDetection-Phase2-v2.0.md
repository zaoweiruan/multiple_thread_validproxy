# 代理地域检测增强方案 — Phase 2: 去重阶段地域富化 + UI 显示 + 数据库迁移

> **版本:** v2.0
> **日期:** 2026-07-07
> **状态:** 📝 草案
> **关联:** Deduplicator, RegionDetector, Profileitem, ProfileExItem, ProxyListPanel, ProxyListModel
> **前置:** Phase 1 (docs/specs/2026-07-06-Spec-RegionDetection-v1.0.md) - 仅 RegionDetector 类实现完成

---

## 1. 调整说明

### 1.1 与 Phase 1 的关系

Phase 1 已实现：
- `RegionDetector` 类（`detect()` 静态方法、TLD映射、域名后缀匹配、备注关键词匹配）
- `Profileitem` 结构体 `region` 字段（位置 35）
- `Profileitem::fromStmt()` 读取第 35 列 Region

Phase 1 **未实现**（移至本阶段）：
- 数据库 DDL 变更（ALTER TABLE ADD COLUMN Region）
- SubitemUpdaterV2 INSERT/UPDATE SQL 适配
- ProxyListPanel UI Region 列
- RegionDetector 在实际业务链路中的调用

### 1.2 Phase 2 三处调整

| # | 调整项 | 说明 |
|---|--------|------|
| 1 | **数据库表字段调整** | `ProfileItem` 表缺少 `Region` 列，需要 DDL 变更 + 迁移 |
| 2 | **ProxyList 窗口 Region 显示** | Host 列后插入 Region 列，支持排序/搜索 |
| 3 | **检测时机收窄** | 仅去重阶段执行检测，且仅对 `delay>0 && region==""` 的代理进行 |

> **调整 3 的影响**：Region 检测从「每 INSERT/UPDATE 触发」变为「去重阶段的批量富化步骤」，大幅降低检测频率。GeoIP/GeoSite 等重型文件解析仅在去重阶段首次触发，不会影响订阅更新的正常流程。

---

## 2. 当前状态分析

### 2.1 数据模型现状

`Profileitem` 结构体已有 `region` 字段（位置 35），`fromStmt()` 已读取：

```cpp
// include/Profileitem.h
struct Profileitem {
  // ... 34 个 DB 字段 ...
  std::string echforcequery;   // 34: EchForceQuery
  std::string region;          // 35: Region
  // ... 非 DB 字段 ...
};
```

### 2.2 数据库 DDL 现状

当前 `CREATE TABLE ProfileItem`（tests/test_dedup.cpp:21-31）创建 35 个 TEXT 列，**缺少 `Region`**：

```sql
CREATE TABLE ProfileItem (
    IndexId TEXT PRIMARY KEY,
    ConfigType TEXT, ConfigVersion TEXT, Address TEXT, Port TEXT,
    -- ... 35 列，无 Region ...
    EchConfigList TEXT, EchForceQuery TEXT
    /* 缺少 Region TEXT */
);
```

### 2.3 ProxyListPanel UI 现状

| 枚举 | 值 | 列标题 | 宽度 |
|------|-----|--------|------|
| COL_ROWNUM | 0 | # | 40 |
| COL_TYPE | 1 | Type | 80 |
| COL_ADDRESS | 2 | Host ↕ | 100 |
| COL_PORT | 3 | Port | 70 |
| COL_DELAY | 4 | Latency ↕ | 80 |
| COL_FAILURES | 5 | Failures ↕ | 80 |
| COL_REMARKS | 6 | Remarks | 160 |
| COL_MESSAGE | 7 | Message | 160 |
| COL_INDEXID | 8 | IndexId | 120 |
| COL_COUNT | 9 | — | — |

**无 Region 列**。`GetValueByRow()`、`Compare()`、`filterBySearch()` 均无 region 处理逻辑。

### 2.4 RegionDetector 调用现状

`RegionDetector::detect()` 定义完成但 **未被任何业务代码调用**。Phase 1 原计划在 `SubitemUpdaterV2` 的 INSERT/UPDATE SQL 中调用，但考虑到：
- 订阅更新时 region 非业务关键字段
- 域名检测需要外部 DNS 或 geo 文件，延迟不可控
- 大部分代理在去重阶段才会被测试并确认有效

**调整为仅在去重阶段调用**。

### 2.5 Deduplicator 流水线现状

5 个阶段：

```
Phase 0: 标记有效代理（delay>0 的标记为受保护 subid）
Phase 1: 移除无效地址（私有 IP、畸形地址）
Phase 2: 黑名单移动（连续失败阈值）
Phase 3: 移除配置无效（checkRequired 失败）
Phase 4: 去重合并（CTE 按组合保留最优）
→ cleanupProfileExItem()
```

新加入 **Region 检测阶段** 插入相位在此流水线中。

---

## 3. 调整 1: 数据库表字段调整

### 3.1 CREATE TABLE DDL

```sql
CREATE TABLE ProfileItem (
    -- ... 现有 35 列 ...
    EchForceQuery TEXT,
    Region TEXT       -- ← 新增第 36 列
);
```

### 3.2 数据库迁移

在 `ProfileExItemDAO::migrateTable()` 中添加：

```cpp
// ProfileExItemDAO.cpp
void ProfileExItemDAO::migrateTable(sqlite3* db) {
    // ... 现有迁移逻辑 ...

    // 添加 Region 列
    std::string sql = "ALTER TABLE ProfileItem ADD COLUMN Region TEXT;";
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        // 列已存在是正常情况（幂等）
        Logger::write("WARN: ALTER TABLE ADD Region (may already exist): "
                      + std::string(errMsg), LogLevel::WARN);
        sqlite3_free(errMsg);
    } else {
        Logger::write("INFO: Added Region column to ProfileItem", LogLevel::INFO);
    }
}
```

### 3.3 向后兼容

`Profileitem::fromStmt()` 已有列数保护：

```cpp
// Column 35: Region (仅在列数 > 35 时读取)
if (sqlite3_column_count(stmt) > 35) {
    text = (const char*)sqlite3_column_text(stmt, 35);
    obj.region = text ? text : "";
} else {
    obj.region = "";
}
```

### 3.4 INSERT/UPDATE SQL 变更

**INSERT**（`SubitemUpdaterV2.cpp` ~948 行）：已含 36 列（含 Region）。当前列数和绑定已对齐，仅需确认 Region 列在 INSERT 中的位置正确（第 36 个占位符，对应 `bindText` 第 36 个参数）。

**UPDATE**（`SubitemUpdaterV2.cpp` ~888 行）：`SET Region = ?` 加入 SET 子句，WHERE IndexId = ? 前绑定 Region。

> **注意**：由于 Region 检测已移至去重阶段，INSERT 时 Region 写入空字符串 `""`，UPDATE 时 Region 维持不变（不覆盖）。去重阶段的 RegionDetector 负责写入正确值。

---

## 4. 调整 2: ProxyListPanel Region 列显示

### 4.1 列序调整

Host 列后插入 `Region` 列，后续列序号 +1：

| 枚举 | 值 | 列标题 | 宽度 |
|------|-----|--------|------|
| COL_ROWNUM | 0 | # | 40 |
| COL_TYPE | 1 | Type | 80 |
| COL_ADDRESS | 2 | Host ↕ | 100 |
| **COL_REGION** | **3** | **Region ↕** | **70** |
| COL_PORT | 4 | Port | 70 |
| COL_DELAY | 5 | Latency ↕ | 80 |
| COL_FAILURES | 6 | Failures ↕ | 80 |
| COL_REMARKS | 7 | Remarks | 160 |
| COL_MESSAGE | 8 | Message | 160 |
| COL_INDEXID | 9 | IndexId | 120 |
| COL_COUNT | 10 | — | — |

### 4.2 ProxyListModel.h 枚举变更

```cpp
// src/ui/ProxyListModel.h
enum {
    COL_ROWNUM   = 0,
    COL_TYPE     = 1,
    COL_ADDRESS  = 2,
    COL_REGION   = 3,     // ← 新增
    COL_PORT     = 4,     // ← 原 3
    COL_DELAY    = 5,     // ← 原 4
    COL_FAILURES = 6,     // ← 原 5
    COL_REMARKS  = 7,     // ← 原 6
    COL_MESSAGE  = 8,     // ← 原 7
    COL_INDEXID  = 9,     // ← 原 8
    COL_COUNT    = 10,    // ← 原 9
};
```

### 4.3 GetValueByRow 新增 case

```cpp
// ProxyListModel.cpp
void ProxyListModel::GetValueByRow(wxVariant& variant,
    unsigned int row, unsigned int col) const
{
    if (!proxies_ || row >= proxies_->size()) return;

    const db::models::Profileitem& p = (*proxies_)[row];

    switch (col) {
    case COL_ROWNUM:
        variant = wxVariant(static_cast<long>(row + 1));
        break;
    case COL_TYPE:
        variant = wxVariant(p.configtype);
        break;
    case COL_ADDRESS:
        variant = wxVariant(p.address);
        break;
    case COL_REGION:                          // ← 新增
        variant = wxVariant(p.region);        // ← 新增
        break;                                // ← 新增
    case COL_PORT:
        variant = wxVariant(p.port);
        break;
    // ... 其余 case 不变 ...
    }
}
```

### 4.4 Compare 新增 case

```cpp
// ProxyListModel.cpp
int ProxyListModel::Compare(const wxDataViewItem& item1,
    const wxDataViewItem& item2, unsigned int col, bool ascending) const
{
    // ... 前置检查 ...
    const auto& a = (*proxies_)[idx1];
    const auto& b = (*proxies_)[idx2];

    int cmp = 0;
    switch (col) {
    case COL_ROWNUM:   cmp = compareInt(idx1, idx2); break;
    case COL_ADDRESS:  cmp = a.address.compare(b.address); break;
    case COL_REGION:   cmp = a.region.compare(b.region); break;   // ← 新增
    case COL_PORT:     cmp = compareInt(a.port, b.port); break;
    case COL_DELAY:    cmp = compareDelay(idx1, idx2); break;
    // ...
    }
    return ascending ? cmp : -cmp;
}
```

### 4.5 ProxyListPanel 列创建

```cpp
// ProxyListPanel.cpp 构造函数
listCtrl_->AppendTextColumn("#",        COL_ROWNUM,   wxDATAVIEW_CELL_INERT,  40);
listCtrl_->AppendTextColumn("Type",     COL_TYPE,     wxDATAVIEW_CELL_INERT,  80);
listCtrl_->AppendTextColumn("Host ↕",   COL_ADDRESS,  wxDATAVIEW_CELL_INERT, 100);
listCtrl_->AppendTextColumn("Region ↕", COL_REGION,   wxDATAVIEW_CELL_INERT,  70);  // ← 新增
listCtrl_->AppendTextColumn("Port",     COL_PORT,     wxDATAVIEW_CELL_INERT,  70);
listCtrl_->AppendTextColumn("Latency ↕", COL_DELAY,  wxDATAVIEW_CELL_INERT,  80);
listCtrl_->AppendTextColumn("Failures ↕", COL_FAILURES, wxDATAVIEW_CELL_INERT, 80);
listCtrl_->AppendTextColumn("Remarks",  COL_REMARKS,  wxDATAVIEW_CELL_EDITABLE, 160);
listCtrl_->AppendTextColumn("Message",  COL_MESSAGE,  wxDATAVIEW_CELL_INERT, 160);
listCtrl_->AppendTextColumn("IndexId",  COL_INDEXID,  wxDATAVIEW_CELL_INERT, 120);
```

### 4.6 filterBySearch 增加 region

```cpp
// ProxyListPanel.cpp
void ProxyListPanel::filterBySearch(const wxString& query) {
    // ... 原逻辑 ...
    if (p.address.find(q) != std::string::npos ||
        p.remarks.find(q) != std::string::npos ||
        p.region.find(q) != std::string::npos ||   // ← 新增
        p.indexid.find(q) != std::string::npos) {
        filtered.push_back(p);
    }
}
```

---

## 5. 调整 3: 去重阶段 Region 检测

### 5.1 检测策略 — 复用 Phase 1 RegionDetector

使用 Phase 1 已实现的 `RegionDetector::detect(address, remarks)` 静态方法：

```
detect(address, remarks):
  1. detectFromRemarks(remarks)        ← 备注关键词匹配（"香港"→HK）
  2. detectFromDomainSuffix(address)   ← 已知域名后缀（apple.com→US）
  3. detectFromTld(address)            ← TLD 映射（.jp→JP）
  4. return "Unknown"
```

### 5.2 去重流水线新阶段

在 `Deduplicator::deduplicate()` 的 Phase 1（移除无效地址）之后、Phase 2（黑名单）之前插入 Region 检测阶段，或者在 cleanup 之前作为独立阶段：

**推荐位置**：**Phase 4（去重合并）之后、cleanupProfileExItem() 之前**，此时剩下的都是有效、非重复的代理。

```
Phase 0: 标记有效代理
Phase 1: 移除无效地址
Phase 2: 黑名单移动
Phase 3: 移除配置无效
Phase 4: 去重合并 (CTE)
Phase 5: Region 检测富化  ← 新增
→ cleanupProfileExItem()
```

也可以插入在 Phase 0 之后、Phase 1 之前，因为 Phase 0 已筛选出 `delay > 0` 的代理。

**推荐：Phase 0 之后、Phase 1 之前**，因为：
- Phase 0 已通过 `ProfileExItem.Delay > 0` 筛选出有效代理
- 后续 Phase 1-4 会删除部分代理，region 检测在这些删除之前进行也无妨（被删的代理 region 值随之删除）
- 早检测 → Region 值随记录一起被清理，数据一致性好

### 5.3 筛选条件

```sql
SELECT pi.IndexId, pi.Address, pi.Remarks
FROM ProfileItem pi
JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId
WHERE CAST(pe.Delay AS INTEGER) > 0
  AND (pi.Region IS NULL OR pi.Region = '');
```

仅对 **已通过连通性测试**（`pe.Delay > 0`）且 **Region 为空** 的代理执行检测。

### 5.4 Deduplicator 新增方法

```cpp
// include/update/Deduplicator.h
namespace update {

class Deduplicator {
public:
    // ... 现有方法 ...
    bool deduplicate();

private:
    // ... 现有私有方法 ...
    int deduplicatePhase0();       // 标记有效代理
    int deduplicatePhase1();       // 移除无效地址
    int deduplicateBlacklistPhase();
    int deduplicateConfigErrorPhase();
    int deduplicateMergedPhase();

    int deduplicateRegionPhase();  // ← 新增：Region 检测富化
    void cleanupProfileExItem();
};

}
```

### 5.5 实现逻辑

```cpp
// src/update/Deduplicator.cpp

int Deduplicator::deduplicateRegionPhase() {
    // 查询需要检测的代理：delay > 0 且 region 为空
    std::string sql =
        "SELECT pi.IndexId, pi.Address, pi.Remarks "
        "FROM ProfileItem pi "
        "JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId "
        "WHERE CAST(pe.Delay AS INTEGER) > 0 "
        "  AND (pi.Region IS NULL OR pi.Region = '')";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("ERROR: Failed to prepare region detection query - "
                      + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return 0;
    }

    const char* text;
    std::vector<std::tuple<std::string, std::string>> updates;  // (indexId, region)

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        // IndexId
        text = (const char*)sqlite3_column_text(stmt, 0);
        std::string indexId = text ? text : "";
        // Address
        text = (const char*)sqlite3_column_text(stmt, 1);
        std::string address = text ? text : "";
        // Remarks
        text = (const char*)sqlite3_column_text(stmt, 2);
        std::string remarks = text ? text : "";

        std::string region = utils::RegionDetector::detect(address, remarks);
        updates.emplace_back(indexId, region);
    }
    sqlite3_finalize(stmt);

    if (updates.empty()) {
        Logger::write("INFO: Region detection: no proxies need detection", LogLevel::INFO);
        return 0;
    }

    // 批量 UPDATE Region
    int updated = 0;
    std::string updateSql = "UPDATE ProfileItem SET Region = ? WHERE IndexId = ?";
    sqlite3_stmt* updateStmt = nullptr;

    if (sqlite3_prepare_v2(db_, updateSql.c_str(), -1, &updateStmt, nullptr) != SQLITE_OK) {
        Logger::write("ERROR: Failed to prepare region update - "
                      + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return 0;
    }

    for (const auto& [indexId, region] : updates) {
        sqlite3_bind_text(updateStmt, 1, region.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(updateStmt, 2, indexId.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(updateStmt) == SQLITE_DONE) {
            updated++;
        }
        sqlite3_reset(updateStmt);
    }
    sqlite3_finalize(updateStmt);

    Logger::write("INFO: Region detection: " + std::to_string(updated)
                  + " proxies enriched with region data", LogLevel::REPORT);
    return updated;
}
```

### 5.6 插入 deduplicate() 流水线

```cpp
bool Deduplicator::deduplicate() {
    // ... Phase 0-4 ...

    Logger::write("Phase 5/6 - Detecting region for valid proxies", LogLevel::REPORT);
    regionDetectCount_ = deduplicateRegionPhase();
    Logger::write("Phase 5 completed: enriched " + std::to_string(regionDetectCount_)
                  + " proxies", LogLevel::REPORT);

    // ... cleanupProfileExItem() ...
}
```

### 5.7 Deduplicator.h 新增计数器

```cpp
// include/update/Deduplicator.h
int regionDetectCount_ = 0;      // ← 新增

// Getter
int getRegionDetectCount() const { return regionDetectCount_; }
```

### 5.8 日志输出统计

去重完成后，在 Dedup Summary 中增加 Region 检测统计：

```
========================================
Deduplication Summary
========================================
Total deleted: 1200
Total remaining: 8500
Region enriched: 3200  (新增)
Dedup completed successfully
```

---

## 6. 设计：Phase 2 Geo 文件兜底（可选/后续）

### 6.1 原有 GeoIP/GeoSite 方案推迟

Phase 2 v1.0 中设计的 GeoIP/GeoSite 文件解析作为**后续增强**，原因：

| 因素 | 说明 |
|------|------|
| 检测频次大幅降低 | 仅在去重阶段触发，非实时插入 |
| RegionDetector 现有策略覆盖率高 | TLD + 域名后缀 + 备注关键词 覆盖大部分场景 |
| geo 文件引入复杂度高 | Protobuf 解码器 (~200行)、GeoIpReader (~500行) 实现成本高 |
| ~28 MB 内存占用 | 仅在去重阶段使用，性价比存疑 |

### 6.2 决策

- **Phase 2 当前范围（v2.0）**：实现数据库迁移、UI 显示、去重阶段 Region 检测（使用 Phase 1 RegionDetector）
- **GeoIP/GeoSite 增强**：作为 **Phase 3** 规划，当用户反馈 Region 检测准确率不足时再实施

### 6.3 Geo 文件路径配置（仍保留）

Geo 文件路径配置仍可在 `ConfigReader` 中预留，但默认行为为「未配置时不启用」：

```json
{
  "geo": {
    "geoip_path": "",
    "geosite_path": ""
  }
}
```

留空表示不启用 geo 文件解析。

---

## 7. 文件变更清单

### 新增文件

| 文件 | 预估行数 | 说明 |
|------|---------|------|
| — | — | 本阶段无新增文件 |

### 修改文件

| 文件 | 变更说明 |
|------|---------|
| `src/ProfileExItemDAO.cpp` | `migrateTable()` 添加 `ALTER TABLE ProfileItem ADD COLUMN Region TEXT` |
| `src/ui/ProxyListModel.h` | 插入 `COL_REGION = 3`，调整后续枚举值 |
| `src/ui/ProxyListModel.cpp` | `GetValueByRow()` 增加 `COL_REGION` case；`Compare()` 增加 `COL_REGION` case |
| `src/ui/ProxyListPanel.cpp` | Host 列后追加 Region 列；`filterBySearch()` 增加 region 搜索 |
| `include/update/Deduplicator.h` | 新增 `deduplicateRegionPhase()` 声明、`regionDetectCount_`、getter |
| `src/update/Deduplicator.cpp` | 新增 `deduplicateRegionPhase()` 实现；`deduplicate()` 插入 Phase 5 |
| `tests/test_dedup.cpp` | `CREATE TABLE ProfileItem` 增加 `Region TEXT` 列 |
| `CMakeLists.txt` | 本阶段无变更（无需新增源文件） |

### 无需修改的文件

| 文件 | 原因 |
|------|------|
| `include/Profileitem.h` | Phase 1 已完成 `region` 字段和 `fromStmt()` 读取 |
| `src/SubitemUpdaterV2.cpp` | INSERT 时 Region 写入空字符串，不做检测 |
| `include/ConfigReader.h` + `src/ConfigReader.cpp` | Geo 路径配置推迟到 Phase 3 |
| `src/ShareLink.cpp` | Region 已初始化为空字符串（Phase 1 完成） |

---

## 8. 实施计划

### Task 1: 数据库迁移

**文件**: `src/ProfileExItemDAO.cpp`

- [ ] 在 `migrateTable()` 中添加 ALTER TABLE ProfileItem ADD COLUMN Region TEXT
- [ ] 幂等处理：列已存在时捕获 SQLITE_ERROR，仅 WARN 日志
- [ ] 更新测试 `tests/test_dedup.cpp` 中 `CREATE TABLE ProfileItem` 增加 `Region TEXT`

### Task 2: ProfileExItem 列数验证

**文件**: `include/Profileitem.h`

- [ ] 确认 `fromStmt()` 的列数保护逻辑 `sqlite3_column_count(stmt) > 35` 正确
- [ ] 确认 `ProfileitemDAO::getAll()` 使用 `SELECT *` 时不会因列数不足而越界

### Task 3: ProxyListModel 列枚举调整

**文件**: `src/ui/ProxyListModel.h`

- [ ] 插入 `COL_REGION = 3`
- [ ] 调整 COL_PORT(4), COL_DELAY(5), COL_FAILURES(6), COL_REMARKS(7), COL_MESSAGE(8), COL_INDEXID(9), COL_COUNT(10)

### Task 4: ProxyListModel GetValueByRow / Compare

**文件**: `src/ui/ProxyListModel.cpp`

- [ ] `GetValueByRow()` 增加 `case COL_REGION`
- [ ] `Compare()` 增加 `case COL_REGION`

### Task 5: ProxyListPanel 列创建 + 搜索

**文件**: `src/ui/ProxyListPanel.cpp`

- [ ] Host 列后追加 `AppendTextColumn("Region ↕", COL_REGION, wxDATAVIEW_CELL_INERT, 70)`
- [ ] `filterBySearch()` 增加 `p.region.find(q) != std::string::npos`

### Task 6: Deduplicator Region 检测阶段

**文件**: `include/update/Deduplicator.h` + `src/update/Deduplicator.cpp`

- [ ] 头文件新增 `deduplicateRegionPhase()` 声明、`regionDetectCount_` 字段、getter
- [ ] 实现 `deduplicateRegionPhase()`：查询 `delay>0 && region为空` → 调用 `RegionDetector::detect()` → 批量 UPDATE
- [ ] `deduplicate()` 流水线中插入 Phase 5
- [ ] Summary 日志增加 Region 统计

### Task 7: 测试适配

**文件**: `tests/test_dedup.cpp`

- [ ] `CREATE TABLE ProfileItem` 增加 `Region TEXT` 列
- [ ] 验证 `insertProfile()` 后 region 默认值

### Task 8: 全量编译 + 集成测试

- [ ] `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`
- [ ] `cmake --build build --parallel 8`
- [ ] `ctest -V`
- [ ] 验证去重后 region 列正确填充

---

## 9. 接受标准

1. [ ] `ProfileExItemDAO::migrateTable()` 正确添加 `Region TEXT` 列，幂等运行
2. [ ] `tests/test_dedup.cpp` 的 DDL 包含 `Region TEXT`
3. [ ] ProxyListPanel 在 Host 列后显示 `Region ↕` 列
4. [ ] Region 列支持文字排序（字母序）
5. [ ] 搜索框输入 region 名称可过滤对应行
6. [ ] `Deduplicator::deduplicateRegionPhase()` 仅处理 `delay>0 && region==""` 的代理
7. [ ] 去重日志输出 Region 富化统计
8. [ ] Region 检测不阻塞去重主流程（异常时静默降级）
9. [ ] `SubitemUpdaterV2` 的 INSERT/UPDATE 不受影响（Region 写入空字符串或维持不变）
10. [ ] 全量 `ctest -V` 通过
11. [ ] 所有新代码遵守项目 `auto` 禁用规范

---

## 附录 A: 列顺序迁移说明

新增 `COL_REGION`（Host 列后，Port 前）会改变 `COL_COUNT` 值及后续所有列枚举值。所有 switch-case 和列创建代码中的枚举引用会自动适应（枚举值变更），但需关注：

- 任何硬编码的列序号（如排序状态恢复、列宽设置）需同步更新
- 已保存的 `sortState_.column` 值在升级后可能偏移（此问题存在但影响可控，因排序状态为运行时内存状态）

**由于枚举值变动涉及多处文件，所有文件必须同时提交，无法分步实施。**

## 附录 B: 实施顺序建议

```
Task 1 (DB migration) → Task 2 (列数验证) → Task 3-5 (UI) → Task 6 (Dedup) → Task 7 (测试) → Task 8 (编译+集成)
                      ↕ (Task 2 验证 fromStmt 兼容性)
Task 4 和 Task 6 可并行（独立模块）
```

---

*文档结束 — v2.0 合并了三处调整：数据库迁移、UI Region 列、去重阶段检测*
