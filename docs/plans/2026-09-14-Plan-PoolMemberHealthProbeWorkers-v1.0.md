# 2026-09-14 Plan: 池成员健康探测接入常驻 Xray 探针池（probeWorkers 配置化）

- 版本：v1.0
- 日期：2026-09-14
- 状态：待执行
- 关联 Spec：`docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`
- 关联 Note：`docs/notes/2026-09-09-Note-ProxyPoolMonitorUnify-v1.0.md`

---

## 1. Goal

解决 `StandaloneProxyPool` 中**非直连协议成员（configtype ≠ 4/10，如 VMess/VLESS/Trojan/Shadowsocks）无法被健康探测**的问题：

- 现状：`ProxyHealthEvaluator::probe()` 对非 4/10 成员直接返回 `tested=false`（lastError=`protocol requires xray-side probe (not directly probeable)`），导致池成员 `probed` 永远为 false，健康状态不可见。
- 目标：新建**常驻 Xray 探针池**（`ProxyProbePool`），复用 Xray 实例的 gRPC 动态注入能力，对非直连成员执行真实连通性探测（注入 outbound → cURL 经本地 mixed 入站 → 移除 outbound）。
- 用户补充要求（2026-09-14）：**常驻 Xray 测试实例数量纳入配置管理**，即探针 worker 数 `probeWorkers` 由 `config.json` 的 `standalone_pool.evaluate.probeWorkers` 控制（默认 2）。

## 2. Architecture

### 2.1 复用策略（已与用户确认）

**复用**（基础设施组件）：
- `XrayInstance` 默认配置模板（socks-in mixed + api + outbounds[direct, proxy] + routing[api→api, TCP→proxy]）——即探针 worker 配置，无需 `setExplicitConfig`。
- `XrayApi` gRPC `addOutboundDirect` / `removeOutboundDirect`（动态注入/移除）。
- `config::OutboundBuilderFactory::create(profile, tag)` 生成成员 outbound。
- `PortManager::findAvailable` / `freePort` 端口分配。
- `CurlEasyHandle` cURL 探测（参照 `ProxyHealthEvaluator::probeSocksHttp` 与 `ProxyTester`）。
- `waitInstanceReady` 模式（复制 `XrayManager.cpp` 内 static 实现）。

**不复用**：
- `ProxyBatchTester` 类（生命周期/DB/判活标准 200||204 不匹配）。
- `XrayManager` 单例（全局共享状态会与批测试冲突）。

### 2.2 新增组件

```
ProxyProbePool (namespace proxy)
├── workers_: vector<shared_ptr<XrayInstance>>   // 常驻探针 worker（默认配置模板）
├── apis_:    vector<unique_ptr<xray::XrayApi>>  // 每 worker 一个 gRPC 客户端
├── busy_:    vector<bool> + mutex               // worker 占用标记
├── start()  → 分配端口对 → 启动 XrayInstance → waitInstanceReady
├── stop()   → 逐个 stop + freePort
└── probeMember(target, testUrl, connectMs, totalMs, out)
     → 找空闲 worker → removeOutboundDirect("proxy")
     → OutboundBuilderFactory.create(profile,"proxy") → addOutboundDirect
     → CurlEasyHandle 经 127.0.0.1:socksPort 探测 → removeOutboundDirect → 释放 worker
```

### 2.3 接线

- `ProxyHealthEvaluator`：新增 `setProbePool(ProxyProbePool*)`；`probe()` 对非 4/10 成员改走探针池。
- `StandaloneProxyPool`：`start()` 创建并启动探针池；`stop()` 释放；`doProbe()` 填充 `MemberProbeTarget.profile`。
- 配置：`StandalonePoolConfig::evaluate.probeWorkers`（默认 2）+ `StandalonePoolConfigParser` 解析。

## 3. Tech Stack

- C++17（**禁止 `auto` 类型推导**，全栈约束）
- wxWidgets 3.2+（本计划不涉及 UI 层）
- Xray-core + gRPC API（`USE_GRPC_API=1`）
- cURL（`CURL::libcurl`）
- boost::json（`libboost_json-gcc14-mt-x64-1_91.a`）
- Google Test（`gtest_main` / `gtest`）
- CMake + Ninja（Debug）

## 4. 文件结构

