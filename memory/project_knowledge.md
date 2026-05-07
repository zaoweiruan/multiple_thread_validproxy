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

### 2026-05-06：修复日志双重时间戳及清理 debug 输出

#### 问题
- **双重时间戳**：`logInfo()`/`logError()` 手动添加 `[timestamp] [mode]` 前缀，然后 `Logger::write()` 又添加一次时间戳，导致日志出现重复时间戳（如 `[2026-05-06 08:43:24] [INFO] [2026-05-06 08:43:24] [sync] ...`）
- **多余 debug 输出**：`ConfigReader.cpp` 和 `main.cpp` 中存在多处 `std::cerr`/`fprintf(stderr, ...)` debug 输出，未使用统一日志系统
- **未使用函数**：`getTimestamp()` 函数因日志修复后不再使用，产生编译 warning

#### 修复内容
- 重写 `logInfo()`/`logError()`，移除手动时间戳和模式前缀，统一由 `Logger::write()` 处理
- 移除 `ConfigReader.cpp` 中所有 `std::cerr` debug 输出（sync 配置读取相关）
- 移除 `main.cpp` sync 段中的 `fprintf(stderr, "[DEBUG] ...")` 调试代码
- 删除未使用的 `getTimestamp()` 函数

#### 修改文件
- `src/main.cpp`：`logInfo()`、`logError()` 重写，删除 `getTimestamp()`
- `src/ConfigReader.cpp`：移除 sync 段 debug 输出

#### 验证结果
- ✅ 构建成功，无 warning
- ✅ 日志格式正常：`[2026-05-06 08:43:24] [INFO] [sync] message`

### 2026-05-06：废弃 update_subscription 和 check_auto_update_interval 字段，完善 Logger 配置

#### 问题
- **废弃字段未清理**：`update_subscription`（ConfigReader.h:21）和 `check_auto_update_interval`（ConfigReader.h:23）从未被使用，属于死代码
  - `update_subscription`：声明但未解析
  - `check_auto_update_interval`：已解析但从未被读取使用
- **Logger 配置未应用**：`config.json` 中包含 `level`、`console_level`、`file_level`，但 `ConfigReader.cpp` 未解析，导致配置无效
- **Logger::setFileEnabled() 缺失**：以下命令模式未调用 `Logger::setFileEnabled()`，导致 `log_enabled` 配置不生效：
  - `-TU` (tourl) 模式
  - `-IS` (import-sub) URL 分支
  - `-T`/`-U`/`-UA` 模式

#### 修复内容
1. **废弃字段清理**：
   - 删除 `AppConfig` 中的 `update_subscription` 和 `check_auto_update_interval` 字段
   - 删除 `ConfigReader.cpp` 中 `check_auto_update_interval` 的解析代码（第 137-144 行）
   - 删除 `bin/config.json` 和 `bin/worker/config.json` 中的 `update_subscription` 和 `check_auto_update_interval` 键

2. **Logger 级别配置解析与应用**：
   - `AppConfig` 新增字段：`log_level`、`log_console_level`、`log_file_level`
   - `ConfigReader.cpp` 添加对 `log.level`、`log.console_level`、`log.file_level` 的解析
   - `main.cpp` 中添加 `Logger::setFileLevel()` 和 `Logger::setConsoleLevel()` 调用

3. **完善 Logger::setFileEnabled() 调用**：
   - `-TU` 模式：在 `appConfig` 加载后添加调用
   - `-IS` URL 分支：在 `appConfig` 加载后添加调用
   - `-T`/`-U`/`-UA` 模式：在 `appConfig` 加载后添加调用

#### 修改文件清单
- `include/ConfigReader.h`：删除 2 个废弃字段，新增 3 个日志级别字段
- `src/ConfigReader.cpp`：删除 `check_auto_update_interval` 解析，新增日志级别解析
- `src/main.cpp`：添加 3 处 `Logger::setFileEnabled()`，应用日志级别配置
- `bin/config.json`：删除废弃字段
- `bin/worker/config.json`：删除废弃字段

#### 验证结果
- ✅ 构建成功（无新增 warning）
- ✅ `update_subscription` 和 `check_auto_update_interval` 已完全移除
- ✅ Logger 配置（`level`、`console_level`、`file_level`）现在会被解析和应用
- ✅ 所有命令模式的 `log_enabled` 配置现在都会生效

### 2026-05-06：修复 LOG_ERROR 解析 bug，补全所有命令模式日志级别设置

