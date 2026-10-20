# RCA: `existing tag found` 注入失败（CLI 回退把整份 Standalone 配置里的 direct/block 一并注入）

> 版本 v1.0 · 2026-10-20 · 类型：**根因分析（RCA）**
> **本文档仅做根因分析与诊断，不包含任何代码改动**。
> 来源：`docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md` §16 遗留问题 #1。

---

## 0. 结论摘要

| 项 | 结论 |
| :--- | :--- |
| **现象** | 批量测试中一部分代理直接以 `XRAY_ERROR - <indexId> (tag=proxy)` 结束，**完全未进入连通性测试**，最终无结果；日志中伴随大量 `existing tag found: direct` / `existing tag found: proxy` |
| **主根因 R1（契约错配）** | `ConfigGenerator` 把**完整的 Standalone Xray 配置**（`outbounds=[proxy, direct, block]`）写入 `TestConfig::outbound_json`，而 `XrayApi::addOutboundDirect` 的 gRPC 编码路径只读取 `outbounds[0]`；无 protobuf 编码器的协议会回退到 CLI `XrayApi::addOutbound`，该路径把**整份 JSON 原文**管道喂给 `xray api ado` → `ado` 逐个注入全部 outbound → `direct` 与测试实例内置常驻的 `direct` 同名 → `exit 1` |
| **直接触发** | 实例侧测试配置永远自带 `direct`（`buildTestConfig` 无条件添加；dummy profile 的 `address` 为空时更只有 `direct` 一个 outbound）。因此**任何**走 CLI 回退的注入都必然在 `direct` 上撞名 |
| **次生根因 R2（部分成功不可见）** | `xray api ado` 按文档顺序注入：先把 `proxy` 加进去（成功、打印 `adding: proxy` + `{}`），再在 `direct` 上失败退出。调用方只看到退出码 1，把整次注入判为失败，但 `proxy` 已残留；`XrayApi::addOutbound` 内部的「空输出 300ms 重试」**不会先删除 tag**，于是同一 worker 的重试会撞上自己上次残留的 `proxy`，产生第二种签名 `existing tag found: proxy` |
| **触发面 R3** | 仅影响**回退到 CLI 路径**的协议。本次现场 13 个失败对象 **100% 为 ConfigType 7（Hysteria2）**——`Hysteria2OutboundBuilder` 输出 `protocol="hysteria"`，该协议不在 `supportedTypeUrls` 中，必然回退。同族（TUIC / WireGuard / Anytls / Naive）只要同样无编码器，失败率同样为 100% |
| **直接证据** | `bin/worker/log/ui_20261020_092411.log`（4,085 行，2026-10-20 09:24:11–09:51:18）：`XRAY_ERROR` 13 条、`[XrayApi] addOutbound FAILED:` 39 条（13×3 次重试）、`existing tag found: direct` 34 条、`existing tag found: proxy` 20 条；13 个 indexId 经 SQL 校验全部为 ConfigType 7 |
| **后果** | 受影响协议子集（本次 13 条，库内 Hysteria2 共 3,077 条）**100% 无测试结果**；每条额外浪费 3 次注入 + 退避等待 + 一次实例重启评估。**不损坏数据、不影响其他协议**。原 §16 措辞「整批代理无结果」不精确，实为「该协议子集无结果」 |
| **严重度** | **高**（确定性 100% 失败、无自动降级；受协议范围限制，非全库） |
| **范围** | `src/ConfigGenerator.cpp`（配置生成）→ `src/XrayApi.cpp`（注入，gRPC/CLI 双路径）→ `src/ProxyBatchTester.cpp`（调度与失败处理）。不涉及订阅解析、存储、去重、连通性判定 |
| **修复方向** | P0：CLI 注入前把载荷归一化为 `{"outbounds":[<outbounds[0]>]}`，与 gRPC 语义对齐；P1：内部空输出重试前先删 tag（或删除内部重试，交给外层）；P2：最终失败后清理 tag。详见 §6（不在本次实施） |

