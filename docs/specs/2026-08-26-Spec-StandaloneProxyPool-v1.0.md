# Spec — StandaloneProxyPool（单进程代理池 + 动态注入 + 周期健康评估）

- 文档类型: Spec（功能设计规格）
- 模块: StandaloneProxyPool
- 版本: v1.0
- 日期: 2026-08-26
- 状态: Draft（已吸收 2026-08-26 评审报告，有条件通过：进入实现，合入前须补齐 AddHandler/RemoveHandler 源码验证与三项集成测试：selector 缓存失效 / Observatory 动态发现 / Remove 已有连接生命周期）
- 关联需求: 用户提出的"代理生命周期整体管理方案"；参考 `docs/CONTEXT.md`、2026-08-25 / 2026-08-26 两份 standalone 代理 bugfix 文档、`docs/plans/project-plans-tracker.md`
- 实现约束: C++17；**全栈代码禁用 `auto` 类型推导**（AGENTS.md §1）；文档先行（本 Spec 先于代码）；TDD（Google Test，置于 `tests/`）

---

## 1. 背景与目标

当前"单开代理"（standalone proxy）为每个 `indexId` 启动一个**独立 Xray 进程**（`AppController::startStandaloneProxy`），并用 `ProcessExitListener`（R6 自动接管）监控退出、写 `proxy_runtime_history`。痛点：

1. 多进程、多端口难统一监控与切换。
2. 进程崩溃后 stderr 易丢失（已在 2026-08-25 bugfix 用 `StandaloneProxyLogForwarder` + `xray_stderr_<port>.log` 缓解，但仍为每进程一份）。
3. 无法在**运行时**把多个代理聚合成一个出口并按健康度择优（用户需求："能否即时反馈挂掉的代理"）。

目标：用**一个常驻 Xray 进程**承载一个"代理池"，支持：

- 运行时**动态注入/移除**成员代理（不重启、不写死在启动配置里）。
- 基于 Xray **Balancer + Observatory** 的**周期健康评估**（alive / 延迟 / 连续失败）。
- 通过 `a/b/c` 三项**相互独立**的开关做"健康反馈 / 自动剔除 / 自动择优"。
- 监控面板里**手工删除**不合格代理。
- `ProxyListPanel` 双击 = 注入到池（空池时懒启动）。

---

## 2. 范围

### 2.1 Phase 1（本次实现）
- 空池启动（不预置成员）。
- `ProxyListPanel` 双击某个代理 → 注入到池（池未运行则懒启动）。
- 监控面板展示池内成员（IndexId / 出口端口 / 健康状态 / 延迟），并支持**手工删除**。
- 周期健康评估 + 三项开关（reportHealth / autoPruneDead / autoOptimize）全部生效。

### 2.2 后续（本 Spec 预留接口，不在 Phase 1 实现）
- 多选代理 → "作为一组启动"（批量成池）。
- 池配置的 UI 编辑（Phase 1 仅 config.json 静态配置）。

---

## 3. 总体架构

```
                         ┌─────────────────────────────────────────────┐
                         │  AppController (owner)                        │
                         │   - pool_ : StandaloneProxyPool               │
                         │   - injectProxyToPool(indexId)                │
                         │   - removePoolMember(indexId)                 │
                         └───────────────┬─────────────────────────────┘
                                         │ owns / drives
                                         ▼
                         ┌─────────────────────────────────────────────┐
                         │  StandaloneProxyPool  (new class)             │
                         │   - XrayInstance instance_  (独立进程)         │
                         │   - memberMap_ : map<indexId, MemberState>     │
                         │   - ProxyHealthEvaluator evaluator_           │
                         │   - inject(indexId)/remove(indexId)           │
                         │   - getMembers() → vector<PoolMemberRow>       │
                         └───────┬───────────────────┬───────────────────┘
                                 │                   │
              gRPC Handler API   │    周期查询        │ gRPC Routing/Observatory
                                 ▼                   ▼
                  ┌──────────────────────┐   ┌──────────────────────────┐
                  │ XrayApi              │   │ XrayInstance (1 进程)      │
                  │ addOutbound/         │   │ 控制面(静态):              │
                  │ removeOutbound       │   │  - api dokodemo           │
                  │ getOutboundStatus /  │   │  - socks mixed 入站        │
                  │ getBalancerInfo      │   │  - 出站[direct, balancer]  │
                  └──────────────────────┘   │  - balancer(selector)     │
                                             │  - observatory             │
                                             │  - routing.balancerTag     │
                                             │ 成员(动态,运行时注入):      │
                                             │  - px-<indexId>            │
                                             └──────────────────────────┘
```

