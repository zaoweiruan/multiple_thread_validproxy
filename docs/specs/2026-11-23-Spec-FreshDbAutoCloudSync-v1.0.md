---
title: "Spec: 创建空库后自动从云端同步订阅"
type: spec
status: draft
date: 2026-11-23
module: service/CloudSyncService, service/DatabaseBootstrap, main_gui, main_cli
version: v1.0
---

# Spec — 创建空库后自动从云端同步订阅 v1.0

## 1. 目标

用户诉求（原文）：

> 新增功能：当生成空库时，自动下载云端更新订阅

**核心语义**：当启动流程因数据库缺失/损坏而创建或重建一个**空库**后，
自动从云端（WebDAV `subscriptions.json`）拉取订阅并写入 `SubItem` 表，
使新库一建立即拥有订阅，无需用户再手工导入。

**owner 决策（2026-11-23）**：

- 同步失败（未配置凭据 / 网络失败 / 云端无 `subscriptions.json`）→ **静默降级，仅写日志**，
  不弹窗、不阻断启动；用户仍可手动导入订阅。
- 生效范围 = **GUI + CLI 都生效**，行为一致。

## 2. 背景与现状

| 现状 | 说明 |
|---|---|
| `service::DatabaseBootstrap` | 只读探测 + 幂等建表。GUI `bootstrapDatabase()` 与 CLI `openDatabase()` 在库缺失/损坏时创建/重建**空库** |
| `service::CloudSyncService::pullSubscriptions()` | 已有：拉取云端 `subscriptions.json` 并 upsert 到 `SubItem`，**从不删除**本地条目 |
| `net::WebDavCredentialsStore::load()` | 编译期内嵌凭据解码；未 provisioned 时返回 false |
| 空库创建后的现状行为 | 仅提示 `Startup.Created` / `Startup.Rebuilt`（"请导入订阅"），**无云端拉取** |

关键事实：

- GUI 启动路径：`main()` → `bootstrapDatabase()`（可能创建空库）→ `dbService.open()` →
  种子 DAO → `wxEntry`。`bootstrapDatabase()` 创建库时数据库句柄**尚未打开**。
- CLI 启动路径：`openDatabase()` 内部 `inspect` → 必要时 `create` → `sqlite3_open_v2` →
  `applyPragmas` → 种子 DAO。15 个命令复用该函数。
- `CloudSyncService` 无 wxWidgets 依赖，GUI/CLI/单测均可用。

## 3. 输入 / 输出

**输入**：

| 输入 | 来源 |
|---|---|
| 新建的数据库连接 | `sqlite3*`（创建空库后打开） |
| 远端目录 | 与现有 GUI 菜单一致：`validproxy` |
| WebDAV 凭据 | 编译期内嵌，`WebDavCredentialsStore::load` |
| 端点 | 编译期常量 `https://dav.jianguoyun.com/dav/` |

**输出**：`CloudSyncResult`（`added`/`updated`/`error`）。仅用于日志；不弹窗、不阻断。

## 4. 方案

### 4.1 新增 `CloudSyncService::pullSubscriptionsInto`

在 `service/CloudSyncService` 新增静态方法，作为 GUI/CLI 创建空库后的统一入口：

```cpp
// 凭据加载器可注入 seam（单测确定性覆盖“无凭据”分支，避免联网）。
using CredentialLoader = bool (*)(net::WebDavCredentials&, std::string&);

static CloudSyncResult pullSubscriptionsInto(sqlite3* db, const std::string& remoteDir,
                                             CredentialLoader loader =
                                                 &net::WebDavCredentialsStore::load);
```

行为：

1. `loader(credentials, error)` 失败 → 记 `WARN` 日志 + 返回 `{ok=false, error=...}`（**不抛异常**）。
2. 成功 → 构造 `WebDavClient(credentials, "https://dav.jianguoyun.com/dav/")` +
   `SubitemDAO(db)` + `CloudSyncService(client, remoteDir, /*localConfigPath=*/"", subDao)`，
   调用 `pullSubscriptions()` 并返回其结果（`pullSubscriptions` 不使用 `localConfigPath`）。

`localConfigPath` 传空串合法：`pullSubscriptions()` 仅使用 `subDao_`/`client_`/`remoteDir_`。

### 4.2 GUI：`main_gui.cpp`

1. `bootstrapDatabase()` 签名增加输出参数 `bool& freshCreated`：
   - `PromptCreate` 分支 `create()` 成功 → `freshCreated = true`
   - `PromptRebuild` 分支 `create()` 成功 → `freshCreated = true`
   - 其余分支不置位；函数入口置 `false`。
2. `main()`：`bootstrapDatabase` 返回 true 后、`dbService.open()` 成功且种子 DAO 完成后，
   若 `freshCreated`，调用
   `service::CloudSyncService::pullSubscriptionsInto(db, "validproxy")`，
   结果按 `ok` 记 `REPORT`/`WARN` 日志（**不弹窗**，owner 决策）。