---

## 1. 症状与现场证据

### 1.1 现场基本信息与模式计数

| 项 | 值 |
| :--- | :--- |
| 日志文件 | `bin/worker/log/ui_20261020_092411.log` |
| 大小 / 行数 | 365,108 B / 4,085 行 |
| 时间跨度 | 2026-10-20 09:24:11 – 09:51:18 |
| `log.file_level` | `WARN`（因此正常进度不可见，只有 WARN/ERROR 及子进程原始输出） |
| 生成版本 | 含 F5 修复的构建（`requesting whole-batch cancellation` 该版本有、基线 `726599d` 无） |

| 模式（`Select-String -SimpleMatch`） | 计数 | 解读 |
| :--- | ---: | :--- |
| `XRAY_ERROR` | **13** | 13 个代理注入彻底失败，未进入连通性测试 |
| `[XrayApi] addOutbound FAILED:` | **39** | = 13 × 3 次重试，**无一例外全部用尽重试** |
| `failed to add outbound` | **54** | 子进程原始输出中的失败行（含内部空输出重试产生的额外次数） |
| `existing tag found: direct` | **34** | 主签名（整份配置里的 `direct` 撞名） |
| `existing tag found: proxy` | **20** | 次签名（上一次部分成功残留的 `proxy` 撞名） |
| `adding: proxy` | **64** | `ado` 已成功加入的证明 |
| `adding: direct` | **44** | `ado` 尝试加入 `direct` 的次数 |
| `removeOutbound FAILED` / `removeOutboundDirect FAILED` | **0** | 删除链路正常（见 §3.5） |

失败区间高度集中：首条 `XRAY_ERROR` 09:38:52，末条 09:47:34（行 3536 → 4077），与 09:38:50–09:38:55 的实例重启窗口重叠。

### 1.2 失败对象 100% 为 ConfigType 7（Hysteria2）

对 13 个失败 `indexId` 直接查询生产库 `bin/worker/guiNDB.db`：

| indexId | host | ConfigType |
| :--- | :--- | :--- |
| 5931375171680243344 | 194.55.235.77 | 7 |
| 4172079534975817752 | de.northvale.cloud | 7 |
| 4810560244965436231 | lv1.northvale.cloud | 7 |
| 4290487164079008872 | fi.northvale.cloud | 7 |
| 4809346004014635135 | nl.northvale.cloud | 7 |
| 4741281966828438511 | sw.northvale.cloud | 7 |
| 5903298059255654002 | 189.74.106.166 | 7 |
| 4993515153125691532 | 189.74.114.41 | 7 |
| 5800737716143926280 | 189.74.114.6 | 7 |
| 4821048092585487917 | hk3.hyhuawei.com | 7 |
| 5613667458598453384 | fr.verymad.net | 7 |
| 5151000169351373619 | 2.26.3.75 | 7 |
| 5101365540377519679 | uk-d9ca48498a.shamanapp.online | 7 |

**13/13 = ConfigType 7 × 100%**，没有例外，也没有其他协议参与失败。库内 ConfigType 分布：`1=17,332`、`3=8,359`、`5=305,500`、`6=43,300`、`7=3,077`（无 8/9/10 行）。

> 该分布本身即解释了为什么「整批」中途只出现 13 条失败：批次的调度是随机/顺序混合的，Hysteria2 只占约 0.8%，被抽样到时才暴露。

### 1.3 两种失败签名（原始日志）

**(A) 主签名：`direct` 撞名（整份配置被注入）**

```
09:38:54 [Worker-0] ... 4741281966828438511
[XrayApi] addOutbound FAILED: exitCode=1, output=adding: proxy
{}
adding: direct
failed to add outbound: rpc error: code = Unknown desc = app/proxyman/outbound: existing tag found: direct
```

