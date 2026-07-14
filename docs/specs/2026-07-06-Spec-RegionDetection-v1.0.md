# 代理地域检测与显示技术方案 (Spec)

> **版本:** v1.0  
> **日期:** 2026-07-06  
> **状态:** 📝 草案  
> **关联:** ProxyListPanel UI, Profileitem 数据模型, SubitemUpdaterV2 数据迁移

---

## 1. 问题陈述

用户在 ProxyListPanel 中查看代理列表时，无法直观识别代理所在的地理区域。代理的 `Address`(Host) 字段包含域名或 IP，但缺乏自动化的地域解析和展示机制，需手动查看后缀推测区域。

## 2. 现状分析

### 2.1 数据模型

`ProfileItem` 表在当前 35 个字段中不包含 `Region` 字段。`fromStmt()` 从 SQLite 按 0-34 列顺序读取：

```cpp
// include/Profileitem.h:97-209
static Profileitem fromStmt(sqlite3_stmt* stmt) {
    // 从 sqlite3_column_text(stmt, 0) 到 (stmt, 34)
    // IndexId(0) ... EchForceQuery(34)
}
```

`ProfileitemDAO::getAll()` 使用 `SELECT * FROM ProfileItem` 返回全部行。

### 2.2 数据库 DDL

测试中 `CREATE TABLE ProfileItem` 定义了 35 个 TEXT 列（`tests/test_dedup.cpp:21-31`）。

### 2.3 ProfileItem INSERT / UPDATE

**INSERT** (SubitemUpdaterV2.cpp:948): 列出全部 35 列，35 个 `?` 占位符，35 个 `sqlite3_bind_*` 调用。

**UPDATE** (SubitemUpdaterV2.cpp:888): 以 `SET Col1=?, Col2=?...` 更新除 IndexId 外的 34 列，35 号绑定 IndexId 用于 WHERE。

### 2.4 ProxyListPanel UI

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

`ProxyListModel::GetValueByRow()` 基于 `col` 枚举值 switch 分发。

`ProxyListModel::Compare()` 按列排序，当前处理 9 列。

`ProxyListPanel::filterBySearch()` 匹配 `address`/`remarks`/`indexId`。

### 2.5 迁移机制

`ProfileExItemDAO` 构造函数中检测表结构缺失列并执行 `ALTER TABLE ADD COLUMN`。无版本化系统。

---

## 3. 设计

### 3.1 Region 检测策略 — Phase 1

**Phase 1: 基于域名后缀 + 常见前缀模式匹配（零外部依赖）**

解析 `address` 字段中的域名后缀并映射至标准地区名称。采用两重策略：

**策略 A — TLD 映射**：提取域名最后一级 TLD（如 `.jp` → `日本`），使用内置 TLD→地区表：

| TLD | Region | TLD | Region |
|-----|--------|-----|--------|
| .jp | 日本 | .de | 德国 |
| .hk | 香港 | .fr | 法国 |
| .tw | 台湾 | .uk | 英国 |
| .kr | 韩国 | .nl | 荷兰 |
| .sg | 新加坡 | .se | 瑞典 |
| .my | 马来西亚 | .no | 挪威 |
| .vn | 越南 | .fi | 芬兰 |
| .th | 泰国 | .it | 意大利 |
| .id | 印度尼西亚 | .es | 西班牙 |
| .ph | 菲律宾 | .ru | 俄罗斯 |
| .in | 印度 | .pl | 波兰 |
| .au | 澳大利亚 | .cz | 捷克 |
| .nz | 新西兰 | .ch | 瑞士 |
| .ca | 加拿大 | .at | 奥地利 |
| .us | 美国 | .be | 比利时 |
| .br | 巴西 | .dk | 丹麦 |
| .mx | 墨西哥 | .pt | 葡萄牙 |
| .ar | 阿根廷 | .gr | 希腊 |
| .za | 南非 | .ie | 爱尔兰 |
| .tr | 土耳其 | .hu | 匈牙利 |
| .sa | 沙特阿拉伯 | .ro | 罗马尼亚 |
| .ae | 阿联酋 | .ua | 乌克兰 |
| .il | 以色列 | .bg | 保加利亚 |

