# 启动配置文件的多条生成路径（Spec / 调查报告）

- **文档类型**: Spec（技术规格 · 调查报告）
- **版本**: v1.0
- **日期**: 2026-08-31
- **状态**: 已归档（调查结论，非变更计划）
- **关联模块**: `XrayInstance` / `XrayManager` / `ConfigGenerator` / `StandaloneProxyPool` / `AppController` / `ProxyBatchTester`

---

## 1. 摘要

本项目存在 **三条相互独立的「生成启动配置文件」路径**，分别服务于三个业务场景：

1. **批量测试代理**（`XrayManager` + `XrayInstance` 硬编码模板）
2. **启动独立代理进程**（`AppController::startStandaloneProxy` 读取外部模板改写）
3. **启动代理池**（`StandaloneProxyPool` → `ConfigGenerator::buildPoolConfig` 动态构造）

三条路径**互不复用、各自为政**，使用了三套完全不同的配置生成机制。本报告梳理其入口、配置生成方式、outbound 来源、API services、管理模型，给出对比总览表，并指出关键发现与潜在风险。

---

## 2. 背景与动机

在修复 Xray exit-code-23（`core: not all dependencies are resolved`）缺陷的过程中，发现 `XrayInstance::createConfigFile()` 同时被多个上游调用方使用，但不同调用方对配置的期望差异极大。为厘清「同一份 '生成 Xray 配置' 需求被几处独立实现」这一事实，特开展本次调查，作为后续统一/加固工作的依据。

---

## 3. 三条路径详述

### 3.1 路径 1 —— 测试（批量）代理

**入口链**: `ProxyBatchTester` → `XrayManager::start()` → `XrayInstance::start()` → `XrayInstance::createConfigFile()`

**配置生成方式**: `XrayInstance::createConfigFile()` 的 **默认（非 explicitConfig）分支**写入一份代码内 **硬编码**的模板（`src/XrayInstance.cpp:300-326`）：

```json
{
  "log": {"loglevel": "warning"},
  "api": {"tag": "api", "services": ["HandlerService","LoggerService","StatsService","RoutingService"]},
  "stats": {},
  "policy": {"levels": {"0": {"statsUserUplink":true,"statsUserDownlink":true}}, "system": {"statsOutboundUplink":true,"statsOutboundDownlink":true}},
  "inbounds": [
    {"tag":"api","protocol":"dokodemo-door","port":<apiPort>},
    {"tag":"socks-in","protocol":"mixed","port":<socksPort>}
  ],
  "outbounds": [
    {"tag":"direct","protocol":"freedom"},
    {"tag":"proxy","protocol":"freedom"}
  ],
  "routing": {"domainStrategy":"AsIs","rules":[
    {"type":"field","inboundTag":["api"],"outboundTag":"api"},
    {"type":"field","outboundTag":"proxy","network":"tcp"}
  ]}
}
```

**关键特征**:
- outbounds 中的 `proxy` 只是 **freedom 占位**；真正的代理 outbound 在 **运行时** 通过 `XrayApi::addOutboundDirect("proxy", outbound_json)`（gRPC）注入，注入前先 `removeOutboundDirect("proxy")` 移除占位（`src/ProxyBatchTester.cpp:196-291`，含最多 3 次重试退避）。
- 每个实例一个 `socks` + `api` 端口，由 `PortManager` 动态分配，`XrayManager::start(count, startPort, apiPort)` 批量拉起（`XrayManager.cpp:102-171`），`waitInstanceReady` 做有界崩溃感知等待（5s）。
- 单实例测试 `runWithIndexId` 走 `startXrayInstances(1)`，使用同一模板（`ProxyBatchTester.cpp:617`）。
- 进程由 `XrayManager` 托管（JobObject `KILL_ON_JOB_CLOSE`），随管理器生命周期收放。

**端口/文件**: `configDir/xray_config_<socksPort>.json`（`XrayInstance.cpp:15` 由 `configDir + "/xray_config_" + socksPort + ".json"` 生成），默认落在 `bin/config/`。

### 3.2 路径 2 —— 启动独立代理进程

**入口链**: UI（`ProxyListPanel.cpp:649`）→ `AppController::startStandaloneProxy(indexId, overridePort)`（`AppController.cpp:643-938`）

**配置生成方式**: **读取外部模板文件**，解析后动态改写，而非代码硬编码：

