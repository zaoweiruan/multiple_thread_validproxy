# CLion MCP Server — 使用与排障指南 v1.1

> 文档类型：Reference
> 适用项目：validproxy
> 日期：2026-11-06
> 作者：AI Agent

---

## 1. 概述

CLion MCP Server 是 JetBrains CLion 内置的 MCP (Model Context Protocol) 服务，运行在 CLion 进程内，通过 HTTP 把 IDE 能力开放给 AI Agent（如 OpenCode）。本项目用它实现：**代码导航、编译诊断读取、调试器全量控制（下断点/单步/看栈/求值/读内存）**，对 C++/wxWidgets 多线程代理验证工具的调试价值极高。

- 服务端版本：`CLion MCP Server` 2026.3 RC（CLion CL-263.7307.108）
- 工具数量：**88 个**
- 传输协议：MCP **Streamable HTTP**（POST 到单一端点）
- 依赖：CLion 必须处于打开状态（本项目 `E:\eclipse_workspace\multiple_thread_validproxy`），且 CMake 工程已加载

---

## 2. OpenCode 配置（opencode.jsonc）

```jsonc
"mcp": {
  "clion": {
    "type": "remote",
    "url": "http://127.0.0.1:64362/stream",
    "enabled": true
  }
}
```

> 配置文件：`C:\Users\dsm\.config\opencode\opencode.jsonc`

### 2.1 关键坑：必须用 `/stream`，不要用 `/sse`

CLion 暴露两个 MCP 端点，行为完全不同：

| 端点 | 传输类型 | POST 行为 | OpenCode `type:"remote"` 可用性 |
|------|----------|-----------|--------------------------------|
| `/sse` | SSE（旧式） | **405 Method Not Allowed**（只允许 GET） | ❌ 连接失败 |
| **`/stream`** | **Streamable HTTP** | **200 + 正常 initialize 响应** | ✅ 可用 |

**根因**：OpenCode 的 `type:"remote"` 使用基于 **POST** 的 Streamable HTTP 传输；若误配为 `/sse`，OpenCode 向 `/sse` 发 POST，必然得到 `405`，日志表现为：

```
mcp transport error  server=clion  error="Error POSTing to endpoint:  (HTTP 405)"
mcp connect failed    server=clion  status.status=failed  status.error="Error POSTing to endpoint:  (HTTP 405)"
```

验证方法（PowerShell，确认 `/stream` 可用）：

```powershell
$payload='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"probe","version":"1.0"}}}'
Invoke-RestMethod -Uri "http://127.0.0.1:64362/stream" -Method POST -Body $payload -ContentType "application/json"
# 期望返回: {"id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{"listChanged":true}},"serverInfo":{"name":"CLion MCP Server","version":"2026.3 RC"}},"jsonrpc":"2.0"}
```

### 2.2 端口是动态的（CLion 重启会变）

`64362` 是**本次 CLion 实例**的动态端口，**每次 CLion 重启后可能变化**。若断开，先查当前端口：

```powershell
# 找到 clion64 的监听端口
Get-NetTCPConnection -OwningProcess (Get-Process clion64 -ErrorAction SilentlyContinue | Select-Object -First 1).Id -State Listen
```

长期方案：在 **CLion → Settings → Tools → MCP Server** 中**固定端口**，避免每次重启改 `opencode.jsonc`。

---

## 3. 能力总览（88 个工具）

### 3.1 调试器 API（`xdebug_*`，20+ 个）— 本项目最大价值

