# 纳管独立代理进程的端口回退实施方案 v1.1（实例闸门 + 逐层 OS 校验 + 丢弃语义收口）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 纳管游离独立代理进程时，按「实例闸门 → 逐层 OS 校验的端口来源链」取得**已核实**的 SOCKS 端口；彻底消灭「以陈旧端口纳管导致周期探活永久失败」与「误杀健康代理」两类缺陷。

**Architecture:** 端口来源链 = 运行时历史表 → 命令行配置 → **OS 活体** → 目录探测（用户裁决「历史表优先」+ v1.3「活体探测需加上 D2 校验」）；**OS 活体事实前置取证一次**（`osPorts` / `socksPorts`），既用于「OS 层候选产出」，又用于**对链上选中候选做 D2 闸门判定**（纯内存比较、零额外 I/O）；判定结论分四态（Verified / Unverified / Unknown / Disproven），**被证伪的候选顺延到链上的下一个来源**（而非直接丢弃）；新增纯函数头文件承载判定，便于单测。

**Tech Stack:** C++17 / SQLite（幂等 ALTER）/ GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档：** `docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.0.md`（被本文取代）；两份独立评审结论见 §0.2

**约束：** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`；`ctest` 必须在 `build/` 目录执行。

---

## 0. 变更记录

| 版本 | 内容 |
| :--- | :--- |
| v1.0 | 初版：实例闸门 + 逐层 OS 校验 + 监控路径同链 |
| **v1.1** | 依据**对抗性评审**（7 阻断 / 12 重要 / 10 一般）与**可行性评审**（5 阻断 / 13 重要 / 10 一般）修订：D2 判据改为**判定树**并引入三态结论（修 B1）；OS 层候选改由 `buildOsCandidates` 产出、不再依赖 `selectOsListeningPort` 的「无握手即 0」语义（修 B2/feas-B2）；**监控路径禁止握手**、改为「历史(已核实) → 内存(已核实) → 磁盘(未核实兜底)」并保留内存兜底（修 B3/feas-B4/I4/I10）；R6 接管路径改用同一条链 + 回填内存端口 + 句柄补 `PROCESS_TERMINATE`（修 B4/I5）；实例闸门改为**两段式 + 空值降级不杀 + 必有日志**，`started_at` 兜底保留（修 B5/feas-B5/I5/I6）；丢弃语义收口为「仅证伪可丢弃、且不对活体进程 `finalizeStop`」（修 B6/feas-B3）；**订正 §1.1 现场证据**（两个配置并非都被删除、10810 来自目录探测层而非历史层、探活周期 30s 而非 5s、日志文案为跨版本拼接）（修 I2/feas-I1/I2/I3/G5/M1/M2）；补 api 端口排除入签名（I1/feas-I6）、消除 `osPort`/`osPorts` 双真相（I8/feas-I7）、闸门判定抽纯函数可单测（feas-I8）、`updateSocksPort` 回写标注为既有代码（I3/feas-I9）、行身份失配改「复用旧 in-progress 行」而非新建（I7/feas-B3）、G2 验收场景按真实现场重写（I9/feas-I2）、验收不污染生产配置（feas-G3） |
| **v1.1.1** | **用户裁决：目录探测层只探测 `<exeDir>\config\` 一个目录**（删除 `<exeDir>\worker\config\`）。理由：① 写入侧唯一（`startStandaloneProxy` 写 `exeDir + "\\config\\"`，`AppController.cpp:927-935`）⇒ 探测面应收敛到「当前运行可执行文件的配置目录」；② `exeDir\worker\config` 属于**另一套安装**（`bin\worker\validproxy.exe` 工作副本）的配置目录，探测它等于跨安装取端口，正是 2026-09-02「两套 config 目录端口漂移」缺陷的来源类别；③ 覆盖损失由**命令行层**补齐 —— 被 `bin\worker\validproxy.exe` 启动的进程，其命令行自带 `-c "...\bin\worker\config\standalone_X.json"` 绝对路径，命令行层可精确解析，目录探测只是「命令行给不出路径」时的兜底。**残留**：`bin\config\standalone_4949226207915085546-xray.json`（陈旧 10810）就在 `<exeDir>\config\` 内 ⇒ 单目录化**不单独**消除 12776 的陈端口命中，该命中由 D2 的 OS 校验证伪并顺延到 OS 层。同步修改点：D1（新增段落）、D7（磁盘层探测同口径）、R4（原「目录探测顺序本身不改」作废）、Task 2 / Task 4（单目录） |
| **v1.3** | **用户裁决：活体探测（OS 活体层）需加上 D2 校验** —— v1.2 把 D2 判定收缩到「仅 R6」，本轮**部分回退**：D2 判定扩展到**纳管路径**，且**OS 活体事实的取证前置到端口链之前**（一次查询、层层复用）。**关键交互（实施时不得忽略）**：v1.2 链序为「历史 → 命令行 → OS → 目录」，若 D2 只施加于 **OS 层自己产出的候选**，则**历史/命令行一旦给出值，OS 层根本轮不到** ⇒ 裁决形同虚设，陈旧历史端口（如 10810）仍会被直接采纳；因此 v1.3 采「**取证先行 + 链式取用 + 单一 D2 闸门**」：**① 取证**（`osPorts` + `socksPorts`，每进程一次）→ **② 链式取用**（历史 → 命令行 → OS → 目录，取第一个非零候选）→ **③ D2 闸门**（用同一份 OS 事实判定选中的候选：`Verified` 采纳 / `Unverified` 放行+WARN / `Unknown` 放行 / `Disproven` ⇒ **顺延到链上的下一个候选**）。⇒ D2 在纳管路径**全面启用**，但**零额外 I/O**（OS 事实只取一次，判定是纯内存集合比较），与 v1.2「成本为零」的论证一致。**连带影响**：① D4(a) 由「链式取用第一个非零值」改为「链式取用 + D2 闸门顺延」；② v1.2 的 R11 残留（陈旧值无证伪手段）**被本裁决关闭** —— 陈旧候选（`c ∉ osPorts`）被判 `Disproven` 并顺延到 OS 层实锤端口；③ D2 判定树的「`osPorts` 为空 ⇒ `Unknown` ⇒ 放行」守卫**保持不变**（修对抗 B1），故表读不到时全链放行、绝不误杀；④ 「唯一候选放宽」（对抗 B2）在 OS 层**仍然必需**，但**必须先过 D2 闸门**（`c == excludedPort` 即 api 端口 ⇒ `Disproven`，修 I1/feas-I6） |
| **v1.4** | **用户裁决：纳管逻辑三段式 + 历史表为唯一信源 + 写历史表 = APPEND** —— ① **历史表优先，三元组判定**（`index_id + pid + started_at`）；② **命令行 + 目录探测**获取配置文件 → 得端口 → **写入历史表**；③ **活体探测 + D2 校验** → 得端口 → **写入历史表**。**追加裁决**：「写入历史表不是 update，应该时 append」+「查找历史表三元组不匹配，不是 update，应该时 append」⇒ 历史表是 **append-only 会话日志**：三元组不匹配时**追加新行**（携带端口），**绝不 UPDATE 既有行**；历史表即**纳管独立代理进程的唯一信源，缺乏活体记录则补齐**。**实施落地**：① `ProxyRuntimeHistoryDAO` 新增 `insertStartWithPort(indexId, startedAt, pid, socksPort, db)`（`insertStart` 委托之，`socks_port<=0` 绑 NULL）；② 策略头 `include/StandaloneAdoptionPolicy.h` 全文重写为四态判定（`Disproven/Unknown/Unverified/Verified`）+ `classifyPortCandidate` / `buildOsCandidates` / `orderOsCandidatesBySocks` / `resolveChainPort`（被证伪则**顺延**链上下一来源），旧 `resolvePort` / `selectOsListeningPort` 删除；③ 纳管路径重排为「取证前置（`listeningTcpPorts` + SOCKS5 握手）→ 链式装配（历史 → 命令行 → 目录(单目录) → OS 候选）→ `resolveChainPort` D2 闸门 → 行身份：三元组命中复用旧行，**未命中则 append 新行并携带端口**」；④ 丢弃分支**不再对活体进程 `finalizeStop`**（关闭活体行会导致下一轮重复 append + `start_count` 再累加）；⑤ 监控路径端口优先序改为 **历史(已核实) → 内存(已核实) → 磁盘(未核实) → 0**（磁盘优先是 12776 陈端口 10810 复发根源）；⑥ R6 接管路径改用同一条链 + D2 闸门（旧进程内存端口须被新进程活体事实证伪）+ 回填 `sp.socksPort`/`startedAt`/`configPath` + 句柄掩码补 `PROCESS_TERMINATE`；⑦ 纳管日志追加 `verdict=`。**验证**：构建 0 error；`RuntimeHistoryDAOTest` 45/45、`StandaloneAdoptionPolicyTest` 26/26、`ProcessInspectorTest` 11/11、`UtilsTest` 全绿；全量 ctest 54/56（唯一既有基线失败 `XrayManagerStartupTest`；`UI_FLOATINGWIDGET` 为已知并行 flaky，单跑复验通过） |
| **v1.5** | **补做 D5 实例闸门**（v1.4 残留项）—— 纳管在**端口解析之前**插入实例身份闸门：以 `PROCESS_QUERY_LIMITED_INFORMATION` 只读句柄读出**权威创建时间**（`processCreationTime(HANDLE)`），与枚举阶段得到的 `procs[i].creationTime` 比对。判定抽为**纯函数** `checkInstanceIdentity(enumeratedCreatedAt, handleCreatedAt)` ⇒ `Match` / `Recycled`（pid 被复用，活进程已是**另一个实例** ⇒ 跳过纳管）/ `Unverifiable`（任一时间为空 ⇒ **降级放行**，绝不误杀健康进程）。**关键设计点**（源自对抗 B5 / feas-B5）：① 闸门用 `PROCESS_QUERY_LIMITED_INFORMATION`，**永不强于枚举本身**（枚举读命令行已需 `PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`）⇒ 闸门不会新增一类「枚举得到却打不开」的静默丢弃；纳管句柄（含 `PROCESS_TERMINATE`）仍在后面单独打开；② **空创建时间不是失配证据** ⇒ `Unverifiable` 一律放行（这是 B5「误杀健康进程」的反面修正）；③ `started_at` 取值改为**三级兜底**（`instanceStartedAt` → `procs[i].creationTime` → `utils::getCurrentTimestampFormatted()`），兜底的理由是「不写空串」而**不是**「让三元组匹配」（写入方与纳管方的 "now" 本就是不同瞬间，必然失配）；④ 闸门跳过**必落可检索日志**（含 `pid` / `indexId` / `enumeratedCreatedAt` / `handleCreatedAt` / `instanceCheck`），并按 pid 去重（首次 WARN、后续 DEBUG）。**验证**：构建 0 error；`StandaloneAdoptionPolicyTest` 30/30、`RuntimeHistoryDAOTest` 45/45、`ProcessInspectorTest` 11/11、`UtilsTest` 全绿；全量 ctest 55/56（唯一既有基线失败 `XrayManagerStartupTest`） |
| **v1.6** | **用户裁决：本次启动的独立代理旁路检测，直接纳管 + 写历史表** —— ① 新增路由决策 `decideAdoptionRoute(alreadyManaged, startedByThisSession)`（`Skip` / `BypassDetect` / `FullDetect`）；② **本次启动**（本会话由本应用 `startStandaloneProxy` 启动、且启动时的创建时间仍与枚举所得一致）的代理走 `BypassDetect`：**完全跳过活体检测**（不查 TCP 表、不做 SOCKS5 握手），端口取 历史表 → 内存 → 会话启动登记；③ 已在管 + `BypassDetect` ⇒ 不重复 OpenProcess / watch / 写 map，**只确保历史表存在该会话行**（三元组命中 ⇒ `updateSocksPort` 纠正端口；未命中 ⇒ `insertStartWithPort` 追加新行带端口）；④ 新增 `SessionStartedProxy` 登记（pid → socksPort + 启动时创建时间）与三个清理点，避免 pid 复用被误认。 |
| **v1.2** | **用户裁决：D2 判定（候选校验）只在 R6 接管路径执行，其它路径一律不做** —— 纳管路径、监控路径、启动路径**不再把任何候选与 OS 事实比对**。**术语澄清（关键）**：「D2 判定」= 对**外部来源**候选（历史/配置/目录）做 `Verified/Unverified/Unknown/Disproven` 分类比对；「**OS 层候选产出**」= 从活体事实中**生产**候选（`listeningTcpPorts` + SOCKS5 握手筛选），属**取证**而非判定，在纳管路径**仍然必需**（否则无法在 `{SOCKS 入站, gRPC api 入站}` 中挑出 SOCKS 端口）。**连带影响**：① 纳管路径改为「链式取用第一个非零值」（新增 `resolveChainPort`），`resolveValidatedPort` 变为 R6 专用；② 纳管路径**不再有「证伪」概念** ⇒ 丢弃条件简化为「链全空」，D6 的「仅证伪可丢弃」在纳管侧自然消解（对抗 B6 / feas-B3 在纳管侧不再适用，但**必须保留**「不对活体进程 `finalizeStop`」）；③ **链序调整**：目录探测层从第 3 位降到**末位** ⇒ 新链 = **历史 → 命令行 → OS 活体 → 目录探测** —— 因无判定可证伪，唯一「不绑定进程实例的盲猜层」（目录探测）必须排在活体事实之后，否则 `bin\config\standalone_4949226207915085546-xray.json`（陈旧 10810，实听 10809）会被直接采纳；④ D2 判定树与 `classifyPortCandidate` **保留但仅供 R6**；⑤ 新增残留风险 R11：纳管不再证伪 ⇒ 「历史缺失 + 命令行缺失 + `<exeDir>\config\` 存在陈旧副本」组合会采纳陈端口（链序调整已把该组合的暴露面压到「OS 层也无解」的情形，而那时进程本就没在监听 ⇒ 探活必失败，与端口对错无关） |

### 0.1 两份评审的一致结论

方向（历史优先 + 逐层 OS 校验 + 实例闸门）**成立**，但 v1.0 **不可直接实施**：判据公式自相矛盾、OS 兜底层被自身门禁掐断、丢弃语义会销毁活体证据、监控链的 200ms 握手落在 UI 线程、闸门会误杀并写空 `started_at`。本文逐项修正。

### 0.2 评审的代码事实（可直接采信）

- `utils::respondsToSocks5Greeting`：`src/Utils.cpp:413-456`，默认 200ms，**仅连 `INADDR_LOOPBACK`**，`SO_RCVTIMEO/SO_SNDTIMEO`，判定 `received==2 && reply[0]==0x05 && reply[1]!=0xFF`。
- `ProcessInspector::processCreationTime(HANDLE)`：`src/ProcessInspector.cpp:397-406`，`GetProcessTimes` 失败**返回空串**；`processCreationTime(DWORD)` `:408-416` 同样可能返回空串。
- `enumerateByName` 内部 `OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ)`（`src/ProcessInspector.cpp:198-206`）⇒ 权限更高的 xray 会得到**空命令行**而被跳过（既有行为，`:1590-1592`）。
- `listeningTcpPorts`：`src/ProcessInspector.cpp:308-366`，查表失败与「无 LISTEN」**都返回空 vector**（API 层不可区分）。
- DAO：`insertStart` `:96-103`（`start_count+1` 在 `:117`）、`findInProgressHistory` 三元组 `:291-294`、`findInProgressHistoryLoose` `:311-332`（`ORDER BY id DESC LIMIT 1`）、`finalizeStop` `:152-260`、`updateSocksPort` `:337-345`（`socksPort<=0` 为 no-op；无事务）、`lookupSocksPort` `:379/383` 要求 `ended_at IS NULL`。
- 探活：`doTestOnlineProxies()` 起于 `src/ui/AppController.cpp:2369`；端口消费 `:2421`；`port<=0` 分支 `:2422-2440`；只写 ProfileExItem（`:2439`、`:2463`）。
- 悬浮窗：`StandaloneFloatingWidget.cpp:433-435`（wxTimer ⇒ **UI 线程**）→ `:932` → `AppController.cpp:1537`；周期 `FloatingWidgetPolicy::clampIntervalMs(checkIntervalMs)`（下限 5000，生产 **30000**）。
- 生产库 `bin\worker\guiNDB.db`：`proxy_runtime_history` id=316（pid 13492，`socks_port=NULL`，`ended_at=2026-10-21 09:04:03`）、id=317（pid 12776，`socks_port=10809`，`ended_at=NULL`）、id=318（pid 13492，`socks_port=10808`，`ended_at=NULL`）；`ended_at IS NULL` 的行共 **126** 条，其中重复 `(index_id,pid,started_at)` 三元组 **10 组**。

---

## 1. 背景与根因

### 1.1 现场证据（**已按磁盘与库实测订正**）

| 项 | 实测值 | 备注 |
| :--- | :--- | :--- |
| pid 13492 | indexId `5046338036629160457`，创建 `2026-10-21 08:39:08`，监听 `127.0.0.1:10808` | cmdline 指向 `bin\worker\config\standalone_5046338036629160457-xray.json` |
| pid 12776 | indexId `4949226207915085546`，创建 `2026-10-21 08:39:14`，监听 `127.0.0.1:10809` | cmdline 指向 `bin\worker\config\standalone_4949226207915085546-xray.json` |
| 配置文件 | `bin\worker\config\` 下**两个都不存在**；但 `bin\config\standalone_4949226207915085546-xray.json` **仍存在**且 `inbounds[0].port=10810` | 13492 的配置在 `bin\config\` 与 `bin\worker\config\` 均不存在；12776 的**陈旧副本在 `bin\config\`** |
| 历史行 | id=317：pid 12776，`socks_port=10809`（**正确**，从未是 10810） | id=318：pid 13492，`socks_port=10808`（上一阶段 OS 回落层已写入） |
| 目录探测顺序 | `{exeDir\config, exeDir\worker\config}`，**先命中 `bin\config`** | ⇒ 2026-09-02 记录的「两套 config 目录端口漂移」缺陷原样保留 |
| 日志 | `bin\log\ui_20261021_090403.log` 含旧版格式的丢弃告警（`[StandaloneProxy] n process pid=13492 indexId=…: config missing and no runtime-history port.`） | **跨版本拼接**（旧二进制格式），现行格式串见 `AppController.cpp:1736-1742` |
| 日志级别 | `bin\config.json` `log.file_level="WARN"` | ⇒ REPORT 级「Adopted dangling process」**不落盘**，不能据日志判断是否纳管，须查库 |

### 1.2 根因

1. **端口来源层缺失**：上一阶段（v1.3）落地的是「命令行配置 → 目录探测 → 运行时历史」三层，**OS 活体层被延后** ⇒ 用户原始诉求「回落到直接读取 xray 的 pid、监听端口等数据」未落地。
2. **目录探测优先命中陈旧副本**：`bin\config` 在前 ⇒ 12776 拿到 `10810`（实际 10809）⇒ 周期探活每 30s 打错端口永久失败。**这是 2026-09-02 缺陷的原样复发**，不是新问题。
3. **丢弃语义销毁活体证据**：丢弃分支 `finalizeStop` 关闭该 pid 的历史行 ⇒ `lookupSocksPort` 要求 `ended_at IS NULL` ⇒ 该 pid 此后连历史端口都取不到；且下一轮三元组失配 ⇒ `insertStart` **新建重复行** + `start_count` **重复累加**。
4. **闸门与兜底缺失（本次引入的防御项）**：现状无「pid 是否已回收」的判定；`started_at` 的秒级兜底值与写入方**必然失配**（两个 "now" 是不同瞬间）。

### 1.3 关键澄清

- 周期探活**不修改** `proxy_runtime_history`：探活只写 `ProfileExItem`（`AppController.cpp:2439`/`:2463`）；历史表的写入点仅 4 个 —— `insertStart` / `updateSocksPort` / `finalizeStop` / `touchHeartbeat`。
- 探活周期 = `proxy_process_monitor.check_interval_ms`，生产实测 **30000ms**（`MainFrame.cpp:1010`、`bin\config.json`），**不是 5s**。
- `utils::isPortListening(port)` **不区分端口归属**，**不可**用于本方案的校验；校验必须用 `listeningTcpPorts(pid)`。

### 1.4 目标

| 编号 | 目标 | 验收方式 |
| :--- | :--- | :--- |
| G1 | 纳管取得的端口**必须经 OS 活体事实校验**（`osPorts` / `socksPorts`），陈旧端口不得被采纳 | 单测（`resolveValidatedPort`）+ 实机（12776 端口 10809 而非 10810） |
| G2 | 校验结论分三态；**只有「证伪」（`c ∉ osPorts`）才允许丢弃**，「不可证实」必须放行 + WARN | 单测（`classifyPortCandidate` / `resolveValidatedPort`） |
| G3 | 「OS 表读不到 / 握手不可判定」**不得**否掉任何候选 | 单测（`osPorts` 为空 ⇒ 全候选 `Unknown` ⇒ 放行第一个） |
| G4 | 实例闸门：pid 已被回收（创建时间不一致）⇒ 不纳管；**且必有日志**；**创建时间不可读 ⇒ 降级不杀** | 单测（`checkInstanceIdentity` 纯函数）+ 实机 |
| G5 | 监控路径**不做握手探测**（UI 线程零阻塞），且**保留内存端口兜底** | 单测（`resolveMonitorPort`）+ 代码走查（监控路径无 socket 调用） |
| G6 | 丢弃**不对活体进程** `finalizeStop`，不新建重复历史行，不重复累加 `start_count` | 代码走查 + 实机查库（同一 pid 不出现两条 in-progress 行） |
| G7 | R6 接管路径与纳管路径**共用同一条链**，并回填内存端口；接管句柄可终止 | 代码走查 + 实机 |
| G8 | 纳管成功路径把**已核实**端口写回历史行 | 既有代码（`AppController.cpp:1824-1827`）复核 + 实机查库 |

### 1.5 非目标

| 编号 | 非目标 | 归属 |
| :--- | :--- | :--- |
| N1 | 区分「OS 表读失败」与「该 pid 确实无 LISTEN」（`listeningTcpPorts` 需返回读取状态） | 后续独立缺陷；本次按「两者都不可证伪 ⇒ 放行」处理 |
| N2 | 清理生产库中 **126 条** `ended_at IS NULL` 的存量孤儿行 / 修复 `start_count` 恒 0（`ProfileExItem` 行不存在时 UPDATE 命中 0 行） | 后续独立缺陷（**本规格不引入**） |
| N3 | 纳管路径的重入保护（三处 detached 线程可并发进入，`:452-454` / `:978-980` / `AppController.cpp:208-210`） | 既有缺陷；本规格**不扩大**该窗口的语义（仅前移句柄、增加一次只读查询） |
| N4 | 「端口被外部改绑」后的历史行语义（同一行既记旧实例启动时间又记新端口） | 残余风险，见 R9 |
| N5 | IPv6-only 监听端口的 SOCKS 探测（`respondsToSocks5Greeting` 仅连 IPv4 回环） | 已知限制 |
| N6 | 监控路径的周期性 OS 复核（如需，须整体搬到后台线程 + 缓存） | 后续可选增强，见 D7 |

---

## 2. 设计决策

### D1：端口来源链（用户裁决「历史表优先」+ v1.3「活体探测需加上 D2 校验」）

**v1.3 链序（纳管路径）**：**运行时历史表 → 命令行配置 → OS 活体 → 目录探测**（链序本身沿用 v1.2 不变）。

**v1.3 关键交互（必须知悉，实施时不得忽略）**：v1.2 取消纳管路径判定 ⇒ OS 层排在链上第 3 位时，**历史/命令行只要给出值，OS 层根本轮不到** ⇒ 「活体探测需加上 D2 校验」这条裁决会**形同虚设**（陈旧历史端口照样被直接采纳）。故 v1.3 把**OS 活体事实的取证前置到端口链之前**（每进程**一次**查询、层层复用），并对**链上选中的候选**统一施加一道 D2 闸门（见 D4(a)）。这既让裁决真正生效（陈旧值被证伪后顺延），又**不增加任何 I/O**（判定是纯内存集合比较）。

与 v1.1 的唯一差异：**目录探测层从第 3 位降到末位**。理由：v1.2 取消纳管路径的 D2 判定 ⇒ **没有任何机制能证伪**一个陈旧候选；而目录探测是四层中**唯一不绑定任何进程实例**的盲猜层（只拿到文件名，盲猜 `<exeDir>\config\`），它命中的可能是**同 indexId 的另一套安装 / 另一次启动**留下的陈旧副本（2026-09-02 端口漂移缺陷类别）。既无证伪能力，就必须排在「活体事实」之后 —— 否则 `bin\config\standalone_4949226207915085546-xray.json`（陈旧 10810，实听 10809）会被直接采纳。

**链序的作用（v1.3）**：链序决定**首选来源**（取第一个非零候选），但**不再等同于最终采纳** —— v1.3 起，选中的候选还要过一道 **D2 闸门**（见 D2/D4(a)）：被证伪（`Disproven`）则**顺延到链上的下一个候选**。历史表排在最前**不代表历史值一定被采纳** —— 12776 的历史值 10809 正确且通过校验；反之若历史值是陈旧的 10810（`10810 ∉ osPorts{10809}`），将被证伪并顺延到 OS 活体层的实锤端口。

`PortSource` 枚举顺序相应调整为 `None → RuntimeHistory → CmdlineConfig → OsTcpTable → DirProbe`（`include/StandaloneAdoptionPolicy.h:37-43` 的 `OsTcpTable` **本就存在**，本次仅调整顺序与标签，不新增枚举值）。

**目录探测层只探测 `<exeDir>\config\` 一个目录**（用户裁决，2026-10-21，见变更记录 v1.1.1）：

- 现实现探测 `{exeDir\config, exeDir\worker\config}` 两个目录（纳管 `AppController.cpp:1698-1700`、监控 `:1476-1479`），本次**收敛为单一目录** `<exeDir>\config\`。
- 依据一：**写入侧唯一** —— 本应用写 standalone 配置只写 `exeDir + "\\config\\"`（`AppController.cpp:927-935`），故「当前运行可执行文件的配置目录」就是唯一合法落点。
- 依据二：**消除跨安装取端口** —— `exeDir\worker\config` 是 `bin\worker\validproxy.exe` 工作副本（**另一套安装**）的配置目录，探测它会把同 indexId 的另一套端口取回来，正是 2026-09-02「两套 config 目录端口漂移」缺陷的来源类别。
- 依据三：**覆盖损失由命令行层补齐** —— 被 `bin\worker\validproxy.exe` 启动的进程，命令行自带 `-c "...\bin\worker\config\standalone_X.json"` 绝对路径，命令行层可精确解析；目录探测本就是「命令行给不出路径（裸文件名 / 解析失败）」时的兜底，此时只能猜「当前 exe 的配置目录」。
- **残留（必须知悉；v1.2 已改处置，v1.3 再收口）**：`bin\config\standalone_4949226207915085546-xray.json`（陈旧 10810）**就在 `<exeDir>\config\` 内** ⇒ 单目录化**不单独**消除陈端口命中。**v1.1 的处置**是「由 D2 的 OS 校验证伪（10810 ∉ osPorts{10809} ⇒ Disproven）后顺延到 OS 层」；**v1.2 取消纳管路径的判定** ⇒ 该命中改由**链序**拦截（目录层降到末位，OS 层排在它之前）；**v1.3 恢复 D2 闸门** ⇒ 该命中被**双重拦截**：既排在末位（链序），又被证伪（`Disproven` ⇒ 顺延）。

### D2：校验判据（**判定树，非合取式**）— 修 B1 / B2 / I1

> **v1.3 适用范围（用户裁决）：活体探测需加上 D2 校验。**
>
> D2 判定在**纳管路径**与 **R6 接管路径**执行；**监控路径**与**启动路径**仍不做候选校验。
>
> **v1.3 的执行形态（关键，勿与 v1.2 混淆）**：D2 判定的**事实基础是一次取证的 OS 活体事实**（`osPorts` + `socksPorts`），判定本身是**纯内存集合比较、零额外 I/O**。纳管路径的执行顺序为：
>
> ```
> 取证（osPorts + socksPorts，每进程一次）
>   → 链式取用（历史 → 命令行 → OS → 目录，取第一个非零候选）
>   → D2 闸门（用同一份 OS 事实判定该候选）
>        Verified   → 采纳
>        Unverified → 采纳（放行 + WARN）
>        Unknown    → 采纳（放行；表读不到时永不否决）
>        Disproven  → **顺延到链上的下一个候选**（被证伪 ⇒ 换来源重试）
> ```
>
> **理由**：① 用户裁决「活体探测需加上 D2 校验」；② 若只对 **OS 层自己产出的候选**判定，则历史/命令行一旦给出值，OS 层轮不到，裁决失效（陈旧历史端口照样被采纳）⇒ 必须把判定施加于**链上选中的候选**；③ 判定是零 I/O 的纯函数 ⇒ 「全链判定」与「仅 OS 层判定」的**探测成本完全相同**（OS 事实只取一次）；④ 保留「`osPorts` 为空 ⇒ `Unknown` ⇒ 放行」的守卫 ⇒ 表读不到时全链放行、**绝不误杀**（修对抗 B1）。
>
> **术语澄清（实施时不得混淆）**：
> - **D2 判定** = 把候选与 OS 活体事实比对，分类为 `Verified`/`Unverified`/`Unknown`/`Disproven`。
> - **OS 层候选产出** = 从活体事实中**生产**候选：`listeningTcpPorts` + SOCKS5 握手筛选（见 D3）。这属于**取证**而非判定，在纳管路径**仍然必需** —— 否则 OS 层无法在 `{SOCKS 入站, gRPC api 入站}` 中挑出 SOCKS 端口。
>
> ⇒ 纳管路径 = **取证（前置）+ 链式取用 + 单一 D2 闸门**；R6 路径 = **取证 + 完整 D2 判定**（候选源混入旧进程陈旧内存值，必须逐个证伪）。

对单个候选 `c`：

```
classifyPortCandidate(c, osPorts, socksPorts, excludedPort):
    if c < 1 or c > 65535            → Disproven   // 非法端口
    if excludedPort > 0 and c == excludedPort
                                      → Disproven   // api 入站端口不是 SOCKS
    if osPorts.empty()               → Unknown     // 无法校验：表读不到 或 该 pid 无 LISTEN
    if c ∉ osPorts                   → Disproven   // 证伪：进程并未在该端口监听
    if socksPorts.empty()            → Unverified  // 握手不可判定
    if c ∈ socksPorts                → Verified    // 实锤：应答了 SOCKS5 问候
    else                             → Unverified  // 在听但握手没应答
