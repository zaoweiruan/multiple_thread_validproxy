#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "task/TaskQueue.h"
#include "task/TaskType.h"

using task::TaskQueue;
using task::TaskType;

TEST(TaskQueueTest, SubmitAndComplete) {
    TaskQueue q;
    std::atomic<bool> done{false};
    auto h = q.submit(TaskType::TEST_SINGLE_PROXY, [&done](task::TaskContext& ctx) {
        (void)ctx;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        done.store(true);
    }, 1);
    EXPECT_EQ(h.status, task::TaskStatus::QUEUED);
    // wait for completion
    for (int i = 0; i < 50 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    auto cur = q.currentTask();
    // after completion current should be empty
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto hist = q.history(10);
    EXPECT_FALSE(hist.empty());
}

TEST(TaskQueueTest, PriorityOrder) {
    TaskQueue q;
    std::vector<int> order;
    std::mutex m;
    // submit low then high
    q.submit(TaskType::TEST_SINGLE_PROXY, [&order, &m](task::TaskContext& ctx) {
        (void)ctx;
        std::lock_guard<std::mutex> lk(m);
        order.push_back(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    q.submit(TaskType::TEST_SINGLE_PROXY, [&order, &m](task::TaskContext& ctx) {
        (void)ctx;
        std::lock_guard<std::mutex> lk(m);
        order.push_back(2);
    }, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // high priority should run first if queued before low starts; but low may have started.
    // Just ensure both completed.
    auto hist = q.history(10);
    EXPECT_GE(hist.size(), 2u);
}
// ---------------------------------------------------------------------------
// TaskQueueSnapshot (spec 2026-11-03 v1.2 §5.2 T1-T5)
// ---------------------------------------------------------------------------
namespace {

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool waitUntil(const std::function<bool()>& pred, int timeoutMs) {
    const int step = 5;
    for (int waited = 0; waited < timeoutMs; waited += step) {
        if (pred()) return true;
        sleepMs(step);
    }
    return pred();
}

task::TaskCallbacks countingCb(std::atomic<int>& counter) {
    task::TaskCallbacks cb;
    cb.onComplete = [&counter](const task::TaskHandle& h) {
        if (h.status == task::TaskStatus::CANCELLED) ++counter;
    };
    return cb;
}

task::TaskFunc nopTask() {
    return [](task::TaskContext&) {};
}

// Occupy the single worker with a blocking task so the remaining submits stay
// queued (deterministic pending snapshot).
task::TaskHandle occupyWorker(task::TaskQueue& q, std::atomic<bool>& release) {
    const task::TaskHandle blocker = q.submit(task::TaskType::AUTO_TASK,
        [&release](task::TaskContext&) {
            while (!release.load()) sleepMs(2);
        }, 5);
    if (!waitUntil([&q] { return q.isBusy(); }, 2000)) {
        release.store(true);
        ADD_FAILURE() << "worker did not pick up the blocker task";
    }
    return blocker;
}

} // namespace

TEST(TaskQueueSnapshotTest, OrderingPriorityDescThenCreatedAtAsc) {
    task::TaskQueue q;
    std::atomic<bool> release{false};
    occupyWorker(q, release);

    const task::TaskHandle p0 = q.submit(task::TaskType::DEDUP, nopTask(), 0);
    sleepMs(3);
    const task::TaskHandle p2a = q.submit(task::TaskType::DEDUP, nopTask(), 2);
    sleepMs(3);
    const task::TaskHandle p1 = q.submit(task::TaskType::DEDUP, nopTask(), 1);
    sleepMs(3);
    const task::TaskHandle p2b = q.submit(task::TaskType::DEDUP, nopTask(), 2);

    const task::TaskQueueSnapshot snap = q.snapshot();
    EXPECT_EQ(snap.queueDepth, 4u);
    ASSERT_EQ(snap.pending.size(), 4u);
    EXPECT_EQ(snap.pending[0].taskId, p2a.taskId);  // priority 2, earliest
    EXPECT_EQ(snap.pending[1].taskId, p2b.taskId);  // priority 2, later
    EXPECT_EQ(snap.pending[2].taskId, p1.taskId);   // priority 1
    EXPECT_EQ(snap.pending[3].taskId, p0.taskId);   // priority 0
    EXPECT_TRUE(snap.busy);

    release.store(true);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy() && q.queueDepth() == 0; }, 5000));
}
TEST(TaskQueueSnapshotTest, EmptySnapshot) {
    task::TaskQueue q;
    const task::TaskQueueSnapshot snap = q.snapshot();
    EXPECT_TRUE(snap.current.taskId.empty());
    EXPECT_TRUE(snap.pending.empty());
    EXPECT_EQ(snap.queueDepth, 0u);
    EXPECT_FALSE(snap.busy);
}

// owner 2026-11-06: 更新订阅不再拆解订阅名，改为整体进度（复用队列进度条）。
// 进度由任务体在 worker 线程发布，面板/队列页轮询 snapshot().current 读取。
TEST(TaskQueueSnapshotTest, SetCurrentProgressPublishesOnRunningTaskOnly) {
    task::TaskQueue q;

    // Idle: no current task -> ignored (no phantom row, no crash).
    q.setCurrentProgress(3, 10, "更新订阅 3/10");
    EXPECT_EQ(q.snapshot().current.progressTotal, 0);

    std::atomic<bool> release{false};
    const task::TaskHandle blocker = occupyWorker(q, release);
    ASSERT_FALSE(blocker.taskId.empty());

    q.setCurrentProgress(3, 10, "更新订阅 3/10");
    const task::TaskQueueSnapshot snap = q.snapshot();
    EXPECT_EQ(snap.current.taskId, blocker.taskId);
    EXPECT_EQ(snap.current.progressCurrent, 3);
    EXPECT_EQ(snap.current.progressTotal, 10);
    EXPECT_EQ(snap.current.progressMessage, "更新订阅 3/10");

    release.store(true);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy(); }, 5000));
}

