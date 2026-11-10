---
title: "bugfix: CLion 工具链切换后 53 个 Google Test 运行配置红叉 + MCP execute_run_configuration 挂起"
type: bugfix
status: fixed
date: 2026-11-10
updated: 2026-11-10
---

# CLion 工具链切换后 53 个 Google Test 运行配置红叉（MCP 执行挂起）

## 1. 症状

CMakeLists.txt 工具链从 w64devkit GCC 16.2.0 切到 CLion 内置 MinGW GCC 15.2.0（`E:/clion/bin/mingw`）后：

- CLion 运行配置列表 **53 个 `factoryName="Google Test"` 配置全部红叉**（`test_autotask`、`test_connectivity_verify` 等全部 `test_*` 目标），提示「不受支持的构建系统」/未绑定 CMake 目标；9 个 CMake 应用程序配置（如 `test_curl_easy_handle`）正常。
- CLion MCP `execute_run_configuration` 执行 `test_autotask`（Google Test 类型）**无输出被中断**：连续多次返回 `{"error":{"type":"aborted","message":"Tool execution interrupted"}}`（`waitForExit` true/false 均同）；对应 `%TEMP%\*.tmp`（16:22–16:25 共 6 个）全 0 字节 → 进程未产出任何输出即卡死。
- `idea.log`（`C:\Users\dsm\AppData\Local\JetBrains\CLion2026.3\log\idea.log`）留有名录：`CMakeExecutableTool - [cmake.exe --build]: (exit code 1) Usage: cmake --build <dir>`（15:57:50/51/52、16:08:55，L46350–L46968）——BuildBeforeRun 发出的 `cmake --build` **缺目录参数**。

## 2. 排查过程中的两个错误假设（已证伪，留档避免重复）

| 假设 | 证伪证据 |
|---|---|
| **A. DLL 缺失导致测试进程起不来** | 干净 PATH 下 `tests/test_autotask.exe` 复现 `EXIT=0xC0000135 STATUS_DLL_NOT_FOUND`；但正常 PATH（含 `E:\clion\bin\mingw\bin`，机器级 PATH 已排在本目录 Tesseract-OCR 之前）下 `test_autotask.exe` **11/11 PASSED** → DLL 非问题，红叉是配置类型问题。 |
| **B. Reload CMake 项目能修复红叉** | 实测 reload 后 `.idea/workspace.xml` 中 53 条 GTest 配置的 `PROJECT_NAME`/`TARGET_NAME`/`CONFIG_NAME` **三个绑定属性被全部剥掉**（reload 前尚在），`<method v="2" />` 仍为空 → 红叉依旧。reload 只从 CMake 模型重新序列化，**不修复坏条目**。 |

## 3. 根因（RCA）

工具链切换后项目模型重建，旧的 Google Test 模板配置**目标绑定失效**：配置块有 `PROJECT_NAME`/`TARGET_NAME`/`CONFIG_NAME` 但缺 `RUN_TARGET_PROJECT_NAME`/`RUN_TARGET_NAME`，且 `<method v="2" />` 为空（无 `BuildBeforeRunTask`）→ Run 前构建步骤解析不出目标 → 发出畸形 `cmake --build`（usage error，进程不产生）→ 测试进程永不启动 → MCP `execute_run_configuration` 挂起至中断（aborted）。红叉与挂起是同一根因的两面。

## 4. 修复

**UI 删除 53 个 GTest 配置 → 重启 CLion**（不是 reload）：Run | Edit Configurations 里选中全部 Google Test 类型条目 → − → Apply → OK → 重启 IDE。

删除后实测：`CMakeGoogleTestRunConfigurationType` 计数 **53 → 0**，红叉消失；9 个 CMake 应用程序配置不受影响；`.idea/runConfigurations/` 仍为空（无文件型配置参与）。注意 **CLion 2026.3 重启后不会为已删配置自动重建永久 GTest 配置**（实测计数=0），「删除」即终态，后续测试走 §5 的两条已验证路径。

## 5. 验证证据

| 项 | 结果 |
|---|---|
| MCP 跑单套件（CTest 配置 + 显示名过滤） | `execute_run_configuration("所有 CTest", "-R ^AutoTaskTest$ --output-on-failure")` → `exitCode=0`，`1/1 Test #19: AutoTaskTest Passed 0.55 sec`（**显示名 ≠ 目标名**：`AutoTaskTest` vs `test_autotask`） |
| MCP 跑整目标（CMake Application 类型） | `execute_run_configuration(test_curl_easy_handle)` → `exitCode=0`，输出全绿 `All tests passed!` |
| MCP 调试链路 | `xdebug_start_debugger_session(test_curl_easy_handle)` → sessionId=254 running，停止后 exitCode=0 |
| 全量 ctest（cmake-build-debug/，CLion 工具链树） | 69 例 **68 过 1 失败**，exit=8；唯一失败 #45 `XrayManagerStartupTest.RealXrayStartsBounded`（冷启动 6472 ms > 3 s 已知噪声）；`UI_TASKLEDGER` 11 assertions 8.05 s 绿（`temp/ctest_clion_tc.log`） |
| 配置实体终态 | workspace.xml：GTest 0 / CMake Application 9 / Catch 1 / CTest 1；`.idea/runConfigurations/` 空 |

## 6. 残余风险与建议

1. **批量重建的确定性子案（本次未用，需要时随时可做）**：向 `.idea/runConfigurations/` 写 53 个 `test_*.xml`，类型 CMake Application，模板逐字取自已验证的 `test_curl_easy_handle` 配置块（含 `TARGET_NAME` + `RUN_TARGET_NAME` + `BuildBeforeRunTask`）。该目录被 gitignore，不污染仓库；IDE 启动时读取。
2. **MCP 跑测试的推荐姿势**：单套件 → CTest 配置 + `-R ^<显示名>$`；整目标 exe → CMake Application 配置；单用例过滤 → 编辑器 gutter 图标/临时配置。**不要**用 CTest 配置跑 `--gtest_filter`（`temp/googletest_err.md` 明确不兼容），也不要再用 Google Test 模板配置跑 MCP。
3. **workspace.xml 是 IDE 托管文件**：IDE 运行时手改会被覆盖（本次 reload 剥属性即为实例）；删配置必须走 UI。
4. **通用教训**：工具链/项目模型变更后，先检查运行配置绑定是否完好（红叉 = 绑定失效信号）再跑 MCP 执行；MCP 挂起 + 0 字节临时输出文件 = 进程未启动，先看 idea.log 的 `CMakeExecutableTool` 记录。

## 7. 关联

- 用户提供的根因分析：`temp/googletest_err.md`（GTest 模板配置未绑定 CMake 目标 → 建议改 CMake 应用程序类型）。
- 同日另一构建产物层 bugfix：`docs/bugfix/2026-11-10-Bugfix-ConnectivityVerifyTest-CorruptPdataCrash-v1.0.md`（INDEX #116）。
- 工具链切换背景：`CMakeLists.txt` 工具链选择块（`$ENV{VALIDPROXY_MINGW_ROOT}` > `E:/clion/bin/mingw` > PATH，提交 `018dee1`）。
- CLion MCP 能力边界：`docs/2026-11-06-Reference-CLion-MCP-Usage-v1.1.md`。
