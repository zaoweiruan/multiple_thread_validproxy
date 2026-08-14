# Spec: 代理评分引入历史服务记录（ProxyScoring History）

- 日期: 2026-08-13（v1.1 修订于 2026-08-14；v1.2 修订于 2026-08-14）
- 类型: Spec
- 模块: `ProxyRuntimeHistoryDAO`（新）/ `ProfileExItemDAO` / `AppController`（ProcessExitListener）/ `ProxyScorer`（新）/ `ProxyListPanel` / `ProcessInspector`（新）
- 版本: v1.2
- 状态: 🔍 draft（评审中）

---

## 0. 修订记录（v1.0 → v1.2）

| # | 评审意见 / 修订要求 | 落点 |
|---|----------------------|------|
| R1 | 启动前比对系统中存活代理进程的 command line 中启动配置文件名是否重复，有相同则弹窗提示直接退出 | §3.3.1 启动前查重；新增 `ProcessInspector`（§3.2.3） |
| R2 | 进程启动后使用 curl handle 通过代理端口访问测试目标 url，确定连通、可用则入表、`insertStart`；不连通则弹窗提示是否关闭代理进程，不关闭则弃管，不入表、不监控 | §3.3.2 启动后连通性验证；`insertStart` 时机后移；弃管标记（§3.3.1） |
| R3 | 后台线程按配置时间（新增），轮询进程句柄 + command line 是否吻合，判断进程存活 | **v1.1**：§3.3.2 Watchdog 周期轮询（`history_watchdog_interval_ms`）。**v1.2 再修订（R5）**：改为事件驱动 `ProcessExitListener`，见下 |
| R4 | 总分 = 0.20 × 速度分 + 0.30 × 稳定性分 + 0.50 × 历史服务分 | §3.4 权重反转；冷启动归一化比例同步调整 |
| R5 | **评审对话（2026-08-14）**：R3 Watchdog 周期轮询能否改为接受进程终止系统消息通知？—— 结论：**可改为事件驱动**。Windows 进程句柄即同步对象，进程退出时自动 signaled，`WaitForSingleObject(handle, INFINITE)` 阻塞等待即系统级"进程终止通知"（零延迟、零 CPU 轮询开销）。§3.3.2 重写为 `ProcessExitListener`（每受管进程一个专用等待线程）；`history_watchdog_interval_ms` 配置项删除；R3 的 command line 判据保留为 finalize 时一次性接管检查 | §3.3.2；§3.3.4；§4.2；§5；§6 |

---

## 1. 需求

用户原话：

> "代理评分需要引入历史记录，如：多次启动代理且稳定提供服务"

**解读**：将代理评分从"一次性测试瞬态"升级为"长期使用行为画像"。一个代理被**反复启动**（用户信任）、**长时间稳定运行**（可靠性高）、**正常退出而非崩溃**（健康）时应获得更高评分；冷启动代理（无历史）保持中性，不惩罚。

**已确认的设计决策**（评审前置 Q&A）：

| # | 决策点 | 结论 |
|---|--------|------|
| D1 | 历史数据范围 | **仅独立代理 StandaloneProxy**（用户实际使用），批量测试临时 Xray 进程不计入 |
| D2 | 存储方案 | **明细表 + 聚合缓存**（`proxy_runtime_history` 明细 + `ProfileExItem` 3 个聚合列） |
| D3 | 退出检测 | **事件驱动**（v1.2）：每受管独立代理一个专用等待线程阻塞 `WaitForSingleObject(handle, INFINITE)`，进程退出即系统通知；finalize 时一次性 `ProcessInspector` command line 检查区分正常退出/崩溃/外部重启接管（v1.0 原选 Watchdog 轮询，经 R5 修订为事件驱动） |

---

## 2. 现状分析（修改前）

### 2.1 已有基础：message 双时间戳（已 completed）

`2026-08-11-Spec-ProfileExMessage-v1.0.md` 与 `2026-08-12-Spec-ProfileExMessage-Sort-v1.0.md` 已落地：