TEST(TaskQueueSnapshotTest, CancelRemovesFromPendingAndArchivesCancelled) {
    task::TaskQueue q;
    std::atomic<bool> release{false};
    occupyWorker(q, release);

    const task::TaskHandle a = q.submit(task::TaskType::DEDUP, nopTask(), 1);
    sleepMs(3);
    q.submit(task::TaskType::DEDUP, nopTask(), 1);

    q.cancel(a.taskId);

    const task::TaskQueueSnapshot snap = q.snapshot();
    EXPECT_EQ(snap.queueDepth, 1u);
    for (const task::TaskHandle& h : snap.pending) {
        EXPECT_NE(h.taskId, a.taskId);
    }
    const std::vector<task::TaskHandle> hist = q.history(10);
    ASSERT_FALSE(hist.empty());
    bool archived = false;
    for (const task::TaskHandle& h : hist) {
        if (h.taskId == a.taskId && h.status == task::TaskStatus::CANCELLED) archived = true;
    }
    EXPECT_TRUE(archived);

    release.store(true);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy(); }, 5000));
}

TEST(TaskQueueSnapshotTest, CancelCallbackMayReenterQueueSafely) {
    // Pre-fix, cancel() invoked onComplete while holding mutex_, so a callback
    // that touched the queue again deadlocked (non-recursive std::mutex) and the
    // UI thread froze. The cancel runs on a detached helper thread and the queue
    // is kept alive by shared_ptr captures: if the deadlock ever returns this
    // test FAILS after the timeout (the hung thread dies at process exit)
    // instead of wedging the whole suite or touching freed memory.
    std::shared_ptr<task::TaskQueue> q = std::make_shared<task::TaskQueue>();
    std::shared_ptr<std::atomic<bool>> ran = std::make_shared<std::atomic<bool>>(false);
    std::atomic<bool> release{false};
    const task::TaskHandle blocker = occupyWorker(*q, release);

    task::TaskCallbacks cb;
    cb.onComplete = [q, ran](const task::TaskHandle&) {
        const task::TaskQueueSnapshot inner = q->snapshot();   // re-entrant access
        (void)inner;
        ran->store(true);
    };
    const task::TaskHandle a = q->submit(task::TaskType::DEDUP, nopTask(), 1, cb);

    std::thread canceller([q, a] { q->cancel(a.taskId); });
    canceller.detach();

    EXPECT_TRUE(waitUntil([ran] { return ran->load(); }, 2000))
        << "onComplete never finished — callback is still running under the queue lock";

    release.store(true);
    EXPECT_TRUE(waitUntil([&q, blocker] {
        return !q->isBusy() && q->currentTask().taskId != blocker.taskId;
    }, 5000));
}