**关键决策**
- **独立进程**：`StandaloneProxyPool` 拥有**自己**的 `XrayInstance`，与 `ProxyFinder` 当前管理的"单出口 current xray"（`ProxyFinder::injectProxyToXray`，标签 `proxy`）互不干扰。原因：避免标签冲突（`proxy` vs `px-*`）、生命周期耦合、以及把"找最优代理当系统代理"和"多代理成池监控"两件事混在一起。
- **控制面静态、成员动态**：balancer / selector / observatory / routing 写在基础配置里（避免新增 `balancing` protobuf 编码器）；成员出站 `px-<indexId>` 在运行时通过 `XrayApi::addOutbound` 注入，Observatory 的 `SubjectSelector:["px-"]` 自动把它们纳入探测。
- **复用**：`XrayInstance`（Job Object / stderr 落盘 / 退出检测）、`XrayApi`（gRPC 调用，与 `ProxyFinder::injectProxyToXray` 同款 `addOutbound`）、`ConfigGenerator::generateConfig(profile)`（产出成员出站 JSON）、`ProcessExitListener` R6 自动接管（崩溃后重拉并重新注入全部成员）。

---

## 4. 配置设计（`config.json` → `config::AppConfig`）

在 `config::AppConfig` 新增 `StandalonePoolConfig standalonePool;`，反序列化键 `standalonePool`：

```json
"standalonePool": {
  "enabled": true,
  "mode": "pool",                       // "pool" | "select"
  "socksPort": 10808,                   // 池出口 SOCKS 入站端口
  "apiPort": 10809,                     // 该进程 gRPC api 端口（区别于其它进程）
  "balancerStrategy": "leastPing",      // 仅 mode=pool 生效
  "observatory": {
    "type": "burst",                    // "burst" | "default"
    "destination": "https://connectivitycheck.gstatic.com/generate_204",
    "intervalSec": 10,
    "samplingCount": 10,
    "timeoutSec": 5
  },
  "evaluate": {
    "intervalSec": 10,
    "reportHealth": true,               // a: 周期把健康状态推到 UI（含已死代理即时反馈）
    "autoPruneDead": false,             // b: 连续失败达阈值自动移除
    "pruneFailStreak": 3,
    "autoOptimize": false               // c: 自动择优（select: OverrideBalancerTarget / pool: 降权最差）
  }
}
```

`a/b/c` 为**独立布尔**，可任意组合（用户明确要求非互斥）。

---

## 5. 模块设计

### 5.1 `StandaloneProxyPool`（新增类，头文件 `include/StandaloneProxyPool.h`，实现 `src/StandaloneProxyPool.cpp`）

职责：拥有 `XrayInstance`、成员表、健康评估器；对外暴露生命周期与查询。

成员：
```cpp
class StandaloneProxyPool {
public:
    StandaloneProxyPool(sqlite3* db, const config::StandalonePoolConfig& cfg);
    ~StandaloneProxyPool();

    bool start();                       // 启动 XrayInstance（写控制面配置）并起 evaluator 定时器
    void stop();                        // 停 evaluator + 停 XrayInstance + 清 memberMap_

    // 注入/移除成员；返回是否成功（含原因写入 Logger）
    bool inject(const std::string& indexId);
    bool remove(const std::string& indexId);

    bool isRunning() const;
    std::vector<PoolMemberRow> getMembers() const;   // 供监控面板

    // 供 ProcessExitListener 回调：进程退出后由 AppController 调 reloadFromMemberMap()
    void relaunchAndReinject();

private:
    void doInject(const std::string& indexId);       // ConfigGenerator::generateConfig + XrayApi::addOutbound("px-<id>")
    void doRemove(const std::string& indexId);       // 两阶段：REMOVE_REQUESTED→removeOutbound(退出候选)→DRAINING→in-flight 结束后 REMOVED（见 §5.8）
    void onEvaluatorTick();                          // 由 ProxyHealthEvaluator 回调
    void applyPolicy(const std::string& indexId, const HealthRecord& rec);

    sqlite3* db_;
    config::StandalonePoolConfig cfg_;
    xray::XrayInstance instance_;                    // 独立进程
    std::unordered_map<std::string, MemberState> memberMap_;
    mutable std::mutex mapMutex_;
    ProxyHealthEvaluator evaluator_;
    std::string socksTag_ = "balancer-out";          // balancer/selector 出口标签
};
```

