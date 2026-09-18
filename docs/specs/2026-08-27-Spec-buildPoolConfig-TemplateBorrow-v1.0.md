# Spec: buildPoolConfig 借鉴模板强化（DNS / block / QUIC 嗅探 / 路由分流）

- **日期**: 2026-08-27
- **模块**: `ConfigGenerator::buildPoolConfig` (`src/ConfigGenerator.cpp`, `include/ConfigGenerator.h`)
- **版本**: v1.0
- **关联**: `bin/xray-config-template.json`、独立代理 `BuildXrayConfig`、独立代理池 `StandaloneProxyPool` / `XrayInstance`
- **状态**: 待实现

---

## 1. 背景与目标

独立代理池（`buildPoolConfig` 生成的 xray 配置）当前只具备最小控制面：
单 socks-in 入站、`direct` 出站、`balancer-out` 负载均衡、`observatory` 健康探测。
相比 `bin/xray-config-template.json` 中的成熟独立代理配置，缺少：

1. **DNS 配置**（`hosts` + DoH `servers`）—— 成员出站若使用域名地址，xray 需可靠解析。
2. **`block` 出站 + UDP/443 拦截规则** —— 防止 QUIC 泄漏（与模板一致的反泄漏策略）。
3. **QUIC 嗅探**（`sniffing.destOverride` 加入 `quic`）—— 提升域名嗅探命中率，路由更准确。
4. **CN / 私网 → `direct` 分流规则**（`geoip:cn` / `geosite:cn` / `geoip:private` / `geosite:private`）。

本 Spec 将以上 4 项从模板“照搬”到池启动配置，并修正一个隐含正确性前提（geo 资源目录）。

---

## 2. 输入 / 输出

- **输入**: `socksPort`, `apiPort`, `StandalonePoolConfig& cfg`（与现状一致，不新增参数）。
- **输出**: xray 26.x 可加载的 JSON 字符串（序列化自 `boost::json::object`）。

### 2.1 新增/变更内容

| 项 | 变更 | 说明 |
|----|------|------|
| `dns` | 新增 | 照搬模板 `dns` 块（hosts 映射 + DoH servers）。 |
| `outbounds` | 增加 `block` | `{"tag":"block","protocol":"blackhole"}`。`direct` 已恒定存在。 |
| `inbounds[0].protocol` | `socks` → `mixed` | 与模板一致，同时支持 SOCKS/HTTP 入站。 |
| `inbounds[0].settings.allowTransparent` | 新增 `false` | 与模板一致。 |
| `inbounds[0].sniffing.destOverride` | 追加 `quic` | 由 `["http","tls"]` → `["http","tls","quic"]`。 |
| `inbounds[0].sniffing.routeOnly` | 新增 `false` | 与模板一致。 |
| `routing.rules` | 重写为有序 9 条 | 见 §3（含模板的国内 DNS IP 列表、DNS 域名直连规则）。 |

> 注：保持池的 `listen: "127.0.0.1"`（仅本地监听，安全）与 `tag: "socks-in"`（路由规则依赖）不变；
> 不采用模板的 `listen: "0.0.0.0"`（会暴露池到全网）。

---

## 3. 路由规则顺序（关键）

xray 按数组顺序评估规则，顺序决定正确性。池配置最终规则数组（自上而下）：

1. `inboundTag:["api"] → outboundTag:"api"` —— 控制面（HandlerService/RoutingService/ObservatoryService）走内建 `api` 出站，必须最高优先级。
2. `network:"udp", port:"443" → outboundTag:"block"` —— 拦截 QUIC（udp/443）反泄漏，与模板一致。
3. `ip:["geoip:private"] → outboundTag:"direct"` —— 私网直连。
4. `domain:["geosite:private"] → outboundTag:"direct"` —— 私网域名直连。
5. `ip:[国内 DNS 解析器 IP 列表] → outboundTag:"direct"` —— 照搬模板的硬编码国内 DNS IP（字面量，无 geo 依赖），确保 DNS 查询本身走直连。
6. `domain:["domain:alidns.com","domain:doh.pub","domain:dot.pub","domain:360.cn","domain:onedns.net"] → outboundTag:"direct"` —— 照搬模板的国内容器 DNS 域名直连。
7. `ip:["geoip:cn"] → outboundTag:"direct"` —— 国内 IP 直连。
8. `domain:["geosite:cn"] → outboundTag:"direct"` —— 国内域名直连。
9. `inboundTag:["socks-in"] → balancerTag:"balancer-out"` —— 兜底：其余流量（境外）经负载均衡器走 `px-*` 成员。