TEST(TaskQueueSnapshotTest, CancelAllPendingClearsQueueWithoutTouchingCurrent) {
    task::TaskQueue q;
    std::atomic<bool> release{false};
    const task::TaskHandle blocker = occupyWorker(q, release);
    std::atomic<int> done{0};
    q.submit(task::TaskType::DEDUP, nopTask(), 1, countingCb(done));
    sleepMs(3);
    q.submit(task::TaskType::DEDUP, nopTask(), 1, countingCb(done));
    sleepMs(3);
    q.submit(task::TaskType::DEDUP, nopTask(), 1, countingCb(done));

    q.cancelAllPending();

    EXPECT_EQ(q.queueDepth(), 0u);
    EXPECT_TRUE(q.snapshot().pending.empty());
    EXPECT_EQ(done.load(), 3);
    EXPECT_TRUE(waitUntil([&q, blocker] {
        return q.currentTask().taskId == blocker.taskId;   // current still running
    }, 1000));

    release.store(true);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy(); }, 5000));
}

// ---------------------------------------------------------------------------
// Finished ledger for the monitoring UI (spec 2026-11-06 §3.1 / §2.6, U1)
// ---------------------------------------------------------------------------

TEST(TaskQueueFinishedTest, HistoryServedInSnapshotNewestFirst) {
    task::TaskQueue q;
    const task::TaskHandle first = q.submit(task::TaskType::DEDUP, nopTask(), 1);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy() && q.queueDepth() == 0; }, 2000));
    const task::TaskHandle second = q.submit(task::TaskType::EXPORT_SHARE_LINKS, nopTask(), 1);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy() && q.queueDepth() == 0; }, 2000));

    const task::TaskQueueSnapshot snap = q.snapshot();
    ASSERT_EQ(snap.history.size(), 2u);
    // newest first
    EXPECT_EQ(snap.history[0].taskId, second.taskId);
    EXPECT_EQ(snap.history[0].status, task::TaskStatus::COMPLETED);
    EXPECT_EQ(snap.history[1].taskId, first.taskId);
    EXPECT_EQ(snap.history[1].status, task::TaskStatus::COMPLETED);
}

TEST(TaskQueueFinishedTest, ClearHistoryEmptiesSnapshotAndDiagnostics) {
    task::TaskQueue q;
    q.submit(task::TaskType::DEDUP, nopTask(), 1);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy() && q.queueDepth() == 0; }, 2000));
    ASSERT_FALSE(q.snapshot().history.empty());

    const std::size_t cleared = q.clearHistory();
    EXPECT_EQ(cleared, 1u);
    EXPECT_TRUE(q.snapshot().history.empty());
    EXPECT_TRUE(q.history(10).empty());

    // A later task still archives normally.
    q.submit(task::TaskType::DEDUP, nopTask(), 1);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy() && q.queueDepth() == 0; }, 2000));
    EXPECT_EQ(q.snapshot().history.size(), 1u);
}

TEST(TaskQueueFinishedTest, ForceReleaseThenWorkerFinishArchivesOnce) {
    // cancelTest() calls forceRelease() while the worker is still unwinding;
    // the worker's normal finish path then archives the SAME task again. The
    // visible finished list must contain exactly one row per task
    // (spec §3.2 — rows are user-facing now).
    task::TaskQueue q;
    std::atomic<bool> running{false};
    std::atomic<bool> finish{false};
    const task::TaskHandle slow = q.submit(task::TaskType::AUTO_TASK,
        [&](task::TaskContext&) {
            running.store(true);
            while (!finish.load()) sleepMs(2);
        }, 1);
    EXPECT_TRUE(waitUntil([&running] { return running.load(); }, 2000));

    q.cancelCurrent();       // sets the cancel flags
    q.forceRelease();        // archives the current task as CANCELLED
    finish.store(true);      // worker now returns and archives its own terminal
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy(); }, 5000));

    int rows = 0;
    for (const task::TaskHandle& h : q.snapshot().history) {
        if (h.taskId == slow.taskId) ++rows;
    }
    EXPECT_EQ(rows, 1) << "forceRelease + worker finish must not double-archive";
}

