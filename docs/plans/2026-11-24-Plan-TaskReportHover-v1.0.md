# 任务完成报告 + 任务列表悬停显示 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 每个任务完成后从日志抽取生成 report（含**结构化统计** total/success/failed/skipped）写入 log 目录；任务列表中悬停已完成任务行时显示其 report。

**Architecture:** `task::TaskStats`（total/success/failed/skipped）由聚合任务操作经**可选出参 `TaskStats* stats = nullptr`** 暴露、任务 lambda 经 `TaskContext::addStats` 累积；`TaskQueue::workerLoop` 任务执行期间用 `Logger::pushCallback` 作用域捕获 INFO+ 日志行；任务完成后经新增 `TaskFinishedSink` 把 `TaskHandle` + 捕获行 + `ctx.stats` 交给 `TaskReportService`（格式化元数据头+统计节+日志摘录、写 `report_<taskId>.txt` 到 `Logger::getLogDir()`、内存索引供悬停即时读）。`TaskMonitorPanel` 在 `pendingList_` 上加 `wxEVT_MOTION`，`HitTest` 定位行 → `pendingIds_[row]` → 查 report 索引 → 弹 `wxPopupTransientWindow` 只读文本浮层；离开隐藏。

**Tech Stack:** C++17、wxWidgets 3.3（wxMSW）、CMake+Ninja、Google Test / Catch2（UI 自动化）。

**Spec:** `docs/specs/2026-11-24-Spec-TaskReportHover-v1.0.md`（§3 R3 统计节 / §8 D6 结构化统计通道 / §9 A3b）

