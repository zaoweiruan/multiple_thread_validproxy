# Xray-core 源码验证：独立代理池 selector/Observatory/移除 语义

- 日期：2026-08-26
- 关联：docs/specs/2026-08-26-Spec-StandaloneProxyPool-v1.0.md §5.8 / §8 / §9.1 / §11
- 结论：**评审 §8 三项运行期担忧在源码层面全部确认无碍**，可解除"合入前源码验证闸门"。

## 1. Balancer selector 前缀自动发现 + 候选缓存失效

文件：`E:/eclipse_workspace/Xray-core/app/proxyman/outbound/outbound.go`

- `Select(selectors)`（L164）按 `strings.HasPrefix(tag, selector)` 匹配（`selector:["px-"]` 即前缀匹配所有 `px-<indexId>`），结果缓存于 `m.tagsCache`（L166-169 命中 / L186 写入）。
- **关键**：`AddHandler`（L103）在加锁后执行 `m.tagsCache = &sync.Map{}`（L107），`RemoveHandler`（L131）同样 `m.tagsCache = &sync.Map{}`（L138）——即每次 add/remove 均**整体失效** selector 候选缓存。
- 调用链：`xray api ado` → `HandlerServiceClient.AddOutbound` → `handlerServer.AddOutbound`（command.go:165）→ `ohm.AddHandler`（command.go 对应 AddOutbound 实现）。我们的 `XrayApi::addOutboundDirect` 走同一条 gRPC HandlerService 路径，因此 `addOutbound` 成功后下一次 `selector=px-` 选择**必然可见新成员**；`removeOutbound` 后新选择不再命中。无需重启 / 无需重建 balancer。

→ **评审 §8「Selector 候选缓存必须失效」：已源码确认满足。**

## 2. Observatory 动态发现新成员

文件：`E:/eclipse_workspace/Xray-core/app/observatory/observer.go`

- `background()`（L63）在每轮探测周期中**重新调用** `hs.Select(o.config.SubjectSelector)`（L71），再对返回 tag 列表逐个 probe（L82-89 / L95-101）。
- 因此 add 新成员后，下一轮 Observatory 探测即将其纳入 `subjectSelector=["px-"]` 集合；remove 后下一轮不再 select 到。

→ **评审 §9.1「Observatory 动态发现」：已源码确认满足。**
（注：observer.go:117 有 TODO「should remove old inbound that is removed」，即 `o.status` 对已移除 tag 可能残留旧条目，但 probe 仅针对当轮 select 到的 tag，已移除成员不会被复探；本池健康取自 `getOutboundStatusDirect` 逐 outbound 状态而非 observatory 的 status 切片，无影响。）

## 3. Remove 时已有连接生命周期（DRAINING）

- `RemoveHandler`（outbound.go:131）从 `taggedHandler` 删除该 tag（L140）并失效缓存（L138）；Xray 不强制中断已建立的连接，存量会话自然结束后关闭（标准 Xray 行为）。
- 本池 `StandaloneProxyPool` 采用两阶段移除：`REMOVE_REQUESTED → api_->removeOutbound → DRAINING → erase`，与该语义兼容——removeOutbound 后 tag 退出选择集合与状态查询，本地仅保留短暂 DRAINING 记账再擦除。

→ **评审 §9.1「删除时已有连接 DRAINING 生命周期」：源码语义兼容，无阻塞点。**

## 4. 合入闸门状态

| §11 闸门项 | 状态 | 证据 |
|------------|------|------|
| ① Xray `Outbound Manager`/`HandlerSelector` Add/Remove 源码验证（缓存失效语义） | ✅ 已源码确认 | outbound.go:107 / :138 |
| ② selector 缓存失效（add 可见 / remove 不命中） | ✅ 已源码确认 | 同上 + command.go:165/173 |
| ③ Observatory 动态发现 | ✅ 已源码确认 | observer.go:71 |
| ④ Remove DRAINING 生命周期 | ✅ 源码兼容 | outbound.go:140 |
| ⑤ 真实 Xray 进程实证（Step 10 手动集成） | ⏳ 待人工运行（无头环境无法跑 GUI/网络） | 见 Spec §11 Step 10 + 本仓库 manual 清单 |

**仍建议**：在具备显示器 + 网络的机器上按 Spec §11 Step 10 跑一次真实集成（双击注入 → 监控面板删除 → 崩溃重注入），作为最终健全性确认；源码层面已无合入 blocker。
