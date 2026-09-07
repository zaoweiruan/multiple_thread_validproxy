# Bugfix: 独立代理池启动端口冲突（10809）

- 日期: 2026-08-26
- 严重度: 功能阻塞（独立代理池无法启动）
- 状态: ✅ fixed / 验证全绿

## 1. 现象

日志 `bin\log\ui_20260826_165236.log` 中：

```
16:52:36 [INFO] [StandaloneProxy] adopt Dangling standalone proxy PID=5708 @10808
16:52:53 [INFO] [StandaloneProxy] started proxy (socks=10809)   # 10808 被占，改用备用 10809
16:53:20 [INFO] [XrayInstance] Executing: ...\xray.exe run -c ".../xray_config_10809.json"
16:53:20 [ERROR][StandaloneProxyPool] failed to start pool instance
16:53:20 [ERROR][AppController] proxy pool start failed
```

子进程 `xray_stdout_10809.log` 内容：

```
Failed to start: main: failed to load config files: ... > infra/conf:
failed to build inbound config with tag socks-in > infra/conf:
unable to listen on domain address: 127.0.0.1:10809
```

进程退出码 `exitCode=23`，原 `logDeathDetails` 只输出 `stderr tail` 而 stderr 文件为空，导致应用日志只看到裸 `exitCode=23`，根因不可见。

## 2. 根因

独立代理池 `StandaloneProxyPool` 在 `AppController::startProxyPool` 中直接使用配置里的
固定 `cfg.socksPort`（默认 `10809`，见 `ConfigReader.h:14`）构造 `XrayInstance` 并监听，
**完全没有经过 `PortManager`** 的端口分配/占用登记。

而同一进程内的「独立代理（standalone proxy）」路径走 `PortManager::findAvailable` 分配端口。
于是一次会话里：

1. 先启动一个 standalone proxy，因 `10808` 被一个悬空进程 PID=5708 占用，
   `PortManager` 自动改用备用端口 `10809` 并登记占用；
2. 用户再打开「独立代理池」，池用固定端口 `10809` 启动 xray，与已运行的 standalone proxy
   在 `127.0.0.1:10809` 撞端口 → xray 绑定失败退出。

这不是 xray 自身 bug，而是**端口分配策略不一致**（池固定端口 vs standalone proxy 走 PortManager）。

## 3. 修复

### 3.1 抽出独立端口解析函数 `proxy::resolvePoolPorts`
`include/StandaloneProxyPool.h` 声明，`src/StandaloneProxyPool.cpp` 实现：

```cpp
bool resolvePoolPorts(config::StandalonePoolConfig& cfg, int attempts = 200);
```

逻辑：
- socks = `PortManager::findAvailable(desiredSocks= cfg.socksPort>0?cfg.socksPort:10809, attempts)`；
  - 失败（无可用端口）→ 直接 return false；
- api = `PortManager::findAvailable(desiredApi= cfg.apiPort>0?cfg.apiPort:10810, attempts)`；
  - 失败 → `freePort(socks)` 后 return false（不泄漏已保留端口）；
- 防御：`api == socks` 时 `freePort(api)` 并以 `socks+1` 为起点重解析 api；
- 成功后写回 `cfg.socksPort = socks; cfg.apiPort = api;`。

> 端口在 `start()` 之前解析、`XrayInstance` 构造时即使用已保留端口；`findAvailable` 同时做
> OS 级 `connect` 探测（`isInUseUnlocked`）与 `usedPorts_` 登记，避免与运行中进程或本进程
> 其它实例冲突。实例在构造时按端口创建，所以端口解析必须放在构造前，故抽为独立函数而非塞入
> 构造函数——也便于单测（无需拉起真实 xray）。

### 3.2 `AppController::startProxyPool` 接线
`src/ui/AppController.cpp`：

```cpp
config::StandalonePoolConfig poolCfg = config_.standalone_pool;
if (!proxy::resolvePoolPorts(poolCfg)) { /* ERR + return false */ }
std::shared_ptr<proxy::StandaloneProxyPool> pool(new proxy::StandaloneProxyPool(poolCfg, xrayPath, configDir));
...
if (!pool->start()) {
    PortManager::freePort(poolCfg.socksPort);
    PortManager::freePort(poolCfg.apiPort);
    return false;
}
{ std::lock_guard<std::mutex> lock(poolMutex_);
  proxyPool_ = pool; poolSocksPort_ = poolCfg.socksPort; poolApiPort_ = poolCfg.apiPort; }
```

### 3.3 `AppController::stopProxyPool` 释放端口
`src/ui/AppController.cpp` + `src/ui/AppController.h` 新增成员 `poolSocksPort_/poolApiPort_`：

```cpp
void AppController::stopProxyPool() {
    ... 取 pool + ports，reset proxyPool_ 与 ports ...
    if (pool) pool->stop();
    if (socks > 0) PortManager::freePort(socks);
    if (api > 0)   PortManager::freePort(api);
}
```

### 3.4 Xray 崩溃日志可观测性补强
`src/XrayInstance.cpp::logDeathDetails` 在原有 `stderr tail` 基础上，额外读取
**stdout** 尾部（xray 把 `Failed to start` / 绑定错误打到 stdout 而非 stderr）：

```cpp
std::string stderrTail = readFileTail(stderrLogPath_, DEATH_STDERR_TAIL_BYTES);
std::string stdoutTail = readFileTail(stdoutLogPath_,  DEATH_STDERR_TAIL_BYTES);
if (!stderrTail.empty()) msg += ", stderr tail:\n" + stderrTail;
if (!stdoutTail.empty())  msg += ", stdout tail:\n"  + stdoutTail;
```

### 3.5 链接修复
`CMakeLists.txt` 的 `test_standalone_proxy_pool` 目标源列表补 `src/PortManager.cpp`
（原缺导致 `undefined reference to PortManager::findAvailable/freePort/clearPorts`）。

## 4. 测试

`tests/TestStandaloneProxyPool.cpp` 新增（含 `#include "PortManager.h"`）：

- `ResolvePoolPortsAvoidsInUsePort`：先 `PortManager::findAvailable` 占用某端口，设
  `cfg.socksPort=occupied`，断言 `resolvePoolPorts` 返回 **不同** 端口且 socks≠api；
- `ResolvePoolPortsKeepsFreeDesiredPort`：端口空闲时保留期望值。

## 5. 验证

- 构建 validproxy / test_standalone_proxy_pool：`0 error`
- `StandaloneProxyPoolTest`：**7/7 PASS**（2 个新增碰撞单测 PASS）
- `UI_POOL`：**5/5 PASS**（交付闸门）
- `PortManagerTest`、`StandaloneConfigPortTest`：PASS
- ctest（相关子集）100% passed

## 6. 已知限制 / 后续

- 仅修复「同进程内多路径端口冲突」；跨进程（另一 validproxy 实例）端口冲突仍由 OS 探测层兜底，
  但 `usedPorts_` 是进程内状态，不跨进程共享——属既有设计，本次未扩展。
- `ConfigReader.h:14` 默认 `socksPort=10809` 仍保留为「期望端口」提示，不再是强制值。
