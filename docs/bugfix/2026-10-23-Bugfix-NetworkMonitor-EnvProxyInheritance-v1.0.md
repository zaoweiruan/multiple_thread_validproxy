# NetworkMonitor 误报断网：探测请求继承环境变量代理（HTTP_PROXY/HTTPS_PROXY）指向已死亡的本地端口

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复「网络实际正常但 `NetworkMonitor` 全部探测返回 `curl_error=7`、判定断网并中止 AutoTask」——让连通性探测**不继承进程环境变量里的代理**，直连目标 URL。

**Tech Stack:** C++17 / libcurl（CURLOPT_PROXY / CURLOPT_NOPROXY）/ GoogleTest / CMake + Ninja（MinGW GCC）

**约束:** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`。

---

## 0. 现场证据

### 0.1 日志

`bin\worker\log\ui_20261023_082641.log`（123 行）：

```
[2026-10-23 08:26:43] [WARN] NetworkMonitor: probe failed url=https://www.baidu.com  curl_error=7 (Could not connect to server)
[2026-10-23 08:26:45] [WARN] NetworkMonitor: probe failed url=https://www.qq.com     curl_error=7 (Could not connect to server)
[2026-10-23 08:26:46] [ERROR] AutoTask: aborted at step 0 [update_all] - network disconnected
[2026-10-23 08:26:47] [WARN] NetworkMonitor: probe failed url=https://www.taobao.com curl_error=7 (Could not connect to server)
[2026-10-23 08:26:47] [ERROR] Network connection check failed (consecutive failures: 1)
[2026-10-23 08:27:03] [ERROR] Network connection LOST (consecutive failures: 2)
[2026-10-23 08:27:21] [ERROR] Network connection LOST (consecutive failures: 3)
[2026-10-23 08:27:38] [ERROR] Network connection LOST (consecutive failures: 4)
```

特征：3 个互不相关的域名**同时**失败、错误码完全一致（7 = `CURLE_COULDNT_CONNECT`）、周期稳定 ⇒ 不是 DNS 故障、不是目标站点故障，而是**统一的出站路径**出问题。

### 0.2 进程环境

```
$env:HTTP_PROXY   = http://127.0.0.1:10808      # 实际变量名 hTTP_PROXY（Windows 不区分大小写）
$env:HTTPS_PROXY  = http://127.0.0.1:10808      # 实际变量名 hTTPS_PROXY
```

`10808` 是本项目的 SOCKS 基端口（`ConfigReader` 的 `proxy.socks_port` 默认值），当前并无进程监听。

WinINET 系统代理：`ProxyEnable=0`、`ProxyServer=127.0.0.1:10808`（陈旧残留，未生效）⇒ **只有环境变量路径**。

### 0.3 对照实验（`curl.exe`）

| 场景 | 命令 | 结果 |
| :--- | :--- | :--- |
| 直连 | `curl.exe --noproxy '*' -o NUL -w "%{http_code}" https://www.baidu.com` | `direct_http=200` |
| 继承环境代理 | `$env:HTTP_PROXY='http://127.0.0.1:10808'; $env:HTTPS_PROXY=$env:HTTP_PROXY; curl.exe -o NUL -w "%{http_code}" https://www.baidu.com` | `proxy_http=000`，`curl_exit=7` |

第二条与日志中 `curl_error=7` **完全一致** ⇒ 根因确证。

---

## 1. 根因

libcurl 在未显式设置 `CURLOPT_PROXY` 时会读取 `http_proxy` / `HTTPS_PROXY` / `ALL_PROXY` 环境变量（大小写不敏感）。`curl_easy_setopt(CURLOPT_NOPROXY, ...)` 默认值为空 ⇒ 该环境代理对**所有**主机生效。

`src/NetworkMonitor.cpp:63` 的探测路径是仓库里**唯一**不设置代理的 `CurlEasyHandle` 消费者：

```cpp
CurlEasyHandle curl;
if (!curl.valid()) return result;

curl.setUrl(url)
    .setNoBody(true)
    .setTimeoutMs(static_cast<long>(timeoutMs))
    .setConnectTimeoutMs(static_cast<long>(timeoutMs / 2))
    .setFollowLocation(true);
// ← 未调用 setProxy()，也未禁用代理
```

⇒ 请求被送到 `127.0.0.1:10808`（无监听）⇒ `CURLE_COULDNT_CONNECT(7)` ⇒ `connected_` 置 false ⇒ `AutoTask` 在网络前置检查处中止。

其余 8 个 `CurlEasyHandle` 消费者（`ProxyTester` / `ProxyFinder` / `ProxyHealthEvaluator` / `ProxyProbePool` / `UrlFetcher` / `SubitemUpdaterV2` / `SubscriptionUpdater` / `RegionBatchResolver`）**全部显式调用 `setProxy()`**，环境变量被覆盖，因此不受影响：

```
src/ProxyHealthEvaluator.cpp:52  curl.setProxy(proxyUrl)
src/ProxyFinder.cpp:251          curl.setProxy(proxyUrl)
src/ProxyProbePool.cpp:310       curl.setProxy("http://127.0.0.1:" + std::to_string(socksPort));
src/ProxyTester.cpp:22           curl.setProxy(proxyUrl)
src/UrlFetcher.cpp:41,70         curl.setProxy(proxyUrl)
src/SubitemUpdaterV2.cpp:726     curl.setProxy(proxyStr)
src/update/SubscriptionUpdater.cpp:69  curl.setProxy(proxyStr)
```

### 1.1 为什么是缺陷而非「用户环境配错」