- `ProfileExItem.message` = `<测试时间>+<启动时间>`（恒含一个 `+`，任一侧可缺失）。
- `AppController::startStandaloneProxy()` 启动成功（AppController.cpp:729）调用
  `ProfileExItemDAO::updateStartupTime(indexId)` 写启动侧。
- `messageActiveTime` / `compareMessage` 静态方法用于 Message 列排序（较新时间）。

**局限**：message 只保留**最近一次**启动时间，历史被覆盖；无启动次数、无运行时长、无退出健康度。

### 2.2 独立代理生命周期现状

- `AppController::startStandaloneProxy(indexId, overridePort=0)`（AppController.cpp:496）：
  CreateProcessA（`CREATE_NEW_CONSOLE`）→ 存 `StandaloneProxyInfo{indexId, configPath, socksPort, processHandle, running}` → `updateStartupTime`。
- `standaloneProxies_`（`standaloneMutex_` 保护）**只增不减**：仅 insert(:721) / find(:743) / 遍历(:753)，**无 erase、无 running=false、无 stopStandaloneProxy 方法**。
- 用户通过手动关闭 Xray 控制台窗口停止代理，AppController 完全无感知 → **无法记录运行时长、无法区分正常退出 vs 崩溃**。

### 2.3 StandaloneProxyEvent 为死代码

- `Events.h:262` 定义 `StandaloneProxyEvent`（indexId/address/socksPort/started/error），注释 "sent when a standalone proxy starts/stops"。
- `ProxyListPanel.cpp:82` 已绑定 `onStandaloneProxyEvent`。
- 但全代码库**无任何 `wxQueueEvent(..., new StandaloneProxyEvent(...))` 发送点** —— 预留未激活。本次设计将其激活为历史采集的 UI 通知链路。

### 2.4 可用的退出判定能力

- `XrayInstance::lastExitCode()` 返回 DWORD（`STILL_ACTIVE`=存活）—— 但那是批量测试临时实例。
- StandaloneProxy 进程句柄 `pi.hProcess` 已保存在 `StandaloneProxyInfo.processHandle`，可用 `WaitForSingleObject(handle, 0)` + `GetExitCodeProcess` 判定退出与退出码。**无需新增进程跟踪设施**。
- **v1.2 关键认识**：Windows 进程句柄本身就是**同步对象**——进程退出时句柄自动变为 signaled。`WaitForSingleObject(handle, INFINITE)` 阻塞等待即系统级"进程终止通知"（事件驱动、零延迟、零 CPU 轮询开销），项目已有成熟先例（`XrayInstance.cpp:204`、`XrayApi.cpp:110`）。这是 R5 将轮询改为事件驱动的依据。

### 2.5 进程枚举基础（R1/R3 前置）

- `src/Utils.cpp:256` 已有 `CreateToolhelp32Snapshot` + `Process32FirstW` 进程枚举（`killProcessByName` 使用）。
- **局限**：仅按进程名（exeName）匹配，**不读取 command line**。R1/R3 需要新增"按 command line 内容匹配配置文件名"能力 → 新增 `ProcessInspector`（§3.2.3）。

### 2.6 数据库现状

- 无任何历史/使用量表；`ProfileExItem` 现有 `delay, speed, sort, message, consecutive_failures`。
- 生产库 `bin/worker/guindb.db`；测试全量 `test/guindb.db`（53,837 profiles）。

### 2.7 连通性验证基础（R2 前置）

- `config.json` 已有 `test.url`（`https://www.google.com/generate_204`）+ `test.timeout_ms`（5000），可直接复用为连通性探测目标。
- 已有 `UrlFetcher` / `CurlEasyHandle`（cURL RAII 封装）可发起 HTTP 请求；需确认其支持通过 SOCKS5 代理端口（`CURLOPT_PROXY` socks5://127.0.0.1:port）访问 —— P0 确认项。
- `startStandaloneProxy` 由 **UI 线程**调用（ProxyListPanel.cpp:419）→ 连通性探测**必须异步化**或限定短超时，避免阻塞界面（P0 确认项）。

