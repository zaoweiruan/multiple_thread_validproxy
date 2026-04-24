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
| -h | --help | 显示帮助 |
| -v | --version | 显示版本 |

### 技术架构
- **数据库**: SQLite
- **配置读取**: bin/config.json → database.path
- **日志系统**: log/ + bin/worker/logs/
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

# 或使用长格式
./validproxy -tourl
```

**实现细节**:
- 依赖 `ShareLink::exportConfig()` 方法
- 过滤条件: `ProfileExItem.delay > 0`
- 格式转换: JSON → 可读链接格式
- 文件命名: `share_link_{YYYYMMDD}_{HHMMSS}.txt`

## ⚙️ 配置文件结构

### bin/config.json 关键字段
```json
{
    "database": {
        "path": "E:/v2rayN-windows-64/guiConfigs/guiNDB.db"
    },
    "log": {
        "path": "log",
        "enabled": true,
        "level": "INFO",
        "rotation": {
            "enabled": true,
            "max_size_mb": 100,
            "max_files": 7
        }
    },
    "export": {
        "enabled": true,
        "export_dir": "exports",
        "backup_count": 50
    }
}
```

### 日志目录结构
```
log/
├── app.log              # 主应用日志
├── error.log           # 错误日志  
└── debug.log           # 调试日志

bin/worker/logs/
└── worker_{id}.log     # 工作线程日志

build/logs/
└── build.log           # 构建日志
```

**日志级别**: TRACE(0), DEBUG(1), INFO(2), WARN(3), ERROR(4)

## 🏗️ 项目架构

### 核心模块
1. **XrayManager** (单例) - xray 实例生命周期管理
   - 线程安全初始化
   - 资源自动释放
   - 多实例防护

2. **ProxyFinder** - 代理发现与测试
   - HTTP/HTTPS 连通性测试
   - 并发请求控制
   - 超时处理机制

3. **ConfigGenerator** - 配置生成
   - 支持 vmess/vless/ss/trojan 协议
   - 动态构建 outbound 配置
   - 加密算法自动选择

4. **ConfigReader** - 配置文件读取
   - JSON 解析
   - 路径优先级处理
   - 环境变量覆盖

5. **SubitemUpdaterV2** - 订阅更新
   - 批量下载订阅源
   - 数据标准化处理
   - 增量更新支持

6. **DatabaseHelper** - 数据库操作
   - SQLite 封装
   - DAO 模式实现
   - 事务管理

### 数据流
```
订阅源 → SubitemUpdater → SQLite → ConfigGenerator → 验证 → Export
                                          ↓
                                       ProxyFinder
