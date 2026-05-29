# 项目术语表

> 基于 `v1.0.3` 代码审查编制
> 审查日期: 2026-05-11

---

## 一、核心领域术语

### 代理协议

| 术语 | 全称 | 说明 |
|------|------|------|
| **SS** | Shadowsocks | 轻量级加密代理协议，ConfigType=3 |
| **VMess** | - | V2Ray 加密传输协议，ConfigType=1 |
| **VLESS** | - | 无加密轻量级代理协议，ConfigType=5 |
| **Trojan** | - | 基于 HTTPS 的代理协议，ConfigType=6 |
| **SOCKS** | - | 网络会话层代理协议，ConfigType 未单独编号 |
| **HTTP** | HTTP Proxy | 普通 HTTP 代理 |
| **Hysteria2** | - | 基于 QUIC 的加速代理协议 |
| **TUIC** | - | 基于 QUIC 的 TCP 隧道协议 |
| **WireGuard** | - | 现代 VPN 协议（在代码中作为代理的一种） |

### 传输层

| 术语 | 说明 |
|------|------|
| **KCP** | 基于 UDP 的可靠传输协议，用于加速 TCP |
| **mKCP** | V2Ray 对 KCP 的实现 |
| **QUIC** | Google 开发的基于 UDP 的低延迟传输协议 |
| **WebSocket/WS** | 基于 HTTP 的全双工通信协议 |
| **gRPC** | Google 的 RPC 框架，用于 Xray API 通信 |
| **TCP** | 传输控制协议 |
| **TLS** | 传输层安全协议 |
| **Reality** | Xray 的 TLS 指纹伪装技术 |

### 安全与加密

| 术语 | 说明 |
|------|------|
| **Fingerprint** | TLS 指纹（如 Chrome、Firefox），用于流量伪装 |
| **Flow** | 流控模式（如 `xtls-rprx-vision`） |
| **SNI** | Server Name Indication，TLS 服务器名称指示 |
| **ALPN** | Application-Layer Protocol Negotiation，应用层协议协商 |
| **AllowInsecure** | 是否允许不安全证书连接 |
| **StreamSecurity** | 传输流安全设置 |
| **EchConfigList** | ECH (Encrypted Client Hello) 配置列表 |
| **EchForceQuery** | 强制 ECH 查询 |
| **PublicKey** | Reality 协议的公钥 |
| **ShortId** | Reality 协议的短 ID |
| **SpiderX** | Reality 协议的 SpiderX 参数 |

---

## 二、项目模块术语

### 核心管理模块

| 术语 | 类/结构体 | 职责 | 关键方法 |
|------|-----------|------|---------|
| **XrayManager** | `XrayManager` | xray 实例单例管理 | `start()`, `stop()`, `getInstance()`, `release()` |
| **XrayInstance** | `XrayInstance` | 单个 xray 进程生命周期 | `start()`, `stop()`, `isRunning()` |
| **XrayApi** | `xray::XrayApi` | xray gRPC API 通信 | `addOutbound()`, `removeOutbound()`, `ping()` |

### 代理测试模块

| 术语 | 类/结构体 | 职责 | 关键方法 |
|------|-----------|------|---------|
| **ProxyFinder** | `ProxyFinder` | 查找可用代理 | `findFirstWorkingProxy()`, `findWorkingProxy()` |
| **ProxyBatchTester** | `ProxyBatchTester` | 多线程批量测试 | `run()`, `runWithSubId()` |
| **ProxyTester** | `ProxyTester` | 单个代理测试 | `test(socksPort)` |
| **TestResult** | `TestResult` (struct) | 测试结果：包含 latency/errorMsg/success | - |
| **FallbackProxy** | `ProxyFinder::FallbackProxy` | 回退代理信息：indexId/address/socksPort/delay | - |

### 配置与生成模块