1. **选择模板**：`xray-config-template.json`（Xray）或 `singbox-config-template.json`（sing-box），路径来自 `config_.proxy.template_config_path` / `config_.proxy.singbox_template_config_path`，缺省回退 exe 同目录（`AppController.cpp:772-804`）。
2. **生成 outbound**：用 `config::OutboundBuilderFactory::create(profile,"proxy")`（或 `SingBoxOutboundBuilderFactory`）生成 **真实代理 outbound**，拼上 `direct`(freedom) + `block`(blackhole)（Xray），或 `direct` + `block`（sing-box），整体替换模板的 `outbounds` 数组（`AppController.cpp:734-770`）。
3. **改端口**：`standalone_config::applySocksPort(configObj, socksPort)` 按协议定位 SOCKS/mixed inbound 改端口（`AppController.cpp:816`，`inbounds[0]` 可能是 api 入站，不能按索引）。
4. **sing-box 额外**：注入 DNS bootstrap 规则（`profile.address` 域 → `local_local`）避免 bootstrap 死循环（`AppController.cpp:825-848`）。
5. **写文件 + 启动**：写 `standalone_<indexId>-xray.json` / `standalone_<indexId>-singbox.json`，用 `CreateProcessA("<exe> run -c <configPath>", CREATE_NEW_CONSOLE)` 启动，**无 Job Object、脱离管理**（`AppController.cpp:858-900`）。

**关键特征**:
- 这是 **唯一** 依赖外部模板文件（`xray-config-template.json` / `singbox-config-template.json`）的路径；配置 **完整落地到磁盘文件**（outbound 直接写入，非运行时注入）。
- 进程 **常驻独立**，与 `XrayManager` 管理的实例生命周期无关；重复启动时按 config 文件名（`extractConfigFileName`）定位并征询是否终止旧进程（`AppController.cpp:690-729`，见 `2026-08-31-Spec-StandaloneProxyPreStartCleanup-v1.0.md`）。
- 支持 Xray 与 sing-box 双后端。

### 3.3 路径 3 —— 启动代理池

**入口链**: UI（`ProxyListPanel.cpp:834` / `StandalonePoolDialog.cpp:96`）→ `AppController::startProxyPool()` → `StandaloneProxyPool::start()` → `ConfigGenerator::buildPoolConfig()` → `XrayInstance::setExplicitConfig()` → `XrayInstance::createConfigFile()`（**explicitConfig 分支**）

**配置生成方式**: `ConfigGenerator::buildPoolConfig(socksPort, apiPort, cfg)`（`src/ConfigGenerator.cpp:103-346`）用 **boost::json** 对象动态构造 **完整池控制面配置**：

- **inbounds**：`socks-in` mixed + sniffing（destOverride http/tls/quic）。
- **outbounds**：`direct`(freedom) + `block`(blackhole) —— **无** `balancer-out` outbound（注释明确：成员 outbound `px-<indexId>` 运行时经 gRPC 注入，前缀选择器 `["px-"]` 出现后匹配）。
- **observatory**：`subjectSelector:["px-"]` + `probeURL`（复用 `cfg.probeUrl` 或 `cfg.observatory.destination`）+ `probeInterval`。
- **api**：`HandlerService` + `RoutingService` + **`ObservatoryService`**（注意：**保留**了 Observatory 且**配套** `observatory` 块——正确配对）。
- **routing.balancers**：`balancer-out`（xray 26.x 用 `routing.balancers` 取代废弃的 outbound `balancing` 协议）。
- **dns**：`buildPoolDns`（hosts + DoH servers + 大陆 DNS 解析器 IP，需 `XRAY_LOCATION_ASSET` / geo 文件）。
- **routing**：含 CN/private 直连分流规则（`geoip:cn` / `geosite:cn` / `geoip:private` / `geosite:private`），规则顺序保证直连在 `socks-in→balancer-out` 兜底之前（见 `2026-08-27-Spec-buildPoolConfig-TemplateBorrow-v1.0.md`）。

**关键特征**:
- 通过 `instance_->setExplicitConfig(json)` 写入，`createConfigFile()` 的 **explicitConfig 分支**（`XrayInstance.cpp:282-288`）原样写盘（verbatim）。
- 成员代理 `px-<indexId>` 由 `StandaloneProxyPool::injectMember()` 经 `XrayApi::addOutbound`/`addOutboundDirect` gRPC 运行时注入（`StandaloneProxyPool.cpp:58-101`）。
- 池端口由 `proxy::resolvePoolPorts(poolCfg)` 分配，冲突时在 `10809+N/10810+N` 重试（`AppController.cpp:1653-1662`）；进程由 `StandaloneProxyPool` 托管。

**端口/文件**: `configDir/xray_config_<socksPort>.json`（与路径 1 同命名约定，但内容为池控制面配置）。

---

## 4. 对比总览表