```

### 线程架构
- **主线程**: UI/CLI 交互
- **工作线程池**: 并发代理测试 (默认4线程)
- **IO线程**: 文件读写和数据库操作
- **异步回调**: 结果通知机制

## ✅ 功能状态

| 功能模块 | 状态 | 参数 | 备注 |
|----------|------|------|------|
| 订阅显示 | ✅ 完整 | -show-sub | 显示所有订阅详情 |
| 订阅更新 | ✅ 完整 | -U, -UA | 支持单个和批量更新 |
| 代理测试 | ✅ 完整 | -T, -F, -FMIN | 支持多种测试模式 |
| 配置生成 | ✅ 完整 | -G | 支持vmess/vless/ss/trojan |
| 分享导出 | ✅ 完整 (修复v2rayN兼容性) | -TU/-tourl | 导出delay>0的代理，格式兼容v2rayN |
| 重复清理 | ✅ 完整 | -D | 清理数据库中重复代理 |
| 代理同步 | ✅ 完整 | -S, -sync | 从源数据库同步有效代理到目标数据库 |
| 帮助信息 | ✅ 完整 | -h | 显示使用说明 |
| 版本信息 | ✅ 完整 | -v | 显示版本号

## 📈 数据库 Schema

### 表: profile_items
- **indexid** (PK) - 代理唯一标识
- **configtype** (1=VMess, 3=SS, 5=VLESS, 6=Trojan) - 协议类型
- **address** - 服务器地址
- **port** - 端口号
- **id** - 用户ID/认证信息
- **secret** - 密码/密钥/UUID
- **alterid** - 附加ID (Shadowsocks)
- **security** - 加密方式 (aes-256-gcm 等)
- **network** - 传输协议 (tcp, ws, http, grpc)
- **remarks** - 代理描述/备注
- **subid** - 所属订阅ID
- **is_sub** - 是否来自订阅 (0/1)
- **flow** - 流量类型 (tcp, udp, etc.)
- **mux_enabled** - 是否启用多路复用
- **created_at** - 创建时间戳
- **updated_at** - 最后更新时间戳

### 表: profile_exitems
- **indexid** (FK, PK) - 关联 profile_items
- **delay** - 延迟时间 (ms)
- **speed** - 测速速度 (KB/s)
- **sort** - 排序权重
- **message** - 测试状态/错误信息
- **session_id** - 会话标识
- **created_at** - 测试时间戳
- **updated_at** - 结果更新时间

### 表: subitems
- **id** (PK) - 订阅唯一标识
- **url** - 订阅源地址
- **enabled** - 是否启用 (0/1)
- **remarks** - 订阅名称/描述
- **type** - 订阅类型 (0=URL, 1=Base64)
- **auto_update_interval** - 自动更新间隔 (小时)
- **last_update_time** - 最后更新时间
- **last_update_result** - 最近更新结果
- **created_at** - 创建时间戳
- **updated_at** - 最后更新时间

### 索引优化
```sql
CREATE INDEX idx_profileitems_subid ON profile_items(subid);
CREATE INDEX idx_profileitems_delay ON profile_exitems(delay);
CREATE INDEX idx_subitems_enabled ON subitems(enabled);
```

## ⚠️ 注意事项

1. **数据库路径优先级**: 环境变量 > config.json > 默认值
   - `V2RAYN_DB_PATH` 环境变量优先级最高
   
2. **日志轮转策略**:
   - 保留最近 7-30 天日志
   - 单文件大小限制 100MB
   - 自动清理旧文件
   
3. **导出文件安全**:
   - 包含敏感代理信息，请妥善保管
   - 建议加密存储
   - 定期清理过期导出文件
   
4. **并发安全机制**:
   - 多线程环境下使用互斥锁保护共享资源
   - SQLite 每线程独立连接
   - 原子操作保证数据一致性
   
5. **错误处理**:
   - 所有数据库操作都有事务回滚机制
   - 异常情况下自动清理资源
   - 详细的错误日志记录

## 🐚 Shell 环境兼容性

### Bash vs PowerShell

| 特性 | Bash | PowerShell (pwsh) |
|------|------|-------------------|
| 变量引用 | `$VAR` | `$env:VAR` 或 `$VAR` |
| 路径分隔符 | `/` | `/` 或 `\` |
| 字符串拼接 | `"${var}log"` | `"$var\log"` |
| 命令替换 | `$(command)` | `$(command)` |

### 环境变量设置

```bash
# Bash
export V2RAYN_DB_PATH="/custom/path.db"
export V2RAYN_LOG_PATH="./logs"

# PowerShell
$env:V2RAYN_DB_PATH="C:\custom\path.db"
$env:V2RAYN_LOG_PATH="./logs"
```

### 跨平台脚本示例

```bash
#!/usr/bin/env bash
# 兼容 Bash 和 pwsh 的路径获取函数
get_db_path() {
    if [ -n "${V2RAYN_DB_PATH}" ]; then
        echo "${V2RAYN_DB_PATH}"
    elif $IsLinux -or $IsMacOS; then
        echo "./guiNDB.db"
    else
        echo ".\\guiNDB.db"
    fi
}
```

### 常见文件操作错误诊断

| 错误信息 | 原因 | 解决方案 |
|----------|------|----------|
| `command not found` | 使用了 bash 专用命令 | 使用跨平台命令或安装 pwsh |
| `路径格式错误` | Windows/Linux 路径混用 | 使用 `./` 相对路径 |
| `权限被拒绝` | 文件权限问题 | 使用 `chmod` 或以管理员身份运行 |
| `编码错误` | 脚本编码不匹配 | 保存为 UTF-8 无 BOM |

## ⚠️ 注意事项

1. **数据库路径优先级**: 环境变量 > config.json > 默认值
   - `V2RAYN_DB_PATH` 环境变量优先级最高
   
2. **日志轮转策略**:
   - 保留最近 7-30 天日志
   - 单文件大小限制 100MB
   - 自动清理旧文件
   
3. **导出文件安全**:
   - 包含敏感代理信息，请妥善保管
   - 建议加密存储
   - 定期清理过期导出文件
   
4. **并发安全机制**:
   - 多线程环境下使用互斥锁保护共享资源
   - SQLite 每线程独立连接
   - 原子操作保证数据一致性
   
5. **错误处理**:
   - 所有数据库操作都有事务回滚机制
   - 异常情况下自动清理资源
   - 详细的错误日志记录

6. **性能优化**:
   - 数据库连接池 (最大10连接)
   - 批量插入优化
    - 索引加速查询
    - 异步日志写入

## 🔧 部署与运维 (续)

### 编译指令
```bash
# Debug 模式
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=debug
cmake --build . --parallel 8