**策略 B — 子域名/前缀模式匹配**：对未匹配 TLD 的域名，检查常见地域前缀：

- `us-*.domain.com`, `usa-*` → `美国`
- `jp-*` → `日本`
- `sg-*` → `新加坡`
- `hk-*` → `香港`
- `tw-*` → `台湾`
- `kr-*` → `韩国`
- `de-*` → `德国`
- `fr-*` → `法国`
- `uk-*` → `英国`
- `au-*` → `澳大利亚`
- `ca-*` → `加拿大`
- `br-*` → `巴西`
- `in-*` → `印度`
- `nl-*` → `荷兰`
- `se-*` → `瑞典`
- `no-*` → `挪威`
- `fi-*` → `芬兰`
- `ru-*` → `俄罗斯`
- `it-*` → `意大利`
- `es-*` → `西班牙`

**策略 C — IP 地域数据库**：Phase 2（可选），引入 MaxMind GeoLite2 等轻量级 IP 地理库。

**未匹配** → `"未知"`

### 3.2 检测时机

Region 在**以下场景**检测/更新：

1. **订阅更新解析时**（`SubitemUpdaterV2.cpp` 解析新代理后）：新 INSERT 前调用检测函数
2. **批量数据库同步时**（`SubitemUpdaterV2::migrateProxy()`）：INSERT/UPDATE 前调用检测函数
3. **手动触发刷新**：通过 CLI 或 UI 按钮触发全量重新检测
4. **应用启动/加载代理列表时**：对列表中 Region 为空或 "未知" 的记录执行检测

> **设计决策**：Region 是**派生数据**，通过解析 `address` 实时计算后存入数据库，而非运行时动态计算。存入数据库实现了排序/搜索/持久化。

### 3.3 静态检测函数

```cpp
// include/RegionDetector.h
namespace utils {
class RegionDetector {
public:
    // 根据 address 检测代理地域
    static std::string detect(const std::string& address);
private:
    static std::string detectByTLD(const std::string& address);
    static std::string detectByPrefix(const std::string& address);
    static const std::unordered_map<std::string, std::string>& tldMap();
    static const std::vector<std::pair<std::string, std::string>>& prefixPatterns();
};
}
```

### 3.4 ProfileItem 数据模型变更

在 `Profileitem` 结构体中新增 `region` 字段（DB 字段顺序末尾，位置 35）：

```cpp
// include/Profileitem.h
struct Profileitem {
  // ... 现有 35 个 DB 字段 ...
  std::string echforcequery;   // 34: EchForceQuery
  std::string region;          // 35: Region  ← 新增
  // ... 非 DB 字段 ...
};
```

**`fromStmt()` 扩展**（向后兼容处理，仅当 `sqlite3_column_count(stmt) > 35` 才读取）：

```cpp
// Column 35: Region (仅在列数 > 35 时读取)
if (sqlite3_column_count(stmt) > 35) {
    text = (const char*)sqlite3_column_text(stmt, 35);
    obj.region = text ? text : "";
} else {
    obj.region = "";
}
```

### 3.5 数据库变更

**DDL**: `CREATE TABLE` 增加 `Region TEXT` 列：

```sql
CREATE TABLE ProfileItem (
    -- ... 现有 35 列 ...
    Region TEXT
);
```

**迁移**: 在 `ProfileExItemDAO::migrateTable()` 中添加 `ALTER TABLE ProfileItem ADD COLUMN Region TEXT;`。

**INSERT SQL** (SubitemUpdaterV2.cpp:948): `Region` 加入列名表，增加第 36 个占位符：

```sql
INSERT INTO ProfileItem (...) VALUES (?, ..., ?, ?)
-- 36 个占位符
```

**UPDATE SQL** (SubitemUpdaterV2.cpp:888): `Region = ?` 加入 SET 子句，WHERE IndexId = ? 前绑定 Region。

### 3.6 ProxyListPanel UI 变更

