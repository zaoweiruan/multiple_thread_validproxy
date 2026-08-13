# Analysis: addOutboundDirect 注入错误配置问题根因分析 v1.0

- **日期**：2026-08-10
- **模块**：`src/XrayApi.cpp`（addOutboundDirect 注入链路）、`src/ProxyBatchTester.cpp`（worker 批量测试）、`bin/log/ui_20260810_113211.log`（日志取证）、`src/SubitemUpdaterV2.cpp`（订阅导入链路）
- **关联问题**：批量测试期间 `addOutboundDirect FAILED` 大量刷屏（批次③失败 2266 条），需判断是注入编码 bug 还是代理配置数据污染
- **补充（2026-08-10）**：追加 §4.7「订阅导入私网地址代理」专项调查（indexid 5323085616270219902 → 上游 Argh94-vmess 订阅文件原样命中）
- **关联文档**：
  - `docs/bugfix/2026-08-10-Bugfix-SplitHTTP-nil-request-Panic-v1.0.md`（类别 C2 的应用侧防御，本日志为修复前 build）
  - `docs/bugfix/2026-08-10-Bugfix-RelaunchedInstanceContinuesQueue-v1.0.md`（类别 C1/C2 重启后队列延续，本日志为修复前 build）
  - `docs/bugfix/2026-08-10-Bugfix-XrayInstance-StderrCapture-v1.0.md`（本日志 panic 堆栈不可见的观测缺口）
  - `docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md`（注入编码历史修复，本次日志已含全部修复）

---

## 1. 背景与数据来源

- **日志**：`bin/log/ui_20260810_113211.log`（223 行，2026-08-10，validproxy **v1.4.9 (dirty)** Debug build，构建时间 2026-08-07 16:36，编译器 14.2.0）。
- **运行形态**：`XrayManager::start count=4, startPort=1080, apiPort=10080, configDir=...\bin\config, actualCount=4`，4 个 worker 并发批量测试。
- **注入链路**：`ProxyBatchTester` worker → `XrayApi::addOutboundDirect` → gRPC `HandlerService.AddOutbound` → Xray 实例解析 outbound 配置并注入。
- **判定口径**：`addOutboundDirect FAILED` 表示 Xray 返回错误；`[XrayApi] addOutbound FAILED: exitCode=<n>, output=<...>` 表示 gRPC 注入失败后回退到 `xray api ado` 子进程方式仍失败。
- **修复前快照**：C1/C2 两个 panic 均为本次日志 build（2026-08-07 16:36）之前的 Xray-core / validproxy 行为；splithttp 校验、重启队列延续、stderr 捕获三项修复尚未合入该 build。

## 2. 批次统计

| 批次 | 代理量 | 时间窗 | Success | Failed | 备注 |
| :--- | ---: | :--- | ---: | ---: | ---: |
| ① | 193,073 | 11:32:34 – 11:36:41 | 0 | 442 | Success=0 疑为用户中途取消测试 |
| ② | 174,077 | 11:37:04 – 11:38:07 | 0 | 48 | 同上 |
| ③ | 2,715 | 11:38:32 – 12:11:25 | 131 | **2,266** | 失败率 83.5%，错误集中在此时段 |

- Worker 分布：Worker-2/Worker-3 持续命中类别 B（SS 加密不支持）；Worker-0/Worker-1 命中类别 A（非法 ID）与实例 panic（C1/C2）。
- 12:07:33 之后无新的 ERR 记录；12:11:25 批次③ 结束。

## 3. 错误分类总览

