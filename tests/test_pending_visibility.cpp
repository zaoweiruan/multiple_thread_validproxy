// tests/test_pending_visibility.cpp — queued tasks must be visible in snapshot().pending
//
// owner 反馈单: "进行两个不同订阅测试,任务、队列都没有标识/没有排队任务显示"。
// 本测试固定队列侧契约: 任务 A 执行期间提交 B、C, snapshot() 必须立刻看到 A 为
// current、B/C 为 pending, 且 submit 传入的 targetId 原样透传到快照(订阅标识)。
// 若此测试绿而界面不显示 => 问题在运行的 exe 陈旧或 UI 侧, 与队列无关。

#include <gtest/gtest.h>

#include "task/TaskQueue.h"
#include "task/TaskType.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace {

bool waitForPending(const task::TaskQueue& q, size_t expected, int timeoutMs) {
    for (int waited = 0; waited < timeoutMs; waited += 20) {
        if (q.snapshot().pending.size() >= expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

} // namespace

TEST(PendingVisibility, QueuedTasksAppearInSnapshotWithTargetId) {
    task::TaskQueue q;

    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> release{false};
    const task::TaskHandle a = q.submit(
        task::TaskType::TEST_SUBSCRIPTION,
        [&blockerStarted, &release](task::TaskContext&) {
            blockerStarted = true;
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        },
        1, task::TaskCallbacks{}, "sub-AAA");
    for (int i = 0; i < 100 && !blockerStarted.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(blockerStarted.load());

    // A 正在执行 → current=A, 此时排队两个任务。
    const task::TaskHandle b = q.submit(
        task::TaskType::TEST_SUBSCRIPTION,
        [](task::TaskContext&) {},
        1, task::TaskCallbacks{}, "sub-BBB");
    const task::TaskHandle c = q.submit(
        task::TaskType::TEST_ALL,
        [](task::TaskContext&) {},
        1, task::TaskCallbacks{}, "");

    ASSERT_TRUE(waitForPending(q, 2, 2000));

    const task::TaskQueueSnapshot snap = q.snapshot();
    EXPECT_EQ(snap.current.taskId, a.taskId);
    EXPECT_EQ(snap.current.targetId, "sub-AAA");   // 当前任务标识
    ASSERT_GE(snap.pending.size(), 2u);

    // pending 必须包含 B（带 subId 标识）和 C（无单目标 → 空）。
    bool foundB = false, foundC = false;
    for (const task::TaskHandle& h : snap.pending) {
        if (h.taskId == b.taskId) {
            foundB = true;
            EXPECT_EQ(h.targetId, "sub-BBB");       // 排队任务标识
            EXPECT_EQ(h.status, task::TaskStatus::QUEUED);
        }
        if (h.taskId == c.taskId) {
            foundC = true;
            EXPECT_TRUE(h.targetId.empty());        // 无单目标任务 → 空
        }
    }
    EXPECT_TRUE(foundB);
    EXPECT_TRUE(foundC);

    release = true;
}

// ── auto-task step breakdown (owner request 2026-11-04) ─────────────────
TEST(PendingVisibility, ChildTasksAreBoundToCurrentAndClearedOnTaskEnd) {
    task::TaskQueue q;

    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> release{false};
    q.submit(task::TaskType::AUTO_TASK,
             [&blockerStarted, &release](task::TaskContext&) {
                 blockerStarted = true;
                 while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
             },
             1, task::TaskCallbacks{}, "");
    for (int i = 0; i < 100 && !blockerStarted.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(blockerStarted.load());

    // Breakdown with ONE step RUNNING (mirrors the AppController step observer).
    task::TaskHandle step1;
    step1.taskId = "auto-1-update";
    step1.type = task::TaskType::UPDATE_ALL;
    step1.status = task::TaskStatus::COMPLETED;
    step1.targetId = "全部更新";
    task::TaskHandle step2;
    step2.taskId = "auto-2-test";
    step2.type = task::TaskType::TEST_ALL;
    step2.status = task::TaskStatus::RUNNING;
    step2.targetId = "全部测试";
    task::TaskHandle step3;
    step3.taskId = "auto-3-dedup";
    step3.type = task::TaskType::DEDUP;
    step3.status = task::TaskStatus::PENDING;
    step3.targetId = "清洗";
    q.setChildTasks({step1, step2, step3});

    const task::TaskQueueSnapshot mid = q.snapshot();
    ASSERT_EQ(mid.children.size(), 3u);
    // parentTaskId bound to the running AUTO_TASK row; counters maintained.
    EXPECT_EQ(mid.children[0].parentTaskId, mid.current.taskId);
    EXPECT_EQ(mid.current.childTotal, 3);
    EXPECT_EQ(mid.current.childCompleted, 1);
    EXPECT_EQ(mid.current.childFailed, 0);
    // Children are monitor-only: they must NOT enter the executable queue.
    EXPECT_EQ(mid.queueDepth, 0u);

    release = true;
    // After the parent finishes, a later snapshot must show no stale children.
    bool cleared = false;
    for (int i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const task::TaskQueueSnapshot after = q.snapshot();
        if (after.current.taskId.empty() && after.children.empty()) { cleared = true; break; }
    }
    EXPECT_TRUE(cleared) << "children must die with their parent task";
}

TEST(PendingVisibility, ChildTasksIgnoredWithoutCurrentTask) {
    task::TaskQueue q;
    task::TaskHandle step;
    step.taskId = "auto-1-update";
    q.setChildTasks({step});
    EXPECT_TRUE(q.snapshot().children.empty());   // no parent → drop the rows
}

// ── v1.2.6 §14.2 终态保留（owner 2026-11-05: "批量更新队列没有实现" —— 6 秒
// 的 UPDATE_ALL 一结束行就消失，用户以为没实现）──
// 契约：任务结束后 snapshot().recentParent/recentChildren 保留父+子行直到
// 下一个任务 START；children 本身仍随 current 清空（旧契约不变）。
TEST(PendingVisibility, FinishedTaskStaysVisibleAsRecentUntilNextStart) {
    task::TaskQueue q;   // worker auto-starts (see ctor/submit)

    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> release{false};
    q.submit(task::TaskType::UPDATE_ALL,
             [&blockerStarted, &release](task::TaskContext&) {
                 blockerStarted = true;
                 while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
             },
             1, task::TaskCallbacks{}, "");
    for (int i = 0; i < 100 && !blockerStarted.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(blockerStarted.load());

    task::TaskHandle child;
    child.taskId = "upd-s1";
    child.type = task::TaskType::UPDATE_SUBSCRIPTION;
    child.status = task::TaskStatus::COMPLETED;
    child.targetId = "订阅一";
    q.setChildTasks({child});

    release = true;
    // 等父任务结束：recent 出现、children 清空。
    bool kept = false;
    for (int i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const task::TaskQueueSnapshot after = q.snapshot();
        if (after.current.taskId.empty() && !after.recentParent.taskId.empty()) {
            kept = true;
            EXPECT_TRUE(after.children.empty());
            ASSERT_EQ(after.recentChildren.size(), 1u);
            EXPECT_EQ(after.recentChildren[0].taskId, "upd-s1");
            EXPECT_TRUE(after.recentParent.status == task::TaskStatus::COMPLETED ||
                        after.recentParent.status == task::TaskStatus::FAILED);
            break;
        }
    }
    EXPECT_TRUE(kept) << "finished task must stay visible via recent*";

    // 下一个任务 START 时 recent 被清掉。
    std::atomic<bool> secondStarted{false};
    q.submit(task::TaskType::TEST_ALL,
             [&secondStarted](task::TaskContext&) {
                 secondStarted = true;
                 std::this_thread::sleep_for(std::chrono::milliseconds(50));
             },
             1, task::TaskCallbacks{}, "");
    for (int i = 0; i < 200 && !secondStarted.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(secondStarted.load());
    const task::TaskQueueSnapshot during = q.snapshot();
    EXPECT_TRUE(during.recentParent.taskId.empty());
    EXPECT_TRUE(during.recentChildren.empty());
}   // dtor joins the worker (second task has slept 50ms — already done)