---

## 3. 设计方案

### 3.1 数据模型

**3.1.1 明细表 `proxy_runtime_history`（新建）**

```sql
CREATE TABLE IF NOT EXISTS proxy_runtime_history (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  index_id     TEXT NOT NULL,
  started_at   TEXT NOT NULL,          -- ISO8601 "yyyy-MM-dd HH:mm:ss"（与 message 时间戳同格式）
  ended_at     TEXT,                   -- NULL = 进行中会话
  exit_code    INTEGER,                -- 0=正常退出; 259(STILL_ACTIVE)=被强杀; 其他=崩溃
  duration_ms  INTEGER,                -- 退出时回填
  source       TEXT NOT NULL DEFAULT 'standalone'
);
CREATE INDEX IF NOT EXISTS idx_runtime_history_index ON proxy_runtime_history(index_id);
CREATE INDEX IF NOT EXISTS idx_runtime_history_started ON proxy_runtime_history(started_at);
```

**3.1.2 聚合缓存列（`ProfileExItem` 扩展 3 列）**

```sql
ALTER TABLE profileexitem ADD COLUMN start_count      INTEGER NOT NULL DEFAULT 0;
ALTER TABLE profileexitem ADD COLUMN total_runtime_ms INTEGER NOT NULL DEFAULT 0;
ALTER TABLE profileexitem ADD COLUMN crash_count      INTEGER NOT NULL DEFAULT 0;
```

派生量：正常退出率 = `(start_count - crash_count) / start_count`。

**维护策略**：写时同步更新（写入点仅两处：启动 insert、退出 finalize），明细与聚合在**同一事务**内保证一致性；聚合列避免评分/列表加载时扫历史表（53,837 代理规模下的性能红线）。

> **P0 确认项**：`profileexitem` 实际表名、`Database::initialize` 建表/迁移机制（`CREATE TABLE IF NOT EXISTS` + `ALTER TABLE ... ADD COLUMN` 幂等兼容）、`db_` 并发锁模型（`updateTestResult` 已多线程调用，须确认统一入口）。

### 3.2 DAO 接口

**3.2.1 新增 `include/ProxyRuntimeHistory.h` + `src/ProxyRuntimeHistory.cpp`**

```cpp
struct ProxyRuntimeHistoryItem {
  int64_t      id = -1;
  std::string  indexId;
  std::string  startedAt;      // "yyyy-MM-dd HH:mm:ss"
  std::string  endedAt;        // 空 = 进行中
  int          exitCode = 0;
  int64_t      durationMs = 0;
  std::string  source = "standalone";
};

class ProxyRuntimeHistoryDAO {
public:
  explicit ProxyRuntimeHistoryDAO(sqlite3* db);

  // 启动时：插入明细行（ended_at=NULL）+ start_count 递增（同一事务）；返回新行 id
  int64_t insertStart(const std::string& indexId, sqlite3* db = nullptr);

  // 退出时：回填 ended_at/exit_code/duration_ms + 更新 total_runtime_ms/crash_count（同一事务）
  bool finalizeStop(int64_t historyId, const std::string& endedAt,
                    int exitCode, int64_t durationMs, sqlite3* db = nullptr);

  // 供评分/展示查询（可直接读 ProfileExItem 聚合列，此处兜底）
  bool getRuntimeStats(const std::string& indexId, int* startCount,
                       int64_t* totalRuntimeMs, int* crashCount, sqlite3* db = nullptr);
};
```

**3.2.2 `ProfileExItemDAO` 扩展**

- `createTable`/`fromStmt`/`toJson` 同步 3 个聚合列字段（结构体扩展，含既有单测更新）。
- 新增 `bumpStartCount(indexId)`、`accumulateRuntime(indexId, durationMs, crash)` —— 供 `ProxyRuntimeHistoryDAO` 在事务内调用（或由 DAO 直接 UPDATE，二选一，P0 定）。

