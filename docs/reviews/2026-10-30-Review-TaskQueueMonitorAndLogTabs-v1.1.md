# Review — 任务与队列监控面板 + 日志多标签页 (TaskQueueMonitorAndLogTabs) v1.1

- **日期**: 2026-10-30
- **被评审文档**: docs/specs/2026-10-30-Spec-TaskQueueMonitorAndLogTabs-v1.1.md
- **评审类型**: 独立评审（架构 / 实现 / 风险 / 测试 / 代码事实核对）
- **评审结论**: **CONDITIONAL PASS**（0 blockers, 2 important, 1 consistency）— 较 v1.0 的 4 blockers 全部解决

## 1. 评审范围

- v1.1 修订是否解决 v1.0 的 B1–B4 / I1–I6 / D1–D3。
- 代码事实核对（TaskQueue::cancel 实际回调、TaskHandle::createdAt 类型、EntryCompare 语义、proxyBatchTester_ 调用点数量、AutoTaskManager::PoolObserver 签名）。
- 新增风险：shared_ptr 迁移影响面、snapshot 排序正确性。

## 2. v1.0 Blockers 解决情况

| # | v1.0 阻断项 | v1.1 解决情况 | 判定 |
|---|---|---|---|
| B1 | priority_queue 遍历 | §3.2 成员函数内部访问 `queue_.c`；Entry 排序后再转换（避免 string 字典序） | ✅ RESOLVED |
| B2 | tester 生命周期 use-after-free | §2.3 shared_ptr + mutex 策略 + 析构顺序 + 备选 (b) | ✅ RESOLVED |
| B3/D1 | notebook 成员变量不一致 | §3.5 统一 `bottomNotebook_` 成员 | ✅ RESOLVED |
| B4 | 取消排队任务回调语义 | §3.6 按代码实际行为修正为 `onComplete`（onRejected 当前未实现） | ✅ RESOLVED（含代码事实修正） |

## 3. v1.0 Important 解决情况

| # | 项 | v1.1 | 判定 |
|---|---|---|---|
| I1 | timer 仅活动标签页 | §3.6/§3.7 `wxEVT_NOTEBOOK_PAGE_CHANGED` 门控 | ✅ |
| I2 | 标签含英文 TaskType | §3.1 格式 `"<中文> (TEST_ALL)"` | ✅ |
| I3 | LogPanel 布局 | §3.5 notebook MinSize，page 不 SetMinSize | ✅ |
| I4 | BatchQueuePanel 防抖 | §3.7 `running==false && processed==0` | ✅ |
| I5 | 控件 ID 枚举 + UI 测试用例 | §3.6/§3.7 enum + §5 T1–T5 | ✅ |
| I6 | pending 排序语义 | §3.2 priority desc → createdAt asc | ✅ |

## 4. v1.0 Consistency 解决情况

| # | 项 | v1.1 | 判定 |
|---|---|---|---|
| D1 | 成员变量 | §3.5 统一 | ✅ |
| D2 | 文件清单 | §4 补全 CMake/结构体位置/AutoTaskManager | ✅ |
| D3 | ProxyTestUnified Phase D 分工 | §8 明确边界 | ✅ |

## 5. 代码事实核对（v1.1 新增）

1. **TaskQueue::cancel 实际回调**：代码中 `cancel()` 对排队任务调用 `onComplete`（不是 `onRejected`），且 `onRejected` 全仓从未被调用。v1.1 §3.6 已按代码事实修正为 `onComplete`（status=CANCELLED），并明确 onRejected 接入超出本 spec 范围。✅ 修正正确。

2. **TaskHandle::createdAt 类型**：`std::string`（steady_clock count 的字符串）。v1.0 的 TaskHandle 字符串排序存在字典序陷阱（"9" > "10"）。v1.1 §3.2 改为先对 `Entry`（`createdAt` 为 `steady_clock::time_point`）排序再转换。✅ 修正正确且必要。

3. **EntryCompare 语义**：max-heap by priority（`a.priority < b.priority`），同 priority createdAt asc（`a.createdAt > b.createdAt`）。v1.1 排序 `priority desc → createdAt asc` 与之一致。✅

4. **proxyBatchTester_ 调用点**：实际 11 处（非 v1.0 的"约 8 处"）。v1.1 §2.3 已更新为 11 处并列出。✅