```
09:38:54 [Worker-2] ... 5903298059255654002
[XrayApi] addOutbound FAILED: exitCode=1, output=adding: proxy
{}
adding: direct
failed to add outbound: rpc error: code = Unknown desc = app/proxyman/outbound: existing tag found: direct
```

要点：`adding: proxy` 后紧接 `{}` 表示 **proxy 已成功加入**；随后 `adding: direct` 失败。整条命令退出码为 1，但实际是「部分成功」。

**(B) 次签名：`proxy` 撞名（上一次残留）**

```
09:38:54 [Worker-1] ... 4809346004014635135
[XrayApi] addOutbound FAILED: exitCode=1, output=adding: proxy
failed to add outbound: rpc error: code = Unknown desc = app/proxyman/outbound: existing tag found: proxy
```

该签名**只出现在空输出重试之后**（同一 worker 在 09:38:50–09:38:55 的实例重启窗口内先失败一次，内部 300ms 重试未删 tag 就再次注入）。它证明 `proxy` 在失败后并未被清理。

最终统一收敛为：

```
09:47:34 [Worker-x] XRAY_ERROR - 5101365540377519679 (tag=proxy) - <最后一次注入错误>
```

`ProxyBatchTester` 在 3 次重试全部失败后直接 `continue`，**不会**调用 `ProxyTester`，故这 13 个代理没有任何延迟/连通性结果。

### 1.4 与「实例真死」路径的区分

09:38:50–09:38:55 同时存在另一条独立故障链：

| 时间 | 事件 |
| :--- | :--- |
| 09:38:50–09:38:55 | 多条 `addOutbound fallback failed with empty output, likely instance restart window; retrying once` |
| 09:38:52 | 实例重启（api=10082） |
| 09:38:53 | 实例重启（api=10080、10083） |
| 09:38:54 | 实例重启（api=10083） |

该链的成因是**实例已死/正在重启**（注入命令拿到空输出），与 tag 撞名无关。两条链在同一时间窗内叠加，容易误判；判定依据是：**空输出失败不会打印 `adding: ...` 行**，而 tag 撞名的输出里必然先出现 `adding: proxy`。

另：09:39:54–09:41:59 存在一次人工断网（NetworkMonitor 有记录），与本节故障无因果关系——断网期间的失败签名不同（连接类错误），且 13 条 `XRAY_ERROR` 横跨断网前后持续发生。

F5（`batch_policy::shouldReportEndpointUnreachable`）在本日志中工作正常：**没有任何实例重启是由 `existing tag found` 触发的**，重启全部来自空输出/真死实例。

---

## 2. 数据流与调用链

### 2.1 调用链总览

```
ProxyBatchTester::preGenerateConfigs            src/ProxyBatchTester.cpp:597-615
  └─ ConfigGenerator::generateConfig            src/ConfigGenerator.cpp:29-71
       └─ ConfigBuilder::build(Standalone)      src/config/ConfigBuilder.cpp:562-614
            outbounds = [ proxy, direct, block ]
       └─ outbound_json = serialize(fullConfig) ← 整份配置（含 inbounds/routing/dns/log）

ProxyBatchTester::workerThreadFunc             src/ProxyBatchTester.cpp:253-310
  ├─ removeOutboundDirect("proxy")  (预删)      src/ProxyBatchTester.cpp:258
  ├─ addOutboundDirect(outbound_json, "proxy")  src/ProxyBatchTester.cpp:272
  │    └─ XrayApi::addOutboundDirect            src/XrayApi.cpp:2445-2615
  │         ├─ parseOutboundJson → 只取 outbounds[0]       src/XrayApi.cpp:713-717
  │         ├─ hysteria → typeUrl xray.proxy.outbound.Config
  │         ├─ 不在 supportedTypeUrls → 无编码器
  │         └─ return addOutbound(fullJson, tag, out)      src/XrayApi.cpp:2503-2507
  │              └─ runProcess("xray api ado ... stdin:", fullJson)
  └─ 失败 3 次 → XRAY_ERROR + continue          src/ProxyBatchTester.cpp:352-373
```