**3.2.3 新增 `include/ProcessInspector.h` + `src/ProcessInspector.cpp`（R1/R3）**

纯系统工具类（不触碰数据库、不触碰 UI），负责进程枚举与 command line 比对：

```cpp
class ProcessInspector {
public:
  // 是否存在存活进程，其 command line 包含指定配置文件名（如 "standalone_xxx-xray.json"）
  static bool isProcessRunningWithConfig(const std::string& configFileName);

  // 枚举匹配进程名的进程，返回 { pid, commandLine } 列表（供 finalize 接管检查与 R1 查重）
  static std::vector<ProcessInfo> enumerateByName(const std::string& exeName);
};
```

- **command line 读取**：Windows 下基于既有 `CreateToolhelp32Snapshot` 枚举 + 进程 PEB（`NtQueryInformationProcess` + `ReadProcessMemory` 读 `ProcessParameters->CommandLine`）；读取失败时降级为仅按进程名匹配（返回 true，保守拦截）。
- **实现注意**：singbox 分支需按实际 exeName（xray.exe / sing-box.exe）分别枚举；跨架构（32/64 位进程 PEB 偏移）差异由工具类封装屏蔽。

### 3.3 采集链路（v1.1 重写，吸收 R1/R2/R3）

#### 3.3.0 生命周期总览

```
[用户点击启动] 
   → R1 启动前查重（ProcessInspector::isProcessRunningWithConfig）
       重复 → 弹窗提示 → 直接退出（不启动、不弹二次确认）
       不重复 → CreateProcessA 启动进程
   → R2 启动后连通性验证（curl 经 socks 端口访问 test.url）
       连通可用 → updateStartupTime + insertStart 入表 → running=true（受管）
       不连通 → 弹窗提示（是否关闭代理进程？）
                 是 → 终止进程，不入表、不监控
                 否 → 弃管（进程存活但不入表、不监控）
→ R3 ProcessExitListener（事件驱动，v1.2）
        进程句柄 signaled（进程实例退出）→ finalize 回调
          一次性 command line 检查：
            同配置进程仍在 → 外部重启接管（旧会话收尾 + 新会话 insertStart）
            同配置进程消失 → finalizeStop 回填历史（exit_code 判正常/崩溃）
```

#### 3.3.1 `startStandaloneProxy` 变更（AppController.cpp:496，R1/R2）

`StandaloneProxyInfo`（AppController.h:28）新增字段：

```cpp
int64_t runtimeHistoryId = -1;   // insertStart 返回的行 id
bool    managed = false;         // false = 弃管（不入表、不监控）
std::string configFileName;      // "standalone_<indexId>[-singbox|-xray].json"
std::string startedAt;           // insertStart 时刻，供 duration 计算
```

**R1 启动前查重**（在 §:499 fetch profile 之后、CreateProcessA 之前插入）：

```
configFileName = "standalone_" + indexId + (useSingBox ? "-singbox" : "-xray") + ".json"
if (ProcessInspector::isProcessRunningWithConfig(configFileName)) {
    wxMessageBox("代理已在运行（检测到相同配置的存活进程）", "提示", wxOK);
    return false;   // 直接退出，不启动新进程
}
```

**R2 启动后连通性验证**（在 CreateProcessA 成功之后、入表之前插入）：