| 文件 | 动作 | 说明 |
| --- | --- | --- |
| `include/ProxyProbePool.h` | 新增 | 探针池类声明 |
| `src/ProxyProbePool.cpp` | 新增 | 探针池实现 |
| `tests/TestProxyProbePool.cpp` | 新增 | 探针池单元测试 |
| `include/ProxyHealthEvaluator.h` | 修改 | MemberProbeTarget + profile 字段；setProbePool |
| `src/ProxyHealthEvaluator.cpp` | 修改 | probe() 非直连分支走探针池 |
| `include/StandaloneProxyPool.h` | 修改 | probePool_ 成员 + xrayPath_/configDir_ 保存 |
| `src/StandaloneProxyPool.cpp` | 修改 | start/stop/doProbe 接线 |
| `include/ConfigReader.h` | 修改 | evaluate.probeWorkers 字段 |
| `include/config/sections/StandalonePoolConfigParser.h` | 修改 | probeWorkers 解析 |
| `tests/TestStandaloneProxyPool.cpp` | 修改 | parser 断言 + 探针池接线断言 |
| `CMakeLists.txt` | 修改 | CORE_SOURCES + 新测试目标 + 既有目标补源 |
| `docs/plans/project-plans-tracker.md` | 修改 | 登记本计划 |
| `docs/INDEX.md` | 修改 | 登记本计划文档 |---

## 5. Task 分解

> 每个 Task 遵循 TDD：写失败测试 → 验证失败 → 最小实现 → 验证通过 → commit。
> 构建命令：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`；`cmake --build build --parallel 8`；`ctest -V`。
> 验证一律使用 `bin/` 下最新构建产物（禁止 `bin/worker/validproxy.exe` 陈旧副本）。

### Task 1: 配置字段 `evaluate.probeWorkers`

**目标**：`standalone_pool.evaluate.probeWorkers` 可配置（默认 2），纳入配置管理。

**TDD 步骤**：

1. **写失败测试**（`tests/TestStandaloneProxyPool.cpp`）：
   - `ConfigParserDefaultsWhenAbsent`：断言 `config.standalone_pool.evaluate.probeWorkers == 2`（缺省保默认）。
   - `ConfigParserOverrides`：JSON 中 `"evaluate": {"probeWorkers": 5}` → 断言 `== 5`。
   - `ConfigParserRejectsInvalidEnum`（或新增用例）：`"probeWorkers": 0` / `-3` → 断言保持默认 2（v>0 才覆盖）。

2. **验证失败**：编译运行，断言失败（字段尚不存在）。

3. **最小实现**：
   - `include/ConfigReader.h`：`struct evaluate` 增加 `int probeWorkers = 2;`（autoOptimize 之后）。
   - `include/config/sections/StandalonePoolConfigParser.h`：evaluate 块内 autoOptimize 之后增加：
     ```cpp
     if (ev.contains("probeWorkers") && ev.at("probeWorkers").is_int64()) {
         int v = static_cast<int>(ev.at("probeWorkers").as_int64());
         if (v > 0) config.standalone_pool.evaluate.probeWorkers = v;
     }
     ```

4. **验证通过**：`ctest -R StandaloneProxyPoolTest -V` 全绿。

5. **commit**：`feat(config): add standalone_pool.evaluate.probeWorkers config field`

### Task 2: `ProxyProbePool` 类

**目标**：常驻 Xray 探针池，支持多 worker 并发探测非直连成员。

**TDD 步骤**：

1. **写失败测试**（新建 `tests/TestProxyProbePool.cpp`）：
   - `ConstructionBoundaryNoStart`：构造后未 start，`isRunning()==false`、`size()==0`、`stop()` 安全。
   - `ProbeMemberAllWorkersBusyReturnsFalse`：workerCount=1 且模拟占用（或 start 后并发占满）→ `probeMember` 返回 false。
   - `ProbeMemberUnavailableWhenNotStarted`：未 start 时 `probeMember` 返回 false。
   - 【live，`XRAY_REAL_EXE` gate】`LiveProbeMemberInjectsAndProbes`：start 后对非直连 profile（如 VMess 模板）`probeMember` 返回 true 且 `out.tested==true`（注入→cURL→移除全链路）。

2. **验证失败**：编译失败（头文件不存在）。

3. **最小实现**：
   - `include/ProxyProbePool.h`（namespace proxy，include `ProxyHealthEvaluator.h` 获取 MemberProbeTarget/MemberHealth）：
     ```cpp
     class ProxyProbePool {
     public:
         ProxyProbePool(const std::string& xrayPath, int workerCount, const std::string& configDir);
         ~ProxyProbePool();
         bool start();
         void stop();
         bool isRunning() const;
         int size() const;
         bool probeMember(const MemberProbeTarget& target, const std::string& testUrl,
                          long connectTimeoutMs, long totalTimeoutMs, MemberHealth& out);
     private:
         std::string xrayPath_;
         std::string configDir_;
         int workerCount_;
         std::vector<std::shared_ptr<XrayInstance>> workers_;
         std::vector<std::unique_ptr<xray::XrayApi>> apis_;
         std::vector<bool> busy_;
         std::mutex busyMutex_;
         std::atomic<bool> running_;
     };
     ```
   - `src/ProxyProbePool.cpp`：
     - `start()`：`if (running_) return true;` 循环 workerCount 次：`PortManager::findAvailable` 分配 socks/api 端口对（api 与 socks 不同，参照 `resolvePoolPorts` 防冲突逻辑）；`make_shared<XrayInstance>(xrayPath_, socks, api, configDir_)`；**不调 setExplicitConfig**（默认模板即探针配置）；`start()` 后 `waitInstanceReady`（复制 XrayManager.cpp static 实现：TCP connect 127.0.0.1:apiPort，`!isRunning()` 立即失败，5000ms/100ms 步）；失败则 `stop()` + `freePort` + break；全部就绪 `running_=true`。
     - `stop()`：`running_=false`；逐个 `worker_->stop()`；逐个 `PortManager::freePort`。
     - `probeMember()`：找空闲 worker（busy_ 置位，全忙返回 false）→ `api_->removeOutboundDirect("proxy")` 清理 → `OutboundBuilderFactory factory; ob=factory.create(target.profile, "proxy"); ob["tag"]="proxy";` 包 `{"outbounds":[ob]}` → `addOutboundDirect(json, "proxy", resultOut)`（失败回退 `addOutbound`，重试≤3，transientErr 关键词表 + 退避 200/400/800ms）→ `CurlEasyHandle`（setProxy `http://127.0.0.1:`+socksPort、setUrl、setTimeoutMs、setConnectTimeoutMs、setSslVerifyPeer/Host(false)、setNoBody(true)、setFollowLocation(true)）→ `perform()` → `delayMs=getTotalTime()*1000`、`alive=(200<=code<400)` → 无论成败 `removeOutboundDirect("proxy")` → 释放 busy → 填 out → 返回 true。