```

**关键修正**：v1.0 写成合取式 `∧ (c ∈ osPorts)`，使「`osPorts` 为空 ⇒ 放行」永远失效（表读不到即全丢，等于复现本次要修的缺陷）。判定树用**守卫式**表达，`osPorts` 为空时直接返回 `Unknown`，永不否决。

`excludedPort` = `config_.proxy.api_base_port > 0 ? config_.proxy.api_base_port : 10085`（`AppController.cpp:1651-1653` 既有取值）。**必须进入函数签名**（修 I1 / feas-I6）：否则 xray 只启 api inbound 时，唯一候选放宽会把 `10085` 当成 SOCKS 端口写进历史行。

三态语义：

| 结论 | 含义 | 处置 |
| :--- | :--- | :--- |
| `Verified` | 实锤（应答 SOCKS5 问候） | 直接采纳 |
| `Unverified` | 在听但握手不可判定 | **放行**（附 WARN），不得据此丢弃（修 B6） |
| `Disproven` | 证伪（进程不在该端口监听 / 非法 / api 端口） | 该候选被否决 |
| `Unknown` | 无法校验（OS 表不可得） | 放行（修 B3） |

### D3：OS 事实一次查询、层层复用 — 修 B2

单次查询得到两个集合：

- `osPorts` = `listeningTcpPorts(pid)`：该 pid 的**全部** LISTEN 端口（v4+v6，已去重升序）。
- `socksPorts` = 其中**应答了 SOCKS5 问候**的子集（`utils::respondsToSocks5Greeting(port, 200)`）。

**OS 层候选由 `osPorts` 直接产出**（`buildOsCandidates(osPorts, excludedPort)` = 去掉 api 端口后的全部 LISTEN 端口），**不再依赖 `selectOsListeningPort`** —— 后者在「无任何端口应答握手」时返回 0（`StandaloneAdoptionPolicy.h:106-121` 注释明写），会让 OS 兜底层自己贡献 0 候选，导致「2 端口进程 + 握手超时 ⇒ 全链落空 ⇒ 丢弃健康代理」（feas-B2）。

删除 `selectOsListeningPort`，其 8 个既有用例改写为 `buildOsCandidates` + `resolveValidatedPort` 的对应用例（无覆盖损失）。

**v1.2：OS 层候选产出规则（纳管路径，「取证」而非「判定」）**

**v1.3 补充：取证必须前置到端口链之前** —— `osPorts` / `socksPorts` 在**装配候选链之前**就取好（每进程**一次**），随后既用于「OS 层候选产出」，又用于「D4(a) 的 D2 闸门」判定**链上任何来源**选中的候选。**禁止**在链上命中历史/命令行后才去查 OS 表 —— 那会让「活体探测需加上 D2 校验」的裁决落空（历史/命令行一旦命中，OS 层就再也轮不到），且造成「先取历史后查 OS」的双重路径分叉。

```
buildOsCandidates(osPorts, excludedPort) → candidates   // 去掉 api 端口后的全部 LISTEN 端口
produceOsPort(candidates, socksPorts) → int
    if socksPorts 非空            → 取其中最小者（实锤 SOCKS 入站）
    else if candidates.size() == 1 → 取该唯一候选（**唯一候选放宽**，修对抗 B2）
    else                          → 0（无法判定）