| 术语 | 类/结构体 | 职责 | 关键方法 |
|------|-----------|------|---------|
| **ConfigReader** | `config::ConfigReader` | 读取配置文件 | `load()`, `getDefaultConfigPath()` |
| **AppConfig** | `config::AppConfig` (struct) | 应用全局配置结构体 | - |
| **ConfigGenerator** | `config::ConfigGenerator` | 生成 Xray JSON 配置 | `generateConfig()`, `loadProfiles()` |
| **XrayConfig** | `config::XrayConfig` (struct) | 出站/入站 JSON 配置对 | - |

### 订阅管理模块

| 术语 | 类/结构体 | 职责 | 关键方法 |
|------|-----------|------|---------|
| **SubitemUpdaterV2** | `update::SubitemUpdaterV2` | 订阅更新管理 | `run()`, `runSingle()`, `deduplicate()`, `syncDatabases()` |
| **Strategy** | `update::SubitemUpdaterV2::Strategy` (enum) | 更新策略枚举 | - |

### 工具模块

| 术语 | 类/命名空间 | 职责 |
|------|------------|------|
| **Logger** | `Logger` | 统一日志系统（支持文件/控制台输出，6级日志级别） |
| **PortManager** | `PortManager` | SOCKS/API 端口分配与释放 |
| **DatabaseHelper** | `db::Database` | SQLite 数据库连接管理 |
| **CurlEasyHandle** | `CurlEasyHandle` | CURL 安全 RAII 封装（支持移动语义 + 链式调用 API） |
| **UrlFetcher** | `UrlFetcher` | HTTP URL 内容获取 |
| **ShareLink** | `share::ShareLink` | 代理分享链接生成 |
| **Utils** | `utils` (namespace) | 工具函数集 |
| **CurlGlobalGuard** | `CurlGlobalGuard` | CURL 全局初始化/清理 RAII 守卫 |

---

## 三、数据模型术语

### Profileitem（代理配置项）

**文件**: `include/Profileitem.h`, 数据库表 `ProfileItem`

| 字段 | 类型 | 说明 |
|------|------|------|
| **indexid** | string | 唯一标识（主键） |
| **configtype** | string | 代理协议类型（1=VMess, 3=SS, 5=VLESS, 6=Trojan） |
| **configversion** | string | 配置版本号 |
| **address** | string | 代理服务器地址（IP/域名） |
| **port** | string | 代理服务器端口 |
| **id** | string | 用户 ID (UUID) |
| **alterid** | string | VMess 额外 ID |
| **security** | string | VMess 加密方式 |
| **network** | string | 传输协议（tcp/kcp/ws/quic/grpc） |
| **remarks** | string | 备注名 |
| **subid** | string | 所属订阅 ID |
| **issub** | string | 是否来自订阅 |
| **flow** | string | 流控模式 |
| **sni** | string | TLS SNI |
| **alpn** | string | ALPN 值 |
| **coretype** | string | 核心类型 |
| **fingerprint** | string | TLS 指纹 |
| **publickey** | string | Reality 公钥 |
| **shortid** | string | Reality 短 ID |
| **extra** | string | 额外信息 |
| **echconfiglist** | string | ECH 配置列表 |
| **grpcMultiMode** | int | gRPC 多路复用模式 |
| 其他 KCP 字段 | int | kcpMtu、kcpTti、kcpUplink 等 KCP 传输参数 |

### Subitem（订阅配置项）

**文件**: `include/Subitem.h`, 数据库表 `Subitem`

| 字段 | 类型 | 说明 |
|------|------|------|
| **id** | string | 订阅唯一 ID |
| **remarks** | string | 订阅备注名 |
| **url** | string | 订阅 URL |
| **moreurl** | string | 备用 URL |
| **enabled** | string | 是否启用 |
| **useragent** | string | 获取订阅时的 User-Agent |
| **sort** | string | 排序 |
| **autoupdateinterval** | string | 自动更新间隔 |
| **converttarget** | string | 转换目标格式 |
| **prevprofile** | string | 前一个代理 ID |
| **nextprofile** | string | 后一个代理 ID |
| **memo** | string | 备注 |

