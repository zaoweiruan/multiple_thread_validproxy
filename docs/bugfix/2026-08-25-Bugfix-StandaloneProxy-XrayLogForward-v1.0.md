# 修复：独立代理 xray / sing-box 真实报错转发至本地日志

- **日期**: 2026-08-25
- **模块**: `AppController::startStandaloneProxy` / `standalone_proxy::forwardChildLog`
- **版本**: v1.0
- **状态**: ✅ completed（实现 + 编译 + 单元集成测试通过；GUI 端到端待桌面确认）
- **关联文档**: `docs/bugfix/2026-08-25-Bugfix-StandaloneProxy-FlashCrashHang-v1.0.md` §7.1（同会话 Q1 闪崩根因可见性修复的一部分）

---

## 1. 问题背景

用户诉求：**将 xray 真实报错添加到本地日志**（"将 xray 真实报错添加到本地日志"）。

在独立代理（`startStandaloneProxy`）路径下，xray / sing-box 进程以前通过
`CreateProcessA(..., CREATE_NEW_CONSOLE, ...)` 启动，**stdout/stderr 被丢弃到一个无人
读取的控制台窗口**。当代理因配置错误、`address already in use`、资产缺失或 panic 而
闪崩时，用户只能看到「进程退出」之类的笼统提示，看不到 xray 自身打印的具体失败原因，
排查极其困难。

同一会话早前的修复曾尝试把 stderr 重定向到 `bin/log/standalone_<id>_xray_stderr.log`
独立文件，并在崩溃时回读尾部 4KB 粘贴进日志——但这要求用户额外去翻一个独立文件，
与「本地日志」的预期不符，且回读时机、句柄管理都较脆弱。本修复改为**经匿名管道把
子进程输出实时转发进用户原本就在看的本地日志**。

---

## 2. 根因

1. `startStandaloneProxy` 启动子进程时**不重定向 stdout/stderr**，子进程的报错文本
   （配置解析失败 / `address already in use` / 资产缺失 / panic 栈）丢失在无人读取的
   控制台窗口中。
2. 早前的「重定向到独立文件」方案只是把问题从「控制台窗口」搬到「另一个文件」，并未
   真正进入用户关注的本地日志，且崩溃时仍依赖回读文件尾部，鲁棒性差。

---

## 3. 修复方案

### 3.1 匿名管道 + 无窗口启动

- 用 `CreatePipe(&stderrRead, &stderrWrite, ...)` 建立匿名管道。
- `STARTUPINFOA` 设置 `STARTF_USESTDHANDLES`：`hStdOutput = hStdError = stderrWrite`、
  `hStdInput` 重定向到 `NUL`；只读端 `stderrRead` 通过 `SetHandleInformation(...,
  HANDLE_FLAG_INHERIT, 0)` **禁止被子进程继承**，仅写端被继承。
- 启动标志 `CREATE_NO_WINDOW`，不再弹出独立控制台窗口。
- 若 `CreatePipe` 失败，降级为不捕获 stderr（仅记一条 WARN），不影响主流程。

### 3.2 detached 读取线程转发到本地日志

`CreateProcessA` 成功后，**立即关闭我们持有的写端**（`stderrWrite`），这样子进程退出
时管道写端关闭、读取线程读到 EOF 自然结束。随后启动一个 **detached 读取线程**：

- 循环 `ReadFile(stderrRead, ...)` 累积字节流，按 `\n` 切分逐行。
- 每行剥离行尾 `\r`（`\r\n` 兼容）。
- 经 `Logger::write("[xray:<id>] " + line, level)` 写入本地日志：
  - 错误行（`isErrorLine` 命中 error / panic / fatal / fail / invalid / cannot /
    unable / exception / refused / denied，大小写不敏感）→ `LogLevel::ERR`（默认级别
    可见）；
  - 其余行 → `LogLevel::TRACE`。
- 线程 `detach()`：子进程退出后随 EOF 自行结束，无需 `join`；不留悬垂句柄。
- `stderrRead` 由读取线程在 EOF 后 `CloseHandle`。

### 3.3 崩溃日志只补退出码