```

「唯一候选放宽」在 v1.2 下**从"可选"变为"必需"**：纳管路径已无 D2 判定兜底，若 OS 层在「握手全超时」时贡献 0 候选，链会顺延到**末位的目录层**（盲猜，可能命中陈旧副本）⇒ 必须允许「排除 api 端口后只剩一个 LISTEN 端口」时直接采用它。

### D4：解析结果的选择规则（**v1.2 拆成两条：纳管链式取用 / R6 完整判定**）

**(a) 纳管路径 —— 链式取用 + 单一 D2 闸门（被证伪则顺延下一个候选）**（v1.3）

```
resolveChainPort(candidates, osPorts, socksPorts, excludedPort):
    // candidates 已按 D1 层序排列：历史 → 命令行 → OS → 目录
    for c in candidates:
        if c <= 0: continue
        v = classifyPortCandidate(c, osPorts, socksPorts, excludedPort)
        if v == Disproven:  continue                     // 被证伪 ⇒ 顺延到链上的下一个候选
        if v == Unverified: return { port=c, source=<该层来源>, verdict=Unverified }   // 放行 + WARN
        if v == Unknown:    return { port=c, source=<该层来源>, verdict=Unknown }      // 放行
        return { port=c, source=<该层来源>, verdict=Verified }                          // 采纳
    return { port=0, source=None, verdict=NoCandidate }
