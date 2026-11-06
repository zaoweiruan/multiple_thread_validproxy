// tests/test_task_cancel_reset.cpp — architecture guard for the
// cancel-flag-poisoning fix (bugfix 2026-11-03-Bugfix-CancelFlagPoisoning).
//
// Invariant: EVERY task body submitted to TaskQueue must reset
// AppController::cancelRequested_ on the worker (after the task starts), not
// only at submit time on the UI thread. Otherwise a cancel aimed at task A
// poisons task B that was queued behind it — B then aborts before testing a
// single proxy (observed 2026-11-03 16:08: A cancelled, B exited after 3s
// with "Remaining: 939").
//
// The bug was exactly one missing line in one task body, so a source-level
// guard catches the next regression faster than any runtime test.
#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#ifndef VALIDPROXY_SOURCE_DIR
#error "VALIDPROXY_SOURCE_DIR must be defined by CMake"
#endif

namespace {

std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

TEST(TaskCancelResetGuardTest, EverySubmitResetsCancelFlagOnWorker) {
    const std::string path = std::string(VALIDPROXY_SOURCE_DIR) + "/src/ui/AppController.cpp";
    const std::vector<std::string> lines = readLines(path);
    ASSERT_FALSE(lines.empty()) << "cannot read " << path;

    int checked = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("taskQueue_.submit(") == std::string::npos) continue;
        // Skip the comment lines that mention the API.
        if (lines[i].find("//") == 0) continue;

        bool resetsOnWorker = false;
        bool foundBodyEnd = false;
        // The lambda body closes with an 8-space-indented "}, " line.
        for (std::size_t j = i + 1; j < lines.size() && j <= i + 60; ++j) {
            if (lines[j].find("cancelRequested_ = false;") != std::string::npos) {
                resetsOnWorker = true;
            }
            if (startsWith(lines[j], "        }, ")) {
                foundBodyEnd = true;
                break;
            }
        }

        EXPECT_TRUE(foundBodyEnd)
            << "submit at AppController.cpp:" << (i + 1) << " — could not find the lambda body end";
        EXPECT_TRUE(resetsOnWorker)
            << "submit at AppController.cpp:" << (i + 1)
            << " — task body must set cancelRequested_ = false on the worker "
               "(a queued task must not inherit a cancel aimed at an earlier task)";
        ++checked;
    }

    // Guard against the scan silently matching nothing (file moved/renamed).
    EXPECT_GE(checked, 10) << "expected the full set of taskQueue_.submit() call sites";
}

// Invariant 2 (bugfix 2026-11-04 §4.3): subscription-scoped task types must
// pass subId as the submit targetId — otherwise the task list's name column
// shows "-" for update/resolve tasks (only TEST_SUBSCRIPTION passed it first).
TEST(TaskCancelResetGuardTest, SubscriptionScopedSubmitsPassTargetId) {
    const std::string path = std::string(VALIDPROXY_SOURCE_DIR) + "/src/ui/AppController.cpp";
    const std::vector<std::string> lines = readLines(path);
    ASSERT_FALSE(lines.empty()) << "cannot read " << path;

    static const char* kSubscriptionTypes[] = {
        "TaskType::UPDATE_SUBSCRIPTION",
        "TaskType::TEST_SUBSCRIPTION",
        "TaskType::RESOLVE_REGION",
    };

    int checked = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("taskQueue_.submit(") == std::string::npos) continue;
        if (lines[i].find("//") == 0) continue;

        bool subscriptionScoped = false;
        for (const char* type : kSubscriptionTypes) {
            if (lines[i].find(type) != std::string::npos) subscriptionScoped = true;
        }
        if (!subscriptionScoped) continue;

        // The submit call closes with "}, <prio>...);" — the targetId argument
        // must be subId (the callbacks-parameter default form "...subId);" too).
        bool passesTargetId = false;
        for (std::size_t j = i + 1; j < lines.size() && j <= i + 60; ++j) {
            if (startsWith(lines[j], "        }, ")) {
                passesTargetId = lines[j].find("subId)") != std::string::npos;
                break;
            }
        }
        EXPECT_TRUE(passesTargetId)
            << "submit at AppController.cpp:" << (i + 1)
            << " — subscription-scoped task must pass subId as the submit targetId "
               "(task-list name column resolves it via getSubscriptionRemarks)";
        ++checked;
    }

    EXPECT_EQ(checked, 3) << "expected UPDATE_SUBSCRIPTION / TEST_SUBSCRIPTION / RESOLVE_REGION submits";
}