| 工具 | 用途 |
|------|------|
| `xdebug_start_debugger_session` | 从 run configuration 启动调试会话 |
| `xdebug_attach_to_process` | 附加到已运行进程（如 `validproxy.exe`） |
| `xdebug_control_session` | 控制调试会话（暂停/继续/单步/终止） |
| `xdebug_set_breakpoints` / `xdebug_list_breakpoints` / `xdebug_remove_breakpoints` | 行断点管理 |
| `xdebug_set_data_breakpoint` / `xdebug_remove_data_breakpoint` / `xdebug_list_data_breakpoints` | **数据断点**（变量被写坏即捕获，排查竞态/内存损坏） |
| `xdebug_run_to_line` | 运行到指定行 |
| `xdebug_get_threads` / `xdebug_get_stack` / `xdebug_get_frame_values` | **线程列表 / 调用栈 / 帧变量**（多线程排查核心） |
| `xdebug_evaluate_expression` / `xdebug_get_value_by_path` / `xdebug_set_variable` | 当前帧求值 / 取嵌套成员 / 改值 |
| `xdebug_get_fault_status` | 获取异常/访问违例状态 |
| `xdebug_get_registers` / `xdebug_disassemble` / `xdebug_read_memory` | 崩溃点寄存器 / 反汇编 / 内存 |
| `xdebug_get_process_output` | 读被调试进程 stdout/stderr |
| `xdebug_get_debugger_status` | 调试器状态 |

> **针对本项目**：`ProxyBatchTester`/`PortManager`/`SubitemUpdaterV2` 的并发死锁/竞态排查可用 `xdebug_get_threads` + 数据断点；GUI 崩溃/卡死可 `xdebug_attach_to_process` + `xdebug_get_fault_status`。

### 3.2 构建 / 诊断 / 导航

| 工具 | 用途 |
|------|------|
| `get_run_configurations` | 列出 run configuration（本项目 60+ 个：`validproxy`、`validproxy-cli`、`UITests`、全部 `test_*`、`所有 CTest`） |
| `execute_run_configuration` | 启动某配置 |
| `get_file_problems` | 读取某文件的 CLion 分析诊断（错误/警告） |
| `get_diagnostic_info` | 工具链/CMake/编译环境信息（GDB 17.2、CMake 4.4.2、vcpkg `x64-mingw-dynamic`） |
| `load_cmake` | 重载 CMake 工程（改 CMakeLists 后刷新） |
| `open_file_in_editor` / `read_file` / `search_symbol` / `search_text` / `search_file` / `search_regex` | 导航与读取 |
| `execute_sql_query` / `create_database_connection` | 直接查询 SQLite（如 `guindb.db`） |
| `apply_patch` / `reformat_file` / `create_new_file` | 编辑辅助 |
| `git_status` / `get_repositories` / `create_changelist` | Git / Changelist |

---

## 4. 常用调用示例（OpenCode 内 `execute` 沙箱）

```js
// 需在 execute 沙箱内调用，且不要用 const tools = ... 遮蔽全局 tools
const proj = "E:/eclipse_workspace/multiple_thread_validproxy";

// 读取文件诊断
await tools.clion.get_file_problems({ projectPath: proj, filePath: "src/main_gui.cpp" });

// 列出 run configurations
await tools.clion.get_run_configurations({ projectPath: proj });

// 打开文件
await tools.clion.open_file_in_editor({ projectPath: proj, filePath: "src/main_gui.cpp" });
```

> 多数工具要求 `projectPath` 参数；若省略且项目不唯一，CLion 会报"Unable to determine the target project"，此时必须显式传 `projectPath`。

---

## 5. 排障记录：`version.h` 未生成导致的 6 个假阳性诊断

### 5.1 现象

`get_file_problems("src/main_gui.cpp")` 返回 6 个 `ERROR`：

| 行 | 诊断 |
|----|------|
| 10 | 在搜索路径中找不到文件 `version.h` |
| 108 | 无法解析符号 `APP_NAME` / `APP_VERSION` / `APP_GIT_TAG` |
| 109 | 无法解析符号 `APP_BUILD_TYPE` / `APP_BUILD_TIME` |

### 5.2 根因

`version.h` 是**构建期生成头文件**：