子进程闪崩时，`logCrashReason(where)` 仅 `GetExitCodeProcess` 取退出码并记一句
`exitCode=...` + 「崩溃原因见上方本地日志」，指向已经实时落盘的 `[xray:<id>] ...`
报错行。不再回读任何文件。

### 3.4 抽取为独立可测单元

转发逻辑被抽取为 `standalone_proxy::forwardChildLog`（不依赖 wxWidgets / UI），由
`AppController` 的 detached 线程调用。这是本次修复的额外工程改进，使核心行为可被无 GUI
的集成测试覆盖（见 §5）。

---

## 4. 代码改动清单

| 类型 | 文件 | 说明 |
|------|------|------|
| 新增 | `include/StandaloneProxyLogForwarder.h` | 声明 `standalone_proxy::forwardChildLog(HANDLE, const std::string& indexId, std::function<void(const std::string&, LogLevel)>)` |
| 新增 | `src/StandaloneProxyLogForwarder.cpp` | 实现：管道读取、按行切分、错误行识别、级别路由、`[xray:<id>]` 前缀、关闭句柄 |
| 修改 | `src/ui/AppController.cpp` | `startStandaloneProxy` 改用管道 + `CREATE_NO_WINDOW`；detached 线程改调 `standalone_proxy::forwardChildLog`；移除原 `readFileTail` / `stderrFile` 文件回读逻辑；`logCrashReason` 只补退出码 |
| 新增 | `tests/test_standalone_proxy_log_forwarder.cpp` | 无 GUI 集成测试：模拟子进程经管道写日志，断言错误行路由到 `ERR` 且带前缀、CRLF 剥离、末尾无换行行仍输出、`nullptr` 句柄安全 |
| 修改 | `CMakeLists.txt` | `src/StandaloneProxyLogForwarder.cpp` 加入 `UI_SOURCES`；新增测试目标 `test_standalone_proxy_log_forwarder` 并 `add_test(StandaloneProxyLogForwarderTest)` |

---

## 5. 验证

| 项 | 结果 |
|----|------|
| 构建 `validproxy.exe`（含全部改动） | 0 error（仅既有 unused-parameter 警告，非本次引入） |
| 构建测试目标 `test_standalone_proxy_log_forwarder` | 0 error |
| `StandaloneProxyLogForwarderTest` | **2/2 PASS** — 断言：error 行 → `LogLevel::ERR` 且前缀 `[xray:42]`；普通行 → `TRACE`；CRLF 被剥离；末尾无换行行仍输出；`nullptr` 句柄为安全 no-op |
| GUI 端到端（真实点击「开启代理」+ 本地日志出现 `[xray:<id>] ...` 报错行） | 待用户在桌面执行（headless 环境无法弹窗 / 外网） |

> 说明：自动化测试覆盖的是「转发逻辑」这一核心单元——它正是 GUI 路径实际调用的同一
> 函数，因此「日志转发本身会坏」的风险已被排除。完整 `startStandaloneProxy` 链路
> （真实 xray/sing-box 子进程、端口等待、`logCrashReason` 退出码行、弹窗文案）仍需在
> 桌面 GUI 跑一次做最终确认，该步骤在当前 headless 环境无法完成。

---

## 6. 边界与遗留

- **`nullptr` / `INVALID_HANDLE_VALUE` 句柄**：`forwardChildLog` 直接返回，安全。
- **末尾无换行行**：EOF 后仍把残留片段作为最后一行输出（匹配真实进程以 panic 栈结尾
  且未必带换行的情况）。
- **CRLF**：统一按 `\n` 切分并剥除行尾 `\r`，Windows 控制台/文件混用均正确。
- **可选加固（未做）**：当前用 detached 线程（进程退出即 EOF 自然结束）。若要求「进程
  停止前保证 xray 输出全部落盘」的确定性顺序，可在 `StandaloneProxyInfo` 中保存读取
  `std::thread` 并在 `stopStandaloneProxy` / `shutdownStandaloneProxies` 中 `join()`。
  现有 detached 方案对本次诉求已足够，未做以避免额外范围。
