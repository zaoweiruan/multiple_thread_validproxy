---
title: "Spec: WebDAV 云端同步（config.json 引导 / 订阅同步 / 上传）"
type: spec
status: draft
date: 2026-11-13
module: net/WebDavClient, service/CloudSyncService, ui/MainFrame
version: v1.0
---

# Spec — WebDAV 云端同步 v1.0

## 1. 目标

用户诉求（原文）：

> 1. config.json 配置文件缺失，从下述 webdav 网站同步
> 2. 库表中订阅也可以从上述网站文件中同步
> 3. 增加上述数据的文件上传功能

参考协议：RFC 4918（WebDAV）。服务端为坚果云（Nutstore），WebDAV 端点
`https://dav.jianguoyun.com/dav/`。

三条能力的边界：

| # | 能力 | 触发时机 |
|---|---|---|
| R1 | `config.json` 缺失时从云端拉取，落地后继续启动 | **自动**（唯一自动路径：此时无配置可读，不可能有手动入口） |
| R2 | 订阅（`SubItem`）与云端 JSON 双向同步 | 手动（菜单 / CLI） |
| R3 | 把当前配置与订阅上传到云端 | 手动（菜单 / CLI） |

## 2. 输入

| 输入 | 来源 | 说明 |
|---|---|---|
| WebDAV 端点 | 编译期常量 | 默认 `https://dav.jianguoyun.com/dav/` |
| 远端目录 | `config.json` 的 `webdav.remote_dir`，缺省 `validproxy` | 相对端点 |
| 账号 / 应用密码 | 编译期内嵌（加密），见 §5 | 不进 git、不进日志 |
| 本地 `config.json` | `<exeDir>/config.json` | 缺失即触发 R1 |
| 本地订阅 | SQLite `SubItem` | R2/R3 的数据源 |
| 远端文件 | `config.json`、`subscriptions.json` | §4 定义格式 |

## 3. 输出

| 输出 | 位置 | 说明 |
|---|---|---|
| 拉取到的配置 | `<exeDir>/config.json` | 原子写，失败不落地 |
| 同步后的订阅 | SQLite `SubItem` | 事务内 upsert，不删除本地独有条目 |
| 上传结果 | 远端 `config.json` / `subscriptions.json` | PUT，携带 `If-Unmodified-Since` 之类的乐观并发控制 |

## 4. 远端文件格式

### 4.1 `config.json`
与本地 `config.json` **同构**（原样上传、原样下载），不额外包装。

### 4.2 `subscriptions.json`
```json
{
  "version": 1,
  "exported_at": "2026-11-13T10:00:00Z",
  "items": [
    {
      "id": "5544178410297751350",
      "remarks": "示例订阅",
      "url": "https://example.com/sub",
      "enabled": 1,
      "user_agent": "",
      "sort": 0,
      "filter": "",
      "auto_update_interval": 0,
      "update_time": 0,
      "convert_target": "",
      "prev_profile": "",
      "next_profile": "",
      "pre_socks_port": 0,
      "memo": ""
    }
  ]
}
```
- 只含订阅元数据，**不含代理、统计、运行历史**。
- 不含 `url` 中可能出现的凭据（订阅链接本身即凭据，属用户已知数据；日志必须脱敏）。

## 5. 凭据内嵌（用户决策：硬编码 + 加密）

**安全声明（必须接受）**：密钥随二进制分发，因此这是**混淆而非保密**。
任何拿到 exe 的人都能还原账号与应用密码。适用于"防止随手翻仓库/日志"，
不适用于防逆向。

实现方式（避免密码进入 git 与本对话记录）：

1. `scripts/provision_webdav_secret.py` —— 交互读取账号/应用密码，
   用固定密钥做 XOR + Base64，写出 `resources/webdav_secret.inc`。
2. `resources/webdav_secret.inc` 加入 `.gitignore`（**不入库**）。
3. CMake 用 `configure_file` 把该文件包进 `WebDavSecret.cpp`：
   ```cpp
   namespace net { const char* const kWebDavSecretBlob = "…"; }
   ```
4. 运行时 `WebDavCredentials::load()` 反解得到 `user` / `password`。
5. **文件缺失时构建仍成功**，但 `load()` 返回 false，云端能力统一报
   "未配置 WebDAV 凭据"，不崩溃、不影响其他功能。

## 6. 组件设计

### 6.1 `net::WebDavClient`（依赖 curl，不依赖 wx）