- 模板：`include/version.h.in`（定义 `APP_NAME`/`APP_VERSION`/`APP_GIT_TAG`/`APP_BUILD_TYPE`/`APP_BUILD_TIME` 等，经 `@...@` 占位符替换）
- 生成：`CMakeLists.txt:21-27` 的 `add_custom_target(update_version_h ALL ...)` → 输出 `${CMAKE_BINARY_DIR}/include/version.h`
- CLion 的 CMake profile 生成目录是 `cmake-build-debug`；**若该目录从未执行过 `update_version_h`，则 `cmake-build-debug/include/version.h` 不存在** → CLion 分析器报"找不到 version.h"，并连锁导致 6 个宏全部"未解析"。

**这不是真实代码 bug**——其他构建目录（`build/`、`build-release/`、`build-off/`、`build_nogrpc/`）都有 version.h，唯独 `cmake-build-debug/` 缺失。

### 5.3 修复步骤

```powershell
# 1. 生成 version.h（只生成头文件，不动其他产物）
cmake --build cmake-build-debug --target update_version_h
# 输出: [version.h] Generated: .../cmake-build-debug/include/version.h
```

```powershell
# 2. 验证真实编译器能找到（-fsyntax-only，ExitCode 0 即确认）
# 从 cmake-build-debug/compile_commands.json 取 main_gui.cpp 的 command，追加 -fsyntax-only 执行
```

```powershell
# 3. 清除 CLion 分析缓存（关键：仅生成头文件不会自动刷新分析结果）
# CLion 菜单 → File → Invalidate Caches... → Invalidate and Restart
```

> **经验**：生成头文件后，CLion 的分析索引**不会自动刷新**。即使 `version.h` 已存在、`-fsyntax-only` 通过，`get_file_problems` 仍可能持续返回旧错误，直到执行 **Invalidate Caches** 或重启 CLion。这是显示层缓存问题，不代表真实代码错误。

---

## 6. 故障排查表

| 现象 | 原因 | 解决方法 |
|------|------|----------|
| MCP 工具不可见 / 连接失败 | URL 误配为 `/sse` | 改为 `/stream`（见 §2.1） |
| `Error POSTing to endpoint: (HTTP 405)` | 同上（`/sse` 只允许 GET） | 改用 `/stream` |
| 连接拒绝 | CLion 未运行，或端口已变 | 启动 CLion；重查端口（见 §2.2） |
| 工具报 "Unable to determine the target project" | 未传 `projectPath` 且项目不唯一 | 显式传 `projectPath` |
| `get_file_problems` 报 "version.h 找不到" | `cmake-build-debug/include/version.h` 未生成 | 执行 `update_version_h` 目标（见 §5.3） |
| 生成头文件后诊断仍红 | CLion 分析缓存未刷新 | File → Invalidate Caches → Invalidate and Restart |
| `xdebug_*` 无法启动 | 调试器未配置 | CLion Settings 配 MinGW 工具链 + GDB（GDB 17.2 已内置） |

---

## 7. 实测调试工作流（2026-11-06 验证通过）

以下为一次完整闭环，对象是新函数 `TaskQueue::setCurrentProgress`（`src/task/TaskQueue.cpp:227`）。
**全部步骤均由 AI Agent 在 OpenCode `execute` 沙箱内完成**，无需人工在 IDE 里点。

### 7.1 步骤与真实返回