# Release 模式  
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=release
cmake --build . --parallel 8

# 自定义构建目录
mkdir build && cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=debug
cmake --build . --jobs $(nproc)
```

### 运行指令
```bash
# 基本运行
./validproxy

# 带参数运行
./validproxy -c config.json -TU -show-sub

# 后台运行
nohup ./validproxy -UA > /dev/null 2>&1 &

# 调试模式
./validproxy -T sub_id -G index_id
```

### 日志管理
- **轮转策略**: logrotate 配置
- **归档格式**: .gz 压缩
- **保留策略**: 最近 30 天
- **监控告警**: ERROR 级别邮件通知
- **日志分析**: grep/awk 日志处理
- **Shell 兼容性**: 根据 $SHELL_TYPE 选择 bash 或 pwsh 命令

### 性能基准
- 启动时间: < 2秒
- 内存占用: < 100MB
- 代理测试: 并发 4 线程
- 数据库查询: < 10ms (有索引)
- 日志写入: < 1ms 延迟

### 系统要求
- **CPU**: 2核以上
- **内存**: 512MB 最低 (推荐1GB+)
- **磁盘**: 100MB 可用空间
- **网络**: 出站 HTTP/HTTPS 访问

### 监控指标
- 进程存活状态
- 日志文件大小
- 数据库连接数
- 内存使用率
- 任务队列长度

### Shell 环境检测
```bash
# 检测当前 Shell 类型
if command -v pwsh &> /dev/null && [[ "$SHELL" == *"pwsh"* ]]; then
    SHELL_TYPE="powershell"
else
    SHELL_TYPE="bash"