```cpp
namespace net {

struct DavEntry {
    std::string href;           // RFC 4918 <D:href>
    long long   contentLength;  // <D:getcontentlength>，未知为 -1
    std::string lastModified;   // <D:getlastmodified>（HTTP-date，原样保留）
};

struct DavResponse {
    long status = 0;            // HTTP 状态码
    std::string body;           // 响应体（GET 时为文件内容）
    std::string error;          // 传输层错误（curl 错误串）
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

class WebDavClient {
public:
    WebDavClient(const WebDavCredentials& creds, std::string baseUrl);
    DavResponse get(const std::string& remotePath) const;
    DavResponse put(const std::string& remotePath, const std::string& body,
                    const std::string& ifUnmodifiedSince = "") const;
    DavResponse propfind(const std::string& remotePath, int depth = 0) const;
    DavResponse mkcol(const std::string& remotePath) const;
    // 纯函数，可单测：
    static std::vector<DavEntry> parseMultiStatus(const std::string& xml);
    static std::string normalizeHref(const std::string& href);
};

} // namespace net
```
要点：Basic 认证；`CURLOPT_SSL_VERIFYPEER/HOST` 保持开启；
PROPFIND 走 `CURLOPT_CUSTOMREQUEST` + `Depth` 头；
PUT 走 `CURLOPT_UPLOAD`；不跟随跨主机重定向以免泄露凭据。

### 6.2 `service::CloudSyncService`（编排，无 wx）

```cpp
enum class SyncDecision { Skip, Pull, Push, Conflict };

struct SubscriptionRecord { /* 与 SubItem 列一一对应 */ };

class CloudSyncService {
public:
    CloudSyncService(net::WebDavClient& dav, const std::string& localConfigPath,
                     db::SubitemDAO& subDao);

    // R1
    bool ensureLocalConfig(std::string& error);
    // R2
    bool pullSubscriptions(std::string& error);
    bool pushSubscriptions(std::string& error);
    // R3
    bool pushConfig(std::string& error);

    // 纯函数，可单测
    static SyncDecision decide(long long localEpoch, long long remoteEpoch);
    static std::string encodeSubscriptions(const std::vector<SubscriptionRecord>&);
    static bool decodeSubscriptions(const std::string& json,
                                    std::vector<SubscriptionRecord>& out,
                                    std::string& error);
    static long long parseHttpDate(const std::string& httpDate);
};

} // namespace service
```

`decide()` 规则（用户选择：按时间戳）：
- 远端不存在（404）→ `Push`
- 本地不存在 → `Pull`
- `remote > local + 2s` → `Pull`
- `local > remote + 2s` → `Push`
- 其余（含 2s 内抖动）→ `Skip`
`Conflict` 仅在时间戳不可解析时使用，由 UI 弹窗让用户选。

`pullSubscriptions()` 的合并语义：**按 `Id` upsert，不删除本地独有条目**
（远端删掉的订阅不会消失）；`enabled/sort/remarks/url` 等字段以远端为准。

### 6.3 触发入口

| 入口 | 行为 |
|---|---|
| 启动（`config.json` 不存在） | `ensureLocalConfig()` → 成功则继续原启动流程；失败给出可读错误并退出 |
| 菜单「从云端同步订阅」 | `pullSubscriptions()`，结果写状态栏 + 日志 |
| 菜单「上传到云端」 | `pushConfig()` + `pushSubscriptions()` |
| CLI `--cloud-pull-subs` / `--cloud-push` | 与菜单等价，便于脚本与验证 |

菜单项加到 `MainFrame` 现有菜单体系；耗时操作走 `AppController` 的后台
线程模式（参照现有 `AsyncOperationGuard`），禁止在 UI 线程阻塞网络。

## 7. 前置条件

- 用户已生成坚果云应用密码并执行过 `scripts/provision_webdav_secret.py`。
- 远端目录可写（首次上传时若 404，先 `MKCOL`）。
- 本机可访问 `dav.jianguoyun.com`（HTTPS 443）。

## 8. 边界条件