### 2.2 配置生成侧：`outbound_json` 是「整份配置」

`ConfigGenerator::generateConfig`（`src/ConfigGenerator.cpp:29-71`）在 `db_` 可用时：

```cpp
config::ConfigBuilder builder(db_);
config::ConfigVars vars;
vars.socksPort = 10808;
vars.apiPort   = 10085;
configJson = builder.build(config::ConfigMode::Standalone, profile, vars);
config.outbound_json = builder.serialize(configJson);
```

`ConfigBuilder::buildStandaloneConfig`（`src/config/ConfigBuilder.cpp:562-614`）设置了：

| outbound | tag | protocol | 说明 |
| :--- | :--- | :--- | :--- |
| 0 | `proxy` | 由 profile 决定（此处 `hysteria`） | 被测代理 |
| 1 | `direct` | `freedom` | 直连 |
| 2 | `block` | `blackhole` | 阻断 |

因此 `outbound_json` 的顶层键包含 `log`、`dns`、`inbounds`、`outbounds`、`routing`，**不是**单个 outbound 对象，也不是「只含一个元素的 outbounds 数组」。

### 2.3 注入侧：两条路径对同一载荷的解释不同

| 路径 | 代码 | 对 `outbound_json` 的处理 | 结果 |
| :--- | :--- | :--- | :--- |
| **gRPC（主路径）** | `XrayApi::addOutboundDirect` `src/XrayApi.cpp:2445-2615` | `parseOutboundJson` 只读 `outbounds[0]`，编码为 protobuf 后单条 AddOutbound | 正确（只注入 `proxy`） |
| **CLI（回退路径）** | `XrayApi::addOutbound` `src/XrayApi.cpp:153-218` | 把**完整 JSON** 原样写入 `xray api ado` 的 stdin | `ado` 遍历 `outbounds` 数组，逐条注入 → `direct`/`block` 一起进 |

回退触发条件（`src/XrayApi.cpp:2487-2507`）：

1. 协议映射出的 `typeUrl` 不在 `supportedTypeUrls` 列表内 → `hasProtobufEncoder == false` → 回退；
2. 或编码结果 `encodedValue` 为空（服务端类协议的保护分支）→ 回退。

Hysteria2 走第 1 条：`src/config/outbound/Hysteria2OutboundBuilder.cpp:8` 输出 `protocol = "hysteria"`；`parseOutboundJson` 归入 `xray.proxy.outbound.Config`，该 typeUrl 不在支持列表，必然回退。

### 2.4 实例侧：`direct` 为什么永远存在

`XrayInstance::createConfigFile`（`src/XrayInstance.cpp:305+`）在 `db_` 可用时调用 `builder.build(ConfigMode::Test, "xray", dummyProfile, vars)`；`buildTestConfig`（`src/config/ConfigBuilder.cpp:509-558`）**无条件添加 `direct`**，只有 `dummyProfile.address` 非空时才额外添加 `proxy`。测试实例用的 dummy profile 地址为空 → 实例 outbounds = **`[direct]`**。

所以 `ado` 注入 `[proxy, direct, block]` 时：`proxy` 成功、`direct` 必然撞名、进程退出码 1。这与实例是否健康、网络是否通畅完全无关，是**确定性**结果。

### 2.5 `ado` 的执行语义（实测）

- 按 `outbounds` 数组顺序逐条调用 AddOutbound；
- 每成功一条打印 `adding: <tag>` + `{}`（gRPC 返回的空 JSON）；
- 某条失败立即退出，**已成功的条目保留**（不回滚）；
- 退出码非 0。

第 3 点正是 R2 的物理基础。

---

## 3. 根因分析

### 3.1 R1（主根因，P0）：`outbound_json` 的契约在两条注入路径之间不一致

