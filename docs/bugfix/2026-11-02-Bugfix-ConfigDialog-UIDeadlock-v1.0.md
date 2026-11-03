# AutoTask 执行中打开配置对话框阻塞、无法退出

**Goal:** 修复 AutoTask（update_all → test_all → dedup → resolve_region）运行期间打开配置对话框时 UI 阻塞、对话框无法关闭的问题。
**Tech Stack:** C++17、wxWidgets 3.2 (wxMSW)、SQLite WAL、PowerShell。
**依据文档:** `docs/DEV-PROCESS.md`。

---

## 0. 现场证据

- 用户报告：「执行自动任务时，配置界面阻塞，无法退出」。
- AutoTask 的 test_all 阶段为 4920 代理的批量测试（writer 线程持续写入 DB），配置对话框在渲染时对 `db_`（共享 SQLite 连接）执行大量 DAO 读取（SqlQueryDAO/FullConfigTemplateDAO/ComponentBundleDAO 等）。
- 共享连接在 serialized 模式下 UI 线程读取与 writer 线程写入并发使用 → UI 线程在 `sqlite3_step` 内等待 writer 释放锁；同时出现 `SQL错误: bad parameter or other API misuse`（并发 misuse）。
- 模态对话框的事件循环被 UI 线程阻塞 → Cancel/OK 按钮无法响应 → "无法退出"。

## 1. 根因

1. **共享连接并发使用**：`ConfigDialog` 的 `db_` 成员初始化为 `MainFrame::db_`（与批量测试 writer 线程共用的 handle）。AutoTask 期间 writer 持续写入，UI 线程的 DAO 读取并发使用同一 handle → SQLITE_MISUSE/Busy → 阻塞。
2. **UI 线程同步 DB 读**：ConfigDialog 构造期与交互期均在 UI 线程直接读 DB，无独立连接 → 与 writer 线程竞争。
3. **无超时/独立连接**：ConfigDialog 没有 WAL reader connection，无法利用 WAL 的并发读写特性。

## 2. 修复（`src/ui/ConfigDialog.cpp` / `.h`）

- 构造函数：`db_` 初始化为 `nullptr`，构造体内打开**独立的 WAL 连接**（`SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE`，`PRAGMA journal_mode=WAL`，`busy_timeout=5000`，mmap_size 等），赋值给 `db_`；打开失败回退到传入的共享 handle。
- 新增 `ownsDb_` 成员标记所有权；析构函数 `~ConfigDialog()` 关闭自有连接（避免关闭外部共享 handle）。
- 所有 50+ 个 DAO 调用点（`db::...DAO dao(db_)`）**无需改动**——它们自动使用 reader 连接。

## 3. 验证

- `cmake --build build --target validproxy --parallel 8` 编译通过。
- `.\scripts\build-and-test.bat` → 12/12 全绿。
- 用户手工验证：AutoTask 执行中打开配置对话框 → 对话框应能正常渲染与关闭（Cancel/OK 可用）。

## 4. 教训

- 任何 UI 线程 DAO 读取都必须使用独立的 WAL 连接（参考 `loadProxiesAsync` 的 reader 模式），避免与 writer 线程共用 `db_` handle → SQLITE_MISUSE/Busy。
- 模态对话框的事件循环在 UI 线程上运行，任何阻塞的同步 DB 读都会导致对话框"假死"（无法处理关闭按钮事件）。
- `db_` 成员的所有权必须明确标记（`ownsDb_`），避免析构时关闭外部共享 handle。
