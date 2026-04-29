# multiple_thread_validproxy - 长期记忆

## 📋 命令行功能清单

### 导出分享链接 (-TU/-tourl)
- **参数**: -TU 或 -tourl
- **功能**: 导出 proxy (delay>0) 到分享链接文件
- **输出**: bin/exports/share_links_{timestamp}.txt
- **状态**: ✅ 完整实现

### 核心功能参数
| 参数 | 功能 | 描述 |
|------|------|------|
| -c | --config | 指定配置文件路径 |
| -show-sub | --show-sub | 显示所有订阅 |
| -G | -generator | 生成配置 |
| -F | --find-proxy | 查找首个代理 |
| -FMIN | --findminproxy | 查找最小延迟 |
| -U | --update | 更新订阅 |
| -UA | --update-all | 更新全部 |
| -T | --test-sub | 测试订阅 |
| -D | --dedup | 移除重复代理 |
| -S | -sync [src[:dst]] | 同步数据库 |
| -IS | -import-sub-config <file\|url> | 批量导入subitem |
| -h | --help | 显示帮助 |

### 技术架构
- **数据库**: SQLite
- **配置读取**: bin/config.json → database.path
- **日志系统**: Logger 类（统一日志，带时间戳和级别）
- **单例模式**: XrayManager 管理 xray 实例
- **线程安全**: Mutex + Atomic 操作

### 持久化方案
- 会话文件: session-*.md (Markdown)
- 数据库: SQLite binary large object
- 导出文件: bin/exports/ (share links)

## 🔍 -TU 参数详解

**官方描述**: `Export proxies (delay>0) to share links file`

**功能逻辑**:
- 从数据库筛选 `delay > 0` 的有效代理
- 生成可直接分享的链接格式
- 输出到 `bin/exports/share_links_{timestamp}.txt`

**使用示例**:
```bash
# 导出所有有效代理
./validproxy -TU
```

## 🔄 批量导入 Subitem 功能 (-IS/-import-sub-config)

**官方描述**: `Batch import subitems from file or URL`

**功能逻辑**:
- 自动判断输入是**文件路径**还是 **URL**
  - 以 `http://` 或 `https://` 开头 → 作为单个 URL 导入
  - 否则 → 作为文件处理（每行一个 URL）
- 支持 `.txt` 文件，每行格式：`URL [空格备注]`
- 自动生成 remarks（从 URL 提取：域名后路径+文件名）
- 默认值：enabled=1, autoupdateinterval=1440, updatetime=0, sort 递增
- URL 校验（必须 http/https 开头，包含有效域名）
- 重复 URL 跳过（基于 url 字段检查）
- 详细摘要输出（成功/跳过/失败统计）

**使用示例**:
```bash
# 从文件导入
./validproxy -IS bin/subitem-20260428.txt

# 直接导入单个 URL
./validproxy -IS https://raw.githubusercontent.com/owner/repo/main/sub.txt
```

**复用技术**:
1. **ID 生成**: `utils::generateUniqueId()` (src/Utils.cpp:21-32)
   - 生成 19 位数字 ID（4 或 5 开头）
   - 格式：`{4|5}{18位随机数}`
   - 例如：`419283746565049382`
   
2. **统一日志系统**: `Logger` 类（src/Logger.cpp, include/Logger.h）
   - 支持级别：TRACE、DEBUG、INFO、WARN、LOG_ERROR
   - 自动添加时间戳：`[2026-04-29 10:30:43] [INFO] message`
   - 同时输出到：控制台 + 日志文件
   - 线程安全（使用 `std::mutex`）
   
   **日志使用示例**:
   ```cpp
   Logger::init(logDir, prefix);          // 初始化
   Logger::write("message");                 // INFO 级别
   Logger::write("error", LogLevel::LOG_ERROR); // ERROR 级别
   logInfo("message");    // 使用辅助函数（main.cpp）
   logError("error");      // 使用辅助函数（main.cpp）
   ```
   
   **SubitemUpdaterV2 中的日志**:
   - 使用 `Logger::write()` 而非 `SubitemUpdaterV2::log()`
   - 确保日志统一到 Logger 系统

**实现文件**:
- `include/SubitemUpdaterV2.h` - `importSubitemsFromFile()`、`importSingleUrl()` 声明
- `src/SubitemUpdaterV2.cpp` - 实现导入逻辑、辅助函数
- `src/main.cpp` - 参数解析、`-IS` 处理
- `src/Utils.cpp` - `generateUniqueId()` 复用

## 🔄 数据库同步功能 (-S/-sync)

**官方描述**: `Sync valid proxies from source to target DB`

**功能逻辑**:
- 从源库查询 `delay > 0` 的有效代理（静态筛选）
- 对每条代理：
  - 检查订阅（subid）在目标库是否存在，不存在则先插入
  - 检查代理 indexid 是否存在，存在则 UPDATE，不存在则 INSERT