| 类别 | 错误特征 | 频次 | 根因归属 | 修复归属 |
| :--- | :--- | :--- | :--- | :--- |
| **A1** | `encoding/hex: invalid byte: U+002D '-'` / `U+0054 'T'` | 多（L26-40/86-90/96-100） | 代理库 id 字段为非标准 UUID（连字符位置错误/混入字符） | 数据污染 |
| **A2** | `invalid UUID: %2550...%256D-299` 等双重 URL 编码串 | 2（L133-142） | 订阅/分享链接把节点名写入 id 字段且经双重编码 | 数据污染 |
| **B** | `proxy/shadowsocks: Unsupported cipher.` | **最高频**（L41-85/91-95/128-132/185-214） | DB 中遗留 Xray 旧版支持的 SS 加密方法 | 数据污染 |
| **C1** | `panic: index out of range [8] with length 0`（REALITY+gRPC） | 1（11:49:15） | Xray-core reality 握手 bug（reality.go:273） | 运行时稳定性（上游） |
| **C2** | `panic: invalid memory address or nil pointer`（splithttp） | 1（12:04:08） | Xray-core splithttp OpenStream 丢弃 error 致 nil request | 运行时稳定性（应用侧已防御） |
| **D** | `addOutbound FAILED: exitCode=1, output=`（空） | 1（11:49:13） | 实例崩溃重启窗口内 `xray api ado` 回退子进程失败 | 时序问题 |

## 4. 类别详析

### 4.1 类别 A1 — vless/vmess ID 非合法 UUID（最多出现于批次③前段）

- **日志证据**：L26-40、L86-90、L96-100，`encoding/hex: invalid byte: U+002D '-'`，即 Xray 解析 `"id"` 字段时把字符串按 hex 解码失败——连字符出现在非标准位置（UUID 标准格式 `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` 之外的位置）。
- **涉及 indexid（节选）**：5228606564373190777、5789844028225852311、4341186906856532218、5323085616270219902、5466878790547981468；另有 L96-100 `U+0054 'T'`（id 中混入大写字母 T）。
- **根因链**：订阅源 → 解析器/ShareLink 导入时未校验 id 为合法 UUID（v4 字符串）→ 入库 → 测试时 `ConfigGenerator` 按原值写 outbound → Xray 拒绝。

### 4.2 类别 A2 — 双重 URL 编码的明文 ID

- **日志证据**：
  - L133-137：`invalid UUID: %2550%2561%2572%2573%2561%2568%256F%256E%2561%256D-299`，解码一层为 `%50%61%72%73%61%68%6F%6E%61%6D-299`，再解码为 **"Parashahonam-299"**（indexid 4597486637397081502）——id 字段存的是带后缀的节点名。
  - L138-142：`invalid UUID: Telegram-%2540Cooonfig%2540Cooonfig`，解码为 **"Telegram-@Cooonfig@Cooonfig"**（indexid 5096278635437728679）——id 字段存的是节点备注名。
- **根因链**：订阅/分享链接构造时把「显示名/备注」错写入 `id` 参数，且经两次 URL 编码；导入解析只做了 URL 解码、未做 UUID 合法性校验。

### 4.3 类别 B — shadowsocks 不支持的加密方式（最高频）

- **日志证据**：L41-85、L91-95、L128-132、L185-214，`proxy/shadowsocks: Unsupported cipher.`。
- **涉及 indexid（节选）**：4837838884295568130、4220843325944206707、4794367551165210313、5286076816635112945、5850411779012542379、4244112612721688821、5599650781125378682、4050369681440982551、4626508714202341955、5774792591569772022、4318484047574299419、5972530068664389658、4982186825595494456、4457631689554773931、5332028056251325046、5864443574527019790、5135494773324695271。
- **根因链**：Xray v26 起 SS 仅支持 AEAD 系列（aes-128-gcm / aes-256-gcm / chacha20-poly1305 / xchacha20-poly1305）与 2022-blake3 家族；DB 中来自旧订阅/旧版本的 `aes-256-cfb`、`rc4-md5` 等**流式加密方法**全部被拒。属于**存量数据兼容**问题，非注入编码缺陷。

### 4.4 类别 C1 — REALITY + gRPC 握手 panic（Xray-core bug）