### ProfileExItem（代理测试扩展信息）

**文件**: `include/ProfileExItem.h`, 数据库表 `ProfileExItem`

| 字段 | 类型 | 说明 |
|------|------|------|
| **indexid** | string | 关联的代理 ID（外键到 ProfileItem） |
| **delay** | string | 延迟（ms） |
| **speed** | string | 速度 |
| **sort** | string | 排序 |
| **message** | string | 测试消息 |
| **consecutive_failures** | int | 连续失败次数（>=blacklist_threshold 则黑名单） |

---

## 四、配置项术语

**文件**: `bin/config.json`, 解析器: `config::ConfigReader`

| 配置项 | 类型 | 说明 |
|--------|------|------|
| **database.path** | 字符串 | SQLite 数据库路径 |
| **database.sql_query** | 字符串 | 代理查询 SQL |
| **database.sql_by_subid** | 字符串 | 按订阅查询 SQL |
| **xray.executable** | 字符串 | xray 可执行文件路径 |
| **xray.workers** | 整数 | xray 工作实例数（默认 4） |
| **xray.start_port** | 整数 | SOCKS 起始端口 |
| **xray.api_port** | 整数 | API 起始端口 |
| **test.url** | 字符串 | 测试 URL（默认 `https://www.google.com/generate_204`） |
| **test.timeout_ms** | 整数 | 测试超时（ms） |
| **log.enabled** | 布尔 | 是否启用日志 |
| **log.level** | 字符串 | 日志级别 |
| **log.file_level** | 字符串 | 文件日志级别 |
| **log.console_level** | 字符串 | 控制台日志级别 |
| **priority_mode** | 字符串 | 优先级模式 |
| **dedup.enabled** | 布尔 | 是否启用去重 |
| **dedup.after_update** | 布尔 | 更新后是否自动去重 |
| **blacklist_threshold** | 整数 | 黑名单阈值（连续失败次数） |
| **sync.source_db** | 字符串 | 同步源数据库 |
| **sync.target_db** | 字符串 | 同步目标数据库 |

---

## 五、日志级别

| 级别 | 值 | 说明 |
|------|----|------|
| **TRACE** | 0 | 最详细的跟踪日志 |
| **DEBUG** | 1 | 调试信息 |
| **INFO** | 2 | 普通信息（默认） |
| **WARN** | 3 | 警告信息 |
| **ERR** | 4 | 错误信息 |
| **REPORT** | 5 | 报告/摘要输出 |

---

## 六、命令行术语

| 参数 | 含义 | 触发模块 | 数据流 |
|------|------|---------|--------|
| **`-F`** | find-proxy：查找第一个可用代理 | `ProxyFinder` | DB → ConfigGenerator → XrayApi → CURL 测试 |
| **`-FMIN`** | findminproxy：查找延迟最小代理 | `ProxyFinder` | DB → 全部测试 → 排序 → 返回最优 |
| **`-G`** | generator：生成出站 JSON | `ConfigGenerator` | DB → buildVLESS/VMess/SS/...Outbound() |
| **`-U`** | update：更新单个订阅 | `SubitemUpdaterV2` | URL 获取 → 解析 → DB 更新 → 可选去重 |
| **`-UA`** | update-all：更新所有订阅 | `SubitemUpdaterV2` | 遍历所有启用的 Subitem → 逐个更新 |
| **`-T`** | test-sub：测试订阅代理 | `ProxyBatchTester` | SubitemUpdater → ProxyBatchTester → 多线程测试 |
| **`-TU`** | tourl：导出分享链接 | `ShareLink` | DB → toShareUri() → bin/exports/share_links.txt |
| **`-D`** | dedup：去重 | `SubitemUpdaterV2` | 数据库级去重（5字段+源字段匹配） |
| **`-S`** | sync：同步数据库 | `SubitemUpdaterV2` | 源DB → 复制有效代理 → 目标DB |
| **`-IS`** | import-sub-config：批量导入 | `SubitemUpdaterV2` | 文件/URL → 解析 → DB 批量写入 |
| **`-show-sub`** | 显示订阅 | `SubitemUpdaterV2` | DB Subitem 表 → 控制台输出 |
| **`-c`** | config：指定配置文件 | `ConfigReader` | 加载 → 解析 → 填充 AppConfig |
| **`-h`** | help：显示帮助 | main.cpp | 打印 help 文本 |

