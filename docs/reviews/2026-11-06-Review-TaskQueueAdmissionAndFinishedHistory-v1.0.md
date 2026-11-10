# Review — TaskQueueAdmissionAndFinishedHistory v1.0（独立评审记录）

- **日期**: 2026-11-06
- **对象**: `docs/specs/2026-11-06-Spec-TaskQueueAdmissionAndFinishedHistory-v1.0.md`
- **方式**: 5 并行 persona 子代理（coherence / feasibility / design-lens / adversarial / scope-guardian），
  全部要求对照真实代码核验，禁止臆测
- **产出**: 29 条去重发现（4 blocker、1 critical、若干 important/consistency）→ 全部处置，
  定稿 `docs/specs/2026-11-06-Spec-TaskQueueAdmissionAndFinishedHistory-v1.1.md`

## 结论摘要

| Persona | 关键判定 |
|---------|----------|
| coherence | §2.5 标题自相矛盾；16≠13 计数；recentParent 与 finished_ 共存逻辑无法同时满足两条既有测试（必须二选一）；DEDUP「仅 subitem 表」缺证据（实为双表） |
| feasibility | `wxEVT_LIST_CONTEXT_MENU` 不存在（编译不过）；`wxRadioBox::SetString` MSW 整框重建销毁 UIA；本地 wx 实为 3.3.1a；V8 原生 `#32768` 菜单 UIA 可达（不必降级）；软上限 50 无反馈通道不可实现；`clearFinished` 不重置 `historySeq_` 将 RED 既有测试；`UpdateSingleSubPassesSubIdToProgress` 为 v1.2.7 遗留既有 RED |
| design-lens | 1s `DeleteAllItems` 全量重建销毁 selection+scroll，200 行规模为核心缺陷；状态栏 field0 运行中每秒被测试进度覆盖（保护位 `statusTextProtected_` 可用）；FAILED 无视觉区分；空列表/徽章溢出无定义；「清理」动词含混；自动切页被重新提出（owner 已裁决不做） |
| adversarial | **critical**：带积压退出 → `~AppController` 先 close db/release Xray，`~TaskQueue` 后 join，worker 继续执行排队任务并 reopen 已关 .db、投递到已销毁窗口，卡死 5s 强拆；**blocker**：入口副作用发生在 CLICK 时刻（错类型取消、`cancelRequested_` 毒化排队任务、lambda 捕获陈旧 subId/行号、两次 `updateSubscrip` 交错复活已删订阅）；`TaskQueue::cancel(runningId)` 静默 no-op；taskId 唯一性（seq 后缀）无碰撞——此点未能攻破；RESOLVE 排队 40 分钟后陈旧盲发 |
| scope-guardian | G10「纯 UI 模态」为事实错误（确认路径 `updateSubitem`/`deleteSubscription` 同步 DB 写，`updateSubscrip` 先删后插）；G11/G12「排队≠安全」；G13 入队会静默丢弃 AUTO_RESUME 恢复（handler 在 `onComplete` 丢弃 worker 返回值）；软上限 50 违背 owner 裁决应砍；RowKind/onStarted 过度设计；MutexPolicy 改注释零收益；R-1 违背 owner 决策应关闭 |

## 交叉验证（作者复核，全部命中）

- `listbase.h:598` 仅 `wxEVT_LIST_ITEM_RIGHT_CLICK`，无 CONTEXT_MENU 事件 ✓
- `msw/radiobox.h:90` 仅有 `SetString(n,label)` ✓
- `statusTextProtected_`：`MainFrame.cpp:345/350/377/1660` ✓
- `AppController.cpp`：`~AppController` L75、`XrayManager::release` L135、`sqlite3_close(db_)` L144 ✓
- `DeduplicationManager.cpp:186/365/385` 双表（profileitem+subitem）✓
- `TaskQueue.h:69` `cancel(taskId)` 存在；RUNNING 分支 no-op ✓
- `TaskMonitorPanel.cpp:78-82` 仅按钮 Bind + selection Bind，`refreshNow` L118 `DeleteAllItems` ✓
- `UiControlIds.h` 现用到 `wxID_HIGHEST+1406`，+1500 段空闲 ✓

## 处置

v1.0 保留为历史草案；定稿见 v1.1（§10 评审处置表 29/29 全量对齐，含 3 个 owner 裁决点
R-1/R-2/R-3 与 8 项范围外登记）。