Host 列后新增 `Region` 列，后续列序号 +1：

| 枚举 | 值 | 列标题 | 宽度 |
|------|-----|--------|------|
| COL_ROWNUM | 0 | # | 40 |
| COL_TYPE | 1 | Type | 80 |
| COL_ADDRESS | 2 | Host ↕ | 100 |
| **COL_REGION** | **3** | **Region** | **70** |
| COL_PORT | 4 | Port | 70 |
| COL_DELAY | 5 | Latency ↕ | 80 |
| COL_FAILURES | 6 | Failures ↕ | 80 |
| COL_REMARKS | 7 | Remarks | 160 |
| COL_MESSAGE | 8 | Message | 160 |
| COL_INDEXID | 9 | IndexId | 120 |
| COL_COUNT | 10 | — | — |

**`GetValueByRow()`**: `case COL_REGION: variant = wxVariant(p.region); break;`

**`Compare()`**: `case COL_REGION: cmp = a.region.compare(b.region); break;`

**`filterBySearch()`**: 增加 `p.region.find(q) != std::string::npos` 搜索条件。

**ProxyListPanel.cpp 列创建**: 在 `AppendTextColumn("Host ↕", ...)` 后插入：

```cpp
listCtrl_->AppendTextColumn("Region", COL_REGION, wxDATAVIEW_CELL_INERT, 70);
```

### 3.7 ShareLink 导出解析

`ShareLink.cpp` 中的 `parseShareLink` 从分享链接解析出 `Profileitem`。Region 在此处设定为空字符串 `""`，后由订阅更新阶段填充。

---

## 4. 风险与缓解措施

| 风险 | 可能性 | 影响 | 缓解 |
|------|--------|------|------|
| 域名后缀地域映射准确率不足（泛域名 .com/.net/.org） | 高 | 中 | 对这些 TLD 降级使用前缀模式匹配；UI 中显示 "未知" 而非误导 |
| 旧数据库缺少 Region 列导致 `fromStmt` 越界读取 | 低 | 高 | 迁移 ALTER TABLE 确保列存在；`fromStmt` 用 `sqlite3_column_count() > 35` 保护 |
| 订阅更新批量导入时性能下降 | 低 | 低 | Region 检测为纯字符串操作，单次 < 1μs；5 万条 < 50ms |
| 批量同步 `migrateProxy` 中 UPDATE SQL 参数绑定顺序紊乱 | 低 | 高 | 枚举每个绑定位置对照 INSERT 列顺序逐个审核 |

---

## 5. 接受标准

1. [ ] 存在 `RegionDetector` 类，提供 `detect(address)` 静态方法
2. [ ] `Profileitem` 结构体包含 `region` 字段，`fromStmt()` 正确读取
3. [ ] 旧数据库升级：ProfileExItemDAO 自动 ALTER TABLE 添加 Region 列
4. [ ] SubitemUpdaterV2 的 INSERT/UPDATE SQL 包含 Region 字段
5. [ ] ProxyListModel 包含 COL_REGION 枚举，ProxyListPanel 显示 Region 列
6. [ ] 搜索支持按 region 过滤
7. [ ] 排序支持按 region 排序
8. [ ] `ShareLink::parseShareLink` 构造的 Profileitem 含空 region
9. [ ] 数据库迁移前 `fromStmt()` 向后兼容
10. [ ] 相关单元测试覆盖：RegionDetector、fromStmt 兼容、INSERT/UPDATE SQL
11. [ ] 测试数据集 `test/guindb.db` 升级方案

---

## 6. 实施计划

### Task 1: RegionDetector 类实现
- **文件**: `include/RegionDetector.h`（新建）, `src/RegionDetector.cpp`（新建）
- **内容**: `detect()` 静态方法，TLD 映射表 + 前缀模式匹配表
- **单元测试**: `tests/test_region_detector.cpp`