`PoolMemberRow`（供 UI，结构对齐 `StandaloneMonitorRow` 风格）：
```cpp
struct PoolMemberRow {
    std::string indexId;
    std::string host;          // ProfileItem.Address
    int socksPort = 0;         // 池统一出口端口
    bool alive = false;
    long long delayMs = -1;    // -1 = 未知
    int failStreak = 0;
    int score = 0;
    std::string lastError;     // Observatory LastErrorReason
    MemberLifecycleState state = MemberLifecycleState::INJECTING;
};
```

`MemberState`（内部）：在 `PoolMemberRow` 基础上持有 `outboundTag = "px-<indexId>"`、注入时间戳、连续失败计数，以及评审新增的 `bool draining = false;`（已进入摘除/排空阶段）与 `bool removeRequested = false;`（已发起移除请求，等待 in-flight 连接自然结束后再销毁 Handler）。Xray gRPC 不暴露逐连接计数，故 `draining` 为"已停止新选择 + 等待 Xray 回收 in-flight"的最佳近似标志（详见 §5.8）。

### 5.2 `ProxyHealthEvaluator`（新增类，可置于 `StandaloneProxyPool.cpp` 内或独立文件）

职责：定时调用 `XrayApi::getOutboundStatusDirect()` 拉取 `px-*` 各成员 `Alive/Delay/LastErrorReason/LastSeenTime`，更新 `MemberState`，按 `evaluate` 配置应用策略，并通过回调/事件把结果交给 `AppController` → UI。

- 定时器周期 = `evaluate.intervalSec`（建议内部以 `std::thread` + `std::condition_variable` 实现，避免在 UI 线程；与现有 `getRunningDurationsAsync` 模式一致）。
- 评分（建议，可在实现时微调）：`score = alive ? max(0, 100 - delayMs/5) : 0; score -= failStreak * 20;`
- 策略：
  - `reportHealth`：每次 tick 向 UI 派发 `EVT_PROXY_HEALTH_UPDATED`（携带 `vector<PoolMemberRow>`），**已死代理即时可见**（Observatory 已标记 `Alive=false`）。
  - `autoPruneDead`：`failStreak >= pruneFailStreak` 时调用 `StandaloneProxyPool::remove(indexId)`，并派发 `EVT_PROXY_AUTO_REMOVED`。
  - `autoOptimize`（**Phase 1 仅做记录 / 降权，不调用 `OverrideBalancerTarget`、不做 remove+add 重组**——避免连接 churn，评审 §10 明确要求）：
    - 仅依据 `score` 在 `EVT_PROXY_HEALTH_UPDATED` 中标注"当前最优/最差"，写入 `Logger`（REPORT 级）供 UI 展示；`mode == "select"` 与 `mode == "pool"` 行为一致，均不直接改写 Balancer 成员集合。
    - `OverrideBalancerTarget` / 降权重连作为**后续增强**，待 selector 缓存与连接生命周期源码验证通过后再评估（见 §5.8、§9.1）。

### 5.3 `ConfigGenerator::buildPoolConfig`（新增方法，`include/ConfigGenerator.h` / `src/ConfigGenerator.cpp`）

产出**控制面**基础配置（不含成员）：
- `inbounds`: `api`(dokodemo → `apiPort`，services 含 `Handler/Logger/Stats/Routing/Observatory`) + `socks`(`mixed`, `socksPort`)。
- `outbounds`:
  - `direct`（`protocol:"freedom"`）
  - `balancer-out`：`mode == "pool"` → `protocol:"balancing"`, `balancer:{tag:"balancer-out", selector:["px-"], strategy:"leastPing"}`；`mode == "select"` → `protocol:"selector"`, `selector:["px-"]`。
