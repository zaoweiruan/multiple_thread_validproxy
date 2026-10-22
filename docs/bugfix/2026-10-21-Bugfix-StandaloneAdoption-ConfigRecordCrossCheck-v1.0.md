# 纳管独立代理进程：配置与库记录交叉核对（Config ↔ ProfileItem Cross-Check）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 纳管（adopt）游离的独立代理进程时，除了解析 SOCKS 端口，还要核对「正在运行的进程所用的磁盘配置」与「库中该代理的当前记录（ProfileItem 表）」是否一致；不一致时给出**可见告警**（配置已陈旧），端口一律**以链解出的结果为准**写回并纳管。

**Tech Stack:** C++17 / boost::json / SQLite / GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档:**
- `docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.1.md`
- `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-ConcurrencyAndDiagnostics-v1.1.md`
- `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-HistoryHitNoAppend-v1.0.md`

**约束:** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`；文档补丁用 `EndsWith`/`Substring`（禁正则）；`ctest` 必须在 `build/` 目录执行。

---

## 0. 背景与用户裁决

### 0.1 用户提案（逐字）

> 「根据 pid+started_at 匹配历史表，命中则纳管，不命中则解析命令行获取 indexid，并比对配置文件与库中记录是否相符，相符则 append 历史表、纳管，其它清空丢弃？」

### 0.2 三项裁决（逐字）

1. **「库中记录」= ProfileItem 表记录**，比对**配置文件中关键项，如 host、id、key 等**。
2. **「丢弃」= 不纳管**（不写历史行、不建 watcher、不进内存态；**不**对活体进程 `finalizeStop`）。
3. **「配置端口 ≠ 库端口」时以链解出的端口为准写回并纳管。**

### 0.3 提案的采纳与修正

| 提案环节 | 判定 | 说明 |
| :--- | :--- | :--- |
| 先按 (pid, started_at) 匹配历史表 | ✅ 部分采纳 | 现状链序本就是「历史表优先取端口」；但**匹配必须先过 D5 实例闸门**，且必须有 `(pid)` 宽匹配兜底（`started_at` 可能是 now 兜底值 ⇒ 精确三元组必然失配）。 |
| 不命中则解析命令行拿 indexId | ✅ 采纳 | 保持「命令行为主」；历史表反查仅在命令行不可读时作为**兜底**（管理员 xray ⇒ PEB 读失败 ⇒ `commandLine` 为空）。 |
| 比对配置文件与库中记录 | ✅ 采纳（新增环节） | 定位为**诊断性核对**，不是丢弃闸门（见 D1/D2）。 |
| 相符则 append 历史表、纳管 | ✅ 采纳 | 与既有「历史表 = append-only 会话日志、缺乏活体记录则补齐」一致。 |
| 其它清空丢弃 | ⚠️ 收窄 | 「不相符」**不**触发丢弃（见 D2）；丢弃仅在「身份不明」或「端口链全空」时发生（见 D6）。 |

---

## 1. 现场证据与代码事实

### 1.1 配置 JSON ↔ ProfileItem 表的关键项映射（实测）

`Profileitem` 字段（`include/Profileitem.h:21-56`，DB 列序见 `:101`）与出站构造器
（`src/config/outbound/*OutboundBuilder.cpp`、`src/config/StreamSettingsBuilder.cpp`）的对应关系：

| 语义 | 配置 JSON 路径 | ProfileItem 列 | 是否必比 |
| :--- | :--- | :--- | :--- |
| 协议 | `outbounds[proxy].protocol` | `configtype` | ✅ 必比 |
| 服务器地址 | `outbounds[proxy].settings.vnext[0].address`（trojan：`settings.servers[0].address`） | `address` | ✅ 必比 |
| 服务器端口 | `...vnext[0].port`（trojan：`...servers[0].port`） | `port` | ✅ 必比 |
| 用户标识 / 密码 | `...vnext[0].users[0].id`（trojan：`...servers[0].password`） | `id` | ✅ 必比 |
| 请求 Host | `...streamSettings.wsSettings.host` / `xhttpSettings.host` | `requesthost` | ⭕ 可选 |
| Reality 公钥（key） | `...streamSettings.realitySettings.publicKey` | `publickey` | ⭕ 可选 |

### 1.2 关键事实（实测，可直接引用）

1. **本地 SOCKS 端口不在 ProfileItem 里** —— 它由 standalone 配置模板的 `inbounds[0].port` 生成（`ConfigBuilder::buildStandaloneConfig`，`src/config/ConfigBuilder.cpp:562`）。⇒ `ProfileItem.port` 是**远端服务器端口**，与本地 SOCKS 端口**不可混谈**；本次核对比对的正是「远端」语义。
2. standalone 配置的 `outbounds` 顺序固定为 **[proxy(tag="proxy"), direct, block]**（`ConfigBuilder.cpp:586-608`）⇒ proxy 出站用 `tag == "proxy"` 定位（退路：取 `outbounds[0]`）。
3. VLESS/VMess 的 uuid 与 Trojan 的密码**都存在 `Profileitem::id`**（`include/Profileitem.h:28`；`TrojanOutboundBuilder` 写 `servers[0].password = p.id`）⇒ 对比时统一取 `users[0].id` 或 `servers[0].password`。
4. `ProfileitemDAO::getByIndexId(indexId)` 返回 `std::optional<Profileitem>`（`include/Profileitem.h:335`）；AppController 内的用法是**局部对象**：`db::models::ProfileitemDAO dao(db_);`（如 `src/ui/AppController.cpp:783/1549`）。
5. 磁盘上可能存在**陈旧配置副本**：`bin\config\standalone_4949226207915085546-xray.json` 端口 10810，而实听 10809 ⇒ 「磁盘配置」本身就不保证是权威，核对结果必须允许「不相符」而不致错杀。
6. 生产库 `proxy_runtime_history` 有 128 条 `ended_at IS NULL` 孤儿行、11 组重复三元组 ⇒ 本次**不得**新增对活体进程的历史行清空动作。

---

## 2. 设计决策

### D1：核对是**诊断性**的，不是纳管门禁

活体事实（OS TCP 表 + SOCKS5 握手）只能裁决**本地端口**，无法裁决「出站用的是哪份节点信息」。磁盘配置里的 address / uuid / reality 公钥**只有与库记录比对**才能发现「进程在跑旧配置」。

⇒ 核对结果的唯一作用是**让「配置已陈旧」这一真实故障可见**（日志 + 监控行备注），**不参与**「纳管 / 不纳管」的判定。

### D2：「不相符」⇒ 告警，但**仍然纳管**（不丢弃）

三条理由：

1. **不相符是常态**：用户在 GUI 里改了代理（改 address / uuid / reality 公钥）但**没有重启**独立代理 ⇒ 磁盘配置是旧的、库记录是新的 ⇒ 此时丢弃一个**正在正常提供服务**的代理，等于让它从监控中消失，违背本项目一贯的 fail-open 原则（「看不见」比「显示错端口」危害更大）。
2. **订阅刷新**同样会造成不相符（`subid` 刷新改了节点）⇒ 同上。
3. **「相符」也不能证明什么**：相符只说明「这份配置是当前库记录渲染出来的」，并不能证明进程健康；健康性由端口链 + D2 活体闸门负责。

⇒ 与裁决①③一致：**不相符 ⇒ 记 WARN 告警（含不一致字段名）⇒ 继续按链解出的端口写回并纳管**。

### D3：判定三态

| 判定 | 条件 | 处置 |
| :--- | :--- | :--- |
| `Match` | 必比项全部一致 | 正常，日志 `record=match` |
| `Mismatch` | 至少一个必比项不一致 | **告警（WARN）**，日志列出不一致字段 ⇒ **仍纳管** |
| `Unverifiable` | 配置文件读不到 / 出站结构不认识 / ProfileItem 读不到 / 必比项任一为空 | 不比对、不告警，日志 `record=unverifiable`（DEBUG） |

### D4：必比项与可选项

- **必比**：`protocol` / `address` / `port` / `userId` —— 这四项决定「这个代理节点是哪个节点」。任一为空 ⇒ `Unverifiable`（无法比对，不是「不相符」）。
- **可选**：`requestHost` / `publicKey` —— 仅当**双方都非空**时才比较；一方为空 ⇒ 跳过该项（不计入不相符）。

### D5：纯策略抽取（可单测，零 I/O）

在 `include/StandaloneAdoptionPolicy.h` 中新增（namespace `standalone_adoption`）：

```cpp
enum class RecordVerdict { Match = 0, Mismatch, Unverifiable };

struct ProxyRecordSnapshot {
    std::string protocol;     // vless / vmess / trojan / ...
    std::string address;      // server address
    std::string port;         // server port
    std::string userId;       // uuid (vless/vmess) or password (trojan)
    std::string requestHost;  // ws / xhttp request host (optional)
    std::string publicKey;    // reality public key (optional)
};

struct RecordCompareResult {
    RecordVerdict verdict = RecordVerdict::Unverifiable;
    std::vector<std::string> mismatchedFields;  // empty unless Mismatch
};

inline RecordCompareResult compareProxyRecord(const ProxyRecordSnapshot& fromConfig,
                                              const ProxyRecordSnapshot& fromDb);
inline const char* recordVerdictLabel(RecordVerdict verdict);  // match/mismatch/unverifiable
```

`compareProxyRecord` 判定顺序：
1. 必比四项任一为空（**双方**）⇒ `Unverifiable`。
2. 逐必比项比较，不一致则记字段名（`protocol` / `address` / `port` / `userId`）。
3. 可选项仅在双方都非空时比较，不一致记 `requestHost` / `publicKey`。
4. 有不一致 ⇒ `Mismatch`；否则 `Match`。

### D6：丢弃语义（**不变**，明确写入规格以免被误改）

丢弃 = **不纳管**：不写历史行、不建 watcher、不进内存态、**不对活体进程 `finalizeStop`**、记 `discardedStandalonePids_`（pid + 创建时间指纹去重）+ WARN/DEBUG 日志。

触发条件**仅两种**：
1. **身份不明**：命令行不可解析出 `standalone_<indexId>-*.json` 形态的 indexId，且历史表按 (pid, started_at) / (pid) 宽匹配也反查不到 indexId ⇒ 跳过（连端口解析都不做）。
2. **身份明确但端口链全空**：历史 → 命令行 → 目录 → 活体四层都没给出端口 ⇒ 丢弃。

**「配置与库记录不相符」不属于上述任何一种 ⇒ 不触发丢弃。**

---

## 3. 改动清单

### Task 0：策略头 + 单测（纯函数，先写测试）

- [x] `include/StandaloneAdoptionPolicy.h`：新增 D5 的 `RecordVerdict` / `ProxyRecordSnapshot` / `RecordCompareResult` / `compareProxyRecord` / `recordVerdictLabel`（含 `#include <string>`，头文件已有）。
- [x] `tests/test_standalone_adoption_policy.cpp`：新增用例（沿用该文件既有的**逐符号 using 声明**风格，新增符号须补对应 using 声明）：
  - `RecordCheck_MatchWhenAllRequiredEqual`
  - `RecordCheck_MismatchWhenProtocolDiffers`
  - `RecordCheck_MismatchWhenAddressDiffers`
  - `RecordCheck_MismatchWhenPortDiffers`
  - `RecordCheck_MismatchWhenUserIdDiffers`
  - `RecordCheck_MismatchWhenRealityPublicKeyDiffers`
  - `RecordCheck_IgnoresOptionalFieldMissingOnOneSide`
  - `RecordCheck_UnverifiableWhenRequiredFieldEmpty`
  - `RecordVerdictLabel_AllValues`

### Task 1：配置快照读取辅助（`src/ui/AppController.cpp` 静态函数）

- [x] 新增 `static bool readStandaloneProxySnapshot(const std::string& configPath, standalone_adoption::ProxyRecordSnapshot& out);`（放在 `readStandaloneInboundPort` 附近，**文件作用域 static 前向声明**须与定义同域 —— 教训：放进匿名 namespace 会与文件作用域定义成为不同实体 ⇒ 编译报 ambiguous）。
- [x] 实现：ifstream → `boost::json::parse` → `root["outbounds"]` 数组 → 定位 `tag == "proxy"` 的对象（退路：`outbounds[0]`）→ 读 `protocol`；`settings` 下按 `vnext[0]`（vless/vmess）或 `servers[0]`（trojan）取 `address` / `port`（整数转字符串）/ `id`|`password`；`streamSettings` 下取 `wsSettings`|`xhttpSettings`.`host` 与 `realitySettings`.`publicKey`。
- [x] 任一必需项取不到 ⇒ 返回 `false`（调用方跳过核对）。

### Task 2：纳管路径接线

- [x] 在纳管链的**端口解析之后**、历史行身份判定之前插入核对：
  - 仅当 `adoptedConfigPath` 非空（即已从命令行/目录拿到权威或兜底配置路径）时进行。
  - `db::models::ProfileitemDAO dao(db_);` + `dao.getByIndexId(indexId)` → 填充 `ProxyRecordSnapshot`（`configtype`→protocol、`address`、`port`、`id`→userId、`requesthost`、`publickey`）。
  - `compareProxyRecord(configSnapshot, dbSnapshot)`。
- [x] 结果落地：
  - `Mismatch` ⇒ 额外打一条 **WARN** 日志（含 indexId、pid、不一致字段名列表），**不**中断流程。
  - 纳管的 REPORT 日志追加 `record=` + `recordVerdictLabel(...)`。
- [x] **端口写回规则（裁决③）**：历史行写入/纠正的端口**一律取链解出的 `adoptedPort`**（现有无条件 `updateSocksPort(rhId, adoptedPort, db_)` 保持不变）；「配置端口 ≠ 库端口」不改变任何行为。

### Task 3：构建与测试

- [x] `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`（仅在需要重新配置时）
- [x] `cmake --build build --parallel 8` ⇒ 期望 0 error
- [x] `ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest" -V`（**在 `build/` 目录**）⇒ 期望 4/4
- [x] 全量 `ctest -V` ⇒ 期望 **55/56**（唯一失败为既有基线 `XrayManagerStartupTest`；`UI_FLOATINGWIDGET` 为已知并行 flaky）
- [x] `git diff HEAD -U0 -- '*.cpp' '*.h' | Select-String -Pattern '^\+.*\bauto\b' -CaseSensitive` ⇒ **0**

### Task 4：文档登记

- [x] `docs/INDEX.md` §9.1 新增编号 **103**（末号 102 → 103），行格式 `| 103 | [docs/bugfix/<file>.md](./bugfix/<file>.md) | **<中文摘要>** — <细节> | ✅ completed | — |`
- [x] `docs/plans/project-plans-tracker.md` 表尾追加行（**数据行 5 列**，末列为状态 `| ✅ completed |`）
- [x] 本文档追加「实施记录」附录 + 勾选 Task 复选框

---

## 4. 验证

### 4.1 构建与单测

见 Task 3。**不新增测试文件** ⇒ 无需改 `CMakeLists.txt`（ctest 名沿用 `StandaloneAdoptionPolicyTest`）。

### 4.2 实机验收

| 步骤 | 操作 | 判据 |
| :--- | :--- | :--- |
| 1 | 启动 `bin\validproxy.exe`，等待一个纳管周期（30s） | 纳管 REPORT 日志含 `record=match` 或 `record=unverifiable`（生产 `log.file_level=WARN` ⇒ REPORT **不落盘** ⇒ 需临时把 `file_level` 调为 `REPORT` 或在 GUI LogPanel 切级别） |
| 2 | 在 GUI 里改一个代理的 address/uuid（**不重启**该独立代理） | 下一轮纳管出现 **WARN** 日志且含不一致字段名；该代理**仍被纳管**（监控行仍在、端口不变） |
| 3 | 查库 `proxy_runtime_history` | 该 (index_id, pid) 仍只有一条 in-progress 行；`socks_port` = 链解出的端口（不是旧值） |
| 4 | 恢复原配置 | 下一轮纳管 `record=match`（WARN 消失） |

> 验收前备份 `bin\worker\guiNDB.db`；切库路径**无** `VALIDPROXY_NO_ADOPT` 门控（`MainFrame.cpp:451/:977` 有，`AppController.cpp:208-210` 没有）⇒ 勿在 UI 自动化沙箱内切库。

---

## 5. 风险与边界

| 编号 | 风险 | 缓解 |
| :--- | :--- | :--- |
| R1 | 配置 JSON 结构随模板/后端（xray / sing-box）不同 ⇒ 快照可能取不到 | 取不到即 `Unverifiable`，不比对、不告警、不阻断；只实现 xray 形状（vnext / servers），sing-box 形状留待后续 |
| R2 | `Profileitem::port` 与本地 SOCKS 端口混淆 | 快照只取**远端**语义（`outbounds[proxy].settings...port`），并在代码注释中写明与 `inbounds[0].port` 无关 |
| R3 | 不相符告警每 30s 刷屏 | WARN 只在**首次**出现时打印（复用 `discardedStandalonePids_` 同类去重思路：按 indexId + 不一致字段组合去重）；后续降为 DEBUG |
| R4 | 「配置已陈旧」告警被误读为「代理坏了」 | 日志文案明确区分「配置陈旧（建议重启代理以应用新配置）」与「代理不可用（已丢弃）」 |
| R5 | 核对引入额外 IO（读配置文件 + 查 ProfileItem） | 配置文件已在端口链读过一遍 ⇒ 复用同一份解析结果（一次性解析、两次取用）；ProfileItem 查询为单行主键查询 |
| R6 | 出现「不相符 ⇒ 丢弃」的诱惑（提案原文） | 规格 D2/D6 明确写死「不相符不丢弃」，并在验收步骤 2 断言「仍被纳管」 |

### 非目标

- **N1** 生产库 128 条 `ended_at IS NULL` 孤儿行、11 组重复三元组、`start_count` 恒 0 的数据治理。
- **N2** sing-box 配置形状的快照提取。
- **N3** 把「配置陈旧」升级为 UI 标记（监控行备注列 / 状态图标）—— 本次只做日志可见性。
- **N4** 切库时旧 watcher 跨库 `finalizeStop` 与 `db_` 无同步改写（UAF）。
- **N5** `discardedStandalonePids_` 容量治理。

---

## 6. 文档登记

- `docs/INDEX.md` §9.1 新增编号 **103**。
- `docs/plans/project-plans-tracker.md`「近期文档引用」表尾追加行（数据行 5 列，末列为状态）。
- 本文档追加「实施记录」附录。


---

## 附录：实施记录（v1.0，已落地）

### 7.1 代码改动

| 文件 | 改动 |
| :--- | :--- |
| `include/StandaloneAdoptionPolicy.h` | 新增 `RecordVerdict` / `ProxyRecordSnapshot` / `RecordCompareResult` / `compareProxyRecord` / `recordVerdictLabel`（纯函数，D5） |
| `tests/test_standalone_adoption_policy.cpp` | 新增 9 个用例（Match / 四项必比项 Mismatch / publicKey Mismatch / 可选项缺一侧忽略 / 必比项为空 ⇒ Unverifiable / label） |
| `src/ui/AppController.cpp` | 新增 `readJsonString` / `readJsonIntAsString` / `readStandaloneProxySnapshot`（从 `outbounds[tag=proxy]` 取 protocol/address/port/id|vnext[0].users[0].id 或 servers[0].password，及可选 requestHost / reality publicKey）；纳管链端口解析之后插入交叉核对块；不相符 ⇒ WARN（同一 indexId + 同一字段集去重后降级 DEBUG）；纳管 REPORT 日志追加 `record=` |
| `src/ui/AppController.h` | 新增 `staleConfigWarned_`（indexId ⇒ 已报告字段集）与 `staleConfigMutex_` |

### 7.2 验证

- `cmake --build build --parallel 8` ⇒ **0 error**（仅既有 `-Wunused-parameter` 警告）。
- 定向 `ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest"`（在 `build/`）⇒ **4/4 全绿**，`StandaloneAdoptionPolicyTest` **60/60**。
- 全量 `ctest` ⇒ **55/56 通过**（唯一失败 `XrayManagerStartupTest`，既有基线，需真实就绪 xray）。
- `git diff HEAD -U0 -- '*.cpp' '*.h' | Select-String -Pattern '^\+.*\bauto\b' -CaseSensitive` ⇒ **0**。

### 7.3 剩余残留

- sing-box 配置形状的快照提取未实现（取不到 ⇒ `Unverifiable`，不比对不告警）——见非目标 N2。
- 「配置已陈旧」只做日志可见性，未做 UI 标记——见非目标 N3。
- 生产库 128 条 `ended_at IS NULL` 孤儿行、11 组重复三元组、`start_count` 恒 0 未治理——见非目标 N1。