`addOutboundDirect` 的参数语义是「**单个** outbound 的 JSON」，gRPC 实现严格遵守（只取 `outbounds[0]`）；而生产数据源给的是「**完整**配置 JSON」。两者在 gRPC 路径上因为「只取第一个」而**碰巧**表现正确，掩盖了不匹配；一旦回退到 CLI，`ado` 按文档语义处理整个数组，错配立刻暴露。

这是一个**隐式契约**：`TestConfig::outbound_json` 字段从未被文档化为「单 outbound」或「全配置」，两条消费路径各自做了不同假设，且都没有校验。

### 3.2 R2（次生根因，P0/P1）：部分成功不可见 + 内部重试不清理

| 环节 | 行为 | 后果 |
| :--- | :--- | :--- |
| `ado` | 先加 `proxy` 成功，再加 `direct` 失败退出 | 实例内残留 `proxy` |
| `XrayApi::addOutbound` | 只看退出码，`exit 1` ⇒ 整体失败 | 残留状态无人知晓 |
| 内部空输出重试（`src/XrayApi.cpp:192-204`） | 空输出时 sleep 300ms **直接重发同一命令**，不删 tag | 第二次必然 `existing tag found: proxy` |
| 外层重试（`src/ProxyBatchTester.cpp:270-310`） | 每次重试前**有** `removeOutboundDirect(tag)`（:287） | 外层能自愈；但内部重试发生在两次外层尝试之间，制造了签名 B |

签名 B 的 20 条记录即由此产生。它不会造成额外数据损坏，但会把「R1 的确定性失败」伪装成「随机重试抖动」，增加排障难度。

### 3.3 R3（触发面，P1）：协议编码器覆盖缺口

`addOutboundDirect` 的 protobuf 编码器只覆盖了一部分协议；其余协议（Hysteria2、TUIC、WireGuard、Anytls、Naive 等）永久走 CLI 回退。因此 R1 对这些协议是 **100% 命中**，对已覆盖协议是 0% 命中（除非 `encodedValue` 为空触发第二条回退条件）。

现场 13/13 全为 Hysteria2，与「该协议必然回退」完全吻合，是 R1 最干净的验证样本。

### 3.4 为什么既有测试与 F5 没发现

1. `tests/test_batch_worker_policy.cpp` 只测 `isStaleTagInjectionError` 的字符串分类（:45 / :54 / :64），**没有任何测试检查 CLI 注入载荷的形状**；
2. gRPC 是默认路径，而它对全量配置「只取第一个」，测试与日常运行都表现正常；
3. F5 的职责是「失败后是否重启实例」，它成功阻止了重启 churn（本日志 0 次误重启），但**不改变失败本身**——这是 §16 把它归为「既有功能缺陷」的原因；
4. 单元测试不启动真实 xray，`ado` 的多条注入语义从未被集成测试覆盖。

### 3.5 排除项

| 假设 | 结论 | 依据 |
| :--- | :--- | :--- |
| `removeOutbound` / `removeOutboundDirect` 失效，导致 tag 一直残留 | **排除** | 日志 0 条 `removeOutbound FAILED`；受控复现中 `rmo` 返回 `removing: proxy` + `{}`、exit 0，且对不存在的 tag 幂等成功 |
| F5 判定错误导致实例被反复重启 | **排除** | 重启均由空输出/真死实例触发；无一条由 `existing tag found` 触发 |
| 网络中断导致注入失败 | **排除** | 断网时段为 09:39:54–09:41:59，失败横跨前后且签名一致；断网失败是连接类错误，不是 tag 冲突 |
| `tag` 参数传错（如空串或重复） | **排除** | 传入与 JSON 内 tag 均为 `"proxy"`；gRPC 路径同参数正常 |
| xray 版本缺陷 | **排除** | 受控复现与现场输出逐字节一致，属 `ado` 的设计语义（逐条注入、失败不回滚） |

---