- 同时迁移 `profile_exitems` 扩展信息

**参数格式**:
```bash
# 方式1：仅指定源数据库（目标库从配置读取）
./validproxy -S /path/to/source.db

# 方式2：同时指定源和目标（命令行优先）
./validproxy -S /path/to/source.db:/path/to/target.db
```

**配置文件**:
```json
{
  "sync": {
    "source_db": "/path/to/source.db",
    "target_db": "/path/to/target.db"
  }
}
```

**优先级**:
1. 命令行 `-S source:target`（最高）
2. 命令行 `-S source`（target 从配置读取）
3. 配置文件 `sync.source_db` + `sync.target_db`

## 🖥️ 日志系统

### Logger 类（统一日志）

**文件**: `include/Logger.h`、`src/Logger.cpp`

**功能**:
- 静态方法，全局可用
- 支持 5 个级别：TRACE、DEBUG、INFO、WARN、LOG_ERROR
- 自动添加时间戳和级别前缀
- 同时输出到控制台和日志文件
- 线程安全（使用 `std::mutex`）

**使用示例**:
```cpp
// 初始化（在 main.cpp 中）
Logger::init(logDir, commandMode);

// 写入日志
Logger::write("message");                     // INFO 级别
Logger::write("error", LogLevel::LOG_ERROR);    // ERROR 级别
Logger::write("debug", LogLevel::DEBUG);       // DEBUG 级别

// 辅助函数（main.cpp）
logInfo("message");     // 自动带时间戳和模式前缀
logError("error");     // 自动带时间戳和模式前缀

// 刷新和关闭
Logger::flush();
Logger::close();
```

**日志文件位置**: `bin/{commandMode}_{timestamp}.log`

### SubitemUpdaterV2::log() 方法（已弃用）

**文件**: `src/SubitemUpdaterV2.cpp:1028-1035`

**注意**: 新代码应直接使用 `Logger::write()`，而非此方法。

## 🔧 常用工具和模式

### ID 生成
```cpp
// 文件：src/Utils.cpp:21-32
std::string utils::generateUniqueId() {
    static std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::uniform_int_distribution<int> firstDist(0, 1);
    static std::uniform_int_distribution<long long> restDist(0, 999999999999999999);
    
    int first = 4 + firstDist(rng);  // 4 或 5 开头
    long long rest = restDist(rng);
    
    std::ostringstream oss;
    oss << first << std::setw(18) << std::setfill('0') << rest;
    return oss.str();  // 19 位数字
}
```

**使用场景**:
- Profileitem 的 `indexid`
- Subitem 的 `id`（批量导入功能）

### URL 解析和校验
- `isValidUrlFormat(url)` - 检查 http/https 开头 + 有效域名
- `hasValidPath(url)` - 检查是否有路径部分
- `extractRemarksFromUrl(url)` - 从 URL 提取 remarks

## 📝 最近变更

### 2026-04-29：批量导入 Subitem 完善
- 自动判断输入是文件还是 URL
- 添加 `importSingleUrl()` 方法
- 统一使用 Logger 日志系统
- 修复：无效 URL 格式正确处理

### 2026-04-28：批量导入 Subitem 功能
- 实现 `-IS`/`-import-sub-config` 参数
- 支持从 `.txt` 文件批量导入订阅
- 自动生成 remarks、ID、默认值
- URL 校验和重复检测

### 2026-04-28：日志修复
- 修改 `writeLog()` 确保错误总是写入文件
- 添加明确的错误消息："failed to build conf"、"注入xray outbound 错误"
- 修复问题：之前错误只显示在控制台，不写入日志文件

### 2026-04-28：CoreType NULL 处理
- 在 `migrateProxy()` 的 UPDATE 和 INSERT 语句中
- 当 CoreType 为空时使用 `sqlite3_bind_null()` 写入 NULL
- 确保 v2rayN 正确识别代理

### 2026-04-24：数据库同步功能
- 实现 `-S`/`-sync` 参数
- 从源数据库同步有效代理（delay>0）到目标数据库
- 迁移 SubItem、ProfileItem、ProfileExItem 三张表

## 🗂️ 相关文件索引

### 核心头文件
- `include/SubitemUpdaterV2.h` - 订阅更新器（含导入功能）
- `include/Utils.h` - 工具函数（ID 生成）
- `include/Logger.h` - 统一日志系统
- `include/Subitem.h` - Subitem 数据模型
- `include/Profileitem.h` - Profileitem 数据模型

### 实现文件
- `src/SubitemUpdaterV2.cpp` - 订阅更新 + 导入 + 同步
- `src/Utils.cpp` - `generateUniqueId()` 实现
- `src/Logger.cpp` - Logger 类实现
- `src/main.cpp` - 主程序 + 参数解析

### 文档
- `docs/superpowers/specs/2026-04-28-subitem-batch-import-design.md` - 批量导入设计文档
- `memory/project_knowledge.md` - 本文件（长期记忆）