```js
const proj = "E:\\eclipse_workspace\\multiple_thread_validproxy";

// 1) 打断点（location 模式）。必填字段别漏：isLogMessage / isLogStack /
//    isCaptureThreadDump / suspendPolicy / enabled 都是 required，缺任一报 Invalid arguments。
await tools.clion.xdebug_set_breakpoints({
  projectPath: proj,
  breakpoints: [{
    filePath: "src/task/TaskQueue.cpp", line: 230,
    isLogMessage: false, isLogStack: false, isCaptureThreadDump: false,
    suspendPolicy: "ALL", enabled: true, temporary: true,
  }],
});
// => {"results":[{"success":true,"breakpointId":"bp#1","lineText":"current_.progressCurrent = current;"}]}

// 2) 用 run configuration 启动调试会话（配置名来自 get_run_configurations）
await tools.clion.xdebug_start_debugger_session({
  projectPath: proj, configurationName: "test_task_queue", timeout: 90000, graceWaitMs: 3000,
});
// => {"sessionId":"133","state":"paused"}     ← 断点命中即暂停

// 3) 看栈 + 看线程 + 看帧变量（三者可并行）
await tools.clion.xdebug_get_stack({ sessionId: "133", projectPath: proj });
await tools.clion.xdebug_get_threads({ sessionId: "133", projectPath: proj });
await tools.clion.xdebug_get_frame_values({ sessionId: "133", projectPath: proj });
// 栈：setCurrentProgress TaskQueue.cpp:230  ←  SetCurrentProgressPublishesOnRunningTaskOnly_Test::TestBody test_task_queue.cpp:152
// 线程：Thread-1 paused，另 4 条 suspended（worker 线程可见，多线程竞态可逐线程看栈）
// 变量：current = 3 {int} / total = 10 {int} / message = "更新订阅 3/10" {const std::string &}

// 4) 任意表达式求值（验证「空闲首调用被上一行 return 拦掉」这类分支假设）
await tools.clion.xdebug_evaluate_expression({ sessionId: "133", projectPath: proj, expression: "current_.taskId.empty()" });
// => "current_.taskId.empty() = false {bool}"    ← 临时断点只命中运行中那次，假设成立

// 5) 放行跑完（action 必须大写枚举）
await tools.clion.xdebug_control_session({ sessionId: "133", projectPath: proj, action: "RESUME", timeout: 150000 });
await tools.clion.xdebug_get_debugger_status({ sessionId: "133", projectPath: proj });
// => {"sessions":[{"state":"stopped","exitCode":0}]}

// 6) 收尾：清理本次调试期打断点（temporary 断点命中后自动摘除，仍建议列一遍）
await tools.clion.xdebug_list_breakpoints({ projectPath: proj });
await tools.clion.xdebug_remove_breakpoints({ projectPath: proj, breakpoints: [{ breakpointId: "bp#1" }] });
```

### 7.2 调试参数速记（易错点）

| 参数 | 正确写法 | 踩坑表现 |
|------|----------|----------|
| `xdebug_set_breakpoints.breakpoints[]` | 必须给全 `isLogMessage`/`isLogStack`/`isCaptureThreadDump`/`suspendPolicy`/`enabled` | 缺字段 → `Invalid arguments: Missing key` |
| `xdebug_control_session.action` | 大写枚举 `RESUME`/`PAUSE`/`STEP_OVER`/`STEP_INTO`/`STEP_OUT`/`STOP`/`WAIT_FOR_PAUSE`/`DRAIN_EVENTS` | 写 `resume` → `Invalid arguments` |
| `sessionId` | 字符串（如 `"133"`），来自 `xdebug_start_debugger_session` | 省略 → `Unable to determine the target project` |
| `xdebug_get_process_output` | gtest 的 stdout 常经重定向不回传，`output` 可能为空 | 别据此判断测试是否通过，以 `exitCode` + `ctest` 为准 |

---

## 8. 能力边界（本项目）

**优先用 CLion MCP：**
- 后端逻辑「值为什么不对」→ 断点 + `xdebug_evaluate_expression`，比加日志快得多。
- 多线程竞态/死锁 → `xdebug_get_threads`（逐线程栈）+ `xdebug_set_data_breakpoint`（写坏即捕获）。
- 单条 gtest 目标调试 → `get_run_configurations` 里有 64 个配置，逐个 `test_*` 都能单独调试。
- 数据核验 → `create_database_connection` + `execute_sql_query` 直查 `guindb.db`（DAO/迁移结果）。
- 交付前诊断 → `get_file_problems` 逐文件 0 error 核验。