> **说明**：模板中 `geosite:google → proxy` 为单代理场景特化，池无单一 `proxy` 出站（境外统一走 `balancer-out`），故省略；其余“可用且相关”的规则已全部照搬。

> **顺序保证**：第 3–6 条必须在第 7 条之前，否则境外流量会先被 `socks-in → balancer-out` 兜底，CN/私网分流失效。

---

## 4. 正确性前提：geo 资源目录

新增的 `geoip:cn` / `geosite:cn` / `geoip:private` / `geosite:private` 以及 DNS `servers` 中的
`geosite:*` 域名匹配器，要求 xray 启动时能加载 `geoip.dat` / `geosite.dat`。
xray 默认仅在 **执行文件目录 / 当前工作目录** 搜索，而本机布局为
`.../bin/xray/xray.exe` 与 `.../bin/geoip.dat`（资源在 exe 的**祖父目录**），
因此 **必须设置 `XRAY_LOCATION_ASSET`**。

- **运行路径**：`AppController::startProxyPool` 在启动池前设置进程级
  `XRAY_LOCATION_ASSET`（取 `config_.proxy.xray_asset_dir`，回退为 xray exe 的祖父目录），
  由 `CreateProcessW` 继承给池的 xray（与独立代理 `AppController.cpp:835` 一致）。
- **测试路径**：`tests/test_config_generator.cpp` 的 `runXrayTest` 在 `_popen` 前设置
  `XRAY_LOCATION_ASSET`（由 `xrayPath` 推导祖父目录，且校验 `geoip.dat` 存在后设置），
  否则 `BuildPoolConfig_Xray26Loads` 会因 geo 规则加载失败而 FAIL。

---

## 5. 边界条件 / 风险

| 场景 | 处理 |
|------|------|
| 成员出站使用域名地址 | 由新增 `dns` 块解析（DoH + hosts 兜底）。 |
| 客户端经池发 QUIC（udp/443） | 按规则 2 被 `block` 丢弃（与模板一致，迫使走 TCP 类代理协议）。**已知取舍**：池无法代理 QUIC 目标流量；不影响 observatory（其探测直接走 `px-*` 出站，不经入站路由）。 |
| `XRAY_LOCATION_ASSET` 未设置 | xray 启动失败（“failed to load geoip:cn”）。已通过 §4 两处设置兜底。 |
| 测试机无 xray 二进制 | `findXrayBinary()` 返回空 → `BuildPoolConfig_Xray26Loads` 自动 `GTEST_SKIP`，不影响 CI。 |
| 测试机无 geo 数据 | `runXrayTest` 仅在 `geoip.dat` 存在时设置 env；否则跳过兜底（配置仍生成，仅 load 测试可能失败——本地已确认 geo 数据存在）。 |

---

## 6. 测试计划（TDD）

在 `tests/test_config_generator.cpp` 中新增/扩展：

- `BuildPoolConfig_TemplateHardening`（新增）：
  - `root` 含 `dns` 且 `dns.hosts` / `dns.servers` 非空。
  - 存在 `tag=="block"` 且 `protocol=="blackhole"` 的出站。
  - `inbounds[0].sniffing.destOverride` 含 `"quic"`。
  - `routing.rules` 顺序断言：索引 0 = api→api；存在 `network==udp && port==443 → block`；
    存在 4 条 geo → direct（private ip/domain、cn ip/domain）；最后一条 = socks-in → balancer-out。
- `BuildPoolConfig_Structure`（现有，保持通过）：无 `balancing` 协议、balancer 标签/选择器、observatory 存在等断言不受影响。
- `BuildPoolConfig_Xray26Loads`（现有，保持通过）：需 §4 测试路径 env 设置，否则将因 geo 规则失败。

---

## 7. 不做的事（范围外）

- 不改动独立代理 `BuildXrayConfig` / `buildSingboxConfig`。
- 不引入模板中单代理特化的 `geosite:google → proxy` 与硬编码国内 DNS IP 列表。
- 不调整 `log` 级别（现状已是 `warning`）。
- 不修改 `px-*` 运行时注入逻辑（`XrayApi::addOutbound`）。