- **日志证据**：11:49:15，`socks=10000/api=10080`，`exitCode=2`；stderr 尾部为 `panic: runtime error: index out of range [8] with length 0`，堆栈 `reality.(*UClient)` → `transport/internet/reality/reality.go:273` ← `transport/internet/grpc` `getGrpcClient.func1` `dial.go:150` ← `grpc` `http2_client`。
- **重启**：11:49:21 实例被 `evaluateInstanceHealth` 拉起（修复前 build：Worker-0 日志 `retrying proxy 5303960922167817424 once`）。
- **根因链**：REALITY 握手响应为空（长度 0），`reality.go:273` 对响应切片做 `[8:]` 越界 → 进程 panic 退出 → 后续 gRPC 调用全部失败。**根因在 Xray-core 上游**，validproxy 无法在注入层修复；规避手段为跳过 REALITY+gRPC 组合节点或等待上游补丁。

### 4.5 类别 C2 — splithttp nil-request panic（Xray-core bug，应用侧已防御）

- **日志证据**：12:04:08，`socks=10001/api=10081`，`exitCode=2`；stderr 尾部为 `panic: runtime error: invalid memory address or nil pointer dereference`，堆栈 `splithttp.(*Config).FillStreamRequest` `transport/internet/splithttp/config.go:296` ← `DefaultDialerClient.OpenStream` `client.go:63` ← `splithttp.Dial` `dialer.go:481`。
- **重启**：12:04:14 实例被拉起；Worker-1 `retrying proxy 4545187712047278563 once`。
- **根因链**：splithttp `OpenStream` 使用 `req, _ := http.NewRequestWithContext(...)` 丢弃错误——host/path 含控制/空白字符（≤0x20 或 0x7F）时 `url.Parse` 失败返回 `(nil, err)`，`FillStreamRequest` 对 nil request 解引用 panic（Xray-core commit d2758a0 验证）。
- **状态**：本次日志为**修复前 build**；已合入的应用侧防御 `validateSplitHTTPSettings`（network∈{splithttp,xhttp} 时校验 host/path 禁控制字符，非法代理 ERR 拒绝注入）可拦截该场景。

### 4.6 类别 D — 子进程回退在实例重启窗口内失败

- **日志证据**：L101-102，11:49:13，`[XrayApi] addOutbound FAILED: exitCode=1, output=`（**output 为空**）。
- **根因链**：gRPC 注入失败（实例正因 C1 panic 重启）→ 回退 `xray api ado` 子进程 → 恰逢实例重建/端口占用窗口，子进程以 exitCode=1 立即失败且无输出 → 无法区分「配置错误」与「实例不可用」。
- **后续**：该窗口极小（1 条），重启完成后同代理经在途重试块成功注入，未造成吞吐影响。

### 4.7 类别 A1 的地址侧补充 — 私网/回环地址代理（以 indexid 5323085616270219902 为例）

A1 类别中相当比例代理不仅 id 非法，**Address 同时为私网/回环地址**。以 `bin/log/ui_20260810_113211.log` L90 报错的 indexid=5323085616270219902 为例，完整溯源如下：