## Global Constraints
- C++17；**全仓库 C++ 代码禁止 `auto` 类型推导**（项目硬约束）。
- 目标 Windows + MinGW/GCC；wxWidgets 3.3。
- UI 变更交付前跑 `scripts/build-and-test.bat`（UI 自动化门禁）；验证用 `bin\` 最新产物。
- 含中日韩文本改动用 `edit` 精确替换（禁 `clion apply_patch`）。
- 新建/变更专题文档登记 `docs/INDEX.md` + `docs/plans/project-plans-tracker.md`。
- 测试优先（TDD），`TEST_F` 风格；UI 定位源 `tests/ui/UIIds.h`（禁硬编码）。

## Review Focus
- **report 写入失败**：任务终态不变、记 WARN、**不更新索引**、悬停无浮层（写入 try/catch + 任务结果独立）。
- **任务无日志行**：仍生成含元数据+统计 report（正文空）。
- **统计通道向后兼容**：未传 `TaskStats*` 的既有调用（CLI 等）行为不变；任务未提供统计时统计节输出 0。
- **悬停非已完成 / 无 report 行**：不显示浮层（`find()` 缺键返回 false 不插空条目；越界保护）。
- **同 taskId 重复完成**：report 覆盖重写，不重复文件。
- **日志捕获回调异常**：不冒泡进任务执行（try/catch 确保 pop）。
- **find() 线程安全**：锁内拷贝到 `out`，指针不逃逸锁（worker 写 vs UI 读无竞态）。
- **刷新不打断静止悬停**：refreshNow 仅在悬停行不再是"已完成且有 report"（且行移位后 `pendingIds_[hoveredRow_]` 仍等于浮层的 `popupTaskId_`）时隐藏；重建后按 `popupTaskId_` 恢复 `hoveredRow_`。
- **浮层坐标/编码**：ClientToScreen 转换、工作区钳制、UTF-8→wxString（FromUTF8）。
- **浮层 wx API 正确性（wxWidgets 3.3.3）**：`wxPopupTransientWindow(this, wxPU_CONTAINS_CONTROLS)`（`wxPU_NO_FOCUS` 不存在，弃用）；辨识标题 `SetLabel(FromUTF8("任务报告 "+taskId))`（`SetTitle` 仅 wxTopLevelWindow 有，弃用）；`Position(ptOrigin, wxSize(560,420))` 两参。
- **DEDUP 统计链**：无 `doDeduplicate`（已核实）→ 沿 `deduplicateAsync`→`AppController::deduplicate`→`SubitemUpdaterV2::deduplicate`→`Deduplicator::deduplicate` 整链加尾参，避免统计恒 0。
- **AUTO_TASK 填充点**：`run()`/`resume()` 双入口均在 `runSteps()` 返回后（既有汇总处）填充，每步终态计数，取消时报告部分计数（total=success+failed）；`doAutoTaskImpl` 穿线到两条路径。
- **键盘路径守卫**：上下文菜单"查看报告"仅当 `findTaskReport` 成功时追加，触发目标为焦点行（`selectedRow_`）而非列表中心。

---

### Task 1: TaskStats 定义 + TaskContext 统计累积

**Files:**
- Modify: `include/task/TaskType.h`（加 `task::TaskStats` 结构）
- Modify: `include/task/TaskQueue.h`（`TaskContext` 加 `stats` 成员 + `addStats` 方法）
- Test: `tests/test_task_stats.cpp`

**Interfaces:**
- Produces:
  - `namespace task { struct TaskStats { int total=0; int success=0; int failed=0; int skipped=0; }; }`（放 TaskType.h，聚合服务已 include，避免新增头/CMake 目标）
  - `struct task::TaskContext { ...; task::TaskStats stats; void addStats(const task::TaskStats& s){ stats.total+=s.total; stats.success+=s.success; stats.failed+=s.failed; stats.skipped+=s.skipped; } };`
- 语义：`addStats` 逐字段累积（容器任务多次调用求和）；默认全 0。

- [ ] **Step 1: Write the failing test**

```cpp
TEST_F(TaskStatsTest, AccumulatesFields) {
    task::TaskContext ctx;
    ctx.addStats({10, 8, 1, 1});
    ctx.addStats({5, 4, 1, 0});
    EXPECT_EQ(ctx.stats.total, 15);
    EXPECT_EQ(ctx.stats.success, 12);
    EXPECT_EQ(ctx.stats.failed, 2);
    EXPECT_EQ(ctx.stats.skipped, 1);
}
TEST_F(TaskStatsTest, DefaultsToZero) {
    task::TaskContext ctx;
    EXPECT_EQ(ctx.stats.total, 0);  EXPECT_EQ(ctx.stats.success, 0);
    EXPECT_EQ(ctx.stats.failed, 0); EXPECT_EQ(ctx.stats.skipped, 0);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --test-dir build -R TaskStatsTest -V`
Expected: FAIL（无 `task::TaskStats` / 无 `addStats`）

- [ ] **Step 3: Implement**

`TaskType.h` 加 `task::TaskStats`；`TaskQueue.h` `TaskContext` 加 `task::TaskStats stats;` 与 `void addStats(const task::TaskStats&)`（逐字段 `+=`）。

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --test-dir build -R TaskStatsTest -V`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/task/TaskType.h include/task/TaskQueue.h tests/test_task_stats.cpp CMakeLists.txt
git commit -m "feat(task-report): TaskStats 结构 + TaskContext::addStats 统计累积"
```

### Task 2: TaskReportService（元数据 + 统计节 + 落盘 + 内存索引）

**Files:**
- Create: `include/task/TaskReportService.h`
- Create: `src/task/TaskReportService.cpp`
- Test: `tests/test_task_report_service.cpp`

**Interfaces:**
- Produces:
  - `namespace task { struct LogLine { ::LogLevel level; std::string line; }; struct TaskReportInfo { std::string taskId; std::string filePath; std::string content; }; }`
  - `class task::TaskReportService { explicit TaskReportService(std::string logDir); std::string formatReport(const task::TaskHandle&, const std::vector<task::LogLine>&, const task::TaskStats&) const; std::string publish(const task::TaskHandle&, const std::vector<task::LogLine>&, const task::TaskStats&); bool find(const std::string& taskId, task::TaskReportInfo& out) const; };`
- `formatReport`：元数据头（taskId/type/status/createdAt/startedAt/completedAt/durationMs/targetId/error/progressCurrent/progressTotal/parentTaskId，存在才输出）+ `---- statistics ----`（total/success/failed/skipped）+ `---- log excerpt ----` + 逐行原样。
- `publish`：`formatReport` → 写 `logDir/report_<taskId>.txt`（std::ofstream 覆盖式）；**仅在写成功后**才更新 `index_[taskId]` 并返回 filePath；写失败返回空串且**不更新索引、记 `Logger::write(..., LogLevel::WARN)`**。
- `find`：用 `index_.find(taskId)` 迭代器——**缺键返回 false（不插入空条目，避免 A5 弹空浮层）**；命中则**在锁内把内容拷贝到 `out`** 返回 true（指针不逃逸锁，worker 写 vs UI 读无竞态）。线程安全用 `std::mutex`（锁仅覆盖 map 操作，返回的是副本）。

- [ ] **Step 1: Write the failing test**

```cpp
TEST_F(TaskReportServiceTest, FormatReportContainsMetadataStatsAndLogLines) {
    task::TaskHandle h; h.taskId="t1"; h.type=task::TaskType::TEST_ALL;
    h.status=task::TaskStatus::COMPLETED; h.startedAt="100"; h.completedAt="1000";
    task::TaskStats st{20,17,2,1};
    std::vector<task::LogLine> lines{{::LogLevel::INFO, "[2026-11-24 10:00:00] [INFO] batch done"}};
    std::string r = svc_.formatReport(h, lines, st);
    EXPECT_NE(r.find("taskId: t1"), std::string::npos);
    EXPECT_NE(r.find("TEST_ALL"), std::string::npos);
    EXPECT_NE(r.find("total: 20"), std::string::npos);
    EXPECT_NE(r.find("success: 17"), std::string::npos);
    EXPECT_NE(r.find("batch done"), std::string::npos);
}
TEST_F(TaskReportServiceTest, PublishWritesFileAndIndexes) {
    task::TaskHandle h; h.taskId="t2"; h.status=task::TaskStatus::FAILED; h.error="boom";
    task::TaskStats st{5,3,2,0};
    std::string p = svc_.publish(h, {}, st);
    EXPECT_FALSE(p.empty());
    EXPECT_TRUE(FileExists(p));
    task::TaskReportInfo info;
    EXPECT_TRUE(svc_.find("t2", info));
    EXPECT_NE(info.content.find("boom"), std::string::npos);
    EXPECT_NE(info.content.find("failed: 2"), std::string::npos);
}
TEST_F(TaskReportServiceTest, EmptyLogLinesAndZeroStatsStillPublishes) {
    task::TaskHandle h; h.taskId="t3"; h.status=task::TaskStatus::COMPLETED;
    std::string p = svc_.publish(h, {}, {});
    EXPECT_FALSE(p.empty());
    task::TaskReportInfo info;
    EXPECT_TRUE(svc_.find("t3", info));
}
TEST_F(TaskReportServiceTest, MissingTaskIdFindReturnsFalseWithoutInserting) {
    task::TaskReportInfo info;
    EXPECT_FALSE(svc_.find("nope", info));   // 缺键：false，不插空条目
    EXPECT_FALSE(svc_.find("nope", info));   // 再次：仍 false（索引未被污染）
}
TEST_F(TaskReportServiceTest, PublishOverwritesSameTaskId) {
    task::TaskHandle h; h.taskId="dup"; h.status=task::TaskStatus::COMPLETED;
    std::string p1 = svc_.publish(h, {}, {});
    EXPECT_FALSE(p1.empty());
    h.status=task::TaskStatus::FAILED; h.error="second";
    std::string p2 = svc_.publish(h, {}, {});
    EXPECT_EQ(p1, p2);                        // 同一路径覆盖，不产生重复文件
    task::TaskReportInfo info;
    EXPECT_TRUE(svc_.find("dup", info));
    EXPECT_NE(info.content.find("second"), std::string::npos);
}
TEST_F(TaskReportServiceTest, PublishWriteFailureReturnsEmptyAndNoIndex) {
    task::TaskReportService svcBad((std::filesystem::temp_directory_path() / "vp_no_such_dir_zz").string());   // 保证不存在的目录（可移植：非固定 Z: 盘）
    task::TaskHandle h; h.taskId="f1"; h.status=task::TaskStatus::COMPLETED;
    std::string p = svcBad.publish(h, {}, {});
    EXPECT_TRUE(p.empty());
    task::TaskReportInfo info;
    EXPECT_FALSE(svcBad.find("f1", info));   // 写失败不更新索引
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --test-dir build -R TaskReportServiceTest -V`
Expected: FAIL（TaskReportService 未定义）

- [ ] **Step 3: Implement `include/task/TaskReportService.h` + `src/task/TaskReportService.cpp`**

如上接口；`formatReport` 先元数据头（progressCurrent/progressTotal）、再 `---- statistics ----`（total/success/failed/skipped）、再 `---- log excerpt ----`；`publish` 写盘成功后才更新索引（失败返回空串 + 记 WARN，不更新索引）；`find` 用 `index_.find()` 迭代器 + 锁内拷贝到 `out`（无 `clear`——接口未声明、无消费方）；`std::mutex` 保护索引与写盘。

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --test-dir build -R TaskReportServiceTest -V`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/task/TaskReportService.h src/task/TaskReportService.cpp tests/test_task_report_service.cpp CMakeLists.txt
git commit -m "feat(task-report): TaskReportService 报告格式化(含统计节)+落盘+内存索引"
```

### Task 3: TaskQueue 完成 sink + 作用域日志捕获（含 stats 传递）

**Files:**
- Modify: `include/task/TaskQueue.h`（加 `TaskFinishedSink` 类型 + `setTaskFinishedSink` + 私有 `taskFinishedSink_`）
- Modify: `src/task/TaskQueue.cpp`（`workerLoop` 包裹 `entry.func` 的日志捕获 + 完成后调 sink，传 `ctx.stats`）
- Test: `tests/test_task_queue_report.cpp`

**Interfaces:**
- Consumes: `task::LogLine`/`task::TaskStats`/`task::TaskHandle`（Task 1/2）
- Produces:
  - `using task::TaskFinishedSink = std::function<void(const task::TaskHandle&, const std::vector<task::LogLine>&, const task::TaskStats&)>;`
  - `void task::TaskQueue::setTaskFinishedSink(TaskFinishedSink sink);`
- 语义：sink 在任务终态确定、`onComplete` 之后、`busy_.store(false)` 之前调用；捕获行 = `entry.func(ctx)` 执行期间 `Logger` 级别 ≥ INFO 的行；**sink 调用前必须 `popCallback`**（sink 自身日志不入捕获集）；stats = `ctx.stats`（任务结束后读取）。

- [ ] **Step 1: Write the failing test**

```cpp
TEST_F(TaskQueueReportTest, SinkCalledWithHandleLinesAndStats) {
    task::TaskQueue q; std::vector<task::LogLine> got; task::TaskHandle gotH; task::TaskStats gotSt;
    q.setTaskFinishedSink([&](const task::TaskHandle& h, const std::vector<task::LogLine>& l, const task::TaskStats& s){
        gotH=h; got=l; gotSt=s; });
    q.submit(task::TaskType::DEDUP, [](task::TaskContext& ctx){
        Logger::write("dedup line", LogLevel::INFO);
        ctx.addStats({10,8,1,1});
    });
    q.stop(true);
    EXPECT_EQ(gotH.type, task::TaskType::DEDUP);
    EXPECT_NE(got.size(), 0u);
    EXPECT_NE(got[0].line.find("dedup line"), std::string::npos);
    EXPECT_EQ(gotSt.total, 10);
    EXPECT_EQ(gotSt.success, 8);
}
TEST_F(TaskQueueReportTest, FailedTaskStillEmitsSink) {
    task::TaskQueue q; int calls=0;
    q.setTaskFinishedSink([&](const task::TaskHandle& h, const std::vector<task::LogLine>&, const task::TaskStats&){
        if(h.status==task::TaskStatus::FAILED) calls++; });
    q.submit(task::TaskType::DEDUP, [](task::TaskContext&){ throw std::runtime_error("x"); });
    q.stop(true);
    EXPECT_EQ(calls, 1);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --test-dir build -R TaskQueueReportTest -V`
Expected: FAIL（无 sink API / 无 stats 参数）

- [ ] **Step 3: Implement**

`TaskQueue.h` 加 `using TaskFinishedSink = ...;` + `void setTaskFinishedSink(TaskFinishedSink s){ taskFinishedSink_=std::move(s); }` + 私有 `TaskFinishedSink taskFinishedSink_;`。`workerLoop` 内：`entry.func(ctx)` 前 `Logger::pushCallback`（收集 `level>=INFO` 行，try/catch 包裹），`entry.func(ctx)` 后 `Logger::popCallback()`（try/catch 确保 pop）；终态确定、`onComplete` 之后 `if(taskFinishedSink_) taskFinishedSink_(entry.handle, capturedLines, ctx.stats);`。注意 `ctx` 在任务体内被引用，读取 `ctx.stats` 须在任务函数返回后、`ctx` 仍存活时。

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --test-dir build -R TaskQueueReportTest -V`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/task/TaskQueue.h src/task/TaskQueue.cpp tests/test_task_queue_report.cpp CMakeLists.txt
git commit -m "feat(task-report): TaskQueue 完成 sink(含stats) + 任务窗口日志捕获"
```

### Task 4: 聚合任务操作暴露统计（TaskStats* 出参）—— 改动最大

**Files:**
- Modify: `include/SubitemUpdaterV2.h` + `src/SubitemUpdaterV2.cpp`（UPDATE_SUBSCRIPTION/UPDATE_ALL 及 SYNC_DB 的 `syncDatabases`、DEDUP 的 `deduplicate()`）
- Modify: `include/ProxyBatchTester.h` + `src/ProxyBatchTester.cpp`（TEST_SUBSCRIPTION/TEST_SINGLE_PROXY/TEST_ALL/TEST_BATCH/TEST_ONLINE：`run()`/`runWithSubId()`/`runWithIndexId()`）
- Modify: `include/update/Deduplicator.h` + `src/update/Deduplicator.cpp`（DEDUP：`deduplicate()`）
- Modify: `include/RegionBatchResolver.h` + `src/RegionBatchResolver.cpp`（RESOLVE_REGION/RESOLVE_SINGLE_REGION）
- Modify: `include/AutoTaskManager.h` + `src/AutoTaskManager.cpp`（AUTO_TASK：**自身出参**——放弃 childCompleted/childFailed 聚合，因该计数在 `current_` 而 sink 收到的是 `entry.handle`，二者不同源）
- Test: `tests/test_task_stats_exposure.cpp`（代表性：SubitemUpdaterV2 / ProxyBatchTester / Deduplicator）

**Interfaces:**
- 每个聚合任务操作入口方法增加**末位可选出参** `task::TaskStats* stats = nullptr`；在既有汇总点（已计算 total/success/failed/skipped，如 `Update Summary`/`Migration Result`/`Deduplication Summary`）填充。
- 语义：`stats==nullptr` 时行为与现状完全一致（不写、不额外计算）；`stats!=nullptr` 时填充对应字段。简单任务（FIND_*/EXPORT/GENERATE_CONFIG/PROBE_*）不改造服务，由 Task 5 的 lambda 从返回值直接 `ctx.addStats({...})`。

- [ ] **Step 1: Write the failing test（代表性，Deduplicator 最易构造）**

```cpp
TEST_F(TaskStatsExposureTest, DeduplicatorFillsStatsOutParam) {
    // 用测试库构造 Deduplicator（test/ 测试数据），调用 deduplicate(&st)
    // 断言 st.total == getBeforeCount() 且 st.success+st.failed+st.skipped == st.total
    // （skipped 派生 = total-success-failed，不变式恒成立）
}
TEST_F(TaskStatsExposureTest, NullStatsKeepsBehaviorUnchanged) {
    // 传 nullptr 与不传行为一致（编译期 + 运行无异常）
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --test-dir build -R TaskStatsExposureTest -V`
Expected: FAIL（无 `TaskStats*` 出参）

- [ ] **Step 3: Implement（逐服务）**

模式：入口方法签名加 `task::TaskStats* stats = nullptr`；在既有汇总点（REPORT 汇总行生成处）把已算计数写入 `if(stats){ stats->total=...; stats->success=...; stats->failed=...; stats->skipped=...; }`。遵循既有计数变量名，不重复计算。清单：
- `SubitemUpdaterV2` 更新入口（Update Summary：订阅总数/成功/失败/跳过）及 `syncDatabases`（Migration Result：total/succeeded/failed，`skipped=total-success-failed` 派生保证不变式）。
- `ProxyBatchTester::run/runWithSubId/runWithIndexId`（测试总数/成功/失败/跳过）。
- `Deduplicator::deduplicate`：`total=getBeforeCount()`、`success=getAfterCount()`、`failed=getConfigErrorCount()`、`skipped=total-success-failed`（派生，保证 success+failed+skipped==total 恒成立——'merged' 不存在，弃用）。
- `RegionBatchResolver` 入口（解析总数/成功/失败，`skipped=total-success-failed` 派生）。
- `AutoTaskManager::run()` 与 `resume()`（两条入口，AppController.cpp:4430 `resume ? manager.resume() : manager.run(...)`）：**各自在 `runSteps()` 返回后**（既有 "AutoTask: completed/cancelled/failed" / "resumed and completed" 汇总处，AutoTaskManager.cpp:133-136）填充——每步终态 COMPLETED→success、FAILED/CANCELLED→failed、running/pending→不计；`total=success+failed`，取消时报告部分计数；`doAutoTaskImpl` 把 `&local` 穿到 BOTH `manager.run(steps, &local)` 与 `manager.resume(&local)` 再 `ctx.addStats(local)`。

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --test-dir build -R TaskStatsExposureTest -V`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/SubitemUpdaterV2.h src/SubitemUpdaterV2.cpp include/ProxyBatchTester.h src/ProxyBatchTester.cpp include/update/Deduplicator.h src/update/Deduplicator.cpp include/RegionBatchResolver.h src/RegionBatchResolver.cpp include/AutoTaskManager.h src/AutoTaskManager.cpp tests/test_task_stats_exposure.cpp CMakeLists.txt
git commit -m "feat(task-report): 聚合任务操作暴露 TaskStats* 出参统计"
```

### Task 5: AppController 接线（lambda addStats + 注册 sink + findTaskReport）

**Files:**
- Modify: `src/ui/AppController.h`（成员 `task::TaskReportService taskReportService_;` + `bool findTaskReport(const std::string& taskId, task::TaskReportInfo& out) const;`）
- Modify: `src/ui/AppController.cpp`（ctor 初始化 service（logDir=`Logger::getLogDir()`）+ `taskQueue_.setTaskFinishedSink(...)`；各任务 lambda 调 `ctx.addStats`）
- Test: `tests/test_task_report_integration.cpp`（全链路，不依赖 wx）

**Interfaces:**
- Consumes: `task::TaskReportService`（Task 2）、`TaskQueue::setTaskFinishedSink`（Task 3）、`TaskStats*` 出参（Task 4）
- Produces: `bool AppController::findTaskReport(const std::string& taskId, task::TaskReportInfo& out) const;`（转调 `taskReportService_.find`，锁内副本、无指针逃逸）
- 语义：sink 回调（worker 线程）内 `taskReportService_.publish(handle, lines, stats)`；publish 失败（返回空串）不抛出（WARN 已在 Service 内记，且不更新索引）；`taskId` 为空忽略。**统计穿线**：任务 lambda 委托 `do*` helper（`doUpdateSubscription`/`doUpdateAllSubscriptions`/`doTestSubscription`/`doTestSingleProxy`/`doTestAllProxies`/`doSyncDatabases`/`doResolveRegionsBatch`/`doAutoTaskImpl` 等），每个 `do*` 增加**末位参数 `task::TaskStats* stats = nullptr`** 并转发给底层服务调用（调用点传 `&local`，之后 `ctx.addStats(local)`）。**DEDUP 无 `doDeduplicate`**（已核实 AppController.h 仅 `bool deduplicate()` + `void deduplicateAsync`）：走 `deduplicateAsync` lambda → `AppController::deduplicate()` → `SubitemUpdaterV2::deduplicate()` → `Deduplicator::deduplicate()`，沿**整链**加尾参（`AppController::deduplicate(TaskStats*)`、`SubitemUpdaterV2::deduplicate(TaskStats*)`、`Deduplicator::deduplicate(TaskStats*)`），lambda 传 `&local` 后 `ctx.addStats(local)`。简单任务（FIND_*/EXPORT/GENERATE_CONFIG/PROBE_*）lambda 直接从返回值 `ctx.addStats({1, ok?1:0, ok?0:1, 0})`。

- [ ] **Step 1: Write the failing test（集成全链路）**

```cpp
// tests/test_task_report_integration.cpp —— 走 TaskQueue + TaskReportService 全链路（不依赖 wx）
TEST_F(TaskReportIntegrationTest, TaskCompletionPublishesReportFileWithStats) {
    temp::TempDir dir; task::TaskQueue q;
    task::TaskReportService svc(dir.path());
    q.setTaskFinishedSink([&](const task::TaskHandle& h, const std::vector<task::LogLine>& l, const task::TaskStats& s){ svc.publish(h, l, s); });
    q.submit(task::TaskType::TEST_ALL, [](task::TaskContext& ctx){
        Logger::write("done", LogLevel::INFO);
        ctx.addStats({10,9,1,0});
    });
    q.stop(true);
    // 断言 dir 下存在 report_<taskId>.txt 且内容含 "success: 9"
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --test-dir build -R TaskReportIntegrationTest -V`
Expected: FAIL（无此链路）

- [ ] **Step 3: Implement（AppController 接线 + 集成测试通过）**

`AppController` 成员 `task::TaskReportService taskReportService_;` 用 `Logger::getLogDir()` 初始化；ctor 里 `taskQueue_.setTaskFinishedSink([this](const task::TaskHandle& h, const std::vector<task::LogLine>& l, const task::TaskStats& s){ if(!h.taskId.empty()) taskReportService_.publish(h, l, s); });`。`findTaskReport` 转调 `taskReportService_.find`（bool + out 副本）。各任务 lambda：聚合任务经 `do*` helper 尾参穿线 `&local` → `ctx.addStats(local)`；简单任务直接 `ctx.addStats(...)`。集成测试独立跑（不经 wx）。

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --test-dir build -R "TaskReport" -V`
Expected: PASS（含 Task2/5；Task1 的 `TaskStatsTest`、Task3 的 `TaskQueueReportTest` 均不匹配 `-R TaskReport` 过滤器，分别由 Task 1 Step 4、Task 3 Step 2/4 单独验证）

- [ ] **Step 5: Commit**

```bash
git add src/ui/AppController.h src/ui/AppController.cpp tests/test_task_report_integration.cpp CMakeLists.txt
git commit -m "feat(task-report): AppController 接线 TaskReportService + lambda addStats 完成即生成 report"
```

### Task 6: TaskMonitorPanel 悬停显示 report 浮层

**Files:**
- Modify: `src/ui/TaskMonitorPanel.h`（`onListMotion`、`onListLeave`、`showReportPopup(const std::string& taskId, const wxPoint& pos)`、`hideReportPopup`、`onListContextMenu`（为 RowFinished 追加"查看报告"项）、成员 `wxPopupTransientWindow* reportPopup_`、`wxTextCtrl* reportPopupText_`、`long hoveredRow_`、`std::string popupTaskId_`）
- Modify: `src/ui/TaskMonitorPanel.cpp`（ctor `pendingList_->Bind(wxEVT_MOTION, ...)` + `pendingList_->Bind(wxEVT_LEAVE_WINDOW, ...)`；`refreshNow` **顺序固定 + 条件隐藏**（重建前捕获 `popupTaskId_` → 重建后恢复 `hoveredRow_` → 再条件检查）；`onListContextMenu` 为 RowFinished 追加"查看报告"——`findTaskReport` 门控：无 report 则不追加/禁用）
- Test: `tests/ui/TestTaskReportHover.cpp`（UI 自动化，ctest 注册 `UI_TASKREPORTHOVER`）

**Interfaces:**
- Consumes: `AppController::findTaskReport(taskId, out)`（Task 5，bool + 副本）、`pendingIds_`/`rowKinds_`/`pendingList_`
- 语义：`onListMotion`：`HitTest(pos, flags)` → 若命中行且 `rowKinds_[row]==RowFinished` 且 `task::TaskReportInfo info; findTaskReport(pendingIds_[row], info)` → `showReportPopup(pendingIds_[row], pos)`；否则 `hideReportPopup()`。**鼠标离开**：`pendingList_` 与浮层均绑定 `wxEVT_LEAVE_WINDOW` → `hideReportPopup()`（transient window 对 `wxPU_CONTAINS_CONTROLS` 无鼠标离开 dismissal，`MSWDismissUnfocusedPopup` 禁用；不依赖激活驱动）。浮层 `wxPopupTransientWindow`（**风格 `wxPU_CONTAINS_CONTROLS`**——`wxPU_NO_FOCUS` 在 wxWidgets 3.3.3 不存在，弃用；内部控件可交互/滚动）+ 只读多行 `wxTextCtrl`（`wxTE_MULTILINE|wxTE_READONLY`），尺寸上限 560×420。**辨识标题**：`SetLabel(wxString::FromUTF8(("任务报告 " + taskId).c_str()))`（`wxWindow::SetLabel` 在 MSW 映射 `SetWindowText`，UI 自动化经 `EnumWindows`/`GetWindowTextW` 按标题定位——`SetTitle` 仅 `wxTopLevelWindow` 有，popup 非之）。**定位**：`screenPt = pendingList_->ClientToScreen(pos)`（客户端→屏幕），再显式 `SetSize(560,420)`（使 `GetSize()` 驱动钳制）并按工作区钳制（右/下越界则左/上移），`Position(wxPoint(screenPt.x+8, screenPt.y+8), wxSize(0,0))`（`wxPopupWindowBase::Position` 需 **ptOrigin + size 两参**；size 作**锚点偏移**而非尺寸——传 0,0 使 top-left 落在 cursor+8，钳制按自身 `GetSize()`）。**编码**：`SetValue(wxString::FromUTF8(info.content.c_str()))`（UTF-8→wxString，避免 GBK 乱码）。

- [ ] **Step 1: Write the failing test（UI 自动化）**

```cpp
// tests/ui/TestTaskReportHover.cpp，注册 ctest UI_TASKREPORTHOVER
TEST_CASE("Hover finished task row shows report popup", "[taskreporthover]") {
    AppFixture fx; REQUIRE(fx.mainWindow().valid());
    HWND hMain = findMainWindow(fx.pid());
    // 触发一个会写日志并完成的任务（如 DEDUP），等待其进入历史/完成行
    // 模拟悬停：SetCursorPos(完成行屏幕坐标) 预热 + PostMessage WM_MOUSEMOVE（wxMSW 校验 WindowFromPoint）
    // 先枚举 Logger::getLogDir() 下 report_<taskId>.txt 从文件名提取 taskId（taskId 非可预测：steady_clock+counter，不能硬编码）
    // 断言出现 report 浮层（按标题 GetWindowTextW 匹配 "任务报告 <taskId>"，而非 C++ 类名），内容含 report 元数据/统计
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --test-dir build -R UI_TASKREPORTHOVER -V`
Expected: FAIL（无浮层）

- [ ] **Step 3: Implement `TaskMonitorPanel` 悬停 + 浮层**

在 ctor `pendingList_->Bind(wxEVT_MOTION, &TaskMonitorPanel::onListMotion, this);`、`pendingList_->Bind(wxEVT_LEAVE_WINDOW, &TaskMonitorPanel::onListLeave, this);`（鼠标离开自动隐藏，不依赖 transient window 激活 dismissal）；`onListLeave`：`hideReportPopup();`。`onListMotion`：`int row; wxListCtrl::HitTest(pos, row, flags)`；越界/非 RowFinished/无 report → `hideReportPopup()`；否则若 `row != hoveredRow_` 或浮层未显示或 `popupTaskId_ != pendingIds_[row]` → `showReportPopup(pendingIds_[row], pos)`（`hoveredRow_`/`popupTaskId_` 去抖：行内移动不重定位，避免浮层跟随光标抖动与重复 `Popup()`；换行则刷新内容）。`showReportPopup(taskId, pos)`：先 `task::TaskReportInfo info; if(!findTaskReport(taskId, info)){ hideReportPopup(); return; }`（**守卫**：无 report 不弹）；惰性建 `reportPopup_`（`wxPopupTransientWindow(this, wxPU_CONTAINS_CONTROLS)` + `wxTextCtrl`，`SetLabel(wxString::FromUTF8(("任务报告 " + taskId).c_str()))`），`SetValue(wxString::FromUTF8(info.content.c_str()))`，`popupTaskId_ = taskId`，`screenPt = pendingList_->ClientToScreen(pos)`，`SetSize(560,420)`（GetSize 驱动钳制）并按工作区钳制，`Position(wxPoint(screenPt.x+8, screenPt.y+8), wxSize(0,0))`（size 作锚点偏移非尺寸），`Popup()`。`hideReportPopup`：`if(reportPopup_ && reportPopup_->IsShown()) reportPopup_->Dismiss(); popupTaskId_.clear();`。`refreshNow()`：**顺序固定**——(1) 重建前捕获 `popupTaskId_`；(2) 重建完成后按 `popupTaskId_` 扫描 `pendingIds_`（边界保护）恢复 `hoveredRow_` 为其新行号（如 `selectedRow_` 已做），该 taskId 已不在列表则 `hideReportPopup()`；(3) **然后**用恢复后的 `hoveredRow_` 执行**条件隐藏**检查——仅当 `pendingIds_[hoveredRow_] == popupTaskId_`、该行仍 `RowFinished` 且 report 仍存在时保持浮层（静止悬停不被每秒刷新打断），否则 `hideReportPopup()`。`onListContextMenu` 为 RowFinished 追加"查看报告"项（键盘可达路径）：**仅当 `findTaskReport(ctxTaskId_, info)` 成功时追加**（无 report 则省略/禁用），触发目标为焦点行（`selectedRow_`）而非列表中心 HitTest → 复用 `showReportPopup(焦点 taskId, 焦点行位置)`。

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --test-dir build -R UI_TASKREPORTHOVER -V`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/ui/TaskMonitorPanel.h src/ui/TaskMonitorPanel.cpp tests/ui/TestTaskReportHover.cpp CMakeLists.txt
git commit -m "feat(task-report): 任务列表面板悬停已完成行显示 report 浮层"
```

### Task 7: 文档登记 + 全量门禁

**Files:**
- Modify: `docs/INDEX.md`（登记 Spec + 计划）
- Modify: `docs/plans/project-plans-tracker.md`（登记本计划条目）

**Interfaces:**
- Consumes: 全部 Task 1-6

- [ ] **Step 1: 登记 `docs/INDEX.md` 与 tracker**

按既有格式加一行（Spec `2026-11-24-Spec-TaskReportHover-v1.0.md`、Plan `2026-11-24-Plan-TaskReportHover-v1.0.md`）。

- [ ] **Step 2: 运行全量 UI 门禁**

Run: `scripts\build-and-test.bat`
Expected: ALL TESTS PASSED（`UI_FLOATINGWIDGET` 已知 flaky 若失败则单跑复验，判定非回归）

- [ ] **Step 3: 全量 CTest（非 UI 单元）**

Run: `ctest --test-dir build -V`
Expected: 全绿（既有 `XrayManagerStartupTest` 环境基线除外，如再现按项目惯例记录）

- [ ] **Step 4: 提交**

```bash
git add docs/INDEX.md docs/plans/project-plans-tracker.md
git commit -m "docs: 登记任务完成报告+悬停显示 Spec/计划"
```

---

## Self-Review

- **Spec 覆盖**：R1（Task 1-3/5 生成/落盘）、R2（Task 2 `logDir/report_<taskId>.txt`）、R3 统计节（Task 1 定义 + Task 2 格式化 + Task 4 出参 + Task 5 lambda/do* 穿线）、R4（Task 6 悬停浮层）、R5（Task 7 文档+评审）。无缺口。
- **类型一致**：`task::TaskStats`（TaskType.h）/`LogLine`/`TaskReportInfo`/`TaskFinishedSink`（含 stats 三参）/`findTaskReport(taskId, out)`（bool + 副本，无指针逃逸）在 Task 1-6 间名称与签名一致。
- **Review Focus（评审后加固）**：写失败→Task 2 publish 写失败返回空 + 记 WARN + **不更新索引**（PublishWriteFailureReturnsEmptyAndNoIndex）；无日志行→Task 2 EmptyLogLinesAndZeroStatsStillPublishes；统计向后兼容→Task 4 NullStatsKeepsBehaviorUnchanged；悬停非完成行→Task 6 onListMotion 分支；同 taskId→Task 2 PublishOverwritesSameTaskId；回调异常→Task 3 try/catch pop；find 缺键→Task 2 MissingTaskIdFindReturnsFalseWithoutInserting；刷新不打断→Task 6 refreshNow 条件隐藏 + `popupTaskId_` 校验 + 行移位恢复；坐标/编码→Task 6 ClientToScreen + FromUTF8 + 钳制；浮层 wx API→Task 6 `wxPU_CONTAINS_CONTROLS`/`SetLabel`/两参 `Position`（`wxPU_NO_FOCUS`/`SetTitle` 不存在弃用）；DEDUP 链→Task 4/5 整链尾参（无 `doDeduplicate`）；AUTO_TASK→Task 4 `run()` 后填充；键盘路径→Task 6 `findTaskReport` 守卫 + 焦点行；R3：刷新顺序→Task 6/§7 固定 捕获→恢复→检查；Popup()→Spec R4 默认 null focus（`Popup(focus=false)` 参数是 `wxWindow*` 弃用）；Position 锚点→Task 6 `wxSize(0,0)` + `SetSize(560,420)`（size 作锚点偏移非尺寸）；鼠标离开→Task 6 `wxEVT_LEAVE_WINDOW` 隐藏；AUTO_TASK resume→Task 4 `run()`/`resume()` 双入口 + `doAutoTaskImpl` 双路径穿线；Task5 Step4 覆盖声明→含 Task2/5（Task1/3 单独验证）。
- **比例**：计划与 Spec 长度相当，代码块仅出现在决定性签名/断言处；Task 4（最大改动）给出模式+清单而非全量代码。