**仍走既有链路（不要用 CLion 替代）：**
- **UI 门禁**：`UITests` 是独立进程用 UIA 驱动 `validproxy.exe`，跨进程，调试器进不去被测 App 内部；
  以 `scripts/build-and-test.bat`（13/13）为准。`xdebug_attach_to_process` 理论可附加，实测脆弱，仅用于查 App 自身崩溃。
- **全量回归**：`ctest --test-dir build`（**必须带 `--test-dir build`**，仓库根直接 `ctest` 显示 0 个测试）。
- **构建**：`cmake --build build --parallel 8`（Ninja，Debug）；CLion 自带 profile 输出到 `cmake-build-debug/`，
  与项目规范的 `bin/` + `tests/` 输出目录不同，且缺 `version.h` 会产生假阳性诊断（§5）。
- **ASAN / 覆盖率 / cppcheck**：仍是 `cmake -DENABLE_SANITIZERS=ON` / `-DENABLE_COVERAGE=ON` / `scripts/run_static_analysis.ps1`。

---

## 9. 已知坑：`apply_patch` 在含中文的行上会吞字符

**现象**（2026-11-06 实测）：对含 CJK 注释的 C++ 文件打 `apply_patch`：

1. 上下文不匹配 → `Hunk context not found`。原因常在别处：注释换行位置与 patch 中写的不一致，必须先用 `read_file`
   取回**逐字原文**再构造 hunk。
2. 更危险的是补丁成功后**污染邻近 token**：本轮 `wxString::FromUTF8(u8"…")` 曾被写成 `wxString::FromUTF8`（丢 `::`），
   中文注释里丢字（如「测试队列」变「队列页」拼接）。

**规避规范**：
- 涉及**中文/中日韩文本**的修改，一律用 OpenCode 的 `edit` 工具（精确串替换）或 Code Mode 按 UTF-8 读写，
  **不要**对这类行使用 `apply_patch`。
- 若确实用了 `apply_patch`，**必须**逐行复核被改文件（`git diff` + `read_file`），确认 `::`、宏名、`u8"…"` 前缀完整；
  并立即 `cmake --build` 验证。
- 不要用 PowerShell 的 `Get-Content`/`WriteAllLines` 往返改写含中文的文件：控制台码页会让中文变
  `??`/乱码后再写回磁盘，造成不可逆损坏。要脚本化请用
  `[System.IO.File]::ReadAllText($p,[Text.Encoding]::UTF8)` / `WriteAllText`。

---

## 10. 团队约定：CLion MCP 可用时优先

> **规则**：若 CLion MCP server 可用（`tools.clion.*` 可调用），**修改、测试、调试等开发活动优先使用它**，
> 按其能力边界（§8）与规避规范（§9）执行；不可用时回退到 CMake/ctest/build-and-test.bat 既有链路。

落地检查顺序：

1. 先 `get_file_problems` / `search_symbol` / `search_text` 做定位与诊断；
2. 后端行为疑问用 `xdebug_*` 断点求值代替临时日志；
3. 改完用 `get_file_problems` 复查 0 error；
4. **最终仍必须**跑 `ctest --test-dir build` 与（UI 变更时）`scripts/build-and-test.bat` 作为交付门禁——
   CLion 的便利不降低门禁要求。

---

## 修订历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-11-06 | 初始版本：CLion MCP 接入（`/stream` 端点）、88 工具能力总览、`xdebug_*` 调试器映射、`version.h` 假阳性排障记录 |
| v1.1 | 2026-11-06 | 增补 §7 实测调试工作流（断点/栈/线程/求值/放行/清理 + 参数易错表）、§8 本项目能力边界、§9 `apply_patch` 中文字符吞吃坑与规避规范、§10 「CLion MCP 可用则优先使用」团队约定 |