- **日志证据**：L90 `[Worker-1] XRAY_ERROR - 5323085616270219902 (tag=proxy) - grpc call failed: status=2, message=proxy/vmess/outbound: failed to get server spec > proxy/vmess: failed to parse ID > encoding/hex: invalid byte: U+002D '-'`。
- **DB 记录**：`ConfigType=1 (vmess)`，`Address=127.0.0.1`，`Port=1080`，`Id=baacbac-acab-acba-dcba-bbaccacbcaab`（首段仅 7 位 hex，非标准 UUID），`Remarks=@Hope_Net-join-us-on-Telegram`，`Subid=5184538877103642962`。
- **来源订阅**：`SubItem.Id=5184538877103642962` = **Argh94-vmess**（`https://raw.githubusercontent.com/Argh94/V2RayAutoConfig/refs/heads/main/configs/Vmess.txt`，Enabled=1）。
- **上游文件直接命中（决定性证据）**：抓取该订阅当前内容，对 base64 特征串 `MTI3`（="127"）解码命中与 DB **完全一致**的条目：`"add":"127.0.0.1","port":1080,"id":"baacbac-acab-acba-dcba-bbaccacbcaab","ps":"@Hope_Net-join-us-on-Telegram"` —— 该私网代理**原样存在于上游订阅文件中**，并非本程序导入/解析时生成。另有 10 条 `127.0.0.53:80-89`（id 均为测试 UUID `88888888-8888-8888-8888-888888888888`）、`127.1.1.127:80`（ps=`导入于 08-10 10:41 by https://github.com/ts-sf/fly`，聚合链再导入产物）。
- **源码排除**：`src/*.cpp` 中全部 14 处 `127.0.0.1` 均为本地 Xray API/socks 绑定（ProxyTester.cpp:20、ProxyFinder.cpp:236/341、XrayManager.cpp:21、XrayInstance.cpp:297-298、ProxyBatchTester.cpp:109、UrlFetcher.cpp:34、Utils.cpp:165/207、SubscriptionUpdater.cpp:67、SubitemUpdaterV2.cpp:528、ProxyListPanel.cpp:422），解析器无任何把地址默认/改写为 127.0.0.1 的逻辑。
- **全库统计（精确算法：第二八位组数值判断 RFC1918）**：总数 292,581 条中，真私网/回环/保留地址 **42 条（0.014%）**，分布于 10 个订阅源（Argh94-vmess/vless/ShadowSocks/Trojan、黑名单、Epodonios-vless、ShatakVPN-all、SoliSpirit-all_configs、TGParse-mixed、barry-far-Vless）：127.0.0.1×13、127.0.0.53×10（systemd-resolved stub）、反转 IPv6 垃圾域名 `0.5.0.0.7.0.f.1.0.7.4.0.1.0.0.2.xzhi.eu.org`×4、`0.ir0.ir`×3、`0.0.0.einetwork.news`×2、0.0.0.0×2、127.1.1.127×1、127.0.0.0/2/3/4/5/6×1、0.0.17.96×1。注意：172.64/66/67（Cloudflare anycast）、172.232+（RackNerd 段）为**公网**地址，`LIKE '172.%'` 与字符串区间判断均会误报，必须按第二八位组数值（16–31）判断。
- **根因链**：上游自动聚合订阅仓库（Argh94/V2RayAutoConfig 等）原样收录 Telegram 频道与其他仓库的链接，其中包含 (a) 用户「分享本地客户端配置」生成的链接（本地监听地址 127.0.0.1:1080/8080 被当作服务器地址导出）、(b) 自动测试/占位条目（127.0.0.53 systemd-resolved 地址、`88888888-...` 测试 UUID、ts-sf/fly 再导入条目）。validproxy 导入端对 Address 无私网/回环/保留地址校验（仅 2026-08-05 起校验 Security/Id 可打印 ASCII），垃圾节点原样入库并参与批量测试。

## 5. 根因链汇总

```
订阅源 / 分享链接（外部数据）
   │ ① 解析/导入时未校验 id 为合法 UUID（A1/A2）
   │ ② SS 旧加密方法未迁移/过滤（B）
   │ ③ 地址未校验：私网/回环/保留地址原样入库（A1 地址侧，全库 42 条）
   ▼
代理数据库（bin/worker/guindb.db）← 数据污染源
   │
   ▼
ConfigGenerator 按原值生成 outbound JSON（忠实编码，无缺陷）
   │
   ▼
XrayApi::addOutboundDirect gRPC 注入 → Xray 解析失败 → addOutboundDirect FAILED（A/B）
   │
   └─ 少数节点注入成功后 Dial 阶段触发 Xray-core panic（C1 REALITY+gRPC 越界 / C2 splithttp nil request）
        → 实例崩溃 → 重启窗口内回退子进程偶发失败（D）
```