- `observatory`: `{ subjectSelector:["px-"], probeURL/destination, probeInterval/intervalSec, fetchInterval }`（默认 Observatory 配 `burst` 时改用 BurstObserver 字段）。
- `routing`: 一条规则把全部流量指到 `balancerTag:"balancer-out"`（与现有 `createConfigFile` 的 routing 写法对齐）。
- 复用 `ConfigGenerator` 已有的 JSON 拼装辅助函数；不新增 protobuf 编码。

### 5.4 `XrayInstance` 扩展（`src/XrayInstance.cpp`）

- 在启动 api 的 services 列表（约 `XrayInstance.cpp:295`，现为 `["HandlerService","LoggerService","StatsService"]`）加入 `"RoutingService"` 与 `"ObservatoryService"`，使健康查询可用。
- 新增一个 `Pool` 模式开关（或复用现有构造参数），写出的配置文件由 `ConfigGenerator::buildPoolConfig` 提供，而非默认单代理模板。
- 复用现有 `createConfigFile` / 启动 / stderr 落盘（`xray_stderr_<port>.log`）/ `ProcessExitListener` 注册逻辑，无需重写。

### 5.5 `XrayApi` 扩展（`include/XrayApi.h` / `src/XrayApi.cpp`）

- 已有 `addOutbound` / `removeOutbound`（被 `ProxyFinder::injectProxyToXray` 使用，标签 `proxy`）——**直接复用**，池成员用标签 `px-<indexId>`。
- 新增：
  - `bool getOutboundStatusDirect(std::vector<OutboundStatus>& out)` —— 调 `ObservatoryService.GetOutboundStatus`，解析 `OutboundTag/Alive/Delay/LastErrorReason/LastSeenTime`。
  - `bool getBalancerInfoDirect(std::string& selectedTag, std::string& overrideTag)` —— 调 `RoutingService.GetBalancerInfo`（select 模式择优时用）。
  - 复用现有 gRPC 传输（HPACK / HTTP2）与 `validateSplitHTTPSettings` 守卫；不对 `balancing`/`selector` 出站本身做 protobuf 编码（它们来自静态配置）。
  - **约束（评审 §10）**：`addOutbound` / `removeOutbound` **绝不修改 balancer / routing 配置**；Balancer 通过 `selector:["px-"]` 经 Outbound Manager 的 HandlerSelector 自动发现成员，无需 reload。`addOutbound` 成功后下一次 `selector=px-` 选择必须能看到新成员；`removeOutbound` 后新选择不得再命中该成员。**若 Xray 存在 HandlerSelector 候选缓存，必须在注入/移除后使其失效**（实现阶段须源码验证，见 §5.8、§9.1）。

### 5.6 UI 变更

1. **`ProxyListPanel` 双击注入**（关键改动）
   - 现状：`ProxyListPanel.cpp:99` `listCtrl_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, …)` → 调用 `onStartProxy(dummy)`（即起一个独立 standalone 进程）。
   - 改为：双击 → `AppController::injectProxyToPool(indexId)`。池未运行则 `pool_.start()`（懒启动）。保留右键菜单里的"单开代理"（`onStartProxy`）不动，仅改双击语义。
   - 注意 `SubscriptionPanel.cpp:21` 的注释：该 data view 的 `wxEVT_DATAVIEW_ITEM_ACTIVATED` 在双击时不触发——需确认 `ProxyListPanel` 用的是哪种控件；若同样不触发，则改为绑定 `wxEVT_DATAVIEW_ITEM_CONTEXT_MENU` 之外的双击事件，或在 `onStartProxy` 内按配置分流。实现时以实测为准，但**结论**是：双击 = 注入池。

2. **监控面板手工删除**（需求②/③）
   - 扩展 `StandaloneFloatingWidget`（已有的"监控浮窗"，`StandaloneFloatingWidget.cpp:101` `wxEVT_LIST_ITEM_ACTIVATED` → `onItemActivated`），新增"代理池"分区：列出 `pool_.getMembers()`，每行带 **删除** 按钮 → `AppController::removePoolMember(indexId)` → `pool_.remove(indexId)`。
   - 或新增独立 `ProxyPoolMonitorPanel`（实现时二选一，默认扩展现有浮窗，降低 UI 面片数量）。
   - 删除后派发刷新事件，列表即时更新。