```
// 同步限时探测：经 socks5://127.0.0.1:socksPort 访问 config_.test.url
// 循环重试直到成功或超过探测上限（默认 3 次 × test.timeout_ms，P0 确认可配置）
bool ok = verifySocksProxy(socksPort, config_.test.url, config_.test.timeout_ms);
if (ok) {
    exDao.updateStartupTime(indexId);                       // 既有逻辑（:729）
    historyId = historyDao.insertStart(indexId, now);       // 明细行 + start_count+1
    info.runtimeHistoryId = historyId; info.managed = true; info.running = true;
} else {
    // 弹窗询问：是否关闭代理进程？
    int rc = wxMessageBox("代理已启动但连通性验证失败，是否关闭该进程？",
                          "连通性验证失败", wxYES_NO | wxCANCEL);
    if (rc == wxYES) { TerminateProcess(pi.hProcess); ...; return false; }
    // wxNO / wxCANCEL → 弃管：进程存活但不入表、不监控
    info.managed = false; info.running = false;
    Logger::write("[StandaloneProxy] 弃管 " + indexId + "（不记录历史）", LogLevel::WARN);
}
standaloneProxies_[indexId] = std::move(info);
```

**UI 线程阻塞控制（P0 确认项）**：`startStandaloneProxy` 由 UI 线程调用（ProxyListPanel.cpp:419）。同步探测 ≤ 3×timeout（默认 15s）会阻塞界面。备选方案：
1. 探测下沉到已存在的后台线程（如 NetworkMonitor 线程池）或新起 `std::async`，完成后 `wxQueueEvent` 回 UI 弹窗（**推荐**）；
2. 或接受短暂阻塞（探测次数降低为 1 次 + 提示"正在验证..."）。
两案在 P0 阶段确认。

#### 3.3.2 ProcessExitListener（v1.2 重写，R3/R5 事件驱动）

> **v1.2 修订说明（R5）**：v1.1 原为 Watchdog 周期轮询线程（每 3s 遍历 `WaitForSingleObject(handle,0)`）。评审提出改为接受进程终止系统消息通知后，本方案采用 **Windows 原生事件机制**：进程句柄即同步对象，退出时自动 signaled，**每受管进程一个专用等待线程**阻塞 `WaitForSingleObject(handle, INFINITE)`，进程退出瞬间被唤醒。与轮询相比：零检测延迟、零 CPU 轮询开销、无周期配置项、代码更简单。项目已有同模式先例（`XrayInstance.cpp:204`、`XrayApi.cpp:110`）。

AppController 新增成员：

```cpp
std::atomic<bool>      shutdownRequested_{false};   // 析构置位，等待线程据此决定是否写库
std::vector<std::thread> exitListeners_;            // 每受管进程一个等待线程（joinable 管理）
std::mutex             exitListenersMutex_;         // 保护 exitListeners_ 容器
```

- **启动时机**：R2 连通性验证通过、`insertStart` 入表、`managed=true` 后，为该进程**立即启动一个专用等待线程**：

```
exitListeners_.emplace_back([this, indexId, info = &standaloneProxies_[indexId]] {
    // 阻塞等待系统级进程终止通知（进程句柄 signaled）
    WaitForSingleObject(info->processHandle, INFINITE);
    handleProcessExit(indexId);   // 见下
});
```

- **`handleProcessExit(indexId)` 处理流程**（等待线程上下文，进程确已退出）：

```
// ① 获取退出码（区分正常退出 0 / 被强杀 259 / 崩溃）
GetExitCodeProcess(handle, &exitCode);
std::string endedAt = currentTimeString();          // yyyy-MM-dd HH:mm:ss
int64_t durationMs = now - info.startedAt;

// ② R3 判据保留为一次性 command line 检查（非轮询）：
//    句柄 signaled 只代表"我们跟踪的那个进程实例"退出；
//    同配置 command line 进程是否仍在，决定是否外部重启接管。
bool cliAlive = ProcessInspector::isProcessRunningWithConfig(info->configFileName);

if (info->managed && info->running) {
    if (!cliAlive) {
        // 正常路径：进程实例退出且无同配置进程存活 → finalize 回填历史
        historyDao.finalizeStop(info->runtimeHistoryId, endedAt, exitCode, durationMs);
        info->running = false;
        wxQueueEvent(顶层窗口, new StandaloneProxyEvent(indexId, "", socksPort, false, exitCodeMsg));
    } else {
        // 外部重启接管：用户/外部在进程退出后立即重启了同配置代理（句柄陈旧）
        // 视同旧会话正常退出（exit_code=0）收尾；新进程由 R1 查重放行后走全新流程，
        // 若新进程未经本应用启动则不做新会话记录（P0 确认是否自动接管）
        historyDao.finalizeStop(info->runtimeHistoryId, endedAt, 0, durationMs);
        info->runtimeHistoryId = -1; info->managed = false; info->running = false;
    }
}
// ③ 等待线程自然结束
```