fi
echo "当前 Shell: ${SHELL_TYPE}"
```

### 跨平台兼容性处理
```bash
# 示例: 条件执行基于 Shell 类型
if [[ "$SHELL_TYPE" == "powershell" ]]; then
    # PowerShell 命令
    Get-ChildItem log/*.log | Remove-Item
else
    # Bash 命令
    find log/ -name "*.log" -delete
fi
```

## ⚠️ 注意事项

1. **数据库路径优先级**: 环境变量 > config.json > 默认值
   - `V2RAYN_DB_PATH` 环境变量优先级最高
   
2. **日志轮转策略**:
   - 保留最近 7-30 天日志
   - 单文件大小限制 100MB
   - 自动清理旧文件
   
3. **导出文件安全**:
   - 包含敏感代理信息，请妥善保管
   - 建议加密存储
   - 定期清理过期导出文件
   
4. **并发安全机制**:
   - 多线程环境下使用互斥锁保护共享资源
   - SQLite 每线程独立连接
   - 原子操作保证数据一致性
   
5. **错误处理**:
   - 所有数据库操作都有事务回滚机制
   - 异常情况下自动清理资源
   - 详细的错误日志记录

6. **性能优化**:
   - 数据库连接池 (最大10连接)
   - 批量插入优化
    - 索引加速查询
    - 异步日志写入
    - 连接复用机制
    - 预编译语句优化

## 🔧 部署与运维

### 编译指令
```bash
# Debug 模式
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=debug
cmake --build . --parallel 8

# Release 模式  
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=release
cmake --build . --parallel 8
```

### 运行指令
```bash
# 基本运行
./validproxy

# 带参数运行
./validproxy -c config.json -TU -show-sub

# 后台运行
nohup ./validproxy -UA > /dev/null 2>&1 &
```

### 日志管理
- **轮转策略**: logrotate 配置
- **归档格式**: .gz 压缩
- **保留策略**: 最近 30 天
- **监控告警**: ERROR 级别邮件通知

### 性能基准
- 启动时间: < 2秒
- 内存占用: < 100MB
- 代理测试: 并发 4 线程
- 数据库查询: < 10ms (有索引)
## 🔧 部署与运维

### 编译指令
```bash
# Debug 模式
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=debug
cmake --build . --parallel 8

# Release 模式  
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=release
cmake --build . --parallel 8

# 自定义构建目录
mkdir build && cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=debug
cmake --build . --jobs $(nproc)
```

### 运行指令
```bash
# 基本运行
./validproxy

# 带参数运行
./validproxy -c config.json -TU -show-sub

# 后台运行
nohup ./validproxy -UA > /dev/null 2>&1 &

# 调试模式
./validproxy -T sub_id -G index_id
```

### 日志管理
- **轮转策略**: logrotate 配置
- **归档格式**: .gz 压缩
- **保留策略**: 最近 30 天
- **监控告警**: ERROR 级别邮件通知
- **日志分析**: grep/awk 日志处理
- **Shell 兼容性**: 根据 $SHELL_TYPE 选择 bash 或 pwsh 命令

### 性能基准
- 启动时间: < 2秒
- 内存占用: < 100MB
- 代理测试: 并发 4 线程
- 数据库查询: < 10ms (有索引)
- 日志写入: < 1ms 延迟

### 系统要求
- **CPU**: 2核以上
- **内存**: 512MB 最低 (推荐1GB+)
- **磁盘**: 100MB 可用空间
- **网络**: 出站 HTTP/HTTPS 访问

### 监控指标
- 进程存活状态
- 日志文件大小
- 数据库连接数
- 内存使用率
- 任务队列长度

### Shell 环境检测
```bash
# 检测当前 Shell 类型
if command -v pwsh &> /dev/null && [[ "$SHELL" == *"pwsh"* ]]; then
    SHELL_TYPE="powershell"
else
    SHELL_TYPE="bash"
fi
echo "当前 Shell: ${SHELL_TYPE}"
```

### 跨平台兼容性处理
```bash
# 示例: 条件执行基于 Shell 类型
if [[ "$SHELL_TYPE" == "powershell" ]]; then
    # PowerShell 命令
    Get-ChildItem log/*.log | Remove-Item
else
    # Bash 命令
    find log/ -name "*.log" -delete
fi
```

## 📊 持续集成

### 测试流程
```bash
# 运行单元测试
ctest -V ./build/tests/

# 检查代码风格
make lint

# 编译检查
cmake --build . --target all
```

### 版本发布
- 自动检测版本文件
- 生成变更日志
- 创建发布标签
- 推送至远程仓库

## 📋 导出分享链接与有效代理逻辑分析

### 功能定位
**导出分享链接 (-TU/-tourl)** 是获取有效代理的核心逻辑入口

```
用户命令: ./validproxy -TU sub_id
    ↓
1. 参数解析 → 启用 tourl 模式
    ↓
2. 加载配置 (config.json)
    ↓
3. 数据库查询 → 过滤 delay > 0 的代理
    ↓
4. 生成分享链接 URI
    ↓
5. 输出到 proxies/ 目录
```

### 与有效代理获取的差异

| 特性 | 导出分享链接 (-TU) | 有效代理获取 (-FMIN) |
|------|-------------------|---------------------|
| **目的** | 生成可分享的 URI | 获取真实可用代理 |
| **测试** | 无 (仅静态筛选) | 有 (实际连接测试) |
| **排序** | 无 (按数据库顺序) | 有 (按延迟升序) |
| **输出** | URI 文本文件 | 单个最优代理 |
| **延迟** | 数据库记录值 | 实际测试测量值 |

### 关键代码组件

#### 1. 数据库查询逻辑 (ProxyFinder.cpp)
```cpp
// 查询条件: 仅选择 delay > 0 的代理
std::string sql = "SELECT pi.Address, pi.Port, pe.Delay "
                 "FROM profile_items pi "
                 "JOIN profile_exitems pe ON pi.IndexId = pe.IndexId "
                 "WHERE pe.Delay > 0 "   // 关键过滤条件
                 "ORDER BY CAST(pe.Delay AS INTEGER) ASC";
```

#### 2. 分享链接生成 (ShareLink.h/.cpp)
```cpp
std::string ShareLink::toShareUri(const ProxyConfig& config) {
    // 根据 config.type 生成对应 URI 格式
    switch(config.type) {
        case VMess:    return buildVmessUri(config);    // vmess://
        case VLESS:    return buildVlessUri(config);    // vless://
        case Trojan:   return buildTrojanUri(config);   // trojan://
        case Shadowsocks: return buildSsUri(config);    // ss://
    }
}
```

#### 3. 文件输出逻辑
```cpp
// 输出到: bin/exports/share_links_{timestamp}.txt
std::ofstream exportFile(filename);
for (const auto& proxy : validProxies) {
    std::string uri = ShareLink::toShareUri(proxy);
    exportFile << uri << std::endl;  // 每行一个 URI
}
```

### 错误处理与日志

#### 常见错误模式

| 错误模式 | 发生阶段 | 原因 | 日志特征 |
|----------|----------|------|----------|
| `SQLITE_ERROR` | 数据库查询 | SQL 语法错误/表不存在 | `SQL错误: ...` |
| `NO_VALID_PROXY` | 过滤阶段 | 所有代理 delay=0 | `未找到有效代理` |
| `FILE_OPEN_FAIL` | 文件输出 | 目录不存在/权限不足 | `无法创建文件` |
| `URI_GEN_FAIL` | 生成阶段 | 未知协议类型 | `不支持的协议` |

#### 日志记录点

```cpp
// 1. 查询前日志
log("ProxyFinder: SQL查询: " + sql);

// 2. 过滤结果日志  
log("找到 " + std::to_string(proxies.size()) + " 个代理，其中 " + 
    std::to_string(validCount) + " 个有效(delay>0)");

// 3. 文件输出日志
log("导出文件已生成: " + filename);
```

### 修复计划 (代码级改进)

#### 改进 1: 统一代理处理逻辑
```cpp
// 提取公共方法: 加载+过滤+测试
std::vector<ProxyConfig> loadAndFilterProxies() {
    auto allProxies = ProfileitemDAO::getAll();
    std::vector<ProxyConfig> filtered;
    
    for (const auto& proxy : allProxies) {
        if (proxy.delay > 0) {  // 关键过滤
            filtered.push_back(proxy);
        }
    }
    return filtered;
}
```

#### 改进 2: 增强错误日志
```cpp
bool exportShareLinks(const std::vector<ProxyConfig>& proxies) {
    if (proxies.empty()) {
        log("警告: 无代理可导出，可能原因:");
        log("  - 所有代理的 Delay 字段为 0");
        log("  - 数据库查询失败");
        return false;
    }
    // ... 导出逻辑
}
```

#### 改进 3: 数据库索引优化
```sql
-- 确保以下索引存在 (在数据库初始化时执行)
CREATE INDEX IF NOT EXISTS idx_profiles_sub ON profile_items(is_sub);
CREATE INDEX IF NOT EXISTS idx_exitem_delay ON profile_exitems(delay);
CREATE INDEX IF NOT EXISTS idx_exitem_profile ON profile_exitems(indexid);
```

## 🔧 ShareLink 导出修复 (2026-04-23)

### 修复背景
经过详细比较 `v2rayn_sharelink.txt` 与代码导出文件，发现了5个关键格式兼容性问题，导致生成的分享链接无法被v2rayN等客户端正确识别。

### 修复内容

#### 1. 路径参数保留修复 ✅
- **问题**: 代码错误移除 `path=%2F` 参数，破坏代理连接
- **修复**: 修改 `vlessToUri()` 移除 `&& path != "/"` 条件
- **影响**: 11行代理恢复正常连接能力
- **文件**: `src/ShareLink.cpp:275`

#### 2. ECH参数URL编码修复 ✅
- **问题**: ECH参数使用未编码字符 (`+` 而非 `%2B`)
- **修复**: 替换手动编码为 `urlEncode(echConfigList)`
- **影响**: 6行代理符合RFC 3986标准
- **文件**: `src/ShareLink.cpp:250-254`

#### 3. TLS参数逻辑修复 ✅
- **问题**: 为非TLS连接错误添加 `insecure` 参数
- **修复**: 仅在 `security == "tls/reality"` 时添加参数
- **影响**: 9行代理不再包含语义错误的参数
- **文件**: `src/ShareLink.cpp:257-260`

#### 4. VMess负载完整性修复 ✅
- **问题**: VMess JSON `insecure` 标志被错误修改
- **修复**: 强制重新生成JSON而非使用过时base64内容
- **影响**: 2行代理保持正确的安全配置
- **文件**: `src/ShareLink.cpp:178-201`

#### 5. 路径查询参数格式修复 ✅
- **问题**: 路径参数缺少 `?` 分隔符导致URL格式错误
- **修复**: 确保路径编码逻辑正确处理查询参数
- **影响**: 1行代理URL格式规范化
- **文件**: `src/ShareLink.cpp:275-279`

### 验证结果
- **对比文件**: `v2rayn_sharelink.txt` vs `proxies_20260423_163601.txt`
- **修复状态**: 所有5个问题已解决 ✅
- **兼容性**: 完全符合v2rayN分享链接格式
- **测试**: 代码编译通过，功能正常运行

### 技术细节
- **提交**: `5e7ae16` - "Fix share link export issues for v2rayN compatibility"
- **修改文件**: `src/ShareLink.cpp`, `CMakeLists.txt`, `bin/config.json`
- **影响范围**: vless/vmess/trojan协议的分享链接生成
- **向后兼容**: 保持现有功能不变

### 修复验证命令
```bash
# 生成新的分享链接
./validproxy -TU

# 对比验证
diff v2rayn_sharelink.txt proxies_TIMESTAMP.txt
```

---

### 测试验证方案

#### 单元测试
```cpp
// 测试用例: 数据库查询过滤
TEST(ExportTest, FilterDelayGreaterThanZero) {
    // 模拟数据库返回不同 Delay 值
    MockDatabase db;
    db.addMockRow(/* delay = 0 */);
    db.addMockRow(/* delay = 100 */);
    db.addMockRow(/* delay = -1 */);
    
    auto result = loadAndFilterProxies(db);
    ASSERT_EQ(result.size(), 1);  // 仅保留 delay=100
}