### Task 2: ProfileItem 数据模型 + 数据库迁移
- **文件**: `include/Profileitem.h`, `src/ProfileExItemDAO.cpp`（迁移）
- **变更**: Profileitem 结构体新增 `region` 字段；`fromStmt()` 增加第 35 列读取（含列数保护）
- **迁移**: `ProfileExItemDAO::migrateTable()` 中添加 `ALTER TABLE ProfileItem ADD COLUMN Region TEXT`

### Task 3: SubitemUpdaterV2 SQL 变更
- **文件**: `src/SubitemUpdaterV2.cpp`
- **变更**: 
  - 888 行 UPDATE SQL 增加 `Region = ?`
  - 948 行 INSERT SQL 增加 `Region` 列和占位符
  - 对应位置增加 `bindTextOrNull` 绑定
- **全量重新检测**: 在订阅更新循环中调用 `RegionDetector::detect()` 填充 region

### Task 4: ProxyListPanel UI 列添加
- **文件**: `src/ui/ProxyListModel.h`, `src/ui/ProxyListModel.cpp`, `src/ui/ProxyListPanel.cpp`
- **变更**:
  - `ProxyListModel.h`: 插入 `COL_REGION = 3`，调整后续枚举值
  - `ProxyListModel.cpp`: `GetValueByRow()` 增加 `COL_REGION` case；`Compare()` 增加 `COL_REGION` case
  - `ProxyListPanel.cpp`: Host 列后追加 Region 列；`filterBySearch()` 增加 region 搜索

### Task 5: ShareLink 适配 + 测试覆盖
- **文件**: `src/ShareLink.cpp`（region 初始化为空字符串）
- **测试**: `tests/test_region_detector.cpp`（验证 TLD/前缀/未匹配场景）

### Task 6: 文档同步
- **文件**: `docs/INDEX.md`
- **变更**: 添加本 Spec 引用；更新 `docs/plans/project-plans-tracker.md`

---

## 7. 列顺序迁移说明

> **重要**：新增 `COL_REGION`（Host 列后，Port 前）会改变 `COL_COUNT` 值及后续所有列枚举值。所有 switch-case 和列创建代码中的枚举引用会自动适应（枚举值变更），无需手动调整每个 case。

但需关注：
- 任何硬编码的列序号（如排序状态恢复、列宽设置）需同步更新
- 已保存的 `sortState_.column` 值在升级后可能偏移（此问题存在但影响可控，因排序状态为运行时内存状态）

---

## 附录 A: TLD 地域映射表完整版

```
.jp → 日本      .cn → 中国      .hk → 香港
.tw → 台湾      .kr → 韩国      .sg → 新加坡
.my → 马来西亚  .vn → 越南      .th → 泰国
.id → 印尼      .ph → 菲律宾    .in → 印度
.au → 澳大利亚  .nz → 新西兰    .de → 德国
.fr → 法国      .uk → 英国      .nl → 荷兰
.se → 瑞典      .no → 挪威      .fi → 芬兰
.it → 意大利    .es → 西班牙    .ru → 俄罗斯
.pl → 波兰      .cz → 捷克      .ch → 瑞士
.at → 奥地利    .be → 比利时    .dk → 丹麦
.pt → 葡萄牙    .gr → 希腊      .ie → 爱尔兰
.hu → 匈牙利    .ro → 罗马尼亚  .ua → 乌克兰
.bg → 保加利亚  .tr → 土耳其    .sa → 沙特
.ae → 阿联酋    .il → 以色列    .za → 南非
.br → 巴西      .mx → 墨西哥    .ar → 阿根廷
.ca → 加拿大    .us → 美国
```

## 附录 B: 实施顺序建议

由于列枚举值变动（COL_REGION 插入导致 COL_DELAY 从 4→5），**所有文件必须同时提交**，无法分步实施。建议实施顺序：

1. Task 1: RegionDetector（独立，无依赖）
2. Task 2: Profileitem + 迁移（独立，无外部依赖）
3. Task 3: SubitemUpdaterV2 SQL（依赖 Task 1, 2）
4. Task 4: UI 列（依赖 Task 2）
5. Task 5: 测试 + ShareLink（依赖 Task 1, 2）
6. 全量编译 + 集成测试

---

*文档结束*