```

要点：

- **`Disproven` 不立即丢弃，而是顺延** —— 被证伪的只是「这一个来源给出的值」，链上还有其它来源，最终兜底是 **OS 活体的实锤端口**。这与 v1.2「链全空即丢弃」的唯一区别就是这道闸门。
- **`Unknown`（OS 表读不到 / 该 pid 无 LISTEN）⇒ 放行**，永不否决（修对抗 B1：v1.0 的合取式会让 `osPorts` 为空时全链否决，等于复现本次要修的缺陷）。
- **`Unverified`（在听但握手不可判定）⇒ 放行 + WARN**，不得据此丢弃（修对抗 B6）。
- **`excludedPort`（api 端口）⇒ `Disproven`** —— xray 只启 api inbound 时，唯一候选 10085 被排除，不会被当作 SOCKS 端口写进历史行（修 I1 / feas-I6）。
- 判定是**纯内存集合比较，零额外 I/O**；OS 事实由调用方**一次取证**后传入（D3）。
- 落地效果：历史值若是陈旧的 10810（`10810 ∉ osPorts{10809}`）⇒ `Disproven` ⇒ 顺延 ⇒ 命令行层（0）⇒ OS 层（10809，握手实锤 ⇒ `Verified`）⇒ **最终采纳 10809**，纠正陈端口。

**(b) R6 接管路径 —— 完整 D2 判定（唯一使用 D2 的路径）**

```
resolveValidatedPort(candidates, osPorts, socksPorts, excludedPort):
    // candidates 已按层序排列
    for c in candidates:  if classify(c) == Verified   → return c
    for c in candidates:  if classify(c) == Unverified → return c   // 放行 + WARN
    for c in candidates:  if classify(c) == Unknown    → return c   // 放行
    return { port=0, source=None, verdict=Disproven }              // 全部证伪 ⇒ 端口置 0