4. **验证通过**：`ctest -R ProxyProbePoolTest -V` 全绿（live 用例在无 XRAY_REAL_EXE 时 SKIP）。

5. **commit**：`feat(pool): add ProxyProbePool resident xray probe workers`

### Task 3: `ProxyHealthEvaluator` 接入探针池

**目标**：非 4/10 成员探测改走探针池。

**TDD 步骤**：

1. **写失败测试**（`tests/TestStandaloneProxyPool.cpp` 或新建 evaluator 测试）：
   - 构造 evaluator，`setProbePool` 指向未启动探针池 → `probe()` 对非直连成员返回 `tested=false` + `lastError="xray probe worker unavailable"`。
   - 【live】探针池启动后，`probe()` 对非直连成员返回 `tested=true`。

2. **验证失败**：编译失败（setProbePool 不存在）。

3. **最小实现**：
   - `include/ProxyHealthEvaluator.h`：`#include "Profileitem.h"`；`MemberProbeTarget` 增加 `db::models::Profileitem profile;`；前置声明 `namespace proxy { class ProxyProbePool; }`；类增加 `void setProbePool(ProxyProbePool* pool);` + 成员 `ProxyProbePool* probePool_ = nullptr;`。
   - `src/ProxyHealthEvaluator.cpp`：`probe()` else 分支改为：
     ```cpp
     if (probePool_ != nullptr && probePool_->isRunning()) {
         MemberHealth h;
         h.tag = t.tag;
         if (probePool_->probeMember(t, testUrl, connectTimeoutMs, totalTimeoutMs, h)) {
             result.push_back(h);
         } else {
             h.tested = false;
             h.lastError = "xray probe worker unavailable";
             result.push_back(h);
         }
     } else {
         // 现状：tested=false + protocol requires xray-side probe
     }
     ```

4. **验证通过**：相关测试全绿。

5. **commit**：`feat(evaluator): route non-direct members through ProxyProbePool`

### Task 4: `StandaloneProxyPool` 接线探针池

