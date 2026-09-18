# 修复：悬浮框弹窗显示的当前代理进程监控端口错误

- **日期**: 2026-08-25
- **模块**: `AppController::startStandaloneProxy` / `AppController::readStandaloneInboundPort` / `StandaloneFloatingWidget`
- **版本**: v1.0
- **状态**: ✅ completed（实现 + 编译 + 单元测试通过；GUI 端到端待桌面确认）
- **关联文档**: `docs/bugfix/2026-08-25-Bugfix-StandaloneProxy-XrayLogForward-v1.0.md`（同会话的独立代理相关修复）

---

## 1. 问题背景

用户反馈：**悬浮框（StandaloneFloatingWidget）弹窗显示的「当前代理进程监控端口」是错误的**。

悬浮窗 hover 展开的详情表中「监听端口」列数据来自 `AppController::getWatchedStandaloneMonitors()` → `StandaloneProxyInfo.socksPort`，而该值在正常启动路径由 `startStandaloneProxy` 写入为 `socksPort`（动态分配端口）。

## 2. 根因

`startStandaloneProxy` 在把 SOCKS 入站端口写进独立代理配置时，**错误地假设 SOCKS 入站一定是 `inbounds[0]`**：

```cpp
boost::json::object& inbound = configObj["inbounds"].as_array()[0].as_object();
if (inbound.contains("listen_port")) inbound["listen_port"] = socksPort;  // sing-box
else if (inbound.contains("port"))  inbound["port"] = socksPort;         // xray
```

但 `bin/xray-config-template.json` 的 `inbounds` 顺序为：
- `inbounds[0]` = **`api`**（`"protocol": "dokodemo-door"`，tag `api`，`"port": 62826`）—— xray gRPC 控制 API，**不是** SOCKS 代理；
- `inbounds[1]` = **`socks`**（`"protocol": "socks"`，tag `socks`，`"port": 10808`）—— 真正的 SOCKS 监听端口。

因此旧代码把 **api 入站** 的端口改成了 `socksPort`，真正的 **SOCKS 入站** 仍停在模板的 `10808`；同时 `info.socksPort = socksPort` 被记录。结果：
1. 进程实际在 `10808` 监听 SOCKS，而非 `socksPort`；
2. 悬浮窗显示 `socksPort` → 与真实监听端口不一致，**显示错误**。

`readStandaloneInboundPort`（纳管/停止日志与 `getWatchedStandaloneMonitors` 回填）同样读 `inbounds[0].port`，对 xray 会读到 api 端口 `62826`，导致纳管进程也显示错误端口。

> sing-box 模板 `inbounds[0]` 即 `socks-in`（`"type": "socks"`，`listen_port: 10808`），故 sing-box 路径此前恰好正确；修复后两后端统一按 protocol/type 定位 SOCKS 入站，更健壮。

## 3. 修复方案

新增**可测单元** `standalone_config::applySocksPort`：

- `findSocksInboundIndex(inbounds)`：遍历 `inbounds`，按 `protocol`（xray）/ `type`（sing-box）匹配 `"socks" | "mixed"`（优先 `tag == "socks"`），返回索引；找不到返回 `-1`。
- `applySocksPort(config, socksPort)`：定位 SOCKS 入站并写入 `socksPort`；找不到时回退 `inbounds[0]`（兼容旧模板）。

调用点改造：
- `startStandaloneProxy`：原 `inbounds[0]` 硬编码改写 → 调用 `standalone_config::applySocksPort(configObj, socksPort)`。
- `readStandaloneInboundPort`：用 `findSocksInboundIndex` 定位后读取 `port` / `listen_port`。

`info.socksPort` 仍 = `socksPort`，但此刻 SOCKS 入站确实被写成了 `socksPort`，显示端口 == 进程真实监听端口，根因消除。

## 4. 代码改动清单

| 类型 | 文件 | 说明 |
|------|------|------|
| 新增 | `include/StandaloneConfigPort.h` | 声明 `standalone_config::findSocksInboundIndex` / `applySocksPort` |
| 新增 | `src/StandaloneConfigPort.cpp` | 实现：按 protocol/type 定位 SOCKS 入站并写端口 |
| 修改 | `src/ui/AppController.cpp` | ① include 新头；② `startStandaloneProxy` 入站端口改写改调 `applySocksPort`；③ `readStandaloneInboundPort` 按 SOCKS 入站定位读端口 |
| 修改 | `CMakeLists.txt` | `validproxy` SOURCES 增 `src/StandaloneConfigPort.cpp`；新增测试目标 `test_standalone_config_port` 并 `add_test` |
| 新增 | `tests/test_standalone_config_port.cpp` | 单测：api-first 模板下 socks 入站被正确改写、api 入站不动；sing-box `listen_port`；无 socks 入站回退 `inbounds[0]`；`findSocksInboundIndex` 定位正确 |

## 5. 验证

| 项 | 结果 |
|----|------|
| 构建 `validproxy` / 测试目标 | 0 error（仅既有 unused-parameter 警告） |
| `StandaloneConfigPortTest` | **4/4 PASS** —— api-first 模板 socks 入站改写正确且 api 入站不受影响；sing-box `listen_port` 改写正确；无 socks 入站回退 `inbounds[0]`；`findSocksInboundIndex` 定位正确 |
| `StandaloneProxyLogForwarderTest`（同会话上一项修复） | 2/2 PASS（无回归） |
| 悬浮窗 GUI 端到端 | 待桌面确认：开启代理后 hover 弹窗「监听端口」应 = 实际 SOCKS 端口（= `socks_base_port` 或 `overridePort`） |

## 6. 边界与遗留

- **桌面 GUI 端到端确认**仍受 headless 环境限制无法在此完成（需用户在桌面点「开启代理」后 hover 悬浮窗核对端口）。
- 极少数模板若 SOCKS 入站的 `protocol`/`type` 既非 `socks` 也非 `mixed`，才会回退到 `inbounds[0]`（沿用旧行为）；本项目两条模板均命中 `socks`/`mixed`，不受影响。