3. **事件（`src/ui/Events.h`）**
   - `EVT_PROXY_HEALTH_UPDATED`（携带 `vector<PoolMemberRow>`，供监控面板周期刷新）。
   - `EVT_PROXY_AUTO_REMOVED`（携带 `indexId`，供 UI 提示"已自动剔除"）。

### 5.7 `AppController` 集成（`src/ui/AppController.h` / `.cpp`）

- 新增成员 `StandaloneProxyPool pool_;`（或 `std::unique_ptr<StandaloneProxyPool> pool_;`）。
- 新增：
  - `bool injectProxyToPool(const std::string& indexId);` → `pool_.inject(indexId)`（懒启动）。
  - `bool removePoolMember(const std::string& indexId);` → `pool_.remove(indexId)`。
  - `std::vector<PoolMemberRow> getPoolMembers() const;` → `pool_.getMembers()`。
- `ProcessExitListener` 回调中：若退出的是池进程，调 `pool_.relaunchAndReinject()`（R6 模式：重拉进程并把 `memberMap_` 全部重新 `addOutbound`）。
- 与现有 `standaloneProxies_` / `startStandaloneProxy` **并存**，不删除旧单开能力（旧路径仍可用于"临时单开某个代理"）。

---

### 5.8 动态 Outbound 生命周期约束（评审补充）

本节能将评审报告的运行时语义落为硬约束，实现与测试均须遵守。

**单一事实来源**：Xray Core 的 **Outbound Manager** 是 Outbound Handler 的权威注册表；Balancer **不保存独立成员副本**，仅在选择时经 HandlerSelector 按 `selector`（如 `["px-"]`）获取候选。职责边界：

- Outbound Manager：Handler 注册 / 查询 / 生命周期。
- HandlerSelector：按 selector 取当前候选 tag。
- Balancer：依据 selector + strategy 选节点。
- Routing：决定何时用哪个 balancer。
- StandaloneProxyPool：业务层成员生命周期、健康策略、UI 状态。

**AddOutbound（注入）调用链**：`inject(indexId)` → `ConfigGenerator::generateConfig(profile)` → `XrayApi::addOutbound(json,"px-<id>")` → Xray `HandlerService` → **Outbound Manager.AddHandler(handler)** → HandlerSelector.Select(["px-"]) → Balancer 获得最新候选 → strategy（如 leastPing）选节点。逻辑保证：Handler 创建成功 → 注册到 Manager → 启动 Handler → Selector/相关缓存更新。**成功后无需重建 Routing 或 Balancer。**

**RemoveOutbound（移除）两阶段（评审 §5）**：直接 `RemoveHandler` 并立即 `Close` 会打断已有连接，违背"已有连接不受影响"。故 `remove(indexId)` 拆为：

1. **REMOVE_REQUESTED**：调 `XrayApi::removeOutbound("px-<id>")` 使其从 selector 候选集合摘除（新连接不再选择该成员）。
2. **DRAINING**：保留已有 Handler / 连接，标记 `memberMap_[id].draining = true`，等待 in-flight 连接自然结束。
3. **销毁**：in-flight 结束后 `Close Handler` 并将状态置 `REMOVED_MANUAL` / `PRUNED_AUTO`，从 `memberMap_` 删除。

> 限制：Xray gRPC 不暴露逐连接计数，"DRAINING 结束"以"观测到该成员不再有活跃选择 + 超时兜底"近似实现（如 DRAINING 超过 `drainTimeoutSec` 仍强制 Close）。真·逐连接 drain 非 API 能力，记录为已知边界。

**Selector 候选缓存必须失效（评审 §8，合入前验收项）**：若 HandlerSelector / Manager 存在候选 tag 缓存，`AddHandler` / `RemoveHandler` 后必须让其失效，否则新成员不进 Balancer、被删成员仍被命中。验收条件：`addOutbound` 成功后**无需重启 / 无需重建 Balancer**，下一次 `selector=px-` 选择可见新成员；`removeOutbound` 后新选择不再命中。实现阶段须读 Xray Core `Outbound Manager` / `HandlerSelector` 源码确认缓存语义，并在 §9.1 用例中实证。

