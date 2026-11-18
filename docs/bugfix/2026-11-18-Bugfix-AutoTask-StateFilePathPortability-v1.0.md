---
title: "bugfix: 换机运行自动任务刷 failed to write state file（config 内绝对路径 + 无回退）"
type: bugfix
status: completed
date: 2026-11-18
module: autotask
version: v1.0
---

# 换电脑运行自动任务：`failed to write state file: E:\...\bin/worker/autotask_state.json`

## 1. 症状（用户现场日志）

```
[2026-11-18 08:57:59] [ERROR] AutoTask: failed to write state file: E:\eclipse_workspace\multiple_thread_validproxy\bin/worker/autotask_state.json
[2026-11-18 08:57:59] [ERROR] AutoTask: failed to write state file: E:\eclipse_workspace\multiple_thread_validproxy\bin/worker/autotask_state.json
```

在**另一台电脑**上运行（发布包/拷贝的 `bin/` 目录），自动任务每个步骤都刷一条。

## 2. 这些路径是硬编码的吗？

**不是源码硬编码**，是 `bin/config.json` 里存的**上一台机器的绝对路径**：

```json
"auto_task": { "state_file": "E:\\eclipse_workspace\\multiple_thread_validproxy\\bin/worker/autotask_state.json" }
```

- 源码默认值本身是可移植的：`<exe 目录>/worker/autotask_state.json`
  （`AutoTaskManager::defaultStateFilePath()`，仅在 `state_file` 为空时使用）。
- 但把 `bin/`（含 `config.json`）整体拷到别的电脑后，配置里的绝对路径仍然指向原机器，
  `E:`/该目录不存在 → `std::filesystem::create_directories` 失败 →
  `utils::writeFileAtomically` 返回 false → 旧实现**每一步**都记一条 `ERR`，且从不回退。

同类“机器相关”的绝对路径还有（同一份 config 里）：`database.path`、
`sync.source_db` / `sync.target_db`、`proxy.xray_executable` / `proxy.xray_asset_dir`、
`proxy.singbox_*`、`log.dir`（若配置）。它们换机后同样需要人工改，但只有
`auto_task.state_file` 会在任务运行中**持续报错**。

## 3. 根因

1. `AutoTaskManager` 构造函数直接采用配置值，不做可用性判断；
2. `saveStateFile()` 每次失败都 `Logger::write(..., ERR)`，任务每推进一步就重复一条；
3. 配置值为绝对路径时不会退回到 exe 旁默认路径。

## 4. 修复

新增头文件 `include/AutoTaskStatePath.h`（纯函数，可单测）——断点文件路径解析规则：

| `auto_task.state_file` 配置 | 解析结果 |
| --- | --- |
| 空 / 全空白 | `<exe 目录>/worker/autotask_state.json` |
| 相对路径（`worker/x.json`） | 以 **exe 目录**为基准拼接（跟着程序走） |
| 绝对路径（`C:/…`、`\\server\…`、`\rooted`） | 原样使用 |

并新增 `autotask::ensureParentDirectory()`：探测目标父目录是否可用（存在且是目录、
或可递归创建）。

`AutoTaskManager` 构造函数改为：

```cpp
const std::string preferred = autotask::resolveStateFilePath(config_.auto_task.state_file, exeDir);
if (autotask::ensureParentDirectory(preferred)) {
    stateFilePath_ = preferred;
} else {
    stateFilePath_ = autotask::defaultStateFilePath(exeDir);   // 回退
    autotask::ensureParentDirectory(stateFilePath_);
    Logger::write("AutoTask: configured state_file is not usable on this machine ('…'); "
                  "using '…' instead. Set auto_task.state_file to a relative path …",
                  LogLevel::WARN);                                // 只报一次
}
```

`writeState()` 的写失败日志改为**每次任务生命周期只报一次**（`stateWriteErrorLogged_`），
`saveStateFile()` 本身不再打印（保留返回 bool，由调用方决定口径）。
CLI `cancel-task` 同步使用同一解析器，并对写失败给出明确提示。

## 5. 回归测试

