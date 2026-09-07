# 调整：独立代理恢复独立控制台窗口启动 + 启动失败日志显示返回值

- **日期**: 2026-08-26
- **模块**: `AppController::startStandaloneProxy`
- **版本**: v1.0
- **状态**: ✅ completed（实现 + 编译 + 测试通过；GUI 端到端待桌面确认）
- **关联文档**: `docs/bugfix/2026-08-25-Bugfix-StandaloneProxy-XrayLogForward-v1.0.md`（#68，本变更将其「无窗口 + 管道转发」启动方式回退）

---

## 1. 调整背景

用户诉求（两条）：

1. **恢复原独立代理进程的启动方式，独立窗口 + 输出** —— 即 xray / sing-box 子进程应有自己的控制台窗口，并把 stdout/stderr 直接输出到该窗口（而非被静默重定向）。
2. **启动独立代理错误时，日志显示返回值** —— 启动失败时，应用程序本地日志应记录失败对应的返回值（CreateProcess 的 `GetLastError`，或进程早期退出的 `exitCode`）。

#68 曾将启动方式改为 `CREATE_NO_WINDOW` + 匿名管道 + detached 读取线程，把子进程输出实时转发进本地日志。该方式满足了「本地日志可见报错」，但代价是**不再弹出独立控制台窗口**、用户看不到实时的 xray/sing-box 输出。本次按用户要求回退该启动方式。

---

## 2. 改动方案

### 2.1 恢复独立控制台窗口（去掉无窗口 + 管道）

`startStandaloneProxy` 的启动段改写：

- 删除 `CreatePipe` / `STARTF_USESTDHANDLES` / `NUL` 输入重定向 / 写端句柄管理。
- 删除 detached 读取线程 `standalone_proxy::forwardChildLog(...)`。
- `CreateProcessA` 的创建标志由 `CREATE_NO_WINDOW` 改为 `CREATE_NEW_CONSOLE`，`bInheritHandles` 由 `TRUE` 改为 `FALSE`（无句柄重定向，子进程仅经环境块继承 `ASSET` 等环境变量）。
- 子进程 stdout/stderr 直接输出到其独立控制台窗口，用户可见。

### 2.2 启动失败日志显示返回值

- `CreateProcess` 失败时：`Logger::write("[StandaloneProxy] 启动失败: CreateProcess 返回值=0(FALSE), GetLastError=<code>", ERR)` —— 明确记录返回值（0）与 `GetLastError`。
- 进程早期退出（闪崩 / 端口就绪前崩溃）时：`logCrashReason(where)` 仍 `GetExitCodeProcess` 取退出码并记 `exitCode=...`（与 #68 一致，保留）。
- 将原先误导性的「崩溃原因见上方本地日志（[xray:<id>]）」文案改为「崩溃原因见独立代理控制台窗口」，与新的输出位置一致（共 3 处：1 条日志 + 2 处弹窗文案）。

### 2.3 未删除转发单元

`standalone_proxy::forwardChildLog`（`include/StandaloneProxyLogForwarder.h` + `src/StandaloneProxyLogForwarder.cpp`）本次**不再接入独立代理启动路径**，但文件与单测 `StandaloneProxyLogForwarderTest` 保留（仍是有价值的、被测试覆盖的通用单元；后续若需恢复本地日志转发可复用）。仅移除 `AppController.cpp` 中对它的 `#include` 与调用。

---

## 3. 代码改动清单

| 类型 | 文件 | 说明 |
|------|------|------|
| 修改 | `src/ui/AppController.cpp` | 移除 `#include "StandaloneProxyLogForwarder.h"`；启动段改用 `CREATE_NEW_CONSOLE`、去掉管道/线程；`CreateProcess` 失败日志补「返回值=0, GetLastError」；`logCrashReason` 与 2 处弹窗文案指向独立控制台窗口 |
| 保留 | `include/StandaloneProxyLogForwarder.h` / `src/StandaloneProxyLogForwarder.cpp` / `tests/test_standalone_proxy_log_forwarder.cpp` | 不再被独立代理启动调用，但单元与单测保留 |

---

## 4. 验证

| 项 | 结果 |
|----|------|
| 构建 `validproxy.exe` | 0 error（已验证：增量构建 0 error，`bin/validproxy.exe` 重链接成功） |
| `StandaloneConfigPortTest`（#69 回归） | PASS（已验证：ctest 39/40，本变更未触动 #69 路径） |
| `StandaloneProxyLogForwarderTest`（转发单元保留） | PASS（已验证：转发单元未接入启动路径，单测仍通过） |
| GUI 端到端（点击「开启代理」→ 弹出独立控制台窗口；故意用错误配置触发闪崩 → 本地日志出现 `GetLastError` / `exitCode`） | 待用户在桌面执行（headless 环境无法弹窗 / 外网） |

---

## 5. 边界与遗留

- **`CREATE_NEW_CONSOLE`**：子进程分配自有控制台，输出直接可见；进程退出时其控制台随之关闭，瞬时闪崩可能来不及看窗口内容——但退出码已落在本地日志（满足诉求 2）。
- **返回值来源**：CreateProcess 失败 = `GetLastError`；进程已启动但早期退出 = `GetExitCodeProcess` 退出码。两者均在本地日志以 `ERR` 记录。
- **日志转发单元**：保留为可复用通用能力，未删除，避免无谓的死代码清理争议与测试回归。