#### 问题
- **LOG_ERROR 解析 bug**：`Logger::stringToLevel()` 将 `"LOG_ERROR"` 转为小写 `"log_error"`，但匹配的是 `"error"`，导致 `config.json` 中 `"file_level": "LOG_ERROR"` 被错误解析为 `LogLevel::INFO`
- **日志级别未在所有模式应用**：以下命令模式未调用 `setFileLevel()` 和 `setConsoleLevel()`：
  - `generator`、`show-sub`、`find-proxy`/`findminproxy`、`sync`、`dedup`、`import-sub` (文件分支)、`default` 模式
- **config.json 格式问题**：`"file_level": "LOG_ERROR"` 应改为 `"ERROR"` 以匹配枚举命名

#### 修复内容
1. **修复 `stringToLevel()`**：添加对 `"log_"` 前缀的处理（如 `"log_error"` → `"error"`），同时兼容带和不带 `LOG_` 前缀的格式
2. **补全所有命令模式日志级别设置**：在 `Logger::init()` + `setFileEnabled()` 后添加 `setFileLevel()` 和 `setConsoleLevel()` 调用
3. **修正 config.json**：`"file_level": "LOG_ERROR"` → `"ERROR"`（`bin/config.json` 和 `bin/worker/config.json`）

#### 修改文件清单
- `src/Logger.cpp`：修复 `stringToLevel()` 函数（添加 `log_` 前缀移除逻辑）
- `src/main.cpp`：为 `generator`、`show-sub`、`find-proxy`、`sync`、`dedup`、`import-sub`(文件)、`default` 模式添加日志级别设置
- `bin/config.json`：修正 `file_level` 值为 `"ERROR"`
- `bin/worker/config.json`：修正 `file_level` 值为 `"ERROR"`

#### 验证结果
- ✅ 构建成功，无 warning
- ✅ `"LOG_ERROR"` 和 `"ERROR"` 都能正确解析为 `LogLevel::LOG_ERROR`
- ✅ 所有 10 个命令模式都正确应用日志级别配置
- ✅ `config.json` 中的 `file_level` 格式现已标准化

### 2026-05-06：修复 -S 参数解析 Windows 盘符冒号冲突

#### 问题
- **根因**：`-S` 参数使用 `syncParam.find(':')` 查找第一个冒号作为源/目标分隔符，但 Windows 绝对路径中的盘符（如 `E:\path`）也包含冒号
- **错误示例**：
  - `-S E:\source.db` → 错误解析为 source=`E`, target=`\source.db`
  - `-S E:\s.db:E:\t.db` → 错误解析为 source=`E`, target=`\s.db:E:\t.db`
- **后果**：`sourceDb` 或 `targetDb` 为空，触发 `Source or target database not specified` 错误

#### 修复内容
- 重写冒号查找逻辑，迭代查找所有冒号并跳过 Windows 盘符冒号（`[A-Za-z]:\` 或 `[A-Za-z]:/` 模式）
- 仅将非盘符冒号视为源/目标分隔符

#### 修改文件
- `src/main.cpp`：修复 `-S` 参数解析逻辑（约第 120-154 行）

#### 验证结果
- ✅ 构建成功
- ✅ `-S E:\source.db` 正确解析为 source=`E:\source.db`, target=``
- ✅ `-S E:\s.db:E:\t.db` 正确解析为 source=`E:\s.db`, target=`E:\t.db`

### 2026-05-05：修复 ConfigReader 配置解析

#### 问题
- **根本原因**：`ConfigReader::load()` 只解析 `database` 节，未解析 `xray`、`test`、`log`、`dedup`、`notification` 等节
- **导致后果**：`config.xray_executable` 始终为空字符串，调用 `CreateProcessA()` 时参数无效（错误码 87 = ERROR_INVALID_PARAMETER）
- **错误日志**：`ERROR: Failed to create process` + `ERROR_INVALID_PARAMETER (87)`

#### 修复内容
- 完善 `ConfigReader::load()` 函数，添加对所有配置节的解析：
  - `xray` 节：`executable`、`workers`、`start_port`、`api_port`
  - `test` 节：`url`、`timeout_ms`
  - `log` 节：`enabled`、`network_failures`
  - `subscription` 节：`priority_mode`、`check_auto_update_interval`
  - `dedup` 节：`enabled`、`dedup_after_update`、`blacklist_threshold`、`subids`
  - `notification` 节：`enabled`、`on_update`、`on_test`
- 添加默认值处理（else 分支）

#### 修改文件
- `src/ConfigReader.cpp`：重写 `ConfigReader::load()` 函数

#### 验证结果
- ✅ 配置加载成功：`Config loaded successfully`
- ✅ `xray_executable` 正确设置：`E:/v2rayN-windows-64/bin/xray/xray.exe`
- ✅ 无 `ERROR_INVALID_PARAMETER (87)` 错误

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
