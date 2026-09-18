# Bugfix: 独立代理由其它程序启动且配置文件在其它目录时无法解析监听端口

| 项目 | 内容 |
| :--- | :--- |
| 日期 | 2026-08-21 |
| 模块 | `AppController::adoptDanglingStandaloneProxies`（src/ui/AppController.cpp:1251）+ `ProcessInspector::extractConfigFileName`（src/ProcessInspector.cpp:210） |
| 关联 | 前序修复 `2026-08-21-Bugfix-StandaloneMonitor-DataColumns-v1.0.md`（同函数内双目录探测） |

## 1. 现象

其它程序（非本应用主实例/worker 实例）以 `-c <某目录>/standalone_<indexId>-xray.json`
启动 xray/sing-box 后，本应用纳管该悬垂进程时，`socksPort` 恒为 0（监控对话框显示
"-"），尽管进程实际已在监听。

## 2. 根因（RCA）

纳管流程：
1. `enumerateByName` 枚举 xray.exe/sing-box.exe 进程；
2. `extractConfigFileName` 从命令行提取 **纯文件名** `standalone_<id>-xray.json`
   （刻意拒绝含 `\`/`/` 的路径片段，见 ProcessInspector.cpp:225-228）；
3. 仅探测两个固定目录拼接该文件名：
   `exeDir\config\` 与 `exeDir\worker\config\`（AppController.cpp:1371-1379）。

其它程序把配置文件放在任意目录，上述两目录均找不到该文件 → `readStandaloneInboundPort`
返回 0 → 端口丢失。

> 注：若外部程序在命令行使用**完整路径**，第 2 步也会因含 `\` 而拒绝，但 indexId 仍能从
> 纯文件名段正确推导出（"standalone_" 到 ".json" 之间不含分隔符），故进程可被纳管、
> 仅端口解析失败——与现象一致。

## 3. 修复方案

在既有「双目录探测」之后新增**第三步回退**：从启动命令行中提取
**完整路径**（含目录）的 standalone 配置文件，直接读取其端口。

- 新增 `ProcessInspector::extractConfigFullPath(commandLine)`：
  定位 `standalone_…json`，向左回扫至路径边界（遇空格/引号停止），返回
  `D:\other\standalone_<id>-xray.json`；纯文件名（无分隔符）时返回空（交由既有逻辑处理）。
- 纳管流程：若双目录探测 `adoptedPort<=0`，用完整路径再读一次；仍失败才记 WARN 日志。

该方案：
- **权威**：直接读实际配置文件，无歧义（规避 OS 套接字查询在多 inbound 下的端口错配，
  特别是 sing-box 模板含多个 DNS inbound）；
- **最小改动**：不动 `extractConfigFileName` 既有契约（`isProcessRunningWithConfig`
  等调用方不受影响）；`readStandaloneInboundPort` 已接受完整路径，无需改动。

## 4. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `include/ProcessInspector.h` | 声明 `extractConfigFullPath` |
| `src/ProcessInspector.cpp` | 实现 `extractConfigFullPath` |
| `src/ui/AppController.cpp` | 纳管端口解析增加"完整路径"回退分支 |
| `tests/test_process_inspector.cpp`（新增） | `extractConfigFullPath` / `extractConfigFileName` 单测 |
| `CMakeLists.txt` | 注册 `test_process_inspector` 测试目标 |

## 5. 验证

- [ ] TDD RED→GREEN：新增 `extractConfigFullPath` 用例先行编译失败，实现后通过
- [ ] 构建 0 error
- [ ] ctest 回归通过（NetworkMonitorTest 环境抖动除外，已知偶发）
- [ ] 验证：外部程序在其它目录启动 standalone 代理后，Ctrl+M 监控对话框正确显示监听端口