| 测试 | 用例 |
| --- | --- |
| `tests/test_autotask_state_path.cpp`（新 target `AutoTaskStatePathTest`，9 例） | 空/空白→默认；相对→exe 目录；盘符绝对原样；UNC/根相对判定；exe 目录尾部分隔符归一化；父目录缺失可创建；父路径是文件时拒绝；空路径拒绝 |
| `tests/test_autotask.cpp` 新增 2 例 | `UnusableConfiguredPathFallsBackToExeDir`（用“文件当父目录”确定性复现换机场景：断言回退到 `defaultStateFilePath()` 且只产生 1 条 WARN）、`RelativeConfiguredPathFollowsExeDir` |

## 6. 验证

- 构建 `build-tc-clion-msvcrt-Release` EXIT 0（`--parallel 6`）。
- `tests\test_autotask_state_path.exe` → **9/9 PASSED**。
- `tests\test_autotask.exe` → **13/13 PASSED**（含上述 2 个新用例；日志实测输出
  `[WARN] AutoTask: configured state_file is not usable on this machine … using '<exe>/worker/autotask_state.json' instead`）。
- 全量 `ctest`：见 §6.1。

### 6.1 全量回归

`ctest --test-dir build-tc-clion-msvcrt-Release --output-on-failure` → **74/76 通过**
（318 s，EXIT 8）。失败两项均为既有问题、与本次改动无关：

- `44 - LogStatisticsEventTest`（`0xC0000139`）：测试 exe 未嵌 Common Controls v6 manifest，
  加载到 comctl32 5.82 缺 `GetWindowSubclass`（见
  `docs/bugfix/2026-11-17-Bugfix-Win7-UIEncoding-MenuAssertCrash-v1.0.md` §5.1）。
- `64 - UI_FLOATINGWIDGET`：既有已知 flaky（桌面 UWP 覆盖窗口导致采样漂移）。

新增 `AutoTaskStatePathTest`（#76 套件内）通过。

## 7. 换机部署建议（文档口径）

- 拷贝 `bin/` 到新机器后，把 `config.json` 里的绝对路径改成新机器路径，或：
  - `auto_task.state_file` **留空**（推荐，自动跟随 exe 目录）或写成相对路径
    `worker/autotask_state.json`；
  - `database.path` 指向新机器的库文件；`proxy.xray_executable` 指向新机器的
    `xray.exe`（参见 README「代理内核需自行下载并配置目录」）。

## 9. 审计结论（2026-11-18：运行根目录 / 硬编码绝对目录筛查）

- **默认运行根目录**：✅ 已以 exe 所在目录为根。
  `ConfigReader::load()` 取 `utils::getExecutableDir()` 作为 `exeDir` 传给所有 section parser；
  `ConfigPathResolver::resolve()`、`DatabaseConfigParser` / `SyncConfigParser` /
  `ProxyConfigParser` / `AutoTaskConfigParser` / `StandalonePoolConfigParser` 均对相对
  路径按 `exeDir` 解析；`AutoTaskManager::defaultStateFilePath()`、
  `AppController`/`MainFrame`/`ProxyBatchTester` 的 `config/`、`proxies/` 输出，
  `ShareLinkExportService` 等也全部基于 `utils::getExecutableDir()`。
  源码（`src/**`、`include/**`）中**无任何**硬编码盘符/绝对路径字面量（仅 `ProcessInspector.h`
  一行注释示例）。
- **真正的“硬编码绝对目录”**：仅存在于**运行时数据** `bin/config.json`（由应用在
  用户调整配置时写出），例如 `database.path`、`sync.source_db/target_db`、
  `proxy.xray_executable`、`proxy.xray_asset_dir`、`auto_task.state_file`。
  这不是源码硬编码，但拷贝 `bin/` 到别机时会失效。
- **本次处理**：
  - `auto_task.state_file` 已清空为 `""`（本机语义不变：回退到 `<exeDir>/worker/autotask_state.json`），
    使发布包/拷贝开箱即可用。
  - 其余机器相关字段（数据库路径、xray/singbox 内核路径与资源目录）保留为当前机器配置；
    README 已明确它们需按目标机器调整（见 README「环境要求」的代理内核下载提示）。
  - `bin/test_config.json` / `bin/test_config_empty.json` 为测试夹具，保持原值（测试在 sandbox
    中生成自己的 config）。
- **建议**：发布时不要附带 `bin/config.json` 的生产副本；用户首次启动按引导生成。
  （当前仓库 tracked 的 `bin/config.json` 仍为本机生产配置。）
