# multiple_thread_validproxy - 长期记忆

## 📋 命令行功能清单

### 完整参数列表
| 参数 | 功能 | 描述 | 状态 |
|------|------|------|------|
| `-c, --config <path>` | 配置文件路径 | 默认: config.json | ✅ |
| `-show-sub` | 显示所有订阅 | 列出所有订阅信息 | ✅ |
| `-G, -generator <id>` | 生成 outbound JSON | 根据 indexId 生成配置 | ✅ |
| `-F, -find-proxy` | 查找首个代理 | 找到第一个可用代理即返回 | ✅ |
| `-FMIN, -findminproxy` | 查找最小延迟 | 测试全部后返回延迟最小 | ✅ |
| `-U, -update <id>` | 更新单个订阅 | 更新指定订阅 | ✅ |
| `-UA, -update-all` | 更新所有订阅 | 更新所有启用的订阅 | ✅ |
| `-T, -test-sub <id>` | 测试订阅代理 | 测试订阅中的代理 | ✅ |
| `-TU, -tourl` | 导出分享链接 | 导出 delay>0 的代理到文件 | ✅ |
| `-D, -dedup` | 移除重复代理 | 去重功能 | ✅ |
| `-S, -sync [src[:dst]]` | 同步数据库 | 从源库同步有效代理到目标库 | ✅ |
| `-IS, -import-sub-config <file\|url>` | 批量导入订阅 | 从文件或URL批量导入 | ✅ |
| `-h, --help` | 显示帮助 | 显示所有参数说明 | ✅ |

### 核心模块说明
1. **XrayManager** - xray 实例单例管理
   - `getInstance()` - 获取单例实例
   - `start(count, startPort, apiPort)` - 启动指定数量的 xray 实例
   - `stop()` - 停止所有实例
   - `release()` - 释放单例

2. **ProxyFinder** - 代理查找模块
   - `findFirstWorkingProxy()` - 查找第一个可用代理 (-F)
   - `findWorkingProxy()` - 测试所有代理，返回延迟最小的 (-FMIN)

3. **ConfigGenerator** - 根据 configType 生成 JSON 配置
   - 支持类型: 3=SS, 1=VMess, 5=VLESS, 6=Trojan

4. **ConfigReader** - 读取配置文件

5. **SubitemUpdater** - 订阅更新（含去重逻辑）

6. **ProxyBatchTester** - 多线程并发测试（含黑名单逻辑）

### 导出分享链接 (-TU/-tourl)
- **参数**: -TU 或 -tourl
- **功能**: 导出 proxy (delay>0) 到分享链接文件
- **输出**: bin/exports/share_links_{timestamp}.txt
- **状态**: ✅ 完整实现

### 技术架构
- **语言**: C++ 20/17 (使用 C++17 features like std::optional)
- **类型**: 代理验证工具
- **目标平台**: Windows
- **构建系统**: CMake + Ninja
- **数据库**: SQLite3
- **配置读取**: bin/config.json → database.path
- **日志系统**: Logger 类（统一日志，带时间戳和级别）
- **单例模式**: XrayManager 管理 xray 实例
- **线程安全**: Mutex + Atomic 操作

### 数据库路径配置
| 类型 | 路径 | 说明 |
|------|------|------|
| 测试数据库 | `test/guindb.db` | 开发和测试参考，完整路径: `E:\eclipse_workspace\multiple_thread_validproxy\test\guindb.db` |
| 生产数据库 | `bin/worker/guindb.db` | 实际运行使用的数据库 |

**注意**: 方案文档和验证命令中引用的 `test/guindb.db` 均指测试数据库。

### 构建/测试命令 (Windows + GCC/MinGW)
```bash
mkdir build && cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=debug
cmake --build . --parallel 8
./validproxy.exe
ctest -V
cmake --build . --target clean

# 运行单个测试
ctest -R TestName -V
./build/tests/test_name
```

### 第三方依赖
| 库 | 版本 | 安装方式 | 路径 |
|---|---|---|---|
| Boost | 1.80+ | 手动 | D:\boost_1_88_0 |
| boost/json | - | Boost 已包含 | - |
| curl | latest | vcpkg | - |
| sqlite3 | latest | vcpkg | - |
| xray-core | latest | 手动 | ../Xray-core |

### 代码规范摘要
- **命名**: 类/结构体 PascalCase, 函数/方法 camelCase, 成员变量 snake_case
- **文件**: 头文件 `.h`, 源文件 `.cpp`, 每个类一个头文件
- **类型**: 使用 `std::string`, `std::vector<T>`, `std::optional<T>`, `sqlite3*`
- **错误处理**: `std::cerr` 输出错误, 返回空容器表示错误
- **导入顺序**: 系统头文件 → 第三方库头文件 → 项目内部头文件

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

### 2026-05-05：使用可配置阈值替代硬编码数字5

#### 设计变更
- **问题**：SQL 查询中硬编码阈值5（`consecutive_failures < 5`），修改 `blacklist_threshold` 配置不生效
- **解决方案**：在 `config.json` 的 SQL 中使用占位符 `{blacklist_threshold}`
- **实现**：在 `ConfigReader.cpp` 中添加占位符替换逻辑，读取配置后自动替换

#### 修改内容
- `bin/config.json`：`sql` 和 `sql_by_subid` 查询中的 `< 5` 改为 `< {blacklist_threshold}`
- `src/ConfigReader.cpp`：添加 `replacePlaceholder()` lambda，自动替换占位符
- `bin/worker/config.json`：更新 SQL 查询，移除旧的 `blacklisted` 字段引用