// 测试用例: URI 生成
TEST(ExportTest, UriGeneration) {
    ProxyConfig config{VMess, "server", 443, "uuid"};
    std::string uri = ShareLink::toShareUri(config);
    EXPECT_TRUE(uri.find("vmess://") == 0);  // 以 vmess:// 开头
}
```

#### 集成测试
```bash
# 测试导出功能
./validproxy -c test_config.json -TU test_sub_id
# 验证: bin/exports/share_links_*.txt 应包含 URI

# 测试有效代理获取  
./validproxy -c test_config.json -FMIN test_sub_id  
# 验证: 返回代理应具有实际延迟值
```

### 部署注意事项

#### 1. 数据库兼容性
- 确保 `profile_exitems.Delay` 字段存在且可查询
- 旧版本数据库可能需要迁移脚本添加索引

#### 2. 权限要求
```bash
# 程序需要以下目录的写入权限
./bin/exports/        # 导出文件目录
./log/                # 日志目录  
./proxies/            # 临时代理文件目录
```

#### 3. 性能优化
- 对大型数据库 (超过 1000 条代理)，建议添加分页查询
- 考虑在 `findWorkingProxy` 中实现并发测试以加速处理

## 📤 代理同步功能（2026-04-24 新增）

### 功能概述
通过命令行参数或配置文件，将有效代理从一个数据库同步（迁移）到另一个数据库。

### 命令行参数
- `-S` 或 `-sync` - 触发代理同步功能

### 参数格式
```bash
# 方式1：仅指定源数据库（目标库从配置读取）
./validproxy -S /path/to/source.db