| 条件 | 行为 |
|---|---|
| 凭据未配置 | 云端入口全部禁用；启动时若 config.json 缺失，直接报既有"配置缺失"错误（不变差） |
| 网络不可达 / 超时 | 超时 10s；失败只在日志 + 状态栏提示，不阻塞启动、不重试 |
| 远端 401/403 | 明确提示"WebDAV 凭据被拒绝"，不提示为网络错误 |
| 远端 404 | 配置拉取 → 报"云端无配置"；上传 → 先 `MKCOL` 再 PUT |
| 本地 config.json 已存在 | **R1 不触发**（绝不覆盖用户本地配置） |
| 本地订阅表为空 | Pull 后变为远端内容；Push 时上传空数组 |
| 远端 JSON 损坏 | 拒绝解析，保留本地不变，报可读错误 |
| 时钟偏移 | 2 秒容忍窗口，避免 FAT/exFAT 时间粒度导致误判 |
| 凭据泄露面 | 账号/密码不进日志、不进异常信息、不进状态栏 |

## 9. 验收标准

1. 删除 `config.json` 后启动：自动拉取成功、落地、程序正常进入主界面。
2. `config.json` 存在时启动：**不发起任何网络请求**（可用抓包/日志断言）。
3. 云端无 `config.json` 时：报"云端无配置"，程序按既有路径退出（退出码 1）。
4. 订阅 Pull：远端 3 条 → 本地新增 3 条；再次 Pull 不产生重复（按 Id upsert）。
5. 订阅 Pull 不删除本地独有条目。
6. 订阅 Push：远端文件内容可被 `decodeSubscriptions()` 还原为相同集合。
7. 上传时远端目录不存在 → 自动 `MKCOL` 后成功。
8. 时间戳判定：远端新 → Pull；本地新 → Push；相同 → Skip（单元测试覆盖全部 5 分支）。
9. 凭据缺失：F1/F2/F3 全部给出"未配置凭据"，程序其余功能正常。
10. 构建 0 error；`CloudSyncTest` 全绿；既有测试无新增失败。

## 10. 测试计划（TDD）

`tests/test_cloud_sync.cpp` —— **实际实现 24 例**（纯逻辑，不发真实网络；下表为最初 16 例设计，实现时按需增补了日期解析/凭据/转换等用例）：

| # | 用例 | 断言 |
|---|---|---|
| 1 | `DecideReturnsPushWhenRemoteMissing` | 远端无条目 → Push |
| 2 | `DecideReturnsPullWhenLocalMissing` | 本地无条目 → Pull |
| 3 | `DecideRemoteNewerPulls` | remote = local + 60s → Pull |
| 4 | `DecideLocalNewerPushes` | local = remote + 60s → Push |
| 5 | `DecideWithinToleranceSkips` | 差 1s → Skip |
| 6 | `DecideUnparsableTimestampsConflict` | 无法解析 → Conflict |
| 7 | `ParseHttpDateHandlesRfc1123` | `Wed, 21 Oct 2026 07:28:00 GMT` → 固定 epoch |
| 8 | `ParseMultiStatusExtractsHrefAndMtime` | 两个 `<D:response>` → 2 条，含 href/mtime |
| 9 | `ParseMultiStatusIgnoresMalformedXml` | 截断 XML → 空结果 + 不抛异常 |
| 10 | `NormalizeHrefStripsPercentEncoding` | `%2F` → `/`，去掉 scheme/host 前缀 |
| 11 | `EncodeSubscriptionsRoundTrips` | encode→decode 集合相等 |
| 12 | `DecodeSubscriptionsRejectsCorruptJson` | 非法 JSON → false + error 非空 |
| 13 | `DecodeSubscriptionsAllowsMissingOptionalFields` | 只有 id/url → 其余取默认 |
| 14 | `BuildRemoteUrlJoinsPaths` | base 尾斜杠/base 无斜杠/含空格 → 结果正确 |
| 15 | `CredentialsBlobRoundTrips` | encodeSecret→decodeSecret 还原 user/password |
| 16 | `CredentialsBlobRejectsTamperedInput` | 篡改字节 → false |

手工验收（真实网络，需凭据）：
- `validproxy-cli.exe --cloud-pull-subs` / `--cloud-push` 各执行一次，
  与网页端对比文件内容。

## 11. 风险与非目标

**风险**

| 风险 | 处置 |
|---|---|
| 硬编码凭据可被逆向提取 | 已在本 Spec §5 明确声明为混淆；如需真正保密应改 DPAPI/凭据管理器（可由用户后续决定） |
| 订阅链接（含 token）被写入云端 JSON | 这是用户自己的数据；日志与错误信息一律脱敏，不打印完整 URL |
| 时间戳判定在时钟不准时误判 | 2s 容忍窗口 + `Conflict` 分支交给用户 |
| 覆盖用户本地订阅属性 | 仅 upsert 不删除；Pull 前后写日志（新增/更新条数） |
| 网络阻塞 UI | 全部走后台线程；超时 10s |

