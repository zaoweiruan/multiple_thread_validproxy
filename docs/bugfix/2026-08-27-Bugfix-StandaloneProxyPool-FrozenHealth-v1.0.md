# Bugfix: 独立代理池成员健康检查冻结（active / -1 / 否 / failStreak 0）

- **日期**: 2026-08-27
- **模块**: `StandaloneProxyPool` + `ProxyHealthEvaluator` + `ConfigGenerator` + `AppController`
- **影响面**: 独立代理池（standalone proxy pool）成员健康评价列
- **严重度**: P0（核心功能失效，代理池形同虚设）
- **状态**: ✅ completed

## 1. 问题现象

用户在 `ProxyListPanel` 的独立代理池 Monitor 对话框中观察到：所有 socks5 成员的状态恒定为
`active` / `延迟 -1` / `否`（不健康）/ `failStreak 0`，无论真实网络是否可达。

日志侧表现（`AppController` 拉起的 pool 实例）：
```
[REPORT] [StandaloneProxyPool] health: N members
```
`evaluatorLoop` 每轮都报 "N members"，但 N 个成员的 `lastDelayMs` / `lastAlive` 永远停在注入时的
默认值（`-1` / `false`），UI 的 延迟/状态/成功率 列全部冻结。

## 2. 根因（系统级调试结论）

`StandaloneProxyPool::evaluatorLoop` → `ProxyHealthEvaluator::probe` 依赖
`xray::XrayApi::getOutboundStatusDirect`，即通过 xray **ObservatoryService** 的
`GetOutboundStatus` 获取每个 outbound 的 alive/delay。

经 gRPC reflection 实证（Xray 26.3.27 / 26.2.6 同构）与 `LiveInjectionAndObservatoryPath`
实时测试确认：

- xray 的 observatory（无论顶层 `observatory` 还是 balancer 的 `observation` 块）在 **xray 进程
  Start() 时** 就根据 `subjectSelector` 解析出要观测的 outbound 标签集合并固定下来。
- 代理池的成员 outbound（`px-<indexId>`）是 **运行期通过 gRPC `AddOutbound` 注入** 的，Start 时
  尚不存在 → observatory 的观测列表为空 → `GetOutboundStatus` 返回 **0 条**。
- 因此 `ProxyHealthEvaluator::probe` 得到空向量，merge 逻辑什么也不更新 → 成员永远停在注入态。

> 第一轮尝试：在 `ConfigGenerator::buildPoolConfig` 的 balancer 上补 `observation` 块（让 observatory
> 挂在 balancer 上）并把 `probeUrl` 复用 `config.json test.url`。结构性单测 `BuildPoolConfigObservationBlock`
> 通过，但实时测试 `LiveInjectionAndObservatoryPath` 仍返回空 —— 证实 **observatory 路径对运行时注入的
> outbound 天生不可见**，必须换机制。

## 3. 修复

### 3.1 健康探测改为直接探测成员上游代理（`ProxyHealthEvaluator`）

废弃 observatory 路径，改为对每个成员的上游代理 **直接用 cURL 探测**：

- `include/ProxyHealthEvaluator.h`：新增 `MemberProbeTarget`（tag / configtype / address / port /
  username / password），`MemberHealth` 增加 `tested` 字段（false 表示协议不可直接探测）；
  `probe(xray::XrayApi&)` → `probe(targets, testUrl, connectTimeoutMs, totalTimeoutMs)`。
- `src/ProxyHealthEvaluator.cpp`：对 `configtype == 4`（SOCKS）/ `10`（HTTP）构建 cURL 代理 URL
  直接探测（SOCKS 用 `socks5h://user:pass@host:port`，HTTP 用 `http://user:pass@host:port`；
  user/pass 映射沿用 `SOCKSOutboundBuilder`/`HTTPOutboundBuilder` 的 `user=security`、`pass=id`）；
  发起 HEAD 请求，以 HTTP 状态码 [200,400) 判 alive、以 `CURLINFO_TOTAL_TIME` 计时；异常（连接失败等）
  视为 alive=false 并填 `lastError`）。