**目标**：池启动时创建探针池，探测时填充 profile，停止时释放。

**TDD 步骤**：

1. **写失败测试**（`tests/TestStandaloneProxyPool.cpp`）：
   - 【live】`LiveInjectionAndObservatoryPath` 扩展：注入非直连成员（configtype=2/VMess 模板）→ `probeNow()` 后 `v.probed==true`。
   - 构造边界：未 start 时 `stop()` 安全（探针池未创建）。

2. **验证失败**：live 断言失败（probed 仍 false）。

3. **最小实现**：
   - `include/StandaloneProxyPool.h`：`#include "ProxyProbePool.h"`；成员 `std::unique_ptr<ProxyProbePool> probePool_;`（evaluator_ 之后）；成员 `std::string xrayPath_; std::string configDir_;`。
   - `src/StandaloneProxyPool.cpp`：
     - 构造：初始化列表保存 `xrayPath_(xrayPath), configDir_(configDir)`。
     - `start()`：`running_=true` 之后、`evaluatorThread_` 启动之前：
       ```cpp
       probePool_.reset(new ProxyProbePool(xrayPath_, cfg_.evaluate.probeWorkers, configDir_));
       if (probePool_->start()) {
           evaluator_.setProbePool(probePool_.get());
       } else {
           WARN log "probe pool unavailable, non-direct members will not be probed";
       }
       ```
     - `stop()`：`evaluatorThread_.join()` 之后、`instance_->stop()` 之前：`evaluator_.setProbePool(nullptr); if (probePool_) probePool_->stop();`
     - `doProbe()`：构建 target 时增加 `t.profile = m.profile;`（值拷贝，锁内完成）。

4. **验证通过**：`ctest -R StandaloneProxyPoolTest -V` 全绿。

5. **commit**：`feat(pool): wire ProxyProbePool into StandaloneProxyPool lifecycle`

### Task 5: CMake 注册 + 文档修订

**目标**：构建系统接入新源文件/测试目标；文档同步。

**TDD 步骤**：

1. **CMakeLists.txt**：
   - `CORE_SOURCES` 增加 `src/ProxyProbePool.cpp`。
   - `test_standalone_proxy_pool` 目标源列表增加 `src/ProxyProbePool.cpp`。
   - 新增 `test_proxy_probe_pool` 目标（源：`tests/TestProxyProbePool.cpp` + `src/ProxyProbePool.cpp` + XrayInstance/XrayApi/PortManager/Utils/Logger/LoggerInstance/OutboundBuilderFactory/ProfileConfigRepository/ProfileNormalizer/StreamSettingsBuilder/XrayConfigAssembler/9 outbound builders/ProfileitemDAO/ProfileExItemDAO/DnsCache；`USE_GRPC_API=1`；链接 gtest_main gtest libsqlite3.a libboost_json-gcc14-mt-x64-1_91.a CURL::libcurl -lws2_32；`add_test(NAME ProxyProbePoolTest)`）。
2. **验证**：全量 `cmake --build build --parallel 8` + `ctest -V` 全绿。
3. **文档**：
   - `docs/plans/project-plans-tracker.md`：登记本计划。
   - `docs/INDEX.md`：登记本计划文档路径。
   - `docs/notes/2026-09-09-Note-ProxyPoolMonitorUnify-v1.0.md`：修复 `## 3. 已识别问题` 标题被吞入表格行的问题；§2 行号改 functionName 形式；P2 收窄为注入路径；P3/P4 补清单项或显式声明范围外。
   - `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`：§6 风险表补充探针池相关风险（worker 数配置、端口占用、gRPC 注入失败回退）。
4. **commit**：`docs(pool): register probe pool plan and update monitor unify docs`

---

## 6. 验收标准

1. `standalone_pool.evaluate.probeWorkers` 可配置（默认 2），parser 单测覆盖默认/覆盖/非法。
2. `ProxyProbePool` 启动 K 个常驻 Xray 探针 worker，端口互不冲突。
3. 非直连成员（configtype ≠ 4/10）经探针池探测后 `probed==true`，`lastDelayMs`/`lastAlive` 真实反映连通性。
4. 直连成员（4/10）行为不变（仍走 `probeSocksHttp`）。
5. 探针池不可用时池功能降级不崩溃（成员 `tested=false`）。
6. 全量 `ctest -V` 全绿；C++17 无 `auto`；验证用 `bin/` 构建产物。