**非目标**

- 不做自动定时同步（用户选择全手动；仅 R1 在配置缺失时自动）。
- 不做冲突的自动合并（按 Id upsert，两侧同名不同 Id 视为两条）。
- 不上传代理、统计、运行历史（只 config.json + 订阅元数据）。
- 不实现 WebDAV 之外的协议（S3/OneDrive 等）。
- 不处理远端目录级的 ACL/共享（使用账号自身的空间）。

## 12. 文件级改动清单

| 文件 | 改动 |
|---|---|
| `include/net/WebDavClient.h` / `src/net/WebDavClient.cpp` | 新增：GET/PUT/PROPFIND/MKCOL + MultiStatus 解析 |
| `include/net/WebDavCredentials.h` / `src/net/WebDavCredentials.cpp` | 新增：内嵌加密凭据的装载与校验 |
| `resources/WebDavSecret.cpp.in` | 新增：`configure_file` 模板（读 `webdav_secret.inc`） |
| `scripts/provision_webdav_secret.py` | 新增：交互生成 `webdav_secret.inc`（gitignored） |
| `include/service/CloudSyncService.h` / `src/service/CloudSyncService.cpp` | 新增：编排 + 纯函数（决策/编解码/URL 拼接） |
| `src/main_gui.cpp` | 新增：启动前 `ensureLocalConfig()`（仅当 config.json 缺失） |
| `src/main_cli.cpp` | 新增：`--cloud-pull-subs` / `--cloud-push` |
| `src/ui/MainFrame.cpp`（+ `Events.h`） | 新增：两个菜单项 + 事件 |
| `src/ui/AppController.cpp` | 新增：后台执行与状态回报 |
| `tests/test_cloud_sync.cpp` | 新增：16 例 |
| `.gitignore` | 忽略 `resources/webdav_secret.inc` |
| `CMakeLists.txt` | 生成 `WebDavSecret.cpp`、加入 `CORE_SOURCES`、注册 `CloudSyncTest` |

## 13. 实现与验证记录（2026-11-13）

构建：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` + `cmake --build build --parallel 8` → EXIT=0。
工具链 GCC 15.2.0 / msvcrt（与 `bin/` 内 2026-08-14 预编译 wx DLL 的 CRT 一致）。

自动化测试（隔离 worktree）：

| 命令 | 结果 |
|---|---|
| `ctest -R CloudSyncTest` | **24/24 通过** |
| `ctest -R DatabaseBootstrapTest` | 28/29 通过，1 跳过（无法创建符号链接） |
| `ctest -E "UI_"`（全量非 UI） | **56/57 通过**；仅 `XrayManagerStartupTest` 失败（既有环境噪声基线，与本功能无关） |

真实网络验证（坚果云 `https://dav.jianguoyun.com/dav/`，远端目录 `validproxy`）：

| # | 操作 | 结果 |
|---|---|---|
| 1 | `validproxy-cli.exe --cloud-push`（空库） | `config.json 已上传` / `订阅已上传：0 条`，自动 `MKCOL` 建目录成功 |
| 2 | 写入 2 条订阅后 `--cloud-push` | `订阅已上传：2 条` |
| 3 | 另一个空库 `--cloud-pull-subs` | `新增 2，更新 0`；Id/Remarks/Url/Enabled/Sort/Memo 全部一致 |
| 4 | `--cloud-pull-config`（本地 config 存在） | `本地 config.json 已存在，跳过云端拉取`（未发请求） |
| 5 | `--cloud-pull-config`（挪走本地 config） | `已从云端拉取 config.json` |
| 6 | GUI 启动时 config.json 缺失 | 日志 `startup: 已从云端拉取 config.json`，主窗口正常打开 |

凭据：由 `scripts/provision_webdav_secret.py --from-env` 生成 `resources/webdav_secret.inc`
（已 gitignore），构建期 `configure_file` 编入。**该文件不入库**；未配置时构建仍成功，
云端入口统一返回"凭据未配置"。

**未验证**：

- GUI 菜单两项（「从云端同步订阅」/「上传配置与订阅到云端」）已接线并编译通过、
  主窗口可正常打开，但**点击行为未做端到端点击验证**（UI 自动化受 wx CRT 问题影响）。
- `If-Unmodified-Since` 乐观并发控制参数已透传但未启用（当前 PUT 不带该头）。
