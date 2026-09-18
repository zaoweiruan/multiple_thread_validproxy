# Bugfix: 独立代理携带/UI 刷新端口与进程真实监听端口漂移（cmdline 权威修复）

| 项目 | 内容 |
| :--- | :--- |
| 日期 | 2026-09-02 |
| 模块 | `AppController::adoptDanglingStandaloneProxies` / `getWatchedStandaloneMonitors`（src/ui/AppController.cpp、src/ui/AppController.h） |
| 关联 | 前序修复 `2026-08-21-Bugfix-StandaloneProxy-PortExternalConfigDir-v1.0.md`（cmdline 完整路径作为**第三兜底**）；本次将其提升为**首位权威**，是同一问题的延续修复 |

## 1. 现象

用户使用视觉模型解析 `bin/log/2fa39116-099e-4fda-bc9e-792f5f0efff1.png` 截图后报告：
"没有进程监听10810端口"。实测 3 个运行中的 xray.exe 孤儿进程：
PID 5816 监听 10808、PID 8540 监听 10811、PID 21500 **实际只监听 10809**；
而 UI 与日志登记 PID 21500（indexId=5269891185604931259）为 `Adopted ... SOCKS5 :10810`。
**登记端口(10810) ≠ 实际监听端口(10809)，10810 无进程监听。**

## 2. 根因（RCA）

同一 indexId 在**两套 config 目录**各有一份配置文件，端口不同：
- `bin\config\standalone_5269891185604931259-xray.json` = **10810**（修改于 2026/9/1 10:25，来自基于项目根 `bin\` exeDir 的 validproxy 启动写入）；
- `bin\worker\config\standalone_5269891185604931259-xray.json` = **10809**（修改于 2026/9/2 15:20，来自基于 `bin\worker\` exeDir 的 `validproxy.exe` 副本启动，且 PID 21500 实际用**这份** `-c` 启动）。

携带/UI 刷新端口解析按固定目录顺序 `{exeDir\config, exeDir\worker\config}` **目录探测**，
**优先命中陈旧的 `bin\config`（10810）**，于是 UI/日志登记 10810；但进程真实用 `bin\worker\config`
（10809）启动。cmdline 权威路径（`extractConfigFullPath`）虽在 08-21 加入，但仅作为**第三兜底**，
目录探测一旦命中即不再使用 → 端口漂移。

设计反模式：`bin\worker\validproxy.exe` 副本分离运行导致两套 config 目录、同一 indexId 端口漂移；
目录探测无法区分哪份配置是进程真实使用的。

## 3. 修复方案

**以进程 `-c` 命令行指定配置为权威端口源**，目录探测降为纯兜底：
1. **`adoptDanglingStandaloneProxies`**：Step (a) 先 `extractConfigFullPath(procs[i].commandLine)`
   → `readStandaloneInboundPort(fullPath)` 读端口；`>0` 时记 `info.configPath = fullPath`。
   Step (b) cmdline 为空/读不到端口时，才回退目录探测 `{exeDir\config, exeDir\worker\config}`，
   命中记 `info.configPath = dir + configFileName`。**关键缺口修复**：原携带路径从不设置
   `configPath`，导致 UI 刷新无法拿到权威路径。
2. **`getWatchedStandaloneMonitors`**：新增 `WatchedEntry::configPath` 字段（Pass1 从
   `it->second.configPath` 复制）；刷新循环先 `readStandaloneInboundPort(configPath)`，
   失败才回退目录探测。`row.socksPort = (diskPort>0) ? diskPort : watched[j].socksPort` 不变。

效果：UI/日志登记端口 == 进程真实监听端口，杜绝两套 config 目录下端口漂移。

## 4. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ui/AppController.cpp` | `adoptDanglingStandaloneProxies` cmdline 权威优先 + 记录 `configPath`；`getWatchedStandaloneMonitors` configPath 优先刷新 |
| `src/ui/AppController.h` | `WatchedEntry` 结构新增 `configPath` 字段 |

## 5. 验证

- [x] 构建 0 error：`cmake --build build --parallel 8` 成功（bin/validproxy.exe 2026/9/2 17:18:36，仅既有 unused-parameter warning，与本次无关）
- [ ] ctest -V 回归无异常（本次改动仅影响 adopt/UI 刷新端口解析路径，不影响单测逻辑）
- [ ] 实机验证：`bin\worker\validproxy.exe` 副本启动的孤儿进程被携带/刷新时登记端口 == netstat 实际监听端口