- vmess/vless/trojan/ss/hysteria2/tuic/wireguard 等需要 xray 讲线协议的类型：`tested=false`，
  不覆盖成员状态（避免误判为死亡），留待后续 xray 侧探测。

### 3.2 池调度接入（`StandaloneProxyPool`）

- `include/StandaloneProxyPool.h`：`PoolMember` / `PoolMemberView` 增加 `probed` 标志；
  新增成员 `ProxyHealthEvaluator evaluator_`（原 `evaluatorLoop` 内的局部变量改为成员）；
  新增 `probeNow()`、`doProbe()`、`mergeHealth()`。
- `src/StandaloneProxyPool.cpp`：
  - `doProbe()`：从 `members_` 取出 ACTIVE 成员的 profile 拼成 `MemberProbeTarget`，调用
    `evaluator_.probe`，testUrl 取 `cfg_.probeUrl`（为空回退 `cfg_.observatory.destination`），
    timeout 取 `cfg_.observatory.timeoutSec`（默认 10s）；
  - `mergeHealth()`（调用方持锁）：把探活结果写回 `lastDelayMs` / `lastAlive` / `lastError` /
    `failStreak` / `probed`；
  - `evaluatorLoop()`：原 `evaluator.probe(*api_)` 改为 `doProbe()` + `mergeHealth()`；
  - `probeNow()`：同步跑一轮探测并更新视图（供测试与注入后即时反馈）。

### 3.3 探测 URL 复用全局 test.url（`ConfigGenerator` + `AppController`）

- `include/ConfigReader.h`：`StandalonePoolConfig` 增加 `std::string probeUrl;`。
- `src/ConfigGenerator.cpp`：`buildPoolConfig` 的 observatory `probeURL` 与 balancer `observation.probeURL`
  统一改用 `cfg.probeUrl.empty() ? cfg.observatory.destination : cfg.probeUrl`（保留 balancer `observation`
  块作为 xray 26.x 规范结构）。
- `src/ui/AppController.cpp`：`startProxyPool` 中 `poolCfg.probeUrl = config_.test_url;`，使池探针与
  全应用共用同一探测端点。

### 3.4 测试链接修正（`CMakeLists.txt`）

`test_standalone_proxy_pool` 目标补充 `CURL::libcurl`（新引入 cURL 直接探测）。

## 4. 验证

- `tests/test_standalone_proxy_pool.cpp`：
  - 新增 `BuildPoolConfigObservationBlock`（断言 balancer 带 `observation` 块且 probeURL 复用 test.url）；
  - `LiveInjectionAndObservatoryPath`（opt-in 实时）改为断言 `pool.probeNow()` 后成员 `probed==true`
    （不再是 observatory 空返回）。
- 构建与运行：
  - `test_standalone_proxy_pool` **9/9** PASS（含实时 `LiveInjectionAndObservatoryPath` PASS）；
  - `ConfigGeneratorTest.BuildPoolConfig*` **3/3** PASS；
  - `XrayApiDirectTest` **115/115** PASS（observatory 路径单测不受影响）；
  - `validproxy`（GUI，含 `AppController`）与 `validproxy-cli` 构建 0 error。

## 5. 已知限制 / 后续

- vmess/vless/trojan/ss/hysteria2/tuic/wireguard 成员当前 `tested=false`，健康列保持注入态
  （不会因本修复而误判）。完整支持需为这些协议单独起 xray 侧探测（类似 `ProxyTester`），列为后续项。
- 用户实测数据集以 socks5 为主，本修复已覆盖其冻结问题。
- 修复前的 `BuildPoolConfigStructure` 断言 `"socks"` 与实现输出 `"mixed"` 不符（注释本身写 "mixed socks
  inbound"），属于既有测试错别字，已同步改为 `"mixed"`。
