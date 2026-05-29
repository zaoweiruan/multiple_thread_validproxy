# Subitem 批量导入功能设计文档

## 1. 概述

### 1.1 功能描述
通过命令行参数 `-IS` 或 `-import-sub-config <file>`，从 `.txt` 或 `.csv` 文件批量导入订阅(Subitem)到 `SubItem` 数据库表。

### 1.2 目标
- 简化批量订阅添加流程
- 支持简单的文本文件格式（每行一个URL）
- 自动生成合理的默认值（remarks、sort等）
- 避免重复导入相同的URL

### 1.3 测试文件
- 路径：`E:\eclipse_workspace\multiple_thread_validproxy\bin\subitem-20260428.txt`
- 格式：每行一个订阅URL，可选空格后跟备注

---

## 2. 架构设计

### 2.1 实现方案（方案A - 推荐）
扩展现有 `SubitemUpdaterV2` 类，添加 `importSubitemsFromFile()` 方法。

**理由**：
- 与现有的 `syncDatabases()` 实现模式一致
- 直接访问 `SubitemDAO`，无需额外依赖
- 代码集中在订阅管理相关的地方
- 项目已有 1776 行的 SubitemUpdaterV2.cpp，再添加 100-150 行是合理的

### 2.2 文件修改清单
1. **`include/SubitemUpdaterV2.h`** - 添加方法声明
2. **`src/SubitemUpdaterV2.cpp`** - 实现导入逻辑
3. **`src/main.cpp`** - 添加参数解析和执行

### 2.3 数据流
```
文件 → 读取行 → 解析URL+备注 → URL校验 → 检查重复 
  → 生成Subitem → 插入数据库 → 输出摘要
```

---

## 3. 实现细节

### 3.1 ID 生成

**复用现有功能**：`utils::generateUniqueId()` (src/Utils.cpp:21-32)

```cpp
std::string generateUniqueId() {
    static std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::uniform_int_distribution<int> firstDist(0, 1);
    static std::uniform_int_distribution<long long> restDist(0, 999999999999999999);
    
    int first = 4 + firstDist(rng);  // 4 或 5 开头
    long long rest = restDist(rng);
    
    std::ostringstream oss;
    oss << first << std::setw(18) << std::setfill('0') << rest;
    return oss.str();  // 例如：419283746565049382
}
```

**生成的 ID 格式**：19位数字，4或5开头

### 3.2 Remarks 提取逻辑

**函数**：`extractRemarksFromUrl(const std::string& url)`

**规则**：remarks = 域名后第一个路径名称 - 最后文件名（无扩展名）

```cpp
// 示例：
// 输入：https://github.com/a/b.txt
// 处理：
//   1. 解析 URL，提取路径：/a/b.txt
//   2. 域名后第一个路径：a
//   3. 最后文件名（无扩展名）：b
//   4. 组合：a-b
// 输出：a-b

// 边界情况：
// - URL无路径：使用域名（如 https://example.com → example.com）
// - 目录路径：使用最后一段（如 /a/b/ → b）
// - 无文件名：只用路径部分
```

### 3.3 Subitem 字段默认值

| 字段 | 默认值 | 说明 |
|------|--------|------|
| `id` | `utils::generateUniqueId()` | 19位唯一ID（4/5开头） |
| `remarks` | `extractRemarksFromUrl(url)` | 从URL提取：域名后路径+文件名 |
| `url` | 从文件读取 | 订阅URL |
| `moreurl` | `""` | 空字符串 |
| `enabled` | `"1"` | 启用 |
| `useragent` | `""` | 空字符串 |
| `sort` | `当前最大sort + 10` | 递增排序 |
| `filter` | `""` | 空字符串 |
| `autoupdateinterval` | `"1440"` | 自动更新间隔（分钟） |
| `updatetime` | `"0"` | 更新时间（0表示未更新） |
| `converttarget` | `""` | 空字符串 |
| `prevprofile` | `""` | 空字符串 |
| `nextprofile` | `""` | 空字符串 |
| `presocksport` | `""` | 空字符串 |
| `memo` | `""` | 空字符串 |

### 3.4 重复检测

**逻辑**：检查 `url` 字段是否已存在