```

「第一个 Verified 优先」保证：R6 场景下旧进程的陈旧内存值被证伪时，新进程的实锤端口胜出。

> **两条规则的差异（v1.3）**：纳管路径 = **链序（历史 → 命令行 → OS → 目录）+ 一道 D2 闸门**，被证伪的候选顺延到下一个来源；R6 路径 = **完整 D2 判定**（候选源里混入了**旧进程的陈旧内存值**，必须逐个证伪，且「第一个 Verified 优先」保证新进程的实锤端口胜出）。两者共用同一个 `classifyPortCandidate`，差别只在**是否顺延**。

### D5：实例闸门（**两段式 + 空值降级 + 必有日志**）— 修 B5 / feas-B5 / I5 / I6

1. **闸门句柄**：`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)`。
   - 选 `LIMITED` 而非 `QUERY_INFORMATION`：闸门只需 `GetProcessTimes`，而 `LIMITED`（Vista+）权限要求最宽 —— 用 `QUERY_INFORMATION|PROCESS_TERMINATE` 会让「能枚举到但打不开」的高权限 xray 被静默丢弃（feas-R4）。
2. `instanceStartedAt = ProcessInspector::processCreationTime(gateHandle)`。
3. `checkInstanceIdentity(enumeratedCreatedAt, handleCreatedAt)`（**纯函数，可单测** — 修 feas-I8「闸门无测试入口」）：
   - 两者非空且相等 → `Match`（同一实例）
   - 两者非空且不等 → `Recycled`（pid 已回收）⇒ `CloseHandle` + **记日志** + 跳过
   - 任一为空 → `Unverifiable`（**不判 Recycled**）⇒ **降级放行**（继续纳管）+ WARN
4. 纳管仍使用既有完整掩码句柄 `OpenProcess(PROCESS_QUERY_INFORMATION|SYNCHRONIZE|PROCESS_TERMINATE)`（`AppController.cpp:1750-1752` 不变），因纳管需要终止能力。

**`started_at` 兜底保留**（修 B5）：`startedAt = instanceStartedAt 非空 ? instanceStartedAt : (procs[i].creationTime 非空 ? procs[i].creationTime : utils::getCurrentTimestampFormatted())`。
兜底的**理由是「不写空串」**（空 `started_at` ⇒ `durationMsBetween` 恒 0 ⇒ 时长统计失真），**不是**「让三元组能匹配」—— 写入方与纳管方的 "now" 本就是不同瞬间，必然失配。失配由 D2 的 OS 校验吸收（端口可救回）。

**行身份失配的处置**（修 I7 / feas-B3）：`findInProgressHistory`（三元组）失配时，**先**用 `findInProgressHistoryLoose(indexId, pid)` 找同 `(indexId,pid)` 的 in-progress 行：
- 命中 ⇒ **复用该行**（不再 `insertStart`），并把其 `started_at` 更新为权威 `instanceStartedAt`（新增 DAO 方法 `updateStartedAt`）；
- 未命中 ⇒ 才有 `insertStart`。

这样避免「同一次物理启动重复计数 `start_count`」与「旧行永久残留成幻影运行中」。

### D6：丢弃语义收口 — 修 B6 / feas-B3

**v1.2（纳管路径）**：丢弃条件**简化为「链全空」**（`resolution.port == 0` 且 `verdict == NoCandidate`）—— 因为纳管路径不再做 D2 判定，**不存在「证伪」概念**，不会因某个候选被否而丢弃。唯一的丢弃原因 = 四层都没给出任何端口。

**v1.1 原文（仅适用于 R6 判定路径）**：「只有「全部候选均为 Disproven」才允许丢弃」（即 `osPorts` 非空且每个候选都不在 `osPorts` 内 —— 有活体事实且与候选矛盾）。R6 路径下全部证伪时**不丢弃进程**（新进程一定存在），而是**把端口置 0**（不得保留已被证伪的旧进程端口，否则探活永远打错端口），该行 `socks_port` 留 NULL。

丢弃时**必须**：
- `CloseHandle` 已打开的句柄（feas-G1）；
- **不写新历史行、不建 watcher**；
- **不对仍在运行的进程 `finalizeStop`** —— 保留行（活体证据），只记日志 + 记入 `discardedStandalonePids_`（首次 WARN、后续 DEBUG）；
- 日志打印 `verdict`（纳管侧固定为 `NoCandidate`）与 `source` 标签。

> **对抗 B6 / feas-B3 在纳管侧自然消解**：二者都源于「把『不可证实』当『被证伪』⇒ 对活体进程 `finalizeStop` ⇒ 行被关闭 ⇒ `lookupSocksPort` 永久取不到 ⇒ 重复 `insertStart` + `start_count` 重复累加」。v1.2 取消纳管侧判定后，纳管侧**根本不会走到 `finalizeStop`**（丢弃 = 链全空，此时也没有可确认的端口需要收尾），该级联在纳管侧不再成立。**但「不对活体进程 `finalizeStop`」这条纪律必须保留**（R6 侧与未来任何收尾逻辑仍适用），且「行身份失配时复用旧 in-progress 行」（D5 第 4 点）仍是防止 `start_count` 重复累加的唯一手段。

`discardedStandalonePids_` 只增不清 ⇒ pid 复用后日志会永久降级为 DEBUG（feas-G4）⇒ 每次纳管周期需清理「已不存在的 pid」条目（按 `enumerateByName` 结果反查）。

### D7：监控路径（**禁止握手、保留内存兜底**）— 修 B3 / feas-B4 / I4 / I10

`getWatchedStandaloneMonitors()`（`AppController.cpp:1412-1525`）在 **UI 线程**（`StandaloneFloatingWidget.cpp:433-435` wxTimer）与探活线程（`:2404`）**并发**被调用。v1.0 要求监控链也做 OS 校验（需要 200ms 握手）⇒ 每周期 UI 冻结 0.4~2.5s（配 5000ms 时每 5s 卡一次）。

**新链**（`resolveMonitorPort`，纯函数）：

```
resolveMonitorPort(historyPort, memoryPort, diskPort):
    1 ≤ historyPort ≤ 65535 → historyPort   // 纳管时已 OS 核实并回写
    1 ≤ memoryPort  ≤ 65535 → memoryPort    // 同一份已核实值（watched[j].socksPort）
    1 ≤ diskPort   ≤ 65535 → diskPort      // 未核实的兜底（可能陈旧）
    否则 → 0
```

- **监控路径不做任何 socket 调用**（无 `listeningTcpPorts`、无握手）⇒ UI 线程零阻塞、零表扫描。
- **保留内存兜底**（`watched[j].socksPort`）—— v1.0 的「历史 → 磁盘 → OS」丢掉了它，是回归（I4）。
- **磁盘层的目录探测同口径收敛为单一目录** `<exeDir>\config\`（v1.1.1 用户裁决）：现 `:1476-1479` 的 `dirs[2] = {exeDir\config\, exeDir\worker\config\}` 改为单目录；且该磁盘值是**未核实兜底**，排在已核实的历史值与内存值之后（`:1485` 的「磁盘优先」是 12776 陈端口复发的直接来源）。
- 「磁盘优先」的现状（`:1485` `row.socksPort = (diskPort > 0) ? diskPort : watched[j].socksPort;`）是 12776 陈旧端口复发的直接来源 ⇒ 改为**历史优先**。
- `byId` 查表（`:1451-1454` / `:1494-1504`）需**上移**到端口块（`:1468-1486`）之前，才能取到 `row.pid`（`:1497`）与 `row.socksPort`。

### D8：R6 接管路径与纳管路径同链 — 修 B4 / I5 / I12 / feas-I5

R6 块（`AppController.cpp:1229-1300`）现状缺陷：端口只有「配置文件」与**旧进程内存值**两个来源（无 OS 层）；`updateSocksPort` 在 `takeoverPort<=0` 时是 DAO no-op（`ProxyRuntimeHistory.cpp:337-339`）⇒ 新行 `socks_port` 留 NULL；接管后只回填 3 个字段（`:1289-1295`）⇒ **`sp.socksPort` 仍是旧进程端口** ⇒ 探活继续打旧端口；句柄掩码缺 `PROCESS_TERMINATE` ⇒ 接管后杀不掉。

修订：
1. 端口解析改用**与纳管相同的链**（历史 → 命令行 → 目录 → OS，逐层 D2 校验），且**优先**用新进程的 `extractConfigFullPath(procs[i].commandLine)`。
2. 允许**有界重试**（最多 5 次 × 300ms）：新进程可能尚未 bind ⇒ `osPorts` 为空 ⇒ 全部 `Unknown` ⇒ 放行首个候选（可能是旧端口）。重试给新进程 bind 的时间窗。该路径运行在 watcher 线程（本就 30s 心跳间隔），不阻塞 UI。
3. 接管成功后**回填** `sp.socksPort` / `sp.startedAt` / `sp.configPath`（`:1289-1295` 补 3 行）。
4. 接管句柄掩码补 `PROCESS_TERMINATE`。

### D9：纯策略抽取 — 修 I8 / feas-I7

`include/StandaloneAdoptionPolicy.h`（header-only，先例 `src/ui/UnifiedMonitorRows.h`）新增/重写：

```cpp
enum class PortVerdict { Verified, Unverified, Disproven, Unknown };

struct PortCandidate { int port = 0; PortSource source = PortSource::None; };

struct PortResolution {
    int port = 0;
    PortSource source = PortSource::None;
    PortVerdict verdict = PortVerdict::Unknown;
};

inline PortVerdict classifyPortCandidate(int candidate,
                                         const std::vector<int>& osPorts,
                                         const std::vector<int>& socksPorts,
                                         int excludedPort);

inline std::vector<PortCandidate> buildOsCandidates(const std::vector<int>& osPorts,
                                                    int excludedPort);

inline PortResolution resolveValidatedPort(const std::vector<PortCandidate>& candidates,
                                            const std::vector<int>& osPorts,
                                            const std::vector<int>& socksPorts,
                                            int excludedPort);

enum class InstanceCheck { Match, Recycled, Unverifiable };
inline InstanceCheck checkInstanceIdentity(const std::string& enumeratedCreatedAt,
                                           const std::string& handleCreatedAt);

