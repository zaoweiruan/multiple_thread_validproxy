# Current Context

> **角色**: 提供跨会话的上下文锚点。每轮会话开始时，Agent 应检查并更新 §三。
> **维护规则**: §三 在每轮会话结束时更新：移除已完成的旧条目，添加本会话的 Bug 修复引用。
> **边界**: 只记录"在哪里"和"最近做了什么"的上下文锚点，不记录深度知识（归档至 `docs/project-knowledge.md`）。

---

## 一、基于 xray、v2rayn 开源项目

- **v2rayn 源码目录**: `E:\eclipse_workspace\v2rayn`
  - 关键文件:
    - `v2rayN\ServiceLib\Handler\ConfigHandler.cs` — DedupServerList() 方法
    - `v2rayN\ServiceLib\Models\ProfileItem.cs` — 数据模型
    - `v2rayN\ServiceLib\Manager\AppManager.cs` — ProfileItems() 获取代理列表
- **xray 源码目录**: `E:\eclipse_workspace\Xray-core`
- **wxWidgets 源码目录**: `E:\eclipse_workspace\wxWidgets`
  - 关键文件:
    - `src/generic/datavgen.cpp:2091` — wxDataViewMainWindow Create() 缺失 CS_DBLCLKS 导致 MSW 平台双击无效

## 二、项目配置文件

- `E:\eclipse_workspace\multiple_thread_validproxy\bin\config.json`

## 三、本会话引用

> 每轮会话开始时，将前一轮的已关闭条目移至 `docs/project-knowledge.md §7` 或 `docs/bugfix/`。
> 格式: 条目简述 → `docs/bugfix/YYYY-MM-DD-xxx.md`

