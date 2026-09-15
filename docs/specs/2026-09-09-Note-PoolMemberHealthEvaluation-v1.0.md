---
title: "Note: 代理池成员健康度评估（v1.0）"
module: src/StandaloneProxyPool + src/ProxyHealthEvaluator + include/ProxyHealthEvaluator.h
status: draft
date: 2026-09-09
supersedes: 无
---

# 说明：代理池成员健康度评估（v1.0）

## 1. 背景与目的

在评审 `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`（统一代理池与代理监控窗口）过程中，发现**代理池成员健康度评估层存在缺口**：专用协议成员（vless/vmess/trojan/ss/hysteria2/tuic/wireguard）当前完全不被实测。本 Note 将该问题及方案方向单独沉淀，作为后续完善代理池健康评估的设计基线。

## 2. 现状机制（源码事实）

| 环节 | 位置 | 行为 |
| --- | --- | --- |
| 评估循环 | `StandaloneProxyPool::evaluatorLoop()` | 后台线程每 `cfg_.evaluate.intervalSec`（默认 10s）一轮：`relaunchIfNeeded → doProbe → mergeHealth → 策略 → 两阶段移除 → reportHealth → notifyChanged` |
| 探测装配 | `StandaloneProxyPool::doProbe()` | 锁内快照 ACTIVE 成员 → `MemberProbeTarget{tag, configtype=atoi(profile.configtype), address, port, username=profile.security, password=profile.id}`；`testUrl = cfg_.probeUrl 非空 ? cfg_.probeUrl : cfg_.observatory.destination`；`totalMs = timeoutSec*1000`（默认 5000）；`connectMs = min(totalMs, 3000)` |
| 测试 URL 注入 | `AppController::startProxyPool()` | `poolCfg.probeUrl = config_.test_url`（复用全局测试 URL，修复 frozen-field health bug） |
| 直连实测 | `ProxyHealthEvaluator::probe()` | 串行 cURL **HEAD** 经上游代理到 testUrl：configtype==4→`socks5h://`；==10→`http://`；`delayMs=getTotalTime()*1000`；`alive=(200<=code<400)`；免 TLS 校验 |
| 结果写回 | `StandaloneProxyPool::mergeHealth()` | 仅处理 `px-` 前缀且已在 members_ 的成员；alive→failStreak=0，失败→failStreak+=1；`lastProbe=now` |
| 两阶段剔除 | `StandaloneProxyPool::evaluatorLoop()` | ACTIVE→（autoPruneDead 且 failStreak>=pruneFailStreak(3)）→REMOVE_REQUESTED→下一周期 `removeOutbound` 成功→DRAINING→erase |
| 同步探测 | `StandaloneProxyPool::probeNow()` | `doProbe → mergeHealth → notifyChanged` 单轮入口；调用方：`AppController::probePoolNow()`（UI「刷新」路径）；**注入路径未接线**（头文件注释声称供注入后即时反馈，实际未接线） |

## 3. 已识别问题

| # | 问题 | 影响 | 现状 |
| --- | --- | --- | --- |
| P1 | 专用协议成员无法被 cURL 直连实测（`configtype` 非 4/10 时 `tested=false`，返回 "protocol requires xray-side probe (not directly probeable)"） | 无延迟/存活数据；`leastPing` balancer 无样本；`failStreak` 不涨故 `autoPruneDead` **不会误杀**（双刃剑） | vless/vmess/trojan/ss/hysteria2/tuic/wireguard 成员实际**从未被测量** |
| P2 | 注入路径未接线 `probeNow()`：成员注入成功后不立即触发单轮评估，最长等一个评估周期（默认 10s） | 体验与反馈延迟；「加入代理池 → 期望立即看到健康」落空 | `AppController::probePoolNow()`（UI「刷新」路径）已接线；注入路径（injectMember 成功后）仍未接线 |
| P3 | 池评估是**直连上游代理**（cURL 经成员自身 socks/http），**不经池 Xray 转发链路** | 观测的是「上游可达性/延迟」而非用户实际流量路径（本地 socks-in → balancer → 成员 outbound → 目标）的端到端链路质量 | xray ObservatoryService 只报告 `Start()` 时存在的 outbound，运行时注入成员不可见（历史 frozen-field health bug 成因） |
| P4 | 串行探测在大成员数下单轮耗时可能超过 `intervalSec` | 评估周期漂移 | 周期固定 10s，无漂移补偿 |