```cpp
bool isUrlExists(sqlite3* db, const std::string& url) {
    std::string sql = "SELECT COUNT(*) FROM SubItem WHERE Url = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    return exists;
}
```

**处理**：如果存在则跳过（不覆盖）

### 3.5 Sort 值获取

```cpp
int getNextSortValue(sqlite3* db) {
    std::string sql = "SELECT MAX(CAST(Sort AS INTEGER)) FROM SubItem";
    sqlite3_stmt* stmt = nullptr;
    int maxSort = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            maxSort = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return maxSort + 10;  // 递增10
}
```

---

## 4. URL 校验逻辑

### 4.1 校验层次

#### 1️⃣ 基础格式校验（必须）
```cpp
bool isValidUrlFormat(const std::string& url) {
    // 检查是否以 http:// 或 https:// 开头
    if (url.find("http://") != 0 && url.find("https://") != 0) {
        return false;
    }
    
    // 检查是否包含域名（至少有点号分隔）
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    
    std::string hostPart = url.substr(schemeEnd + 3);
    size_t pathStart = hostPart.find('/');
    std::string domain = (pathStart != std::string::npos) 
                        ? hostPart.substr(0, pathStart) 
                        : hostPart;
    
    // 域名至少包含 "x.y" 格式
    return domain.find('.') != std::string::npos;
}
```

#### 2️⃣ 路径有效性校验（可选）
```cpp
bool hasValidPath(const std::string& url) {
    // 检查是否有路径部分（不只是域名）
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    
    std::string hostPart = url.substr(schemeEnd + 3);
    size_t pathStart = hostPart.find('/');
    
    // 如果完全没有路径，可能是无效的订阅 URL
    if (pathStart == std::string::npos || pathStart == 0) {
        return false;  // 只有域名，无路径
    }
    
    return true;
}
```

### 4.2 校验失败处理

```cpp
struct ImportResult {
    int totalLines;
    int successCount;
    int skippedCount;      // URL 重复
    int failedCount;        // URL 格式无效
    int invalidFormat;      // 格式不合法
    std::vector<std::string> failures;  // 失败的 URL + 原因
};

// 在处理每行时：
if (!isValidUrlFormat(line)) {
    result.failedCount++;
    result.invalidFormat++;
    result.failures.push_back(line + " (格式不合法 - 无有效域名)");
    continue;
}

if (!hasValidPath(line)) {
    // 可选：记录警告，但不跳过
    writeLog("WARN: URL只有域名无路径: " + line);
}
```

### 4.3 校验规则总结

| 规则 | 是否必须 | 示例 | 处理 |
|------|----------|------|------|
| 以 http/https 开头 | ✅ | `https://example.com/sub` ✓ | 失败则跳过 |
| 包含有效域名 | ✅ | `example.com` ✓ | 失败则跳过 |
| 有路径部分 | ⚠️ 可选 | `/sub.txt` ✓，只有域名 ⚠️ | 警告但不跳过 |
| URL 可访问 | ❌ 不校验 | 返回200 | 插入后由更新功能检查 |

---

## 5. 命令行参数

### 5.1 参数格式
```bash
# 方式1：使用 -IS 参数
./validproxy -IS /path/to/import.txt

# 方式2：使用 -import-sub-config 参数
./validproxy -import-sub-config /path/to/import.txt
```

### 5.2 参数解析（main.cpp）
```cpp
// 在现有参数解析中添加：
} else if (arg == "-IS" || arg == "-import-sub-config") {
    commandMode = "import-sub";
    if (i + 1 < argc) {
        importFilePath = argv[++i];
    } else {
        std::cerr << "Error: -IS requires a file path" << std::endl;
        return 1;
    }
}
```

### 5.3 执行逻辑（main.cpp）
```cpp
if (commandMode == "import-sub") {
    Logger::init(logDir.string(), commandMode);
    logInfo("starting subitem import...");
    
    update::SubitemUpdaterV2 updater(db, appConfig->xray_executable, *appConfig, 
                                     logOutStream.is_open() ? &logOutStream : nullptr, 
                                     baseDir);
    
    bool result = updater.importSubitemsFromFile(importFilePath);
    logInfo(result ? "import completed" : "import failed");
    
    Logger::flush();
}
```

