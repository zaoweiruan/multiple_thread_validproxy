# Bugfix: XrayInstance 子进程 stderr/退出码观测增强 v1.0

- **日期**: 2026-08-10
- **模块**: `XrayInstance`（`include/XrayInstance.h`, `src/XrayInstance.cpp`）
- **关联问题**: gRPC 注入方式（fc15eec, 2026-07-31）下 xray 实例在活跃注入期间偶发崩溃/挂起，根因未定
- **关联文档**: `docs/bugfix/2026-08-07-Xray-instances-are-not-automatically-restarted.md`

## 1. 背景与动机

RCA（见会话分析）确认以下事实链：

1. xray 实例死亡只发生在 gRPC 注入活跃期间（`ui_20260810_100203.log`：3 次死亡）。
2. **xray panic 堆栈不可见**：`XrayInstance::start()` 使用 `CreateProcessW(CREATE_SUSPENDED|CREATE_NO_WINDOW)` 且未重定向标准句柄，`bInheritHandles=FALSE` → 子进程无有效 stderr，xray 的 panic 输出（默认写 stderr）被静默丢弃。
3. 生成的 xray 配置 `"log": {"loglevel": "warning"}` 未指定日志文件 → xray 日志全部走 stderr。
4. `crash_*.dmp`（`src/CrashHandler.cpp`）捕获的是**本应用进程**，与 xray 无关。
5. `bin/worker/log` 中不存在 gRPC 切换（07-31）之前的日志，无法量化旧方式崩溃频率。

**决策（用户确认）**: 暂不改动注入/编码逻辑（A/B 方向），仅实施观测增强（C 方向）——捕获 xray 子进程 stderr/stdout 与退出码，收集下一次崩溃现场后再定修复方案。

## 2. 变更范围

| 文件 | 变更 |
| --- | --- |
| `include/XrayInstance.h` | 新增成员：stdout/stderr 文件句柄、`lastExitCode_`；新增 `readFileTail` 静态辅助与 `lastExitCode()` 访问器 |
| `src/XrayInstance.cpp` | `start()` 创建重定向句柄并设置 `STARTF_USESTDHANDLES`；启动期死亡与 `isRunning()` 检测死亡时记录退出码 + stderr 尾部；`stop()` 关闭文件句柄 |
| `tests/test_xray_instance_stderr.cpp` | 新增 Google Test：用假进程（批处理）验证 stderr 捕获、退出码、启动期死亡与运行期死亡路径 |
| `CMakeLists.txt` | 注册 `XrayInstanceStderrTest` |
| `docs/INDEX.md` | §9.1 登记本文档 |

**不修改**: `XrayApi.cpp`（gRPC 客户端与手写 protobuf 编码）、`XrayManager.cpp`、`ProxyBatchTester.cpp`。

## 3. 设计

### 3.1 重定向文件

- 路径：`<configDir>/xray_stdout_<socksPort>.log`、`<configDir>/xray_stderr_<socksPort>.log`（与 `xray_config_<socksPort>.json` 同目录，位于 `bin/worker/config/`）。
- `CreateFileA` + `FILE_SHARE_READ|FILE_SHARE_WRITE` + `CREATE_ALWAYS`（每次 relaunch 截断重建）。
- 句柄创建时 `SECURITY_ATTRIBUTES.bInheritHandle = TRUE`，供子进程继承。
- `CreateProcessW` 改为 `bInheritHandles=TRUE`，`si.dwFlags = STARTF_USESTDHANDLES`：
  - `hStdInput` → `NUL`（xray 不读 stdin）
  - `hStdOutput` / `hStdError` → 上述文件句柄
- 句柄生命周期：`start()` 创建 → 子进程存活期间保持打开 → `stop()` 或启动失败路径关闭；析构兜底（复用 `stop()` 路径与句柄成员清理）。

> 说明：xray 在未配置 log 文件时默认将 access/error 日志与 panic 堆栈写入 stderr，因此 stderr 文件是崩溃现场的主要载体；stdout 一并捕获以备诊断。

### 3.2 退出码与 stderr 尾部上报

- 新增 `mutable DWORD lastExitCode_`（初始 `STILL_ACTIVE`），在检测到进程非存活时记录。
- `isRunning()`（const，现有轮询检测点）：当 `GetExitCodeProcess` 返回非 `STILL_ACTIVE` 时，以 `LogLevel::ERR` 输出：
  - 退出码
  - stderr 文件尾部（最近 4 KB，UTF-8 读取，按行截断）
- `start()` 的 5 秒存活轮询死亡分支（现 L106-118）同样输出退出码 + stderr 尾部——启动即 panic 的场景可立即暴露。
- 新增 `readFileTail(const std::string& path, size_t maxBytes)`：读文件最后 `maxBytes` 字节并截断到完整行，供日志与测试复用。

### 3.3 兼容性

- 不改 `CreateProcessW` 的 `CREATE_SUSPENDED`/`CREATE_NO_WINDOW` 标志与 Job 对象逻辑。
- 未设置 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 失败时的降级路径不受影响。
- 线程安全：stdout/stderr 句柄仅由 `start()`/`stop()` 在 `stateMutex_` 保护下创建/关闭；`isRunning()` 只读 `lastExitCode_`。

## 4. 验证方案

1. 单测（`tests/test_xray_instance_stderr.cpp`）：
   - **运行期死亡**：以批处理伪装 xray（`ping -n 9 127.0.0.1` 保持存活约 8 秒 > 5 秒存活轮询，随后 `echo PANIC 1>&2` + `exit /b 42`）。断言：`start()==true` → 轮询 `isRunning()` 转 false → `lastExitCode()==42` → stderr 文件尾部含 `PANIC`。
   - **启动期死亡**：以 `powershell.exe` 伪装 xray（`run -c <cfg>` 为非法命令，立即退出）。断言：`start()==false`，stderr 文件非空。
2. `cmake --build build --parallel 8` 编译通过。
3. `ctest --test-dir build -R XrayInstanceStderrTest -V` 通过。
4. 实机：等待下一次 xray 崩溃，`bin/worker/config/xray_stderr_<port>.log` 应包含 panic 堆栈与退出码。

## 5. 预期收益与后续

- 下一次崩溃将同时获得：精确退出码 + xray 自身 panic 堆栈 → 可定位到 xray-core 内部精确行（H1 验证或证伪）。
- 若 panic 堆栈证实为 gRPC 注入配置触发的 xray handler 空指针，再按 RCA 的 B 方向（raw JSON bytes 回退）实施修复，本观测设施保留用于回归验证。