- **析构清理**：`shutdownRequested_ = true` 后对 `exitListeners_` 中**已结束**的线程 `join()`；对**仍阻塞等待**（对应进程还活着）的线程 **detach()** —— Windows 进程退出时 OS 终止全部线程，无悬挂风险；等待线程唤醒后若 `shutdownRequested_` 为 true 则跳过 DB 写库（进行中会话行保持 `ended_at=NULL`，P4 可选兜底补录）。
- **线程安全**：`standaloneProxies_` 仍受 `standaloneMutex_` 保护（`handleProcessExit` 内持锁访问 `info` 字段）；DB 写入复用现有 `db_` 并发模型；`exitListeners_` 容器受 `exitListenersMutex_` 保护。
- **弃管进程**：`managed=false` 不启动等待线程，天然不被监控（见 §3.3.3）。
- **事件投递目标**：`StandaloneProxyEvent` 绑定在 `ProxyListPanel`，AppController 无其引用 → **P0 确认**经 `MainFrame` 顶层窗口转发或改绑定位置。

#### 3.3.3 弃管语义（R2）

- 弃管进程：`managed=false`，**不 insertStart、不 updateStartupTime、不启动 ProcessExitListener 等待线程**，不产生任何历史记录。
- 仍可从 `getStandaloneSocksPort(indexId)` 查询端口（进程存活，用户手动使用）。
- 弃管进程被用户手动关闭后不产生任何记录（无等待线程，天然不监控）。
- 弃管是**一次性状态**：用户下次再次启动同代理时重新走完整流程（查重 → 启动 → 验证 → 受管/弃管）。

#### 3.3.4 `config.json` 新增配置（R4 权重；v1.2 移除周期配置）

```json
{
  "scoring": {
    "weights": { "speed": 20, "stability": 30, "history": 50 }
  }
}
```

- `weights`：三因子权重（默认 20/30/50，见 §3.4）。
- **v1.2 修订（R5）**：v1.1 的 `history_watchdog_interval_ms`（轮询周期）**删除** —— 事件驱动模式不需要周期轮询配置。
- `ConfigReader` 新增解析字段 + 默认值兜底（缺省不报错）。

### 3.4 评分模型（三因子加权，v1.1 R4 权重反转）

新增纯函数引擎 `include/ProxyScorer.h` + `src/ProxyScorer.cpp`（**不触碰数据库**）：

```
总分 = 0.20 × 速度分 + 0.30 × 稳定性分 + 0.50 × 历史服务分
```

> **R4 修订说明**：v1.0 原为 50/30/20（速度主导）。评审意见 R4 反转权重为 **20/30/50**（历史服务主导）——反映"多次启动且稳定提供服务"是评分的第一优先级；速度降为最低权重。

- **速度分**：延迟 EMA（沿用既定方案，Speed 字段保留现状）。
- **稳定性分**：`consecutive_failures`（黑名单阈值 11）+ 测试成功率（沿用既定方案）。
- **历史服务分** `historyScore ∈ [0,100]`：

```
kicker   = min(log2(start_count + 1) / log2(6), 1.0)   // 启动次数因子，5 次满值
health   = (stable + 1) / (start_count + 2)            // 贝叶斯平滑正常退出率（stable = start_count - crash_count）
duration = clamp(avgRuntimeMs / 1_800_000, 0, 1)       // 平均单次运行 ≥30 分钟满分
historyScore = 100 × (0.30×kicker + 0.40×health + 0.30×duration)
```