| 维度 | 路径1 批量测试 | 路径2 独立代理 | 路径3 代理池 |
|---|---|---|---|
| **入口** | `XrayManager::start` | `AppController::startStandaloneProxy` | `startProxyPool` → `StandaloneProxyPool` |
| **配置生成** | 硬编码模板（`createConfigFile` 默认分支） | 读外部模板 + 改写 outbounds | boost::json 构造（`buildPoolConfig`） |
| **Outbound 来源** | 运行时 gRPC 注入 `proxy` | 写盘时直接生成（真实 outbound） | 运行时 gRPC 注入 `px-<id>` |
| **API services** | Handler/Logger/Stats/Routing（**无 Observatory**） | 由外部模板决定 | Handler/Routing/**Observatory** |
| **Observatory 块** | ❌ 无 | 由外部模板决定 | ✅ 有（`subjectSelector:["px-"]`） |
| **Balancer** | ❌ | ❌ | ✅ `routing.balancers` |
| **DNS/geo 分流** | ❌ | 由外部模板决定 | ✅ `buildPoolDns` + CN/private 直连 |
| **管理模型** | XrayManager 托管（JobObject） | 脱离管理（独立进程） | StandaloneProxyPool 托管 |
| **配置文件名** | `xray_config_<socks>.json` | `standalone_<id>-xray.json` / `-singbox.json` | `xray_config_<socks>.json` |
| **后端** | Xray | Xray / sing-box（可选） | Xray |
| **UI 入口** | ProxyTestService / CLI (`main_cli.cpp:537`) | ProxyListPanel:649 | ProxyListPanel:834 / StandalonePoolDialog |

---

## 5. 关键发现与风险点

### 5.1 三套机制互不复用（最大风险）
同一份「生成 Xray 配置」的需求被 **三处独立实现**：硬编码模板（路径1）、外部模板改写（路径2）、boost::json 构造（路径3）。任一处的 Xray 兼容性修复（例如移除 ObservatoryService、balancer 从 outbound 迁移到 routing）都 **必须三处同步修改**，存在 **漂移风险**——某条路径修了、其它路径遗漏时，便会出现「换一种启动方式就崩溃」的隐蔽问题。

### 5.2 Observatory 处理「相反」，互为镜像验证
- **路径1**（批量测试）默认模板 **移除** `ObservatoryService`（因无 `observatory` 块，会导致 Xray 26.x exit-23）。
- **路径3**（代理池）`buildPoolConfig` **保留** `ObservatoryService` **且** 配套 `observatory` 块——正确配对。
- 二者互为对照：若某天路径3 的 `observatory` 块与 services 列表不同步，将重现 exit-23。**这是未来维护时最容易踩的坑。**

### 5.3 端口/文件命名冲突风险
路径1 与路径3 **共用** `xray_config_<socksPort>.json` 命名约定。若某时池与批量实例分配到相同的 `socksPort`，会 **互相覆盖配置文件**，导致启动读取到对方的配置。

### 5.4 路径2 依赖外部模板文件的可变性
`xray-config-template.json` / `singbox-config-template.json` 是 **运行时外部文件**。若其含未配对的 api service（历史教训，见 exit-23），独立代理会静默崩溃；且因 `CREATE_NEW_CONSOLE` + 脱离 Job Object，错误只出现在独立 console 窗口，应用日志仅记录 exit code，排查成本高。

### 5.5 唯一复用点
`XrayInstance` 以 `explicitConfig_` 是否为空二分支，隐含「池 / 测试」两种模式，是路径1 与路径3 的 **唯一共用代码点**（`createConfigFile()`）。此为未来统一配置生成的最佳切入点。

---

## 6. 结论与建议

1. 三条路径当前各自正确，但其配置生成机制的分裂是长期维护风险。
2. 建议后续（另行立项）将「Xray 启动配置生成」收敛为一个 **统一配置构造器**（单点工厂），输入业务模式（test / standalone / pool），输出完整配置 JSON；三处现有实现逐步迁移复用，消除漂移。
3. 短期应至少做到：`api.services` 与对应功能块（`observatory`/`stats`/`policy`）的配对校验，作为统一配置构造器的内置防御。
4. 端口/文件命名应在统一化时一并区分（如路径3 改用 `pool_xray_config_<socks>.json`），避免与路径1 冲突。

---

## 7. 参考文档

- `docs/specs/2026-08-27-Spec-buildPoolConfig-TemplateBorrow-v1.0.md`（路径3 buildPoolConfig 模板加固）
- `docs/specs/2026-08-26-Spec-StandaloneProxyPool-v1.0.md`（路径3 代理池设计）
- `docs/specs/2026-08-31-Spec-StandaloneProxyPreStartCleanup-v1.0.md`（路径2 前置清理）
- `docs/bugfix/2026-08-27-Bugfix-StandaloneProxyPool-Injection-Observatory-v1.0.md`（Observatory 路径修正）

## 8. 文件锚点索引

| 文件 | 关键行 |
|---|---|
| `src/XrayInstance.cpp` | `createConfigFile` 280-333；`setExplicitConfig` 335-337；构造 10-20；`start` 26+ |
| `src/XrayManager.cpp` | `start` 102-171 |
| `src/ConfigGenerator.cpp` | `buildPoolConfig` 103-346 |
| `src/StandaloneProxyPool.cpp` | `start` 28；`injectMember` 58-101 |
| `src/ui/AppController.cpp` | `startStandaloneProxy` 643-938；`generateConfig` 589-604；`startProxyPool` 1614+；`stopProxyPool` 1698；`injectProxyToPool` 1720 |
| `src/ProxyBatchTester.cpp` | worker 注入 196-291；`runWithIndexId` 617 |
| `src/main_cli.cpp` | XrayManager 537 |

---

*本文档为调查结论归档，不含代码变更。如后续启动统一配置构造器，需在本文档基础上另行编写实施 Spec 与 TDD 计划。*