**Observatory / leastPing 动态发现（评审 §9）**：Observatory 用 `SubjectSelector:["px-"]`，动态成员进入 Balancer 候选 ≠ 进入 Observatory 健康数据集，二者相关但不相同。须验证：①新增成员是否被 Observatory 及时发现并入探测；②健康数据未建立时 leastPing 的临时行为（实现时定性记录，不假设"立即可用"）；③删除成员后 Observatory 停止 / 忽略该成员。

**autoOptimize（Phase 1 记录优先）**：见 §5.2——Phase 1 不调用 `OverrideBalancerTarget`、不做 remove+add 重组，仅记录/降权，避免连接 churn。

## 6. 代理生命周期状态机

### 6.1 池（Pool）状态
`IDLE → STARTING → RUNNING → (STOPPING → IDLE)`，`RUNNING → DEAD → (RELAUNCH → STARTING)`（崩溃接管）。

### 6.2 成员（Member）状态
```
INJECTING ──成功──▶ ACTIVE ──Observatory alive=false──▶ UNHEALTHY (健康态，与生命周期正交)
   │                   │                                     │
   │失败(配置/崩溃)     │ autoPruneDead 达阈值 / 手工删除       │ 恢复 alive=true ──▶ ACTIVE
   ▼                   ▼                                     │
FAILED_INJECT      REMOVE_REQUESTED ──▶ DRAINING ──▶ REMOVED_MANUAL / PRUNED_AUTO
                  (停止新选择)     (等待 in-flight 结束)   (销毁 Handler + 清 memberMap_)
```
状态枚举 `MemberLifecycleState { INJECTING, ACTIVE, UNHEALTHY, FAILED_INJECT, REMOVE_REQUESTED, DRAINING, PRUNED_AUTO, REMOVED_MANUAL }`。

---

## 7. 错误处理与边界条件（CRITICAL）

- **注入失败**：`ConfigGenerator::generateConfig` 抛 `checkRequired()` 异常 / `XrayApi::addOutbound` 返回 false → 标记 `FAILED_INJECT`，写 `Logger::ERR`（复用 `XrayApi::getLastError()`），**不**把该成员计入 `ACTIVE`。
- **Xray 进程不存在 / 路径错**：`start()` 失败 → 池保持 `IDLE`，UI 提示；与现有 standalone 启动失败处理对齐。
- **gRPC 不可用（api 端口被占 / xray 未就绪）**：`addOutbound` / `getOutboundStatusDirect` 失败 → 退避重试 N 次，仍失败则记日志并跳过本次 tick；不崩溃主线程。
- **崩溃接管（R6）**：`ProcessExitListener` 检测到池进程退出 → `relaunchAndReinject()`：重启进程，遍历 `memberMap_` 重新 `addOutbound` 所有 `px-*`；UI 通过 `EVT_PROXY_HEALTH_UPDATED` 恢复展示。
- **重复注入**：`inject(indexId)` 时若 `memberMap_` 已有该 `indexId`（ACTIVE/INJECTING），直接返回 true（幂等），不重复 `addOutbound`。
- **配置冲突**：`mode` 非法值 → 默认 `pool`；`observatory.intervalSec<=0` → 默认 10。
- **无 `auto`**：所有 C++17 代码显式写出类型（AGENTS.md §1 硬约束）。

---

## 8. 时序（关键流程）

### 8.1 注入流程（双击）
`ProxyListPanel 双击` → `AppController::injectProxyToPool(id)`
→ `pool_.isRunning()?` 否 → `pool_.start()`（写控制面配置 + 起 XrayInstance + 起 evaluator）
→ `pool_.inject(id)` → `doInject` → `ConfigGenerator::generateConfig(profile)` → `XrayApi::addOutbound(json,"px-<id>")`
→ `memberMap_[id].state = ACTIVE` → `EVT_PROXY_HEALTH_UPDATED` 刷新监控面板。