连通性探测的语义是「**本机能否直连外网**」。它必须独立于「当前进程是否配置了代理」：

1. 进程环境变量可能由用户上一次代理会话残留写入（本例），也可能是 shell 继承。
2. 若探测走代理，则代理故障会被误判为「网络断网」⇒ 触发 `AutoTask` 中止、`ProxyBatchTester` 暂停等全局副作用，与真实网络状态无关。
3. 项目自身**从不写入** `HTTP_PROXY`/`HTTPS_PROXY`（`AppController.cpp:1041` 的通用 `SetEnvironmentVariableA` 与 `:2786` 的 `XRAY_LOCATION_ASSET` 是仅有的两处），因此「谁写的」不可控，只能在消费侧做防御。

---

## 2. 修复方案

### 2.1 `include/CurlEasyHandle.h`

新增与 `setProxy()` 对称的 `disableProxy()`（链式返回）：

```cpp
    /// Explicitly bypass every proxy source: environment variables
    /// (http_proxy / HTTPS_PROXY / ALL_PROXY, case-insensitive on Windows)
    /// and any inherited CURLOPT_PROXY default.
    /// Use for probes that must measure DIRECT connectivity.
    CurlEasyHandle& disableProxy() {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_PROXY, ""), "disableProxy");
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_NOPROXY, "*"), "disableProxy");
        return *this;
    }
```

两个选项都要设：
- `CURLOPT_PROXY, ""` 清空显式代理；
- `CURLOPT_NOPROXY, "*"` 阻断 libcurl 从环境变量补全代理（只设前者时 libcurl 仍会回落到环境变量）。

### 2.2 `src/NetworkMonitor.cpp`

`CheckURLWithDnsFlag` 在链式配置中加入 `.disableProxy()`，并注释 RCA：

```cpp
        curl.setUrl(url)
            .disableProxy()   // RCA 2026-10-23: env HTTP_PROXY/HTTPS_PROXY (stale
                              // 127.0.0.1:10808) made every probe fail with curl 7
            .setNoBody(true)
            ...
```

### 2.3 可观测性（可选，低成本）

`NetworkMonitor::Start()` 中若检测到环境变量代理且值非空，写一条 `INFO` 日志说明「探测将忽略环境代理」。**不做**环境变量清洗——修环境变量属于用户侧行为，代码不应越界。

---

## 3. 任务

- [x] T1 在 `CurlEasyHandle` 增加 `disableProxy()`（返回值链式，风格对齐 `setProxy`）。
- [x] T2 `NetworkMonitor::CheckURLWithDnsFlag` 链式接入 `.disableProxy()` + RCA 注释。
- [x] T3 `NetworkMonitor::Start()` 增加「检测到环境代理，探测将直连」的 INFO 日志。
- [x] T4 `tests/test_network_monitor.cpp` 新增回归用例：在测试进程内把 `HTTP_PROXY`/`HTTPS_PROXY` 置为死端口 `127.0.0.1:1`，对既有 `LoopbackHttpServer` 发起探测，断言 `success == true`；用例结束恢复原值（含「原本不存在则 unset」分支）。
- [x] T5 构建 + 定向测试 + 全量 ctest + `auto` 检查。
- [x] T6 登记 `docs/INDEX.md` §9.1（编号 106）与 `docs/plans/project-plans-tracker.md` 表尾。

### 3.1 T4 测试设计要点

- 复用既有 `LoopbackHttpServer`（Winsock 回环，`sin_port = 0` 取临时端口）；回环地址本身**不经过** `CURLOPT_NOPROXY` 影响，故必须**显式**污染环境变量才能复现「继承代理」这一条路径。
- `SetEnvironmentVariableA("HTTP_PROXY", "http://127.0.0.1:1")` 同时设 `HTTPS_PROXY`。
- 断言用保存的原始值做 RAII 恢复（`GetEnvironmentVariableA` 返回 0 且 `GetLastError() == ERROR_ENVVAR_NOT_FOUND` ⇒ 原本不存在 ⇒ 恢复为 `SetEnvironmentVariableA(name, nullptr)`）。
- 反向对照（可选）：临时把 `disableProxy()` 注释掉的版本跑一次应失败——若失败则证明该用例真的有鉴别力。

---

## 4. 影响面与风险

| 项 | 评估 |
| :--- | :--- |
| 直连判定语义 | **正确**：探测目的就是验证本机直连能力 |
| 需要经代理才能出网的环境 | 探测会失败并报断网——这与「网络不通」的真实语义一致；且原行为在无环境变量时同样是直连 |
| `CURLOPT_NOPROXY "*"` 副作用 | 仅影响该 `CurlEasyHandle` 实例，`DnsShareCache`（`CURL_LOCK_DATA_DNS`）不受影响，DNS 缓存继续命中 |
| 其它 8 处消费者 | 不改动（均已显式 `setProxy`） |

---

## 5. 验证计划

1. `cmake --build build --parallel 8` ⇒ 0 error。
2. 在 `build/` 内 `ctest -R NetworkMonitorTest -V` ⇒ 全绿（含新增用例）。
3. 在 `build/` 内全量 `ctest` ⇒ 与基线 55/57 一致（既有失败 `XrayManagerStartupTest` + 已知 flaky `UI_FLOATINGWIDGET`）。
4. `git diff HEAD -U0 -- '*.cpp' '*.h' | Select-String -Pattern '^\+.*\bauto\b' -CaseSensitive` ⇒ 0。
5. 现场复验（可选）：保持环境变量污染存在，运行 `validproxy-cli` 观察不再出现 `LOST`。