---

## 七、更新策略

| 策略 | 值 | 说明 |
|------|----|------|
| **DirectFirst** | - | 直接连接获取订阅 URL，失败则通过代理重试 |
| **ProxyFirst** | - | 通过代理获取订阅 URL |
| **DirectOnly** | - | 仅直连获取订阅 URL |

---

## 八、去重逻辑

| 概念 | 说明 |
|------|------|
| **deduplicatePhase0** | 基于 cache 表 ID 的预去重阶段 |
| **deduplicatePhase1** | 基于代理地址/端口/协议 5 字段去重 |
| **deduplicateMergedPhase** | 合并去重（CTE 实现，统一写入临时表后替换原表） |
| **5-field key** | 去重匹配键：Address + Port + Id + ConfigType + Security |
| **consecutive_failures** | 连续失败计数器（>= blacklist_threshold 时自动跳过） |
| **blacklist_threshold** | 黑名单阈值（配置项，默认值在 config.json 中） |

---

## 九、数据库术语

| 术语 | 说明 |
|------|------|
| **guindb.db** | SQLite 数据库文件（"GUI 数据库"的简称，继承自 v2rayn 命名） |
| **ProfileItem** | 代理配置表（存储所有代理服务器信息） |
| **Subitem** | 订阅配置表（存储订阅源信息） |
| **ProfileExItem** | 代理扩展信息表（延迟、速度、连续失败计数等） |
| **test/guindb.db** | 测试数据库（开发和测试用） |
| **bin/worker/guindb.db** | 生产数据库（实际运行使用） |
| **DAO** | Data Access Object，数据访问对象模式（如 ProfileExItemDAO） |
| **空白测试库 / 空白库** | - | 测试目录中仅包含表结构、无实际代理数据的空数据库 |

---

## 十、缩写与简称对照

| 缩写 | 全称 |
|------|------|
| **SS** | Shadowsocks |
| **VMess** | - (V2Ray 加密传输) |
| **VLESS** | - (V2Ray 无加密传输) |
| **KCP** | KCP Protocol (快速可靠 UDP) |
| **QUIC** | Quick UDP Internet Connections |
| **SNI** | Server Name Indication |
| **ALPN** | Application-Layer Protocol Negotiation |
| **ECH** | Encrypted Client Hello |
| **TLS** | Transport Layer Security |
| **gRPC** | gRPC Remote Procedure Call |
| **DAO** | Data Access Object |
| **RAII** | Resource Acquisition Is Initialization |
| **CTE** | Common Table Expression (SQL) |
| **UUID** | Universally Unique Identifier |
| **GUI** | Graphical User Interface |
| **Socks** | SOCKS (Socket Secure) |
| **API** | Application Programming Interface |
| **RPC** | Remote Procedure Call |
| **URL** | Uniform Resource Locator |

---

## 十一、设计模式术语

| 模式 | 使用位置 | 说明 |
|------|---------|------|
| **Singleton** | `XrayManager` | 全局唯一 xray 实例管理器 |
| **RAII** | `CurlEasyHandle`, `CurlGlobalGuard`, `XrayInstance` | 资源获取即初始化（自动管理 CURL/Xray 进程生命周期） |
| **DAO** | `ProfileExItemDAO` | 数据访问对象（封装 SQLite 操作） |
| **Fluent API** | `CurlEasyHandle` | 链式调用： `.setUrl().setProxy().setTimeoutMs().perform()` |
| **Strategy** | `SubitemUpdaterV2::Strategy` | 更新策略模式（DirectFirst/ProxyFirst/DirectOnly） |
| **Multi-thread Worker** | `ProxyBatchTester` | 多线程工作队列模式 |