### 5.4 日志方式
**使用 `SubitemUpdaterV2::log()` 方法**（src/SubitemUpdaterV2.cpp:1028-1035）

**理由**：
1. **一致性**：与现有的 `syncDatabases()` 功能使用相同的日志方式
2. **功能完整**：`log()` 方法同时输出到：
   - 控制台（std::cout）
   - `logOut_` 文件流（如果有效）
   - `Logger` 系统（统一日志，带时间戳和级别）
3. **代码复用**：无需重新实现日志逻辑

**示例用法**：
```cpp
bool SubitemUpdaterV2::importSubitemsFromFile(const std::string& filePath) {
    log("========================================");
    log("Subitem Import Starting...");
    log("File: " + filePath);
    log("========================================");
    
    // ...
    
    if (!isValidUrlFormat(url)) {
        log("ERROR: Invalid URL format: " + url);
        continue;
    }
    
    log("Imported: [" + subitem.remarks + "] " + url);
    
    // ...
    
    log("========================================");
    log("Import Summary: Success=" + std::to_string(successCount) + 
        ", Skipped=" + std::to_string(skippedCount) + 
        ", Failed=" + std::to_string(failedCount));
    log("========================================");
}
```

---

## 6. 输出格式

### 6.1 详细摘要输出

```
========================================
Subitem Import Summary
========================================
Total lines: 10
Success: 8
Skipped (duplicates): 1
Failed (invalid format): 1
========================================
Imported URLs:
  1. [a-v2ray_configs_no1] https://raw.githubusercontent.com/...
  2. [a-v2ray_configs_no2] https://raw.githubusercontent.com/...
  ...
========================================
Skipped URLs (duplicates):
  1. https://example.com/existing (already exists)
========================================
Failed URLs (invalid format):
  1. https://example (格式不合法 - 无有效域名)
========================================
```

### 6.2 ImportResult 结构
```cpp
struct ImportResult {
    int totalLines;
    int successCount;
    int skippedCount;      // URL 重复
    int failedCount;        // URL 格式无效
    int invalidFormat;      // 格式不合法计数
    std::vector<std::string> importedList;   // 成功的URL+remarks
    std::vector<std::string> skippedList;    // 跳过的URL+原因
    std::vector<std::string> failedList;      // 失败的URL+原因
};
```

---

## 7. 关键函数签名

### 7.1 SubitemUpdaterV2.h 声明
```cpp
namespace update {
    class SubitemUpdaterV2 {
    public:
        // ... 现有方法 ...
        
        // 批量导入 Subitem
        bool importSubitemsFromFile(const std::string& filePath, 
                                   const std::string& baseDir = "");
        
    private:
        // ... 现有方法 ...
        
        // 辅助函数
        static std::string extractRemarksFromUrl(const std::string& url);
        static int getNextSortValue(sqlite3* db);
        static bool isUrlExists(sqlite3* db, const std::string& url);
        static bool isValidUrlFormat(const std::string& url);
        static bool hasValidPath(const std::string& url);
    };
}
```