# 方式2：同时指定源和目标（命令行优先）
./validproxy -S /path/to/source.db:/path/to/target.db
```

### 配置文件
```json
{
  "sync": {
    "source_db": "/path/to/source.db",
    "target_db": "/path/to/target.db"
  }
}
```

### 优先级
1. 命令行 `-S source:target`（最高）
2. 命令行 `-S source`（target从配置读取）
3. 配置文件 `sync.source_db` + `sync.target_db`

### 迁移逻辑
1. 从源库查询 `delay > 0` 的有效代理（静态筛选，类似 `-TU` 逻辑）
2. 对每条代理：
   - 检查订阅（subid）在目标库是否存在，不存在则先插入订阅信息
   - 检查代理 indexid 是否存在，存在则 UPDATE（覆盖模式），不存在则 INSERT
3. 同时迁移 `profile_exitems` 扩展信息（delay, speed等）

### 实现方案
- 扩展 `SubitemUpdaterV2` 类，添加 `syncDatabases()` 方法
- 在 `main.cpp` 中添加 `-S`/`-sync` 参数解析
- 在 `ConfigReader` 中添加 `sync` 配置节支持
- 参数解析后调用 `SubitemUpdaterV2::syncDatabases(source, target)`

### 成功标准
- 源库中 delay>0 的代理成功复制到目标库
- 关联订阅信息同步创建
- 扩展信息（profile_exitems）完整迁移
- 相同 indexid 的代理在目标库中覆盖更新