## 4. 最小复现（受控实验）

**环境**：2026-10-20 12:01，Windows；`xray = E:\v2rayN-windows-64\bin\xray\xray.exe`；临时配置：api `127.0.0.1:18999`（`HandlerService`）、socks `18998`、outbounds `[direct]`（与测试实例一致）。实验结束后临时目录已删除。

| # | 操作 | 输出 | 结论 |
| :--- | :--- | :--- | :--- |
| A | `rmo --server 127.0.0.1:18999 proxy` | `removing: proxy` / `{}` / exit 0；重复执行同样 exit 0 | 删除链路与幂等性正常 |
| B | `ado` 载荷 = `{"outbounds":[{"tag":"proxy","protocol":"freedom"},{"tag":"direct","protocol":"freedom"},{"tag":"block","protocol":"blackhole"}]}` | `adding: proxy` / `{}` / 空行 / `adding: direct` / `failed to add outbound: rpc error: code = Unknown desc = app/proxyman/outbound: existing tag found: direct` / exit 1 | **与现场签名 A 逐字节一致** |
| C | 紧接着重跑 `ado`（载荷去掉 `block`，仅 `proxy` + `direct`） | `adding: proxy` / `failed to add outbound: … existing tag found: proxy` / exit 1 | 证明 B 结束后 `proxy` **残留**（签名 B 的成因） |
| D | `ado` 载荷 = `{"outbounds":[{"tag":"proxy2","protocol":"freedom"}]}` | `adding: proxy2` / `{}` / exit 0 | **单 outbound 载荷完全正常** |
| E | `rmo proxy` 后执行 D 的单 outbound 载荷 | 成功 exit 0 | 外层「先删再加」策略有效 |

**判定**：R1 得到逐字节级别的证实；`direct` 冲突是确定性结果而非偶发；把载荷裁成单个 outbound 即可消除。

---

## 5. 影响评估

| 维度 | 评估 |
| :--- | :--- |
| **受影响对象** | 所有因「无 protobuf 编码器」而回退 CLI 的协议。库内 Hysteria2 = 3,077 条（占 ConfigType 5/6/7 合计约 0.8%）；TUIC/WireGuard/Anytls/Naive 在本库无行，但代码路径相同 |
| **失败形态** | 每条 3 次注入尝试全败 → `XRAY_ERROR` → 跳过连通性测试 → 无结果；该代理本轮**永久失去**被验证的机会 |
| **耗时放大** | 每条失败注入约 0.2–0.5s，加退避与可能的实例重启评估；本次 13 条合计产生 39 次 FAILED 与 4 次重启 |
| **数据完整性** | 无影响：不写坏 profile、不误删、不污染 delay/health |
| **可观测性** | 有明确 `XRAY_ERROR`，但错误文本（`existing tag found: direct`）指向「tag 冲突」，容易被误读为删除链路缺陷——本次 RCA 之前的 §16 描述即受此误导 |
| **触发概率** | 对受影响协议 **100%**（确定性）；对已覆盖协议 0%（除非 `encodedValue` 为空） |
| **用户可见性** | 表现为「有些代理怎么测都没有结果」，反复重测仍失败 |

---

## 6. 修复方向（供后续 Spec，本次不含代码改动）