### 4.3 CLI：`main_cli.cpp`

`openDatabase()` 增加局部 `bool createdFresh = false;`：

- `DatabaseBootstrap::create` 成功 → `createdFresh = true`
- `sqlite3_open_v2` 成功 + `applyPragmas` 之后，若 `createdFresh`，调用
  `service::CloudSyncService::pullSubscriptionsInto(db, "validproxy")`，
  结果按 `ok` 记 `REPORT`/`WARN` 日志（**不弹窗**）。

15 个 `openDatabase` 调用点**零改动**（逻辑内聚在函数内部）。

### 4.4 i18n 文案更新

`resources/i18n/strings.json` 修改 `Startup.Created` 与 `Startup.Rebuilt`：

- 原文案提示"请导入订阅"，与自动同步行为不符。
- 改为提示"将自动从云端同步订阅"（云端可用时）；并运行 `scripts/gen-i18n.ps1`
  重新生成 `include/i18n/StringId.h` 与 `src/i18n/StringTable.cpp`。

## 5. 前置条件

- 依赖 `CloudSyncService` / `WebDavClient` / `SubitemDAO` 已存在（均已实现）。
- 数据库路径已由 `bootstrapDatabase`/`openDatabase` 建立并打开。

## 6. 边界条件

| 条件 | 行为 |
|---|---|
| 未 provisioned 凭据 | 静默：`WARN` 日志，`ok=false`，不联网，不弹窗 |
| 云端无 `subscriptions.json` | `pullSubscriptions` 返回错误 → `WARN` 日志，静默 |
| 网络失败 / 超时 | `WebDavClient` 既有超时（connect 5s / request 10s）→ `WARN` 日志，静默 |
| `subscriptions.json` 解码失败 | 既有 `decodeSubscriptions` 错误 → `WARN` 日志，静默 |
| 同步成功 | `REPORT` 日志（含 `added`/`updated`），不弹窗 |
| 数据库创建失败 | 既有路径不变：返回 false，不触发同步 |

**约束**：同步仅在**本次**创建/重建空库时触发；已有可用库（`Continue`）绝不触发。

## 7. 测试计划（TDD）

| # | 测试 | 断言 |
|---|---|---|
| T1 | `test_cloud_sync.cpp`：`pullSubscriptionsInto` 无凭据（注入 `loader` 恒 false） | 返回 `ok=false`、`error` 非空、不抛异常、不联网 |
| T2 | 既有 `test_cloud_sync.cpp` / `test_database_bootstrap.cpp` 全量回归 | 无退化 |
| T3 | 既有 ctest 全量回归 | 无退化（构建 EXIT 0） |

`bootstrapDatabase`/`openDatabase` 位于 `main_*.cpp`（不可单测），其 `freshCreated`
接线以代码走查 + 手动验证覆盖。

## 8. 验收标准

1. `cmake --build build --parallel 6` 成功（EXIT 0），无 `auto` 新增。
2. `ctest --test-dir build -V` 全量通过（无新增失败）。
3. 手动：构造缺库场景（删除 `database.path` 指向的库或置空库），GUI 启动选择"创建"，
   观察日志出现 `cloud sync` 拉取记录；无凭据/断网场景仅 `WARN` 且正常进入 GUI。
4. 手动：CLI 命令（如 `-show-sub`）在缺库时创建空库并触发同步，stdout/日志可见。

## 9. 风险

- **网络阻塞启动**：同步在启动路径执行，最长约 connect 5s + request 10s。
  仅发生在"创建空库"这一罕见路径；GUI 沿用 `ProgressWindow` 显示（可选），
  且 owner 已确认静默降级，不阻断正常启动。
- **空库被意外填充**：仅在 `freshCreated`（本次确实创建）时触发，`Continue` 路径零改动。
- **i18n 生成**：改 `strings.json` 后必须运行 `gen-i18n.ps1`，否则 `StringTable.cpp` 与源不一致。

## 10. 变更文件清单

| 文件 | 变更 |
|---|---|
| `include/service/CloudSyncService.h` | 新增 `pullSubscriptionsInto` 静态方法 + `CredentialLoader` |
| `src/service/CloudSyncService.cpp` | 实现 `pullSubscriptionsInto` |
| `src/main_gui.cpp` | `bootstrapDatabase` 加 `freshCreated`；`main` 触发同步 |
| `src/main_cli.cpp` | `openDatabase` 内部 `createdFresh` 触发同步 |
| `resources/i18n/strings.json` | `Startup.Created` / `Startup.Rebuilt` 文案 |
| `include/i18n/StringId.h`、`src/i18n/StringTable.cpp` | `gen-i18n.ps1` 重新生成 |
| `tests/test_cloud_sync.cpp` | 新增 T1 |
| `docs/INDEX.md`、`docs/plans/project-plans-tracker.md` | 登记 |