**结论**：`addOutboundDirect` 的编码实现无缺陷（历次 gRPC 字段/路径/类型修复均已生效），**失败根因是代理数据库中的配置数据污染（A+B 占绝大多数）与 Xray-core 运行时缺陷（C）**。

## 6. 修复建议

### P1 数据污染（最高优先级，覆盖 A1/A2/B 全部失败）

- **导入/更新阶段校验**：`SubitemUpdaterV2`/`SubscriptionParser`/`ShareLink` 解析时——(a) vless/vmess `id` 必须为合法 UUID v4（正则校验，非标准连字符/可见字符直接丢弃或标注）；(b) SS `method` 加入 AEAD+2022-blake3 白名单，旧流式加密方法丢弃或迁移；(c) **Address 必须为公网可达地址**——拒绝 127.0.0.0/8、10.0.0.0/8、172.16.0.0/12、192.168.0.0/16、169.254.0.0/16、0.0.0.0/8、::1、fc00::/7、fe80::/10，并拒绝 `0` 开头垃圾域名与反转 IPv6 域名（如 `0.5.0.0.7.0.f.1.0.7.4.0.1.0.0.2.xzhi.eu.org`）；RFC1918 判定必须按第二八位组数值（172.16–31），不可用 `LIKE '172.%'`（误伤 Cloudflare 172.64/66/67 等公网段）。
- **测试前预过滤**：`ProxyBatchTester::preGenerateConfigs` 对非法 id / 不支持 cipher / 私网地址 直接标记 skip（复用既有 `pregenFailedFlags_` 机制），避免进入注入链路。
- **存量清洗**：扩展 `Deduplicator::deduplicateConfigErrorPhase` 删除非法配置代理与私网地址代理（与 2026-08-04 GarbageSS 修复同思路，CLI `-D` / UI 去重触发）。

### P2 运行时稳定性（C 类）

- **C1（REALITY+gRPC）**：validproxy 侧可在测试调度层对 REALITY 传输 + gRPC 流组合节点降级为「已知不可用」跳过，或等待 Xray-core 上游修复 reality.go:273 越界；不建议注入层硬编码规避。
- **C2（splithttp）**：已由 `validateSplitHTTPSettings` 应用侧防御覆盖（本日志为修复前 build）；后续可跟踪上游 `OpenStream` 错误处理补丁。

### P3 回退时序（D 类，低优先）

- `xray api ado` 回退子进程失败且 `output` 为空时，增加一次「重启窗口」短等待重试，或将失败归因于实例不可用而非配置错误，减少噪音。

## 7. 结论

1. 批次③ 2,266 条失败中，绝大多数由 **id 字段数据污染（A1/A2）与 SS 旧加密方法（B）** 导致，Xray 按规范拒绝注入，属预期行为；注入编码链路无缺陷。
2. 少量失败由 **Xray-core 运行时 panic（C1/C2）** 引发，实例重启机制已能自愈，且 splithttp 场景已有应用侧防御。
3. 建议按 P1 优先落地导入校验 + 存量清洗 + 预生成过滤，可在不修改注入链路的前提下将失败率降至接近 0；C1 依赖上游修复或调度层规避。
4. 私网/回环地址代理专项调查（以 5323085616270219902 为例，详见 §4.7）：**私网地址 100% 来自上游订阅聚合源**——Argh94/V2RayAutoConfig 等仓库原样收录 Telegram 频道「分享本地配置」链接（本地监听 127.0.0.1:1080/8080 被误当服务器地址）与自动测试/占位条目（127.0.0.53 systemd-resolved 地址、`88888888-...` 测试 UUID、聚合链再导入条目）；上游文件 base64 解码后与 DB 记录逐字段一致，本程序解析器无地址默认/改写逻辑。全库共 42 条（0.014%）。导入端补齐地址合法性校验（P1-c）后此类节点将在源头被拦截。
