# Bugfix: 独立代理池成员注入失败 + Observatory 健康检查路径错误

- 日期: 2026-08-27
- 严重度: 功能阻塞（独立代理池成员无法注册到 Xray，健康检查恒为空）
- 状态: ✅ fixed / 单元测试全绿
- 关联文件: `src/StandaloneProxyPool.cpp`, `src/ProxyFinder.cpp`, `src/XrayApi.cpp`, `include/XrayApi.h`, `tests/test_xray_api_direct.cpp`

## 1. 现象

`StandaloneProxyPool::injectMember()` 返回 false（日志 `[StandaloneProxyPool] inject failed for px-<id>`），或即便返回 true，注入的成员在 Xray 的 outbound 列表中并不存在，导致：

- 健康检查循环 `ProxyHealthEvaluator::probe()` 永远拿不到成员状态（`px-*` 标签匹配不到）；
- 成员 `lastAlive` 恒为 false、`failStreak` 不增长，自动剔除/优化策略全部失效；
- 池子"看起来在跑"但没有任何代理真正可以被路由使用。

`ProxyFinder::testConnection()` 同样走 `addOutbound` 子进程路径，注入也失败。

## 2. 根因

### 2.1 注入路径选错（StandaloneProxyPool / ProxyFinder）

`injectMember` 通过 `config::OutboundBuilderFactory::create()` 生成**单个** outbound 对象
`{tag, protocol, settings, ...}`，直接传给 `XrayApi::addOutbound()`。

`addOutbound()` 把该 JSON 作为 stdin 喂给 `xray api ado`。但 `ado` 子命令期望的是
`AddOutboundRequest` protobuf 的 JSON（`{"outbound": {...}}` 形式），而非裸 outbound，因此
xray 返回 `no valid outbound found`，注入静默失败。

正确的、已在 `ProxyBatchTester` 中验证可用的路径是 `XrayApi::addOutboundDirect()`：
它手工构造 `AddOutboundRequest` protobuf（`HandlerService/AddOutbound`）。但 `addOutboundDirect`
内部的 `parseOutboundJson()` **要求包装后的形式** `{"outbounds":[{...}]}`（与 `ConfigGenerator`
产出一致），而池子此前从未产生这种包装形式——所以即便切到 direct 路径，也会因格式不匹配而
回退到坏掉的 `addOutbound`。

### 2.2 Observatory 服务路径错误（getOutboundStatusDirect）

`ProxyHealthEvaluator::probe()` 调用 `XrayApi::getOutboundStatusDirect()`，后者向
`/xray.app.observatory.command.ObservatoryService/GetOutboundStatus` 发请求（依据 proto 包名
`xray.app.observatory.command` 推导）。但 **xray 运行时实际注册的服务名是
`xray.core.app.observatory.command.ObservatoryService`**（旧版 `xray.core.` 前缀）。

> 该结论已通过 gRPC reflection + 直接探测在 Xray 26.3.27 上实证：
> - `xray.core.app.observatory.command.ObservatoryService/GetOutboundStatus` → OK
> - `xray.app.observatory.command.ObservatoryService/GetOutboundStatus` → UNIMPLEMENTED
>
> 注意：HandlerService 在 xray 中**双重注册**（`xray.app.proxyman.command.HandlerService` 与
> `v2ray.core.app.proxyman.command.HandlerService` 均存在），而 observatory command 只注册
> `xray.core.app...` 一个名字——这正是注入（HandlerService）成功、而健康检查（ObservatoryService）
> 报 `unknown service` 的原因。

gRPC 调用因此必然失败（`unknown service`），probe 永远返回空。

## 3. 修复

- `StandaloneProxyPool::injectMember()` / `reinjectAll()`：用 `factory.create(profile, tag)`
  生成对象后**显式钉死 `ob["tag"] = tag`**，再包装成 `{"outbounds":[ob]}`，优先调用
  `addOutboundDirect()`，失败时回退 `addOutbound()`（与 `ProxyBatchTester` 既有模式一致）。
- `ProxyFinder::testConnection()`：同样改为 `addOutboundDirect()` 优先、回退 `addOutbound()`。
- `XrayApi::getOutboundStatusDirect()`：修正 Observatory 路径为运行时注册名
  `/xray.core.app.observatory.command.ObservatoryService/GetOutboundStatus`；将其提取为可单测的
  `static const char* XrayApi::observatoryStatusPath()`。

## 4. 验证

- 新增单测 `ObservatoryStatusPathIsCorrect`：锁定修正后的服务路径
  `/xray.core.app.observatory.command.ObservatoryService/GetOutboundStatus`，并断言含
  `core.app.observatory.command` 前缀。
- 新增单测 `ParseOutboundJsonWrappedFormVless` / `...Freedom`：验证池子新产出的
  `{"outbounds":[...]}` 包装形式能被 `parseOutboundJson` 正确解析出 `tag` 与 `typeUrl`。
- 新增单测 `ParseOutboundJsonRejectsBareObject`：确认旧的裸对象形式现被正确拒绝，证明包装
  已成必需。
- **实证**：对 Xray 26.3.27 启用 `ReflectionService` 枚举已注册服务，确认
  `xray.core.app.observatory.command.ObservatoryService` 存在而 `xray.app.observatory.command...`
  不存在；并直接探测 `GetOutboundStatus`（OK）vs `UNIMPLEMENTED`。
- opt-in 实时集成测试 `StandaloneProxyPool.LiveInjectionAndObservatoryPath`（`XRAY_REAL_EXE` 门控）
  **PASS**——注入 `injected px-900012355` 成功、Observatory 健康检查不再报 `unknown service`。
- 构建：`test_xray_api_direct`（17/17 通过）、`test_standalone_proxy_pool`（7/7 通过）、
  `validproxy.exe` GUI 链接通过。

## 5. 影响范围

- 仅影响独立代理池成员注入与健康检查、ProxyFinder 连接测试；不改变批量测试
  （`ProxyBatchTester` 早已使用 direct 路径）。
- Observatory 健康探测现在能正确返回 `px-*` 成员状态，自动剔除/优化策略恢复生效。