inline int resolveMonitorPort(int historyPort, int memoryPort, int diskPort);
```

- **消除双真相**（修 I8 / feas-I7）：**删除** `osPort` 单值入参，OS 层候选一律由 `buildOsCandidates(osPorts, excludedPort)` 从集合派生，不再有「`osPort` 与 `osPorts` 可能矛盾」的空间。
- **删除** `selectOsListeningPort`（职责被 `buildOsCandidates` + `resolveValidatedPort` 的 Verified 优先取代）。
- `resolvePort`（旧 4 参版）**删除**（其职责被 `resolveValidatedPort` 取代）；`PortSource` 顺序按 D1 调整；`portSourceLabel()` 保留。

### D10：写入端不变式（**附例外说明**）— 修 I11

| 写入路径 | 端口来源 | 是否已核实 |
| :--- | :--- | :--- |
| `startStandaloneProxy`（UI 启动） | `socksPort` | ✅ `waitForPort(socksPort, ..., pi.hProcess)`（`:1019`）已确证在监听。**例外**：「连通性验证失败但用户选择保留」分支（`:1103-1126`）下进程可能根本未在监听，`updateSocksPort` 仍写（`:1155`）⇒ 该分支为例外 |
| R6 接管 | D8 链 | ✅ 经 D2 校验（重试窗口内仍为 `Unknown` 时按 D2 放行并 WARN） |
| 纳管 | D1 链 + D2 校验 | ✅ Verified / Unverified（放行+WARN）皆视为可写入；**仅 Disproven 不写** |

---

## 3. 改动清单

### Task 0：纯策略头重写（`include/StandaloneAdoptionPolicy.h`）

- [ ] 按 D9 重写：新增 `PortVerdict` / `PortCandidate` / `PortResolution` / `InstanceCheck` 与 5 个纯函数。
- [ ] **v1.2 新增**：`PortVerdict` 增补 `NoCandidate`（链全空）；新增纯函数 `resolveChainPort(...)`（D4a）与 `produceOsPort(candidates, socksPorts)`（D3，OS 层候选产出，含唯一候选放宽）。
- [ ] **v1.3 变更**：`resolveChainPort` 签名改为 `resolveChainPort(candidates, osPorts, socksPorts, excludedPort)`（**链式取用 + D2 闸门**，被证伪顺延）；`PortVerdict::NotChecked` **删除**（纳管路径不再有「未判定」状态，`verdict` 恒为真实判定结论）。
- [ ] **v1.3 标注**：`classifyPortCandidate` 由「仅供 R6」扩展为**纳管路径与 R6 共用**（D2 v1.3 适用范围）；`resolveValidatedPort` 仍为 **R6 专用**（差别仅在「是否顺延」），二者保留在公共头中（纯函数、可单测、无副作用）。
- [ ] 删除 `selectOsListeningPort` 与旧 `resolvePort`（4 参版）；`PortSource` 顺序按 D1 调整（`None → RuntimeHistory → CmdlineConfig → OsTcpTable → DirProbe`）；`portSourceLabel()` 增补新枚举标签。
- [ ] `resolveValidatedPort` 内部**不得**出现 `osPort` 单值入参。
- [ ] 全文件**禁止** `auto`。

### Task 1：策略单测重写（`tests/test_standalone_adoption_policy.cpp`）

- [ ] 既有 15 例（7 resolvePort/label + 8 selectOsListeningPort）**全部重写**。
- [ ] **v1.2 标注**：下列 `Classify_*` / `Resolve_*`（判定类）用例同时服务于**纳管路径（经 D2 闸门）**与 **R6 路径**；纳管路径另加 `ResolveChain_*` / `ProduceOsPort_*` 用例。
- [ ] 新增用例（覆盖 D2 判定树三分支与 D4 选择规则）：
  - `Classify_RejectsOutOfRange`
  - `Classify_RejectsExcludedApiPort`
  - `Classify_UnknownWhenOsPortsEmpty`（**修 B1 的核心用例**）
  - `Classify_DisprovenWhenPortNotInOsPorts`
  - `Classify_VerifiedWhenInSocksPorts`
  - `Classify_UnverifiedWhenOsPortsHitButNoSocksAnswer`
  - `Classify_UnverifiedWhenSocksPortsNonEmptyButCandidateNotInIt`
  - `Resolve_PrefersFirstVerifiedAcrossLayers`（历史层被证伪、OS 层实锤 ⇒ 取 OS）
  - `Resolve_FallsBackToUnverifiedWhenNoVerified`（**修 B2：2 端口进程握手超时不被杀**）
  - `Resolve_PassesThroughWhenOsPortsEmpty`（**修 B3：表读不到全放行**）
  - `Resolve_DiscardsWhenAllCandidatesDisproven`
  - `Resolve_EmptyCandidatesYieldsZero`
  - `BuildOsCandidates_ExcludesApiPort`（**修 I1**）
  - `BuildOsCandidates_KeepsAllWhenNoSocksAnswer`（**修 B2：OS 层不贡献 0 候选**）
  - `CheckInstance_Match` / `_Recycled` / `_UnverifiableWhenEitherEmpty`（**修 feas-I8：闸门可单测**）
  - `ResolveMonitorPort_PrefersHistory` / `_FallsBackToMemory` / `_FallsBackToDisk` / `_ZeroWhenAllInvalid`（**修 I4**）
  - `PortSourceLabel_AllValues`（枚举顺序调整后重写）
  - **v1.3 纳管侧（链式取用 + D2 闸门）**：
    - `ResolveChain_TakesFirstVerified`（历史层有值且通过校验 ⇒ 取历史层，`verdict=Verified`）
    - `ResolveChain_SkipsDisprovenAndFallsThrough`（**v1.3 核心用例**：历史层陈旧值 `c ∉ osPorts` ⇒ `Disproven` ⇒ 顺延到 OS 层实锤端口）
    - `ResolveChain_SkipsZeroCandidates`（历史 0 → 命令行 → OS → 目录）
    - `ResolveChain_AllZeroYieldsNoCandidate`
    - `ResolveChain_AllDisprovenYieldsNoCandidate`（链上每个候选都被证伪 ⇒ `port=0` ⇒ 丢弃）
    - `ResolveChain_VerdictIsRealVerdict`（**断言 verdict == Verified/Unverified/Unknown**，杜绝误读为「已核实」）
    - `ResolveChain_PassesThroughWhenOsPortsEmpty`（**修对抗 B1：OS 表读不到 ⇒ 全链 `Unknown` ⇒ 放行第一个非零候选，不得否决**）
    - `ResolveChain_RejectsApiPortCandidate`（候选 == `excludedPort` ⇒ `Disproven` ⇒ 顺延，**修 I1**）
    - `ResolveChain_AcceptsUnverifiedWithWarning`（在听但握手不可判定 ⇒ 放行 `Unverified`，**不得丢弃**，修对抗 B6）
  - **v1.2 OS 层产出**：
    - `ProduceOsPort_PrefersSocksAnswering`（多个应答 ⇒ 取最小）
    - `ProduceOsPort_UniqueCandidateWhenNoSocksAnswer`（**唯一候选放宽**，修对抗 B2）
    - `ProduceOsPort_ZeroWhenAmbiguousAndNoSocksAnswer`（多候选且无应答 ⇒ 0）
    - `ProduceOsPort_ExcludesApiPort`

### Task 2：纳管路径重排（`src/ui/AppController.cpp`）

- [ ] 端口解析块（当前 `:1629-1713`）重排为：
  1. **闸门**：`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` → `processCreationTime` → `checkInstanceIdentity` ⇒ `Recycled` 则 `CloseHandle` + 日志 + 跳过；`Unverifiable` 则 WARN 后继续（**修 B5 / I5 / I6**）。
  2. **OS 事实**：`osPorts = proc::ProcessInspector::listeningTcpPorts(pid)`；对每个 `osPorts` 成员（含 api 端口）探测 `utils::respondsToSocks5Greeting(port, 200)` ⇒ `socksPorts`（**修 M9：定义补 `reply[1]!=0xFF`；仅 IPv4 回环**）。
  3. **候选装配**（层序按 D1 v1.2）：历史（`lookupSocksPort`）→ 命令行（`extractConfigFullPath` + `readStandaloneInboundPort`）→ **OS 活体**（`buildOsCandidates` + `produceOsPort`）→ 目录探测（**仅 `<exeDir>\config\` 一个目录**，v1.1.1 用户裁决；现 `:1698-1700` 的 `adoptedDirs[2]` 改为单目录，删除 `<exeDir>\worker\config\`）。
  4. **`resolveChainPort(candidates, osPorts, socksPorts, excludedPort)` ⇒ `resolution`**（**v1.3：链式取用 + D2 闸门**；被证伪的候选**顺延**到链上的下一个来源；`verdict` 为真实判定结论 `Verified`/`Unverified`/`Unknown`）。
- [ ] **v1.3 变更**：调用点由 `resolveChainPort(candidates)` 改为 `resolveChainPort(candidates, osPorts, socksPorts, excludedPort)`；**OS 事实必须在装配候选链之前取好**（D3 v1.3 补充），并被「OS 层候选产出」与「D2 闸门」**共用同一份**。
- [ ] 采纳 `resolution.port`；日志追加 `source=` 与 `verdict=`（v1.3 起为真实判定结论；`Unverified` 额外打 WARN）。
- [ ] 丢弃分支（当前 `:1714-1744`）按 D6 重写：触发条件 = **链全空**（`resolution.port == 0`）；**不 `finalizeStop` 活体行**、**`CloseHandle`**、不写新行、不建 watcher。
- [ ] 行身份：按 D5 第 4 点，`findInProgressHistory` 失配 ⇒ `findInProgressHistoryLoose` 复用旧 in-progress 行（并更新其 `started_at`），未命中才 `insertStart`（**修 I7 / feas-B3**）。
- [ ] `procStartedAt`（当前 `:1761-1764`）按 D5 第 4 点改为**保留兜底**的三级取值。
- [ ] 纳管成功路径的 `updateSocksPort`（当前 `:1824-1827`）**已存在**，本 Task 仅复核语义（`adoptedPort` 改为 `resolution.port`），不重复实现（**修 I3 / feas-I9**）。
- [ ] `discardedStandalonePids_` 增加「清理已不存在 pid」的周期维护（**修 feas-G4**）。

### Task 3：DAO 扩展（`include/ProxyRuntimeHistory.h` + `src/ProxyRuntimeHistory.cpp`）

- [ ] 新增 `bool updateStartedAt(int64_t historyId, const std::string& startedAt, sqlite3* db = nullptr);`（单条 UPDATE，`ended_at IS NULL` 守卫，`sqlite3_changes()` 判定）。
- [ ] `findInProgressHistoryLoose` 的返回语义保持（返回最大 id 的 in-progress 行 id）⇒ 纳管路径用它做「行身份复用」；必要时补一个返回完整 item 的重载。
- [ ] `lookupSocksPort` 的双键降级分支（`startedAt` 为空时按 `(index_id,pid)` 匹配）**保留**（其它调用方仍可能依赖），但纳管路径改传权威 `instanceStartedAt` ⇒ 该分支在纳管路径**不再触发**（**修 I12：表述与实际对齐**）。
- [ ] **不引入**新的 DB 列（`socks_port` 已存在，`ProxyRuntimeHistory.cpp:39-57` 幂等 ALTER）⇒ **无迁移 / 回滚风险**（feas-G9）。
- [ ] 新增用例（`TEST_F(RuntimeHistoryDAOTest, ...)`）：`UpdateStartedAt_UpdatesValue` / `_RejectsFinalizedRow` / `_RejectsUnknownId`、`FindInProgressHistoryLoose_ReusedForSamePid`。

### Task 4：监控路径同步（`src/ui/AppController.cpp`）

- [ ] `getWatchedStandaloneMonitors()`：端口块（当前 `:1468-1486`）改为 `resolveMonitorPort(historyPort, memoryPort, diskPort)`：
  - `historyPort` = `byId` 查表得到的 `row.socksPort`（`:1494-1504`）；
  - `memoryPort` = `watched[j].socksPort`（**保留内存兜底** — 修 I4）；
  - `diskPort` = 现有磁盘读取结果（**兜底，不再优先**）。
- [ ] `byId` 查表上移到端口块之前（可行：`watched[j].historyId` 在 Pass 1 `:1435` 已就绪，`:1451-1454` 已建表）。
- [ ] **监控路径禁止** `listeningTcpPorts` / `respondsToSocks5Greeting` 等 socket 调用（**修 B3 / feas-B4**）⇒ UI 线程零阻塞。
- [ ] 磁盘层的目录探测**同口径收敛为单一目录** `<exeDir>\config\`（v1.1.1）：现 `:1476-1479` 的 `dirs[2] = {exeDir\config\, exeDir\worker\config\}` 改为单目录（删除 `<exeDir>\worker\config\` 跨安装探测）。
- [ ] 探活消费点 `:2421` 保持读 `mon.socksPort`（值已由新链产出）。

### Task 5：R6 接管路径同步（`src/ui/AppController.cpp`）

- [ ] **v1.3：R6 是「完整 D2 判定」（逐个证伪 + Verified 优先）的使用路径**（v1.2 曾写「D2 判定的唯一使用路径」；v1.3 起纳管路径也用 D2，但**只作为闸门顺延**，语义不同）—— R6 的候选来源里混入了**旧进程的陈旧内存值**（`standaloneProxies_[indexId].socksPort`），必须逐个证伪 ⇒ 用 `resolveValidatedPort(candidates, osPorts, socksPorts, excludedPort)`，**不得**用 `resolveChainPort`。
- [ ] 端口来源改为 D8 的完整链 + 有界重试（5 × 300ms）。
- [ ] 候选装配顺序：**新进程命令行配置路径** → 新进程历史行 → **旧进程内存值（最易陈旧，必须过判定）** → OS 活体。
- [ ] 全部候选被证伪 ⇒ 端口置 **0**（**不得保留旧进程端口**），该行 `socks_port` 留 NULL（DAO `updateSocksPort` 对 `socksPort<=0` 是 no-op，`ProxyRuntimeHistory.cpp:337-339`）。
- [ ] 接管成功后回填 `sp.socksPort` / `sp.startedAt` / `sp.configPath`（`:1289-1295` 补 3 行）。
- [ ] 接管句柄掩码（`:1229-1231`）补 `PROCESS_TERMINATE`。
- [ ] `readStandaloneInboundPort` 的前向声明位置保持**文件作用域 `static`**（不可放进匿名 namespace —— 那是 v1.3 实施期踩过的编译坑：两者是不同实体 ⇒ ambiguous 调用）。

### Task 6：顺带一致性（低风险）

- [ ] `startStandaloneProxy`（`:1145`）已有 `pi.hProcess` 却调 `processCreationTime(pi.dwProcessId)`（重新 OpenProcess）⇒ 改用 `pi.hProcess`（少一次 OpenProcess，消除竞态）。
- [ ] `startStandaloneProxy` 的「连通性失败保留」分支在 D6 表格中登记为例外（`updateSocksPort` 仍写未核实端口）⇒ 或在该分支**不写** `socks_port`（推荐，避免把未监听端口固化为权威值）。

### Task 7：文档登记

- [ ] 实施完成后将本文登记至 `docs/INDEX.md` §7.5（`docs/INDEX.md:156`），编号接续当前最大值。
- [ ] 同步 `docs/plans/project-plans-tracker.md` 的「近期文档引用」表（追加于**表尾**，日期 2026-10-21，类型 `spec`）。**该表表头为 `| 日期 | 类型 | 文档路径 | 说明 |`，无独立状态列** ⇒ 状态写进「说明」列末尾。
- [ ] v1.0 标注为「已被 v1.1 取代」，不单独登记。

---

## 4. 验证

### 4.1 构建与单测

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
cd build
ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest" -V
```