**冷启动处理**：`start_count == 0` → `historyScore = 50`（中性），历史权重收缩为 0，权重在速度/稳定性间按 **40:60** 重新归一化（= 0.20 : 0.30，去掉历史后的相对比例）。**无历史代理评分与现行行为完全一致，零回归。**

**权重配置**：`config.json` 增 `scoring.weights`（speed/stability/history，默认 **20/30/50**），P4 落地（可选）。

**写回**：评分结果写 `ProfileExItem.score`（INTEGER，0-100），沿用既定 0-100 → 5 星映射（UI 展示）。

### 3.5 UI 展示（P4）

- ProxyListPanel 模型增加列：**启动次数** / **累计运行时长**（或平均时长）/ **健康度**（正常退出率）。
- 星级列显示总分（0-100 → 0~5 星，沿用既定方案）。

---

## 4. 影响分析

### 4.1 收益

- ✅ 评分反映真实使用信任度，而非瞬时测试结果。
- ✅ 明细表可审计、可扩展（未来活跃时段/寿命分布分析）。
- ✅ 顺带修复两处潜在缺陷：`StandaloneProxyEvent` 死代码激活；"进程已死但 UI 仍标记运行中"的僵尸状态收敛。
- ✅ 无历史时行为零变化，冷启动无惩罚。

### 4.2 风险与应对

| 风险 | 应对 |
|------|------|
| ProcessExitListener 等待线程与 UI/测试线程并发写 sqlite | 复用现有 `Database` 并发模型；明细+聚合单事务；失败降级为日志（不阻塞业务） |
| AppController 析构时等待线程未回收 | `shutdownRequested_` 置位 + 已结束线程 `join()` + 仍阻塞线程 `detach()`（进程退出 OS 终止线程，无悬挂）；等待线程唤醒后检查标志跳过写库 |
| 用户先关 UI 再关代理控制台，ended 无法回填 | 可接受（UI 已退出，进行中会话行保持 ended_at=NULL，下次启动同代理时兜底补录——P4 可选） |
| `singbox killProcessByName` 误杀全部同名进程（**既有问题**） | 本次不修复；ProcessExitListener 会将其正确记为 crash 并收敛状态，行为更准确 |
| schema 变更兼容 | 建表/ALTER 幂等迁移，与现有建表逻辑同处 `Database` 初始化路径 |
| ProfileExItem 结构体 3 字段扩展回归 | `fromStmt/createTable/toJson` 同步改，更新既有单测 |
| **R1 进程 command line 读取失败**（权限/PEB 偏移） | `ProcessInspector` 降级为仅按进程名匹配（保守拦截重复启动）；读取失败仅 WARN 不崩溃 |
| **R2 连通性验证阻塞 UI 线程** | 探测下沉后台线程 + `wxQueueEvent` 回 UI 弹窗（P0 定案）；或限 1 次探测短阻塞 |
| **R2 弃管进程游离**（不入表不监控） | 属预期行为；弃管仅影响历史记录，不影响用户手动使用代理；再次启动重新走全流程 |
| **R3/R5 进程被外部重启**（句柄陈旧） | ProcessExitListener finalize 时一次性 command line 检查：同配置进程仍在 → 旧会话收尾 + 不再接管；P0 确认是否自动接管新进程 |

---

## 5. 实施计划

