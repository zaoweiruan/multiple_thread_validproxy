#include <gtest/gtest.h>
#include "AutoTaskManager.h"
#include "test_utils.h"

TEST(AutoTaskStepNameTest, stepNameToType) {
    EXPECT_EQ(AutoTaskManager::stepNameToType("update"), AutoTaskStepType::UPDATE_ALL);
    EXPECT_EQ(AutoTaskManager::stepNameToType("test"),   AutoTaskStepType::TEST_ALL);
    EXPECT_EQ(AutoTaskManager::stepNameToType("dedup"),  AutoTaskStepType::DEDUP);
    EXPECT_EQ(AutoTaskManager::stepNameToType("sync"),   AutoTaskStepType::SYNC);
    EXPECT_EQ(AutoTaskManager::stepNameToType("export"), AutoTaskStepType::EXPORT);
    EXPECT_EQ(AutoTaskManager::stepNameToType("unknown"), AutoTaskStepType::UPDATE_ALL);
}

TEST(AutoTaskStepNameTest, stepTypeToName) {
    EXPECT_EQ(AutoTaskManager::stepTypeToName(AutoTaskStepType::UPDATE_ALL), "update");
    EXPECT_EQ(AutoTaskManager::stepTypeToName(AutoTaskStepType::TEST_ALL),   "test");
    EXPECT_EQ(AutoTaskManager::stepTypeToName(AutoTaskStepType::DEDUP),      "dedup");
    EXPECT_EQ(AutoTaskManager::stepTypeToName(AutoTaskStepType::SYNC),       "sync");
    EXPECT_EQ(AutoTaskManager::stepTypeToName(AutoTaskStepType::EXPORT),     "export");
}

TEST(AutoTaskCreateStepListTest, createsCorrectSteps) {
    std::vector<std::string> names = {"update", "test", "dedup"};
    std::vector<AutoTaskStepInfo> steps = AutoTaskManager::createStepList(names);

    ASSERT_EQ(steps.size(), 3);
    EXPECT_EQ(steps[0].name, "update");
    EXPECT_EQ(steps[0].type, AutoTaskStepType::UPDATE_ALL);
    EXPECT_EQ(steps[0].status, StepStatus::PENDING);

    EXPECT_EQ(steps[1].name, "test");
    EXPECT_EQ(steps[1].type, AutoTaskStepType::TEST_ALL);

    EXPECT_EQ(steps[2].name, "dedup");
    EXPECT_EQ(steps[2].type, AutoTaskStepType::DEDUP);
}

TEST(AutoTaskCreateStepListTest, emptyInput) {
    std::vector<AutoTaskStepInfo> steps = AutoTaskManager::createStepList({});
    EXPECT_TRUE(steps.empty());
}

TEST(AutoTaskStatePersistenceTest, roundTripState) {
    TempDir tmp;

    AutoTaskState state;
    state.task_id = "test_task_001";
    state.created_at = "2026-06-15T09:00:00";
    state.current_step_index = 1;
    state.completed = false;
    state.cancelled = false;

    AutoTaskStepInfo s1;
    s1.name = "update";
    s1.status = StepStatus::COMPLETED;
    s1.started_at = "2026-06-15T09:00:00";
    s1.completed_at = "2026-06-15T09:02:00";

    AutoTaskStepInfo s2;
    s2.name = "test";
    s2.status = StepStatus::PENDING;

    state.steps.push_back(s1);
    state.steps.push_back(s2);

    std::string stateFile = tmp.path() + "/autotask_state.json";
    ASSERT_TRUE(AutoTaskManager::saveStateFile(stateFile, state));

    AutoTaskState loaded = AutoTaskManager::loadStateFile(stateFile);

    EXPECT_EQ(loaded.task_id, "test_task_001");
    EXPECT_EQ(loaded.created_at, "2026-06-15T09:00:00");
    EXPECT_EQ(loaded.current_step_index, 1);
    EXPECT_FALSE(loaded.completed);
    EXPECT_FALSE(loaded.cancelled);
    ASSERT_EQ(loaded.steps.size(), 2);

    EXPECT_EQ(loaded.steps[0].name, "update");
    EXPECT_EQ(loaded.steps[0].status, StepStatus::COMPLETED);
    EXPECT_EQ(loaded.steps[0].started_at, "2026-06-15T09:00:00");
    EXPECT_EQ(loaded.steps[0].completed_at, "2026-06-15T09:02:00");

    EXPECT_EQ(loaded.steps[1].name, "test");
    EXPECT_EQ(loaded.steps[1].status, StepStatus::PENDING);
}

TEST(AutoTaskStatePersistenceTest, loadMissingFile) {
    TempDir tmp;
    std::string badPath = tmp.path() + "/nonexistent.json";

    AutoTaskState loaded = AutoTaskManager::loadStateFile(badPath);
    EXPECT_TRUE(loaded.task_id.empty());
    EXPECT_EQ(loaded.current_step_index, 0);
    EXPECT_TRUE(loaded.steps.empty());
    EXPECT_FALSE(loaded.completed);
    EXPECT_FALSE(loaded.cancelled);
}

TEST(AutoTaskStatePersistenceTest, completedState) {
    TempDir tmp;

    AutoTaskState state;
    state.task_id = "completed_task";
    state.created_at = "2026-06-15T10:00:00";
    state.current_step_index = 3;
    state.completed = true;
    state.cancelled = false;

    AutoTaskStepInfo step;
    step.name = "export";
    step.status = StepStatus::COMPLETED;
    step.started_at = "2026-06-15T10:00:00";
    step.completed_at = "2026-06-15T10:01:00";
    state.steps.push_back(step);

    std::string stateFile = tmp.path() + "/completed_state.json";
    ASSERT_TRUE(AutoTaskManager::saveStateFile(stateFile, state));

    AutoTaskState loaded = AutoTaskManager::loadStateFile(stateFile);
    EXPECT_TRUE(loaded.completed);
    EXPECT_FALSE(loaded.cancelled);
    EXPECT_EQ(loaded.steps.size(), 1);
    EXPECT_EQ(loaded.steps[0].status, StepStatus::COMPLETED);
}

TEST(AutoTaskStatePersistenceTest, cancelledState) {
    TempDir tmp;

    AutoTaskState state;
    state.task_id = "cancelled_task";
    state.created_at = "2026-06-15T11:00:00";
    state.current_step_index = 0;
    state.completed = false;
    state.cancelled = true;

    AutoTaskStepInfo step;
    step.name = "update";
    step.status = StepStatus::CANCELLED;
    state.steps.push_back(step);

    std::string stateFile = tmp.path() + "/cancelled_state.json";
    ASSERT_TRUE(AutoTaskManager::saveStateFile(stateFile, state));

    AutoTaskState loaded = AutoTaskManager::loadStateFile(stateFile);
    EXPECT_FALSE(loaded.completed);
    EXPECT_TRUE(loaded.cancelled);
    EXPECT_EQ(loaded.steps[0].status, StepStatus::CANCELLED);
}