5. **AutoTaskManager::PoolObserver**：`std::function<void(ProxyBatchTester*)>`。v1.1 §2.3/§4 已纳入迁移影响。✅

## 6. Important（建议修订，非阻断）

### I1. shared_ptr 迁移的 Phase 1 可行性验证

v1.1 选定方案 (a) shared_ptr + mutex，但迁移影响面比 §2.3 描述的更大：
- `doTestAllProxies` / `testSelectedProxiesAsync` / `doTestSubscription` / auto-task step 共 4 处 `std::make_unique<ProxyBatchTester>` → `std::make_shared`；
- `proxyBatchTester_.store(tester.get())` → `proxyBatchTester_ = tester`（lock）共 11 处；
- `AutoTaskManager::setPoolObserver` lambda 中 `proxyBatchTester_.store(tester)` → 需 lock+assign shared_ptr（或 AutoTaskManager 持有 shared_ptr 并传递）；
- try/catch 中的 `proxyBatchTester_.store(nullptr)` → `reset()`。

**建议**：Phase 1 必须先完成迁移并编译 + 跑 `MutexPolicyTest`/`TaskQueueTest`/`ProxyBatchTester` 相关测试，若迁移 proves 过于侵入，采用 §2.3 备选 (b)。Phase 1 完成后更新 §2.3 选定方案。不阻断 spec 通过。

### I2. snapshot() 读取 running_/busy_ 的原子语义

§3.2 在持 `mutex_` 下读 `running_.load()`/`busy_.load()`。`running_`/`busy_` 在 workerLoop 中是否始终在 `mutex_` 保护下写入？需 Phase 1 实现时确认（若 workerLoop 在无锁路径写入，则 snapshot 读取可能看到不一致的 current_+running_ 组合）。建议 Phase 1 在实现 snapshot 时对 `current_` 与 `running_` 的一致性做一次代码审查。不阻断。

## 7. Consistency（文档一致性）

### C1. §3.2 代码片段中的 `snap.current = current_.handle`

`TaskHandle` 无 `.handle` 字段；`current_` 本身就是 `TaskHandle`。应为 `snap.current = current_;`。需修正。

## 8. 风险评估

| 风险 | 等级 | 缓解 |
|---|---|---|
| shared_ptr 迁移影响面（I1） | 中 | Phase 1 先迁移 + 编译 + 单测；备选 (b) |
| snapshot 原子一致性（I2） | 低 | Phase 1 代码审查 |
| 代码片段笔误（C1） | 低 | 修订 spec |
| wxNotebook 布局回归 | 中 | Phase 2 独立验证日志功能 |
| 控件 ID 不稳定影响 UI 测试 | 中 | §3.6/§3.7 已补充 ID 枚举 |

## 9. 评分

| 维度 | v1.0 | v1.1 | 说明 |
|---|---|---|---|
| 技术可行性 | 7.5 | 8.5 | B2 生命周期方案明确，B1 遍历方式可行 |
| 可实施性 | 6.5 | 8.0 | B1/B3/B4 已解决；shared_ptr 迁移需 Phase 1 验证 |
| 文档完整性 | 7.0 | 8.5 | ID 枚举、回调语义、排序语义、迁移影响均已补充 |
| 验收可测性 | 6.5 | 8.0 | UI 测试用例 T1–T5 已列出 |
| **综合** | **7.0** | **8.3** | **CONDITIONAL PASS**（0 blockers, 2 important, 1 consistency） |

## 10. 修订建议汇总

1. 修正 C1：§3.2 代码片段 `snap.current = current_;`（非 `current_.handle`）。
2. Phase 1 实施时验证 I1（shared_ptr 迁移）与 I2（原子一致性），选定最终方案并更新 §2.3。
3. 其余 I1/I2 不阻断实施。

修订/修正 C1 后，建议升级为 **PASS**，进入 Phase 1 实施。

---

*本评审为 v1.1 独立评审，评审对象 docs/specs/2026-10-30-Spec-TaskQueueMonitorAndLogTabs-v1.1.md。v1.0 评审（docs/reviews/2026-10-30-Review-TaskQueueMonitorAndLogTabs-v1.0.md）的 B1–B4/I1–I6/D1–D3 全部在 v1.1 中解决。*