| 阶段 | 内容 | 预估 |
|------|------|------|
| **P0 前置确认** | ① `profileexitem` 实际表名与迁移机制 ② `db_` 并发锁模型 ③ `StandaloneProxyEvent` 投递路径 ④ `UrlFetcher` SOCKS5 代理支持 ⑤ R2 探测异步化方案（后台线程 vs 短阻塞） ⑥ R3/R5 外部重启接管语义（是否自动接管新进程） ⑦ `ProcessInspector` command line 读取实现（PEB 方案） | 1 天 |
| **P1 数据层** | `ProxyRuntimeHistory.h/.cpp`；`ProfileExItem` +3 聚合列；幂等迁移；H 层测试 | 1–2 天 |
| **P2 采集层** | `StandaloneProxyInfo` 扩展（runtimeHistoryId/managed/configFileName/startedAt）；R1 查重；R2 连通性验证 + 弃管；**ProcessExitListener**（每受管进程专用等待线程 + finalize 一次性 command line 检查 + 析构清理）；事件投递；H 层测试 | 2–3 天 |
| **P3 评分引擎** | `ProxyScorer` 三因子（20/30/50）+ 贝叶斯平滑 + 权重归一化；H 层测试 | 1–2 天 |
| **P4 集成展示** | `score` 写回；ProxyListPanel 历史列 + 5 星；`config.json` `scoring.weights`（v1.2 无周期配置项） | 1 天 |

每阶段验证：`cmake --build build --parallel 8 && ctest -V`；P2 额外 ASAN 构建验证 ProcessExitListener 等待线程/析构竞态。

---

## 6. 测试计划（Google Test）

| 测试 | 覆盖 |
|------|------|
| `RuntimeHistoryDAOTest.InsertFinalize` | 完整生命周期：insert → finalize，明细行字段正确、聚合列同步 |
| `RuntimeHistoryDAOTest.InsertThenFinalizeTwice` | 同一 historyId 二次 finalize 幂等（防 ProcessExitListener 与显式停止竞争） |
| `RuntimeHistoryDAOTest.Aggregation` | 多次启动/退出后 start_count / total_runtime_ms / crash_count 累加正确 |
| `RuntimeHistoryDAOTest.Transaction` | 明细+聚合同事务，中途失败回滚一致 |
| `ProcessExitListenerTest.MockProcessExit` | 启动 mock 进程 → 结束（事件通知唤醒）→ 断言历史行回填 + `StandaloneProxyEvent` 投递 |
| `ProcessExitListenerTest.MockProcessKill` | 强杀场景 → exit_code=259 → crash_count+1 |
| `ProcessExitListenerTest.MockProcessRestart` | 句柄信号化但 command line 仍有同配置进程 → 旧会话 finalize + 不再接管（R3/R5 接管语义） |
| `ProcessExitListenerTest.ShutdownNoDbWrite` | `shutdownRequested_` 置位后进程退出 → 等待线程跳过写库、不悬挂（析构清理） |
| `ProcessInspectorTest.FindByConfig` | 枚举 mock 进程（带指定 command line 参数启动）→ 匹配配置文件名命中（R1） |
| `ProcessInspectorTest.CommandLineReadFailure` | PEB 读取失败降级为进程名匹配（保守拦截） |
| `ConnectivityVerifyTest.SocksProxyOk` | 经 socks 端口访问 test.url 成功 → 判定连通（R2） |
| `ConnectivityVerifyTest.SocksProxyFail` | 探测失败 → 返回不连通（R2 弹窗分支） |
| `ProxyScorerTest.NoHistory` | start_count=0 → 中性 50 分、40:60 权重归一化后与现行一致 |
| `ProxyScorerTest.MixedHistory` | 多次正常启动长时长 → 高分；高崩溃率 → 低分 |
| `ProxyScorerTest.WeightNormalization` | 无历史时 20/30 权重归一化精确性 |

---

## 7. 明确不做（范围约束）

- ❌ 不新增显式"停止代理"按钮（D3 已选 ProcessExitListener 事件驱动兜底）。
- ❌ 不记录批量测试临时 Xray 进程（D1 已限定 StandaloneProxy）。
- ❌ 不做历史明细的 UI 浏览页。
- ❌ 不修复 `singbox killProcessByName` 误杀同名进程的既有问题（另立任务）。
- ❌ `Speed` 字段维持现状（不在本需求范围）。
- ❌ 弃管进程（R2 弹窗选"不关闭"）不产生任何历史记录、不纳入评分 —— 属预期行为。