| 日期 | 条目 | 文档 |
|------|------|------|
| 2026-08-07 | git-master 技能安装 — AGENTS.md §6.2 路由引用但环境缺失；GitHub 直连不通致 npx skills add 失败，配置 git 全局代理 socks5://127.0.0.1:10808 后手动 clone josiahsiegel/claude-plugin-marketplace 复制至全局技能目录（10 文件），重启 Kilo 后生效 | `docs/reports/2026-08-07-Report-GitMasterSkill-Install-v1.0.md` |
| 2026-08-07 | About 窗口编译时间不准确 — version.h 生成依赖 build.ninja 致仅 configure 时刷新（增量构建 APP_BUILD_TIME 停留上次 configure 时刻），改 add_custom_target(update_version_h ALL) 每次构建重生成；配置窗口保存 console 日志级别未同步 LogPanel 下拉框过滤，保存回调补 setInitialLogLevel | `docs/bugfix/2026-08-07-Bugfix-GUI-AboutBuildTime-LogLevelSync-v1.0.md` |
| 2026-08-07 | 订阅更新跳过提示误报 ERROR — updateAll() 全部订阅因更新间隔被跳过时以 ERR 输出 "All subscriptions skipped by update interval - nothing to update"（非错误，随后 return true 正常完成）；修复=ERR→REPORT；真正失败的 "failed to update" 保持 ERR | `docs/bugfix/2026-08-07-Bugfix-SubitemUpdater-SkipLogLevel-v1.0.md` |
| 2026-08-07 | 网络监控探测错误详情日志级别过低 — CheckURLWithDnsFlag 内 probe failed/DNS error/http_code 从 DEBUG/TRACE 提升为 WARN（生产 console_level=WARN 下故障可见）；ThreadLoop 状态机日志保持 ERR | `docs/bugfix/2026-08-07-Bugfix-NetworkMonitor-ProbeLogLevel-Warn-v1.0.md` |
| 2026-08-07 | GUI 启动日志未按 config.json file_level 过滤 — main_gui.cpp Logger::init 前预解析 log 段（ConfigFileStore+ConfigJsonParser+LogConfigParser），file_level=ERROR 时 INFO/DEBUG 不再写入日志文件 | `docs/bugfix/2026-08-07-Bugfix-GuiLogFileLevel-v1.0.md` |
| 2026-08-06 | 状态栏日志文件名不可见 — netMonPanel_ 遮挡 Field1；状态栏扩 4 字段 + SetStatusWidths + 双击 Bind 状态栏 | `docs/bugfix/2026-08-06-Bugfix-MainFrame-StatusBar-LogFile-v1.0.md` |
| 2026-08-06 | GUI 入口 Logger 启动级别补全 console_level + LogPanel 筛选器同步 config.json | `docs/bugfix/2026-08-06-Bugfix-Logger-GUI-ConsoleLevel-v1.0.md` |
| 2026-06-09 | xray.executable 配置值校验 — 加载时 ERROR+弹窗、GUI 文件存在性+扩展名检查 | `docs/superpowers/specs/2026-06-09-Spec-Validproxy-xray-executable-validation-v1.0.md` |
| 2026-06-09 | test_config_reader.exe 输出目录从 bin/ 移至 tests/ — 遵循测试文件目录规范 (AGENTS.md #8) | `CMakeLists.txt` |
| 2026-06-09 | Config validation improvements plan — 5 tasks: in-class initializers, type-warn logs, xray existence check, save failure notification, load() unit tests | `docs/plans/2026-06-09-config-validation-improvements-plan.md` |
| 2026-06-09 | UI 布局调整 — 工具栏去除 dbpath 显示 / searchbox 右移 50px / ProxyDetail 默认隐藏 | `docs/superpowers/specs/2026-06-09-Spec-Validproxy-ui-layout-tweaks-v1.0.md` |
| 2026-06-09 | 订阅列表面板 — 新增"有效"列 (delay>0) + 列名中文化（名称/有效/代理数/更新） | `docs/superpowers/specs/2026-06-09-Spec-Validproxy-subscription-column-i18n-and-valid-count-v1.0.md` |
| 2026-06-09 | Resizable splitter for subscription/proxy panels — wxSplitterWindow implementation with parent fix | `docs/plans/2026-06-09-feat-ui-resizable-splitter-v1.0.md` |
| 2026-06-04 | SubitemUpdaterV2 硬编码 `"bin/config"` 路径修复 | `docs/bugfix/2026-06-04-subitemupdater-hardcoded-binconfig-path.md` |
| 2026-06-12 | Worker join 超时修复 v3 — 动态 join timeout + 轻量 warmup（v2 XrayApi::ping 轮询过重，v1 7s 不足） | `docs/bugfix/2026-06-12-Bugfix-ProxyBatchTester-Worker0-JoinTimeout-v1.0.md` |
| 2026-06-15 | AutoTask 管道诊断不可见修复 — SubitemUpdaterV2 return false + 日志级别低 + step name 别名缺失 + ConfigDialog 改进 + ProxyBatchTester zero-proxy return true | `docs/bugfix/2026-06-15-Bugfix-AutoTask-SubitemUpdater-logging-and-pipeline.md` |
| 2026-06-15 | NetworkMonitor 批量网络中断中止 — 独立 NetworkMonitor 类，后台线程 HEAD 探测，auto atomic 标志，ProxyBatchTester/SubitemUpdaterV2 检测中断自动中止 | `docs/superpowers/specs/2026-06-15-Spec-NetworkMonitor-batch-network-abort-on-disconnect-v1.0.md` |
| 2026-06-16 | **Code Review 全量修复: #1 CLI Ctrl+C / #2 状态文件路径 / #3 SQL(abandon) / #4 未用图标 / #5 重复方法合并 / #6 版本验证 DONE** + 额外修复: NetworkMonitor 启动虚假 RESTORED (firstCheckDone_), MainFrame re-entry guard (5 handlers), REALITY publicKey ERR→INFO, onUpdateType→updateMethod 重命名, "任务完成通知"移至通知分类 | `docs/bugfix/2026-06-16-Bugfix-Review-AutoTask-CLI-StateFile-v1.0.md` |
| 2026-06-16 | **修复 deduplicate() 嵌套事务错误** — deduplicate() 已 BEGIN 事务后，deduplicateConfigErrorPhase() 调用 deleteByIndexId() 再次 BEGIN 导致 SQLite 错误 | `docs/bugfix/2026-06-16-Bugfix-Dedup-NestedTransaction-v1.0.md` |
| 2026-06-18 | SubitemUpdaterV2.cpp 2422→1198 行重构 — 提取 SubscriptionParser/Deduplicator/Importer/SubscriptionUpdater 四个类 | `docs/specs/2026-06-18-Spec-SubitemUpdaterV2-Decomposition-v1.0.md` |
| 2026-06-25 | **NetworkMonitor 断连探测机制** — 断连后暂停测试，按配置阈值连续探测 N 次后再决定是否终止。修改: ConfigReader (probe_on_disconnect 结构体)、NetworkMonitor (consecutiveFailures_ 延迟取消)、ProxyBatchTester (waitForNetworkRecovery 暂停等待) | `docs/specs/2026-06-25-Spec-NetworkMonitor-ProbeOnDisconnect-v1.0.md` |
| 2026-06-26 | **SubitemUpdaterV2 未传入 netMon_** — doUpdateSubscription/doUpdateAllSubscriptions 构造 SubitemUpdaterV2 时未传第7参数 netMon (默认 nullptr)，6处 IsConnected() 空指针短路永不触发 | `docs/bugfix/2026-06-26-Bugfix-SubitemUpdaterV2-MissingNetMon-v1.0.md` |
| 2026-07-01 | **Sync 目标库新订阅 enabled 默认 0** — migrateSubscription() 直接复制源库 enabled 状态，目标库新订阅应默认禁用 | `docs/bugfix/2026-07-01-Bugfix-Sync-Subscription-EnabledDefault-v1.0.md` |
| 2026-07-01 | **SubscriptionPanel/ProxyListPanel 双击无反应** — MSW wxDataViewMainWindow 缺失 CS_DBLCLKS，使用 selection-change-based 双击检测绕过限制 | `docs/bugfix/2026-07-01-Bugfix-DoubleClick-V1.0.md` |
| 2026-07-01 | **对话框屏幕居中** — 6 处 wxDialog/wxMessageDialog 添加 CentreOnScreen() 调用 | `docs/specs/2026-07-01-Spec-DialogCentering-v1.0.md` |
| 2026-07-02 | **Sing-box Support Implementation** — Design spec created; add sing-box as alternative proxy core to Xray with config fields, UI integration, outbound format mapping | `docs/specs/2026-07-02-Spec-Singbox-Support-v1.0.md` |
| 2026-07-03 | **echconfiglist 双格式解析** — 区分 Base64 ECH 配置与 DNS URL 格式，生成正确 sing-box ech.config；计划文档已创建 | `docs/superpowers/plans/2026-07-03-echconfiglist-dual-format-support.md` |
| 2026-07-09 | **DNS 缓存解析模块 DnsCache** — DnsCache 类，静态 resolve()，getaddrinfo IPv4 + 惰性 WSAStartup + 互斥锁保护的 unordered_map 缓存；集成至 RegionDetector Strategy 3；18/18 ctest 通过 | `docs/specs/2026-07-09-Spec-DnsCache-v1.0.md` |
| 2026-07-20 | **GUI 入口缺失 curl_global_init() 崩溃** — main_gui.cpp 未调用 curl_global_init()，右键解析代理地区时线程内 curl_easy_perform 访问违例崩溃 | `docs/bugfix/2026-07-20-Bugfix-CurlGlobalInit-GUI-v1.0.md` |
| 2026-07-29 | **gRPC addOutboundDirect protobuf 字段 + 路径修复** — TypedMessage field 2→3 (proxy_settings)、两处 gRPC 路径 CommandService→HandlerService 修正、4 单元测试 | `docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md` |
| 2026-07-29 | **addOutboundDirect JSON parse 回退修复** — ConfigGenerator 输出 `{"outbounds":[{...}]}` 但代码读取 `rootObj["outbound"]` 单对象导致解析抛异常触发回退；提取 parseOutboundJson 静态方法，协议→typeUrl 映射，10 协议映射单元测试，33/33 tests pass | 同上 |
| 2026-07-31 | **AddOutboundDirect 编码字段修复** — (1) protocol 原值被忽略(恒0)改用配置原值 (2) SenderConfig 字段号 1/3→2/4 (stream_settings/multiplex_settings，以 Xray-core command.proto 为准) (3) encodeStreamConfig port int64 被 is_uint64 守卫静默丢弃，扩展 int64>0 编码；StreamSettingsBuilder 复核无缺陷；新增 10 单测 (EncodeMultiplex/Stream/SenderSettings 系列)，44→54，54/54 pass | `docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md` |
| 2026-08-04 | **代码审计优化 Phase 1-4 全部落地 + CurlEasyHandle 修复** — A1-A6/B1-B14/C1-C9/D1-D8/E1-E14（C2/C5/C8/C9/E3/E4/E8/E9b 验证已实现）；CurlEasyHandle: C6 移动语义补全 cancelFlag_/secondaryCancelFlag_、E3 超时兜底（仅 0/负值→1000ms，修正误伤 NetworkMonitor 快速探测 25ms 的回归）、E4c 二级取消标志 (setCancelFlags/isCancelRequested)；SubscriptionUpdater/SubitemUpdaterV2 fetchUrl TLS 验证恢复、fetchUrlViaProxy 保留豁免+注释；CreateProcessW 迁移；PortManager findAvailable 重写 (45.7s→1.4s)；21/21 测试通过 | `docs/reports/2026-08-04-Report-CurlEasyHandle-Audit-Fixes-v1.0.md` |
| 2026-08-04 | **批量测试网络监控误报断网修复** — waitForNetworkRecovery 无超时 CV 死等（NetworkMonitor 从不 notify networkCv_）→ 一次瞬时探测失败致所有 worker 永久挂起或 3 周期后误取消整批；重写为 250ms 轮询 + 30s 上限；另清理 fc15eec 遗留每代理 2 次 listOutboundsDirect + outbound_json TRACE 调试残留；21/21 测试通过 | 本会话（未建 bugfix 文档） |
| 2026-08-04 | **订阅解析乱码 Shadowsocks 节点修复** — Argh94-ShadowSocks 订阅 malformed ss:// 链接（VLESS 参数伪装成 ss、userinfo 为 URL 编码用户名）+ decodeBase64 非法字符解码为索引 0 → 全库 29811 个代理 Security/Id 二进制垃圾致批量测试全失败（Success:0）；修复：decodeBase64 跳过非法字符、ss:// 分支 isPrintableAscii 校验丢弃、Deduplicator::deduplicateConfigErrorPhase 扩展删除非法配置代理（CLI -D/UI 去重触发）、日志降噪（XRAY_ERROR 不再打印完整 outbound JSON 消除密码泄露、失败日志降 DEBUG）；21/21 通过 | `docs/bugfix/2026-08-04-Bugfix-SubscriptionParser-GarbageSS-v1.0.md` |
| 2026-08-04 | **gRPC xhttp 传输编码与 Xray v26.2.4 兼容修复 + REALITY bytes 编码修复** — 新构建首次真正编码 xhttp（protocolName="xhttp" + `xray.transport.internet.xhttp.Config`）但 v26.2.4 已移除 xhttp 协议（消息类型未注册）→ AddOutbound `proto: not found`；旧构建为静默退化裸 TCP 假成功，非代码回退；修复 network=="xhttp" 统一按 splithttp 编码（protocolName="splithttp" + `xray.transport.internet.splithttp.Config`）；REALITY publicKey/shortId 字段号 23/24 为 BYTES，必须 base64/hex 解码后 encodeLengthDelimited；端到端 207 节点：XRAY_ERROR 8→0（8/8 xhttp 注入成功），3/8 xhttp 连通（cloudflare.182682.xyz/media-ru7/second.shadydomain），88→91 提升，单测 92/92 | `docs/bugfix/2026-08-04-Bugfix-XrayApi-XHTTP-SplitHTTP-Mapping-v1.0.md` |
| 2026-08-05 | **DNS 解析永久缓存 + NetworkMonitor 第二层缓存删除** — DnsShareCache 程序级共享（CURLSH + CURLOPT_DNS_CACHE_TIMEOUT=-1，7 模块复用，进程生命周期）；"统一为单一缓存"调查结论维持两套职责隔离缓存（DnsShareCache 管 libcurl 连接域名、utils::DnsCache 管节点域名）；NetworkMonitor dnsCache_（host→成功时间戳，只写不读死代码）整体删除（成员/include/getHostFromUrl/写入块），dnsFailures_/getDnsFailures() 保留；NetworkMonitorTest 11 用例通过（8.22s） | `docs/specs/2026-08-05-Spec-DnsCache-Permanent-v1.0.md` |
| 2026-08-05 | **导入订阅稽核：去除无法正确生成配置的代理** — utils::isPrintableAscii 提取（Deduplicator/SubscriptionParser 复用）+ SubitemUpdaterV2::isValidProxy 增加 Security/Id 可打印 ASCII 稽核（与去重阶段 deduplicateConfigErrorPhase 判定一致，入库前丢弃二进制垃圾节点）；trojan/hy2 security 空串不误杀；test_utils 新增单测 | `docs/specs/2026-08-05-Spec-ImportProxyValidation-v1.0.md` |
| 2026-08-06 | **代理列表面板测试速度/连接用户数列显示为空** — ProxyListModel/ProxyListPanel 列标题与字符串格式化修复，新增 GetTestSpeedStr/GetUserCountStr 辅助方法 | `docs/bugfix/2026-08-06-Bugfix-ProxyListDisplayValues-v1.0.md` |
| 2026-08-06 | **GUI 入口 Logger 启动级别遗漏 console_level 配置** — main_gui.cpp 补全 setConsoleLevel 调用，与 CLI 一致；LogPanel 筛选器启动级别同步 config.json log_console_level，新增 setInitialLogLevel() 方法；21/21 测试通过 | `docs/bugfix/2026-08-06-Bugfix-Logger-GUI-ConsoleLevel-v1.0.md` |
| 2026-08-06 | **状态栏未铺满底部行** — SetStatusWidths 固定宽度总和 920 逻辑 px < 状态栏逻辑宽 1280 px，右侧空白；末字段改 -1（可变宽度）后 Field3 拉伸铺满，UIA 验证右缘=1280 | `docs/bugfix/2026-08-06-Bugfix-MainFrame-StatusBar-LogFile-v1.0.md` |