---

## 十二、文件目录 (File Directories)

> 以下按逻辑分组，按目录路径格式列出应用中所有文件读写位置。

### `<exeDir>/` 可执行文件所在目录
- **用途：** 所有相对路径的根目录；通过 `GetModuleFileNameA()` 动态获取
- **路径计算：** `src/Utils.cpp:19-28` — `GetModuleFileNameA(NULL, buffer, MAX_PATH)` → `rfind('\\')` 截取目录
- **引用位置：** 几乎所有源文件通过 `utils::getExecutableDir()` 调用

---

### `<exeDir>/config.json` 应用配置文件
- **用途：** 应用主配置（数据库路径、Xray 路径、测试参数、日志级别、去重设置等）
- **读取位置：** `src/ConfigReader.cpp:27-32`
- **文件名格式：** `config.json`（也可通过 `-c <path>` 指定任意 `.json` 文件）
- **路径计算：** `exeDir + "\\config.json"` (ConfigReader.cpp:27)
- **操作：** 读取

---

### `<exeDir>/config/` Xray 运行时配置目录
- **用途：** 存放 Xray 进程 JSON 配置文件及 CLI 生成的出站 JSON
- **创建位置：** `src/main.cpp:126-128`
- **Xray 实例配置写入：** `src/XrayInstance.cpp:12,137-144,171`
  - 文件名：`xray_config_<socksPort>.json`
  - 路径计算：`configDir + "/xray_config_" + std::to_string(socksPort) + ".json"`
- **出站 JSON 写入（CLI -G）：** `src/main.cpp:329-331`
  - 文件名：`<generatorIndexId>.json`
  - 路径计算：`configDir / (generatorIndexId + ".json")`
- **被 XrayManager 引用：** `src/ProxyBatchTester.cpp:16-18`, `src/SubitemUpdaterV2.cpp:1150`, `src/main.cpp:460-463`
- **操作：** 写入（两个写入点），Xray 进程读取

---

### `<exeDir>/log/` 日志文件目录
- **用途：** 存放所有会话的运行日志
- **创建位置：** `src/main.cpp:129-131`
- **日志写入：** `src/Logger.cpp:27-43`
  - 文件名格式：`<prefix>_<timestamp>.log`
  - 路径计算：`logDir + "/" + prefix + "_" + timestamp + ".log"`
  - prefix 取值：`ui`, `test`, `update`, `tourl`, `sync`, `dedup`, `generator`, `show-sub`, `test-sub`, `findminproxy`, `find-proxy`
  - `main.cpp:125` 定义 `logDir = baseDir / "log"`
- **初始化调用：** 分布在 `src/main.cpp:248,294,346,439,504,593,628,678,713,777` 等所有 CLI 模式入口
- **操作：** 写入（每次启动生成一个新文件）

---

### `<exeDir>/proxies/` 分享链接导出目录（CLI 模式）
- **用途：** `-TU` / `--tourl` 命令导出可用代理分享链接
- **创建与写入：** `src/main.cpp:572-577`
  - 文件名：`proxies_<YYYYMMDD_HHMMSS>.txt`
  - 路径计算：`std::filesystem::path(exeDir) / "proxies" / ("proxies_" + timestamp + ".txt")`
- **操作：** 写入

---

### `<TEMP>/` 操作系统临时目录（GUI 模式导出）
- **用途：** GUI 菜单 "Export Share Links" 导出分享链接
- **写入：** `src/ui/AppController.cpp:334-337`
  - 文件名：`proxies_<YYYYMMDD_HHMMSS>.txt`
  - 路径计算：`std::filesystem::temp_directory_path() / ("proxies_" + timestamp + ".txt")`