### 7.2 核心实现伪代码
```cpp
bool SubitemUpdaterV2::importSubitemsFromFile(const std::string& filePath, 
                                              const std::string& baseDir) {
    // 1. 打开文件
    std::ifstream file(filePath);
    if (!file.is_open()) {
        log("ERROR: Cannot open file: " + filePath);
        return false;
    }
    
    // 2. 初始化结果
    ImportResult result;
    db::models::SubitemDAO subDao(db_);
    int nextSort = getNextSortValue(db_);
    
    // 3. 逐行读取
    std::string line;
    while (std::getline(file, line)) {
        result.totalLines++;
        
        // 清理换行符
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
        
        if (line.empty()) continue;
        
        // 4. 解析：URL [空格 备注]
        std::string url = line;
        std::string remarks;
        
        size_t spacePos = line.find(' ');
        if (spacePos != std::string::npos) {
            url = line.substr(0, spacePos);
            remarks = line.substr(spacePos + 1);
        }
        
        // 5. URL 校验
        if (!isValidUrlFormat(url)) {
            result.failedCount++;
            result.failures.push_back(url + " (格式不合法)");
            continue;
        }
        
        // 6. 重复检查
        if (isUrlExists(db_, url)) {
            result.skippedCount++;
            result.skippedList.push_back(url + " (already exists)");
            continue;
        }
        
        // 7. 生成 Subitem
        db::models::Subitem subitem;
        subitem.id = utils::generateUniqueId();
        subitem.remarks = remarks.empty() ? extractRemarksFromUrl(url) : remarks;
        subitem.url = url;
        subitem.enabled = "1";
        subitem.autoupdateinterval = "1440";
        subitem.updatetime = "0";
        subitem.sort = std::to_string(nextSort);
        nextSort += 10;
        
        // 8. 插入数据库
        if (insertSubitem(subitem)) {
            result.successCount++;
            result.importedList.push_back("[" + subitem.remarks + "] " + url);
        } else {
            result.failedCount++;
            result.failures.push_back(url + " (database insert failed)");
        }
    }
    
    // 9. 输出摘要
    printImportSummary(result);
    
    return result.failedCount == 0;
}
```

---

## 8. 修改文件详细清单

### 8.1 include/SubitemUpdaterV2.h
- 添加 `importSubitemsFromFile()` 公共方法声明
- 添加辅助函数声明（private区域）

### 8.2 src/SubitemUpdaterV2.cpp
- 实现 `importSubitemsFromFile()` 方法
- 实现 `extractRemarksFromUrl()` 辅助函数
- 实现 `getNextSortValue()` 辅助函数
- 实现 `isUrlExists()` 辅助函数
- 实现 `isValidUrlFormat()` 辅助函数
- 实现 `hasValidPath()` 辅助函数
- 实现 `insertSubitem()` 辅助函数（或内联在 import 方法中）
- 实现 `printImportSummary()` 辅助函数

### 8.3 src/main.cpp
- 在参数解析中添加 `-IS` / `-import-sub-config` 处理
- 在命令执行部分添加 `import-sub` 模式处理

---

## 9. 测试计划

### 9.1 测试用例
1. **正常导入**：包含10个有效URL的txt文件
2. **重复URL**：文件中包含已存在的URL
3. **无效格式**：URL缺少域名、无http/https前缀
4. **只有域名**：URL只有域名无路径（如 https://example.com）
5. **带备注**：URL后跟空格和备注文本
6. **空行处理**：文件包含空行
7. **混合情况**：同时包含有效、重复、无效URL

### 9.2 测试文件
使用 `E:\eclipse_workspace\multiple_thread_validproxy\bin\subitem-20260428.txt` 作为测试输入。

---

## 10. 成功标准

1. ✅ 能够从.txt文件读取并解析订阅URL
2. ✅ 正确生成remarks（从URL提取或文件中指定）
3. ✅ 自动生成唯一ID、设置默认值（enabled=1, autoupdateinterval=1440, updatetime=0, sort递增）
4. ✅ 跳过重复URL（基于url字段检查）
5. ✅ 过滤无效URL格式（无http/https、无有效域名）
6. ✅ 输出详细摘要（成功/跳过/失败统计）
7. ✅ 通过 `-IS` 或 `-import-sub-config` 参数触发
8. ✅ 所有错误和警告正确记录到日志文件

---

## 11. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| URL 校验误判 | 有效URL被过滤 | 提供警告日志，用户可以检查 |
| 文件编码问题 | 读取失败 | 假设UTF-8或系统默认编码 |
| 数据库插入失败 | 部分导入成功 | 事务处理或逐条插入+错误记录 |
| 大文件性能 | 导入慢 | 逐行处理，不需要全部加载到内存 |

---

## 12. 未来扩展

1. **支持CSV格式**：如果文件扩展名为.csv，使用CSV解析器
2. **导入后自动更新**：导入后自动调用订阅更新
3. **配置文件支持**：通过config.json指定默认字段值
4. **导入报告文件**：生成详细的导入报告到文件
5. **批量启用/禁用**：通过命令行参数控制导入后的启用状态

---

**设计文档版本**: v1.0  
**创建日期**: 2026-04-28  
**状态**: 待审核