- 目标名：`StandaloneAdoptionPolicyTest`（`CMakeLists.txt:1437`）、`RuntimeHistoryDAOTest`（`:1424`）、`ProcessInspectorTest`（`:1474`）、`UtilsTest`（`:668`）。
- `test_process_inspector`（`:1466`）与 `test_utils`（`:662`）**早已链接 `ws2_32`**，本规格**无需**补链接（修 M4）。
- `git diff -U0 | Select-String '^\+.*\bauto\b'` 只能覆盖**已纳入版本控制**的文件；新增文件须单独 grep（修 M8）。
- 全量：`ctest`（在 `build/` 目录执行）。**已知基线失败**：`XrayManagerStartupTest`（需真实就绪 xray）；`UI_FLOATINGWIDGET` 为已知并行 flaky（`WindowFromPoint` z-order 竞态，单跑 2/3 通过）。

### 4.2 实机验收（**按真实现场重写** — 修 feas-I2 / G2）

前置：`bin\config\standalone_4949226207915085546-xray.json` 当前存在且 `port=10810`（陈旧），12776 实际监听 **10809**。

1. 启动新构建 `bin\validproxy.exe`，等待**一个纳管周期**（纳管在 detached 后台线程，`MainFrame.cpp:452-454`；周期 = `proxy_process_monitor.check_interval_ms` = 30000ms）。
2. 查库 `bin\worker\guiNDB.db`：
   - pid 12776 的 in-progress 行 `socks_port = 10809`（**不是** 10810），`ended_at IS NULL`；
   - pid 13492 的 in-progress 行 `socks_port = 10808`；
   - **同一 pid 不得出现两条 in-progress 行**（修 feas-B3 / G6）。
3. 查纳管日志的 `source=` 与 `verdict=` 标签（**REPORT 级可能被 `file_level="WARN"` 屏蔽** ⇒ 需临时把 `bin\config.json` 的 `log.file_level` 调为 `REPORT`，或改用 DEBUG/WARN 通道的丢弃日志）：
   - pid 12776 ⇒ `source=runtime-history`（历史层 10809 命中且**通过 D2 闸门**：`10809 ∈ osPorts` 且握手实锤 ⇒ `verdict=Verified`）；
   - pid 13492 ⇒ `source=os-tcp-table`（历史空 → 命令行空 → **OS 活体 10808**，`verdict=Verified`）；
   - 两者 `verdict` 为**真实判定结论**（`Verified` / `Unverified` / `Unknown`），标签须如实反映，不得写成「已核实」以外的含义；
   - **不得**出现 `source=dir-probe`（目录层已降到末位，且 D2 闸门会证伪陈旧值）。
   - **新增判据（v1.3 核心）**：构造「历史层陈旧值」场景（把历史行的 `socks_port` 改为 10810 而进程实听 10809）后重新纳管 ⇒ 期望 `source=os-tcp-table`、`verdict=Verified`、历史行被**纠正为 10809** ⇒ 证明 D2 闸门「证伪 ⇒ 顺延」生效。
4. **不污染生产配置**：不得改写 `bin\config\standalone_4949226207915085546-xray.json`。若需构造「磁盘端口陈旧」场景，用**临时副本**放到 `bin\worker\config\` 并在验收后删除（修 feas-G3）。
5. 探活表现：12776 的探活应**成功**（打 10809），不再每 30s 失败。

---

## 5. 风险

| 编号 | 风险 | 缓解 |
| :--- | :--- | :--- |
| R1 | OS 表读取失败 ⇒ 全部候选 `Unknown` ⇒ 采纳**未核实**的历史值（可能是陈旧的 10810） | 已被 D2 的三态覆盖：至少会打 WARN；且纳管成功路径回写后，下一轮监控链「历史优先」仍取该值 ⇒ 需人工介入。**接受**（可见优于不可见） |
| R2 | 握手在负载下超时 ⇒ 候选降级为 `Unverified` ⇒ 采纳可能非 SOCKS 的 LISTEN 端口（多 inbound 场景） | 已排除 api 端口；`Unverified` 必带 WARN；实机 xray 对 `05 01 00` 回 `05 00` 已验证 |
| R3 | pid 同秒复用 ⇒ 闸门误判为同一实例 | 闸门比较的是**秒级**创建时间字符串；同秒复用无法区分（R3 残余），影响面 = 误纳管一个同 pid 的新进程 |
| R4 | 目录探测优先命中 `bin\config` 陈旧副本 | 本方案由 D2 的 OS 校验吸收（陈旧值被证伪 ⇒ 顺延到 OS 层）；**目录探测面按 v1.1.1 收敛为单一 `<exeDir>\config\`**（删除 `<exeDir>\worker\config\` 跨安装探测），但**陈旧副本本身仍可能在 `<exeDir>\config\` 内**（12776 的 10810 即如此）⇒ 单目录化**不**构成对该命中的单独修复，**必须**依赖 D2 的 OS 校验 |
| R5 | 纳管无重入保护（三处 detached 线程） | 既有缺陷（N3）；本方案新增的只是一次只读 `OpenProcess`，不改变竞态性质 |
| R6 | `updateSocksPort` 无事务 ⇒ 崩溃可能留下「有端口无 watcher」的行 | 既有设计；影响面小 |
| R7 | 监控链「磁盘兜底」仍可能取到陈旧端口（历史行缺失时） | 已保留内存兜底；历史行缺失 ⇒ 该代理本就没被纳管 ⇒ 不在监控范围 |
| R8 | 生产库存量 126 条 in-progress 孤儿行会持续污染「运行中」统计 | **本规格不处理**（N2）；单独开缺陷 |
| R9 | 端口被外部改绑后，同一行既记旧实例启动时间又记新端口 | 残余（N4），记入日志可诊断 |
| R10 | `GetExtendedTcpTable` 全表扫描在纳管线程（后台）执行 | 每 30s 一次、每 pid 一次，成本可接受；监控路径**不做**该查询（无 UI 线程风险） |
| **R11** | ~~**v1.2 残留：纳管路径不再证伪候选**~~ —— **v1.3 已关闭**（用户裁决「活体探测需加上 D2 校验」）：纳管路径启用 D2 闸门（取证先行 + 链式取用 + 闸门顺延），陈旧候选（`c ∉ osPorts`）被判 `Disproven` 并**顺延**到下一个来源，最终由 OS 活体层的实锤端口胜出。**残余（仍需知悉）**：① `osPorts` 为空（表读不到 / 该 pid 无 LISTEN）⇒ 全部候选判 `Unknown` ⇒ **放行**（修对抗 B1 的必然代价）⇒ 此时陈旧值仍可能被采纳，但该进程本就没在监听，探活必失败，端口对错不影响结论；② `Unverified`（在听但握手不可判定，如 200ms 握手超时）⇒ 放行 + WARN ⇒ 陈旧但**确实在听**的端口不会被纠正；③ 生产库 126 条 in-progress 行中 `socks_port IS NULL` 者占多数（升级前二进制创建）⇒ 这些行仍靠 OS 层首次回填 |

---

## 6. 文档登记

- `docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.1.md`（本文，实施依据）
- `docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.0.md`（被本文取代，不登记）

---

## 附录 A：v1.4 实施记录（已落地）

### A.1 代码改动

| 文件 | 改动 |
| :--- | :--- |
| `include/ProxyRuntimeHistory.h` / `src/ProxyRuntimeHistory.cpp` | 新增 `insertStartWithPort(indexId, startedAt, pid, socksPort, db=nullptr)`；`insertStart` 委托之；INSERT 语句增列 `socks_port`（`>0` 绑 int，`<=0` 绑 NULL）；其余事务/聚合逻辑不变 |
| `include/StandaloneAdoptionPolicy.h` | 全文重写：`PortSource{None,RuntimeHistory,CmdlineConfig,DirProbe,OsTcpTable}`、`PortVerdict{Disproven,Unknown,Unverified,Verified}`、`PortCandidate`、`PortResolution`；纯函数 `containsPort` / `classifyPortCandidate` / `buildOsCandidates` / `orderOsCandidatesBySocks` / `resolveChainPort` / `portSourceLabel` / `portVerdictLabel`；旧 `resolvePort` / `selectOsListeningPort` 删除 |
| `src/ui/AppController.cpp`（纳管路径） | 取证前置（`listeningTcpPorts` → `buildOsCandidates` → 逐端口 SOCKS5 握手 → `orderOsCandidatesBySocks`）→ 链式装配（历史三元组 → 命令行 → 目录(单目录 `<exeDir>\config\`) → OS 候选）→ `resolveChainPort` D2 闸门 → 行身份：三元组命中复用旧行，**未命中先收尾旧 in-progress 行再 `insertStartWithPort` 追加新行（携带端口）**；丢弃分支**不再 `finalizeStop` 活体行**；日志追加 `verdict=` |
| `src/ui/AppController.cpp`（监控路径） | 历史行 join 上移到端口块之前；端口优先序 = 历史(已核实 `socks_port`) → 内存(已核实) → 磁盘(未核实) → 0 |
| `src/ui/AppController.cpp`（R6 接管） | 句柄掩码补 `PROCESS_TERMINATE`；端口改用同一条链 + D2 闸门（旧进程内存端口须被新进程活体事实证伪）；改用 `insertStartWithPort` 追加会话行；回填 `sp.socksPort` / `sp.startedAt` / `sp.configPath` |
| `tests/test_standalone_adoption_policy.cpp` | 全文重写为 26 个 `TEST`（四态判定、链式顺延、`osPorts` 空放行、api 端口排除、`Unverified` 放行、`buildOsCandidates` / `orderOsCandidatesBySocks`、标签） |
| `tests/test_runtime_history_dao.cpp` | 新增 4 例（`insertStartWithPort` 写端口 / 非正端口存 NULL / **append 而非 update** / `insertStart` 仍存 NULL） |

### A.2 验证

- 构建：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` + `cmake --build build --parallel 8` ⇒ **0 error**
- 定向测试（`build/` 目录）：`ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest"` ⇒ **4/4 全绿**（45 / 26 / 11 / —）
- 全量 `ctest` ⇒ **54/56 通过**；唯一既有基线失败 `XrayManagerStartupTest`（需真实就绪 xray）；`UI_FLOATINGWIDGET` 为已知并行 flaky（`WindowFromPoint` z-order 竞态），单跑复验 **PASS**
- 全栈无 `auto` 类型推导（新增/修改行逐行核对）