| 优先级 | 方案 | 要点 |
| :--- | :--- | :--- |
| **P0** | **CLI 载荷归一化** | 在 `XrayApi::addOutbound`（或抽一个共享 helper）解析输入 JSON：若含 `outbounds` 数组且长度 > 1，则只保留 `outbounds[0]`，构造 `{"outbounds":[<first>]}` 再喂给 `ado`。与 `parseOutboundJson` 的 `outbounds[0]` 语义严格对齐。备选：`ConfigGenerator` 额外提供 `single_outbound_json` 字段，但这会扩大 `TestConfig` 结构，优先级低于归一化 |
| **P1** | **内部重试前清理** | 空输出重试（`src/XrayApi.cpp:192-204`）之前调用 `removeOutbound(tag)`；或直接删除内部重试，统一由外层「先删 + 重试」承担（当前外层已具备该能力，内部重试属冗余） |
| **P2** | **最终失败后清理** | 3 次重试耗尽后删除 tag，避免残留污染同实例的后续代理（当前依赖下一次预删，若后续代理标签不同或流程异常则残留） |
| **P3** | **测试补齐** | ①纯函数/单测：输入 3-outbound 文档 → CLI 载荷恰好 1 个 outbound；②集成：用真实 xray 启动实例后执行一次 CLI 注入，断言 exit 0 且 `lso` 只多出 `proxy`；③回归锁 `isStaleTagInjectionError` 既有 11 用例 |
| **可选** | **能力预判** | 对确定无编码器的协议在预生成阶段标记 `UNSUPPORTED`，不进入注入循环，直接给出明确失败原因（避免「看起来像实例故障」） |

> 不建议的方向：为 Hysteria2 补 protobuf 编码器。CLI 路径本身可正常注入 `hysteria`（受控实验 D 已证），补编码器只能掩盖 P0 的契约缺陷，且需跟随 Xray proto 演进。

---

## 7. 验证准则

| # | 验证项 | 通过标准 |
| :--- | :--- | :--- |
| V1 | 单测：CLI 载荷裁剪 | 输入含 `proxy/direct/block` 的完整配置，捕获实际传给 `ado` 的 stdin，断言 `outbounds.size() == 1` 且 `tag == "proxy"` |
| V2 | 单测：单 outbound 输入不回归 | 已单 outbound 的输入保持原样（不重复包装） |
| V3 | 集成：真实实例 | 启动实例（仅 `direct`）后注入 Hysteria2 载荷，期望 exit 0，`xray api lso` 出现 `proxy` |
| V4 | 现场回归 | 用包含 Hysteria2 的批次跑一轮，日志中 `existing tag found` 计数 = 0，ConfigType 7 代理产出 OK/FAIL（而非 `XRAY_ERROR`） |
| V5 | 全量回归 | `BatchWorkerPolicyTest` 11/11；全量 `ctest` 不低于基线（52/54，已知 `XrayManagerStartupTest` 与 `UI_FLOATINGWIDGET` 并行 flake） |

---

## 8. 结论

`existing tag found` 不是删除链路缺陷，也不是重试策略缺陷，而是 **`outbound_json` 的载荷契约在 gRPC 与 CLI 两条注入路径之间不一致**：配置生成侧给的是整份 Standalone 配置，gRPC 侧只消费 `outbounds[0]`，CLI 侧则把整份文档交给 `ado`，`ado` 逐条注入并与实例常驻的 `direct` 撞名。Hysteria2 等无 protobuf 编码器的协议 100% 回退 CLI，因此 100% 失败；`ado` 的「部分成功不回滚」叠加内部重试不清理 tag，进一步制造了 `existing tag found: proxy` 的次生签名，使问题表现为「随机重试失败」而非确定性契约错误。

修复应在**载荷归一化**（P0）处一次性解决，并用「CLI 载荷只含 1 个 outbound」的单测锁死契约；F5 已正确阻止了该缺陷引发的实例重启 churn，与本次修复互不冲突。

---

## 9. 关联文档与证据

| 类型 | 路径 |
| :--- | :--- |
| 来源（§16 遗留问题 #1） | `docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md` |
| 同源 RCA（线程停摆） | `docs/bugfix/2026-10-19-RCA-ProxyBatchTester-WorkerThreadStall-v1.0.md` |
| F5 策略实现 | `include/BatchWorkerPolicy.h`、`tests/test_batch_worker_policy.cpp` |
| 现场日志 | `bin/worker/log/ui_20261020_092411.log` |
| 生产库 | `bin/worker/guiNDB.db`（`profiles.configtype`） |