- **操作：** 写入

---

### `<exeDir>/icons/` 工具栏图标目录
- **用途：** 存放工具栏按钮的 PNG/ICO 图标（嵌入式资源的后备路径）
- **读取：** `src/ui/ToolbarIcons.h:139-156,196-200`
  - 路径计算：`exeDir + "\\icons\\" + name + ".png"` 及 `.ico` 后备
  - 文件列表：`tool_cancel.png`, `tool_config.ico/png`, `tool_dedup.ico/png`, `tool_find.ico/png`, `tool_import.ico/png`, `tool_synchronize.png`, `tool_test.ico/png`, `tool_update.ico/png`, `tool_update1.png`
- **操作：** 读取

---

### `<database_path>` SQLite 数据库文件
- **用途：** 主数据库，存储 ProfileItem（代理）、SubItem（订阅）、ProfileExItem（测试结果）三张表
- **读取配置：** `src/ConfigReader.cpp:67-71` — 从 config.json 读取 `database.path`，通过 `resolvePath()` 解析
- **打开（读写）：** `src/main.cpp:63` — `sqlite3_open(config.database_path.c_str(), &db)`
- **典型值：** `bin/worker/guiNDB.db`（生产）、`E:/v2rayN-windows-64/guiConfigs/guiNDB.db`（外部）、`test/guiNDB.db`（测试）
- **伴随文件：** `guiNDB.db-shm`, `guiNDB.db-wal`（WAL 模式自动生成）
- **操作：** 读写

---

### `<test>/guiNDB.db` 测试数据库
- **用途：** 集成测试使用的数据库文件
- **创建/写入：** `test/init_test_db.cpp:7` — 硬编码路径 `E:/eclipse_workspace/multiple_thread_validproxy/test/guiNDB.db`
- **文件：** `guiNDB.db`, `guiNDB_empty.db`
- **操作：** 读写（测试框架）

---

### `<source_db>` / `<target_db>` 同步数据库
- **用途：** `-S` / `--sync` 命令的源和目标数据库，用于数据库间代理同步
- **配置：** `src/ConfigReader.cpp:217-224` — 从 config.json 读取 `sync.source_db` / `sync.target_db`
- **打开（读写）：** `src/SubitemUpdaterV2.cpp:1829,1836` — `sqlite3_open(sourceDbPath)` / `sqlite3_open(targetDbPath)`
- **CLI 参数：** `src/main.cpp:180-215` 解析 `-S source.db:target.db`
- **操作：** 读写

---

### `<import_file_path>` 批量导入文件
- **用途：** `-IS` / `--import-sub-config` 命令读取的文件（每行一个订阅 URL，可选备注）
- **读取：** `src/SubitemUpdaterV2.cpp:2104` — `std::ifstream file(filePath)`
- **CLI 参数：** `src/main.cpp:219-221` — 从 argv 获取路径
- **示例文件：** `test/import_test.txt`, `test/test_import.txt`
- **操作：** 读取

---

### `<exeDir>/worker/` 工作目录（生产数据库所在）
- **用途：** 生产环境中 `guiNDB.db` 的默认位置（config.json 中引用）
- **包含文件：** `guiNDB.db`、历史 exe 文件、SQL schema 文件（`schema_*.sql`）、测试数据文件等
- **路径计算：** 由 config.json 的 `database.path` 或二进制发布结构决定
- **操作：** 间接读写（通过 SQLite）

---

### `<exeDir>/bin/` 运行时 DLL/资源目录
- **用途：** 存放 wxWidgets 运行时 DLL（`wxmsw32u_*.dll`）、第三方库（`libpng16.dll`, `libjpeg-62.dll` 等）
- **路径计算：** 与 exe 同目录（`GetModuleFileNameA` 返回值所在目录）
- **操作：** 读取（Windows 动态加载 DLL）、写入（CMake 构建时复制 DLL）