## 4. 方案方向

### 4.1 正确姿势：Xray 代拨（xray-side probe）

专用协议是应用层加密+多路复用协议，需 Xray 本地实现加解密/握手，cURL 无法直连。正确链路：

```
cURL → socks5h://127.0.0.1:<探针端口> → Xray(socks 入站)
     → 路由 → 成员 outbound(vless://… px-<indexId>)
     → 代拨 → testUrl
```

**项目现成先例**（批量测试体系已是此模式，池评估器未复用）：
- `ProxyFinder.cpp:228 testProxyConnectivity(int socksPort, targetUrl)`；:236 `proxyUrl = "http://127.0.0.1:" + std::to_string(socksPort)`（先起 Xray 实例注入成员，再 cURL 走本地 socks 入站）
- `ProxyBatchTester.cpp:108 workerThreadFunc(workerId, socksPort, apiPort)`；:294 `proxyTester_->test(socksPort, ...)`
- 池 Xray 配置 `ConfigGenerator.cpp:103 buildPoolConfig` 已有 socks-in（mixed，tag=socks-in，listen 127.0.0.1:poolSocksPort）且 routing 送 balancer——但该入站面向用户流量，不能作为单成员探针

### 4.2 方案 A（推荐）：临时探针实例

对 `tested=false` 成员，在 `evaluatorLoop` 内**串行**执行：用该成员的 profile 生成 outbound + socks 入站，起一次性临时 `XrayInstance` → cURL 经其 socks 端口测 `testUrl` → 销毁。

- 与 ProxyTester 同构；改动集中在 `ProxyHealthEvaluator` / `StandaloneProxyPool`；池核心不动
- 评估周期 10s，串行可控；可顺带修复 P2（`probeNow()` 接线到注入路径/UI「刷新」）
- 与 `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md` §4.6「刷新=probeNow+全量」衔接

### 4.3 方案 B（成本高，暂不建议）：池内注入探针入站

需 `XrayApi` 新增 `AddInbound`（`HandlerService.AddInbound`）——grep 证实当前**无任何 AddInbound/RemoveInbound 代码**；且 routing 规则静态，`buildPoolConfig` 无法为运行时才出现的 `px-<id>` 成员预留按 inboundTag 分流规则，动态路由缺失。

## 5. 后续完善清单

- [ ] 采用方案 A：`ProxyHealthEvaluator` 增加 xray-side probe（对非 4/10 configtype 成员走临时实例代拨）
- [ ] 接线 `probeNow()` 注入路径：注入成功后立即触发单轮评估（UI「刷新」路径已由 `AppController::probePoolNow()` 接线）
- [ ] 池成员延迟/存活数据补全后，回填 `leastPing` balancer 样本与 `autoPruneDead` 误杀保护回归验证
- [ ] 补充 `getUnifiedMonitorRows` / 池健康评估单元测试（用 `test/guindb.db`）
- [ ] 更新 `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md` §6 风险表（评估层缺口）
- [ ] UI_POOL 自动化测试改写后回归
- [ ] 池评估链路升级为端到端（经池 Xray 转发链路：本地 socks-in → balancer → 成员 outbound → 目标），替代直连上游代理观测（P3）
- [ ] 评估周期漂移补偿：串行探测超时预算 / 动态周期调整，避免大成员数下单轮超时（P4）

## 6. 相关文档

- `docs/specs/2026-08-26-Spec-StandaloneProxyPool-v1.0.md`（代理池规格）
- `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`（统一代理池与代理监控窗口规格，本 Note 的触发评审）