### A.3 已知残留（不在本轮范围）

- ~~D5 实例闸门未实施~~ → **已于 v1.5 落地**（见附录 B）：句柄前移 + `checkInstanceIdentity` 纯函数 + 空值降级不杀 + 失配必落日志。对抗 B5 / feas-B5 提出的「空创建时间会误杀健康进程」已用 `Unverifiable` 分支修正。
- 生产库 `proxy_runtime_history` 存量 **126 条 `ended_at IS NULL` 孤儿行** 与 **`start_count` 恒 0**（`UPDATE ProfileExItem` 在行不存在时命中 0 行）属独立缺陷（规格 N2 明确排除）。
- `osPorts` 为空时 `Unknown` 放行 ⇒ 表读不到时无法证伪（对抗 B1 守卫，故意保留）。
---

## 附录 B：v1.5 实施记录（D5 实例闸门，已落地）

### B.1 代码改动

| 文件 | 改动 |
| :--- | :--- |
| `include/StandaloneAdoptionPolicy.h` | 补 `#include <string>`；新增 `enum class InstanceCheck { Match, Recycled, Unverifiable }`；纯函数 `checkInstanceIdentity(enumeratedCreatedAt, handleCreatedAt)`（任一为空 ⇒ `Unverifiable`；相等 ⇒ `Match`；不等 ⇒ `Recycled`）；`instanceCheckLabel(InstanceCheck)` |
| `src/ui/AppController.cpp`（纳管路径） | 在「已在管跳过」之后、端口解析之前插入 **D5 闸门**：`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` → `processCreationTime(hProcess)` → `CloseHandle` → `checkInstanceIdentity`；`Recycled` ⇒ 记 WARN/DEBUG 日志（含 `pid` / `indexId` / `enumeratedCreatedAt` / `handleCreatedAt`）并 `continue`（跳过纳管：不写历史行、不建 watcher、不 bump `start_count`）；`Unverifiable` ⇒ 记 DEBUG 日志并**放行**。`started_at` 改为三级兜底（`instanceStartedAt` → `procs[i].creationTime` → `utils::getCurrentTimestampFormatted()`） |
| `tests/test_standalone_adoption_policy.cpp` | 新增 6 例：`CheckInstance_MatchWhenTimestampsEqual` / `CheckInstance_RecycledWhenTimestampsDiffer` / `CheckInstance_UnverifiableWhenEnumeratedTimeEmpty` / `CheckInstance_UnverifiableWhenHandleTimeEmpty` / `CheckInstance_UnverifiableWhenBothEmpty` / `InstanceCheckLabel_AllValues`（合计 30 例） |

### B.2 验证

- 构建：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` + `cmake --build build --parallel 8` ⇒ **0 error**
- 定向测试（`build/` 目录）：`ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest"` ⇒ **4/4 全绿**（30 / 45 / 11 / —）
- 全量 `ctest` ⇒ **55/56 通过**；唯一既有基线失败 `XrayManagerStartupTest`（需真实就绪 xray）
- 全栈无 `auto` 类型推导（新增/修改行 + 两个新文件逐行核对均为 0）

### B.3 剩余残留（不在本轮范围）

- 生产库 `proxy_runtime_history` 存量 **126 条 `ended_at IS NULL` 孤儿行** 与 **`start_count` 恒 0** 属独立缺陷（规格 N2 排除）。
- `osPorts` 为空时 `Unknown` 放行 ⇒ 表读不到时无法证伪（对抗 B1 守卫，故意保留）。
- `discardedStandalonePids_` 只增不清 ⇒ pid 复用后相关日志永久降级为 DEBUG（feas-G4）。
## 附录 C：v1.6 实施记录（旁路检测，已落地）

### C.1 代码改动

| 位置 | 改动 |
| :--- | :--- |
| `include/StandaloneAdoptionPolicy.h` | 新增 `enum class AdoptionRoute { Skip, BypassDetect, FullDetect }` + `decideAdoptionRoute(alreadyManaged, startedByThisSession)` + `resolveBypassPort(historyPort, memoryPort, sessionPort)` + `adoptionRouteLabel(route)`；`startedByThisSession` 优先于 `alreadyManaged`（已在管的本次启动代理仍需补写历史行，例如切库后） |
| `src/ui/AppController.h` | 新增 `struct SessionStartedProxy { int socksPort; std::string creationTime; }`；`StandaloneProxyInfo` 增 `bool startedByThisSession`；成员 `std::unordered_map<DWORD, SessionStartedProxy> sessionStartedProxies_`（受 `standaloneMutex_` 保护） |
| `src/ui/AppController.cpp` · 启动路径 | 启动成功时登记 `sessionStartedProxies_[pid] = {socksPort, processCreationTime(pi.hProcess)}` 并置 `info.startedByThisSession = true`；三处「杀进程并 erase」分支追加 `sessionStartedProxies_.erase(pid)` |
| 同上 · 纳管路径 | 「已在管跳过」改为**路由决策块**（锁内取 `alreadyManaged` / `memoryPort` / `startedHere`+`sessionPort`，且要求创建时间一致才认定「本次启动」）；**活体检测仅 `FullDetect` 执行**；`BypassDetect` 端口取 `resolveBypassPort(historyPort, memoryPort, sessionPort)`；已在管 + `BypassDetect` ⇒ 只确保历史表行存在（三元组命中 ⇒ `updateSocksPort` 纠正；未命中 ⇒ `findInProgressHistoryLoose` + `finalizeStop` 收尾旧行 + `insertStartWithPort` 追加新行带端口），不 OpenProcess / 不重复 watch / 不重写 `standaloneProxies_`；REPORT 日志仅在真的追加行时输出（避免 30s 周期刷屏） |
| 同上 · 清理点 | 启动路径 NotifyFn、纳管路径 NotifyFn、`stopStandaloneProxy` 三处按 pid `erase(sessionStartedProxies_)`，避免 pid 复用被误认为本会话进程 |
| `tests/test_standalone_adoption_policy.cpp` | 新增 10 例：路由三态真值表 4 例 + `resolveBypassPort` 5 例 + `adoptionRouteLabel` 1 例（文件共 40 例） |

### C.2 验证

- 构建：`cmake --build build --parallel 8` ⇒ **0 error**
- 定向测试（`build/` 目录）：`ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest"` ⇒ **4/4 全绿**
- 全量 `ctest` ⇒ **55/56 通过**；唯一既有基线失败 `XrayManagerStartupTest`（需真实就绪 xray）
- 全栈无 `auto` 类型推导（新增/修改行 + 两个新文件逐行核对均为 0）

### C.3 剩余残留（不在本轮范围）

- 生产库 `proxy_runtime_history` 存量 **126 条 `ended_at IS NULL` 孤儿行** 与 **`start_count` 恒 0** 属独立缺陷（规格 N2 排除）。
- `osPorts` 为空时 `Unknown` 放行 ⇒ 表读不到时无法证伪（对抗 B1 守卫，故意保留）。
- `discardedStandalonePids_` 只增不清 ⇒ pid 复用后相关日志永久降级为 DEBUG（ feas-G4）。
- 纳管无重入保护（三处 detached 线程可并发进入）仍为既有缺陷，本次未改。