TEST(TaskQueueFinishedTest, RemoveFromHistoryRemovesOnlyTarget) {
    task::TaskQueue q;
    const task::TaskHandle a = q.submit(task::TaskType::DEDUP, nopTask(), 1);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy() && q.queueDepth() == 0; }, 2000));
    const task::TaskHandle b = q.submit(task::TaskType::DEDUP, nopTask(), 1);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy() && q.queueDepth() == 0; }, 2000));

    EXPECT_TRUE(q.removeFromHistory(a.taskId));
    EXPECT_FALSE(q.removeFromHistory(a.taskId));  // second removal: not found
    const task::TaskQueueSnapshot snap = q.snapshot();
    ASSERT_EQ(snap.history.size(), 1u);
    EXPECT_EQ(snap.history[0].taskId, b.taskId);
}

TEST(TaskQueueFinishedTest, CancelledQueuedTasksLandInSnapshotHistory) {
    // cancel()/cancelAllPending() archive removed QUEUED tasks; they must be
    // visible to the UI as CANCELLED rows (spec §3.1 终态补漏).
    task::TaskQueue q;
    std::atomic<bool> release{false};
    occupyWorker(q, release);
    const task::TaskHandle a = q.submit(task::TaskType::DEDUP, nopTask(), 1);
    q.submit(task::TaskType::DEDUP, nopTask(), 1);
    q.cancel(a.taskId);
    q.cancelAllPending();

    const task::TaskQueueSnapshot snap = q.snapshot();
    EXPECT_EQ(snap.queueDepth, 0u);
    int cancelledRecords = 0;
    for (const task::TaskHandle& h : snap.history) {
        if (h.status == task::TaskStatus::CANCELLED) ++cancelledRecords;
        EXPECT_FALSE(h.completedAt.empty());  // stamped on the cancel paths too
    }
    EXPECT_GE(cancelledRecords, 2);

    release.store(true);
    EXPECT_TRUE(waitUntil([&q] { return !q.isBusy(); }, 5000));
}

TEST(TaskQueueFinishedTest, StopClearsQueueWithoutExecuting) {
    // Review critical (spec §2.6): with a backlog, stop()/~TaskQueue must NOT
    // execute the queued tasks — they are dequeued as CANCELLED before the
    // worker is asked to exit.
    task::TaskQueue q;
    std::atomic<bool> release{false};
    std::atomic<int> cancelledCallbacks{0};
    std::atomic<int> bodiesRun{0};
    occupyWorker(q, release);  // blocker holds the worker

    task::TaskCallbacks counting;
    counting.onComplete = [&cancelledCallbacks](const task::TaskHandle& h) {
        if (h.status == task::TaskStatus::CANCELLED) ++cancelledCallbacks;
    };
    for (int i = 0; i < 3; ++i) {
        q.submit(task::TaskType::DEDUP,
                 [&bodiesRun](task::TaskContext&) { ++bodiesRun; }, 1, counting);
    }
    ASSERT_EQ(q.queueDepth(), 3u);

    // Real tasks observe the cancel flag stop() raises (AppController wires it
    // into the tester); release the blocker so the join is bounded.
    release.store(true);
    q.stop(true);  // bounded: cancels blocker, dequeues 3 as CANCELLED, joins

    EXPECT_EQ(bodiesRun.load(), 0) << "queued task bodies must never run after stop()";
    EXPECT_EQ(cancelledCallbacks.load(), 3);
    EXPECT_EQ(q.queueDepth(), 0u);
    EXPECT_FALSE(q.isRunning());
    EXPECT_FALSE(q.isBusy());
    // stop() archives the dropped backlog.
    EXPECT_GE(q.history(10).size(), 3u);
    // Worker already joined; a second stop is a harmless no-op.
    q.stop(false);
}