#### 工作原理
1. `config.json` 中配置 `"blacklist_threshold": 5`
2. SQL 查询使用占位符：`... AND (pe.consecutive_failures < {blacklist_threshold})`
3. `ConfigReader.cpp` 读取配置后，自动替换占位符为实际值
4. 用户修改配置后，SQL 查询自动使用新阈值

#### 修改文件清单
- `bin/config.json`
- `src/ConfigReader.cpp`
- `bin/worker/config.json`（运行时配置）

### 2026-05-05：移除 blacklisted 字段，简化黑名单逻辑

#### 设计变更
- **移除** `blacklisted` 字段（冗余，只是 `consecutive_failures >= threshold` 的存储）
- **直接使用** `consecutive_failures < threshold` 判断是否为黑名单
- 简化逻辑：避免冗余字段维护和数据不一致

#### 修改内容
- `ProfileExItem` 结构体：移除 `int blacklisted` 成员
- `fromStmt()`：移除 `blacklisted` 读取（column 6）
- `toString()`：移除 `blacklisted` 输出
- `migrateTable()`：移除添加 `blacklisted` 列的迁移代码
- `updateTestResult()`：移除 `blacklisted` 计算和 INSERT SQL
- `ConfigGenerator::updateProfileExItem()`：移除 `blacklisted` 参数绑定
- `SubitemUpdaterV2::updateProfileItems()`：移除 INSERT 中的 `blacklisted`
- `SubitemUpdaterV2::migrateProfileExItem()`：移除 UPDATE/INSERT 中的 `blacklisted`
- `config.json`：过滤条件改为 `pe.consecutive_failures IS NULL OR pe.consecutive_failures < 5`
- `create_test_db.sql`：CREATE TABLE 移除 `blacklisted` 字段

#### 修改文件清单
- `include/ProfileExItem.h`
- `src/ConfigGenerator.cpp`
- `src/SubitemUpdaterV2.cpp`
- `bin/config.json`
- `create_test_db.sql`

### 2026-05-05：去重机制统一与失败逻辑修复

#### `updateProfileItems()` 返回值逻辑修复
- 空输入（`profiles.empty()`）不再视为失败，返回 `true`
- 全是重复记录（`inserted==0`）不再视为失败，返回 `true`
- 去重跳过是正常行为，只要事务成功提交就返回 `true`

#### `-D` 去重与订阅更新去重机制统一
- **问题**：之前 `-D` 使用 3 字段去重键（`Address+Port+Network`），订阅更新使用 5 字段（`lower(Address)+Port+ConfigType+lower(Id)+lower(Network)`）
- **修复**：修改 `deduplicatePhase2/3/4()` 的 GROUP BY 子句，统一为 5 字段去重键
- **修改文件**：`src/SubitemUpdaterV2.cpp` Phase 2/3/4 的 SQL 语句
- **效果**：避免误判不同协议的代理为重复（如 VMess 和 VLESS 同服务器）

#### 修改文件清单
- `src/SubitemUpdaterV2.cpp`：`updateProfileItems()` 返回值、`deduplicatePhase2/3/4()` GROUP BY 子句

### 2026-05-04：订阅去重与黑名单功能完善

#### ProfileExItem 表结构更新
- 新增字段：`consecutive_failures INTEGER DEFAULT 0`（连续失败次数）、`blacklisted INTEGER DEFAULT 0`（黑名单标记）
- 结构体 `ProfileExItem` 添加对应成员：`int consecutive_failures = 0; int blacklisted = 0;`
- `fromStmt()` 添加新字段读取（column 5、6）
- `toString()` 添加新字段输出
- 添加 `migrateTable()` 静态方法，自动迁移数据库表结构

#### 订阅去重逻辑（SubitemUpdaterV2::updateProfileItems）
- 去重键：`lower(Address) + Port + ConfigType + lower(Id) + lower(Network)`
- 逻辑：检查重复 → 跳过（保留原 IndexId 和测试记录）→ 仅插入新记录
- 移除旧的 DELETE-then-INSERT 逻辑

#### 黑名单功能
- `updateTestResult()`：跟踪连续失败次数，达到阈值（默认5次）标记黑名单
- `config.json` 添加 `blacklist_threshold` 配置项（默认5）
- SQL 查询过滤黑名单代理：`(pe.blacklisted IS NULL OR pe.blacklisted = 0)`

#### 修改文件清单
- `include/ProfileExItem.h` - 结构体、fromStmt、toString、migrateTable
- `src/ConfigGenerator.cpp` - updateProfileExItem() 添加新字段
- `src/SubitemUpdaterV2.cpp` - updateProfileItems() 去重逻辑、migrateProfileExItem() 新字段
- `src/ConfigReader.h` - AppConfig 添加 blacklist_threshold
- `src/ConfigReader.cpp` - 读取 blacklist_threshold 配置
- `bin/config.json` - 添加 blacklist_threshold、更新 SQL 查询
- `create_test_db.sql` - ProfileExItem 表添加新字段、移除 Username/Endpoint

### 2026-04-16：XrayManager 单例模式与 ProxyFinder 模块

#### XrayManager 单例模式
```cpp
// 修改前后对比
// 之前: 每次创建新实例
XrayManager* mgr = new XrayManager(path, workers, log);

// 之后: 使用单例
XrayManager* mgr = XrayManager::getInstance(path, configDir, workers);
mgr->start(count, startPort, apiPort);
XrayManager::release();
```

#### 新增 ProxyFinder 模块
- 独立查找可用代理功能
- 复用 XrayApi.addOutbound 注入配置
- 两个查找模式:
  - `findFirstWorkingProxy()`: 找到第一个即返回 (-F)
  - `findWorkingProxy()`: 测试所有，按延迟排序返回最小的 (-FMIN)

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