### 8.2 评估流程（周期）
`evaluator tick` → `XrayApi::getOutboundStatusDirect()` → 对每个 `px-<id>` 更新 `alive/delay/failStreak/score`
→ `applyPolicy`：`reportHealth`→派发更新；`autoPruneDead`达阈值→`remove`+`EVT_PROXY_AUTO_REMOVED`；`autoOptimize`→`OverrideBalancerTarget`/`降权`。

### 8.3 手工删除流程
`监控面板 删除按钮` → `AppController::removePoolMember(id)` → `pool_.remove(id)`（先置 `REMOVE_REQUESTED` 并调 `XrayApi::removeOutbound("px-<id>")` 使其退出 selector 候选、停止新连接选择，再置 `DRAINING` 等待 in-flight 自然结束后销毁 Handler）→ `memberMap_` 移除（state=REMOVED_MANUAL）→ 列表刷新。详见 §5.8。

---

## 9. 测试策略（TDD，Google Test，置于 `tests/`）

新增 `tests/TestStandaloneProxyPool.cpp`（及必要 fixture）：

1. `InjectRemove_Basic`：构造 `StandaloneProxyPool`（用 mock/fake XrayApi 或本地临时 xray），`inject` 后 `getMembers()` 含该成员；`remove` 后不含。
2. `Inject_Idempotent`：同 `indexId` 连续 `inject` 两次，仅一次 `addOutbound`（可用计数断言）。
3. `Evaluator_Scoring`：喂入 `OutboundStatus{alive=true,delay=50}` → `score` 在预期区间；`alive=false` → score=0 且 `failStreak++`。
4. `AutoPrune_Threshold`：`autoPruneDead=true, pruneFailStreak=3`，连续 3 次 `alive=false` → 自动 `remove` 并触发 `EVT_PROXY_AUTO_REMOVED`。
5. `Relaunch_Reinject`：模拟进程退出 → `relaunchAndReinject` 后 `memberMap_` 全部重新注入。
6. `Config_PoolVsSelect`：`buildPoolConfig` 在 `mode=pool` 产出 `balancing`，`mode=select` 产出 `selector`；含 `observatory` 与 `routing.balancerTag`。
7. `NoAuto_Compile`：静态断言/代码评审确认新代码无 `auto`。

> 注：涉及真实 xray 进程的用例可用 `bin/test_config.json` + `test/guindb.db` 的精简库（`test/guiNDB_empty.db`）做集成；纯逻辑（评分/策略/状态机）用单元测试隔离，不依赖网络。

---

### 9.1 评审验收矩阵（必须覆盖）

来自评审报告 §11，合入前须全绿（单元 / 集成均可，但 selector 缓存、Observatory 发现、Remove 连接生命周期三项须真实 Xray 进程实证）：

| 场景 | 操作 | 预期 | 重点检查 |
|------|------|------|----------|
| 动态加入 | 运行中 `add px-001` | 无需重启即进入 `selector=px-` | Manager 注册、Selector 缓存失效 |
| 加入后选择 | `add` 后立即产生新连接 | `px-001` 可被 Balancer 选择 | Balancer 无需 reload |
| 动态删除 | `remove px-001` | 新连接不再选择 | 候选集合更新 |
| 删除时已有连接 | 先建连再 `remove` | 已有连接继续，随后自然结束 | Handler 生命周期（DRAINING） |
| Observatory | `add` 新成员 | 新成员进入探测集合 | SubjectSelector 动态发现 |
| 重启恢复 | Xray 崩溃/重启 | `memberMap_` 成员全部重注入 | ProcessExitListener / Relaunch |
| 重复注入 | 重复 `add` 同一 indexId | 拒绝或幂等，不产生重复 tag | Tag 唯一性 |
| 并发操作 | `add`/`remove` 与 evaluator 并发 | 无竞态、无死锁 | `mapMutex_` / API 串行化 |

**TDD 覆盖（已实现，`tests/TestStandaloneProxyPool.cpp`，`ctest -R StandaloneProxyPoolTest` 全绿）：**
- `buildPoolConfig` 控制面 JSON 结构（离线校验）：`api.services` 含 Handler/Routing/Observatory；`inbounds` 数组含 `socks-in`（mixed，noauth+udp）；`outbounds` 含 `direct`(freedom) + `balancer-out`(protocol=balancing, selector=["px-"], strategy.type=leastPing)；`observatory.subjectSelector=["px-"]`；`routing.balancerTag="balancer-out"` + field 规则指向 balancer-out。**注意：此校验仅证明 JSON 形态正确，不代表 Xray 运行期接受 prefreeze selector 语义——三项运行实证仍为合入闸门（见 §11）。
- `StandalonePoolConfigParser`：缺省保持、全字段覆盖、非法枚举值被忽略。
- `StandaloneProxyPool` 构造边界（离线）：未 start 时 `isRunning()==false`、`injectMember` 返回 false、`removeMember(未知)` 返回 false、`getMembers()` 空、各 setter 与 `stop()` 安全无异常。

## 10. 风险与权衡

- **Observatory 探测延迟**：默认 10s，死代理最多 ~10s 后才在 UI 标记为死。"即时反馈"指**反馈通道即时**（一旦 Observatory 标记即推送），而非探测频率本身；如需更快，调低 `observatory.intervalSec`（代价：更多探测流量）。
- **单进程风险**：池进程崩溃会短暂影响全部成员；用 R6 自动接管 + 重注入缓解。
- **`addOutbound` 对 `balancing` 出站**：成员是普通 outbound，balancer 经 `selector` 自动纳入，无需编码 `balancing` 结构——这是本方案规避新增 protobuf 编码的核心。
- **与 `ProxyFinder::injectProxyToXray` 关系**：两者共存，互不影响（不同进程、不同标签）。命名上池用 `inject/removeMember`，避免与 `injectProxyToXray` 混淆。

---

## 11. 实现步骤（建议顺序）

1. **Spec 评审 + 登记**：本文件评审通过，写入 `docs/INDEX.md`（§7.5 规范化设计）。
2. `config::AppConfig` 增加 `StandalonePoolConfig` + JSON 反序列化（`ConfigReader`）。
3. `XrayInstance`：api services 增加 `Routing/Observatory`；新增 Pool 模式写配置入口。
4. `XrayApi`：增加 `getOutboundStatusDirect` / `getBalancerInfoDirect`。
5. `ConfigGenerator::buildPoolConfig`。
6. `StandaloneProxyPool` + `ProxyHealthEvaluator`（含状态机、策略）。
7. `AppController` 集成（`injectProxyToPool` / `removePoolMember` / `getPoolMembers` / 退出回调重注入）。
8. UI：`ProxyListPanel` 双击改注入；`StandaloneFloatingWidget` 增加池成员列表 + 删除；`Events.h` 新增事件。
9. TDD 用例（第 9 节）全绿。
10. 用 `bin/test_config.json` + 精简库做手动集成验收（双击注入、监控面板删除、崩溃重注入）。
11. **合入前源码验证闸门（评审 §13）**：
    - **①Xray Core `Outbound Manager`/`HandlerSelector` Add/RemoveHandler 源码验证：✅ 已于 2026-08-26 完成**（证据 `docs/bugfix/2026-08-26-Verify-XraySelectorCache-Observatory-Discovery-v1.0.md`）：`AddHandler`/`RemoveHandler` 均整体重置 `tagsCache`（`app/proxyman/outbound/outbound.go:107`/`:138`），故 `addOutbound`/`removeOutbound` 后 `selector=["px-"]` 前缀缓存必然失效；Observatory `background()` 每轮重新 `Select(subjectSelector)`（`app/observatory/observer.go:71`）动态发现成员；`RemoveHandler` 删除 handler 且存量连接自然结束，与本池两阶段 DRAINING 兼容。**源码层面无合入 blocker。**
    - **②三项集成测试实证（selector 缓存失效 / Observatory 动态发现 / Remove DRAINING）**：⏳ 待真实 Xray 进程 + 显示器人工运行（无头环境无法跑 GUI/网络）。已就绪：`bin/config.json` 与 `bin/test_config.json` 均写入 `standalone_pool.enabled=true`（端口 10809/10810 与 10829/10830），`test_config.json` 另含 `proxy.xray_executable` 指向 `E:/eclipse_workspace/Xray-core/xray.exe`；按 Step 10 跑双击注入 → 监控面板删除 → 崩溃重注入即可闭环。
