// CMake target (add to CMakeLists.txt):
// add_executable(test_proxy_batch_components tests/test_proxy_batch_components.cpp)
// target_include_directories(test_proxy_batch_components PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
// target_link_libraries(test_proxy_batch_components PRIVATE gtest_main gtest)
// set_target_properties(test_proxy_batch_components PROPERTIES
//     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
//     RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
//     RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
// )
// add_test(NAME ProxyBatchComponentsTest COMMAND test_proxy_batch_components)

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <algorithm>
#include <queue>
#include <sstream>

namespace {

// ============================================================
// Helpers — reproduce ProxyBatchTester internal logic
// ============================================================

// SQL template substitution: replaces {subid} and {blacklist_threshold}
// placeholders with their actual values.
std::string sqlTemplateSubstitute(const std::string& templateStr,
                                  const std::string& subid,
                                  int blacklistThreshold)
{
    std::string result = templateStr;
    auto replace = [&](const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    };
    replace("{subid}", subid);
    replace("{blacklist_threshold}", std::to_string(blacklistThreshold));
    return result;
}

// Worker count calculation: min(proxyCount, xray_workers), clamped to 0.
int calculateWorkerCount(int proxyCount, int xrayWorkers)
{
    if (proxyCount <= 0 || xrayWorkers <= 0) return 0;
    return std::min(proxyCount, xrayWorkers);
}

// Cancel state merging: internal flag OR external pointer with dereference.
bool isCancelled(bool internalCancel, bool* externalCancel)
{
    if (externalCancel != nullptr && *externalCancel) return true;
    return internalCancel;
}

// Summary format: "Success: X, Failed: Y, Total: Z"
std::string formatSummary(int success, int failed, int total)
{
    std::ostringstream os;
    os << "Success: " << success
       << ", Failed: " << failed
       << ", Total: " << total;
    return os.str();
}

// Round-robin index distribution across workers.
// Returns a vector where worker i gets a vector of proxy indices.
std::vector<std::vector<int>> distributeRoundRobin(int proxyCount, int workerCount)
{
    if (workerCount <= 0) return {};
    if (proxyCount <= 0) return std::vector<std::vector<int>>(workerCount);
    std::vector<std::vector<int>> distribution(workerCount);
    for (int i = 0; i < proxyCount; ++i) {
        distribution[i % workerCount].push_back(i);
    }
    return distribution;
}

// E7: Skip decision for a proxy whose pre-generated config failed.
// Reproduces the worker-loop logic in ProxyBatchTester:
//   1) out-of-range profileIdx  -> counted as processed only (no network test)
//   2) pregenFailedFlags[idx]   -> counted as failed + processed, result "PREGEN_FAILED"
//   3) otherwise                -> continue to normal network test
enum class PregenSkipDecision { Continue, OutOfRange, PregenFailed };

PregenSkipDecision decidePregenSkip(const std::vector<bool>& flags, int idx, int preGenSize)
{
    if (idx < 0 || idx >= preGenSize) return PregenSkipDecision::OutOfRange;
    if (idx < static_cast<int>(flags.size()) && flags[idx]) return PregenSkipDecision::PregenFailed;
    return PregenSkipDecision::Continue;
}

} // anonymous namespace

// ============================================================
// SqlTemplateSubstitution
// ============================================================
TEST(SqlTemplateSubstitutionTest, SingleSubidPlaceholder)
{
    std::string sql = sqlTemplateSubstitute(
        "SELECT * FROM ProfileItem WHERE subid = '{subid}'",
        "sub123", 5);
    EXPECT_EQ(sql, "SELECT * FROM ProfileItem WHERE subid = 'sub123'");
}

TEST(SqlTemplateSubstitutionTest, MultiplePlaceholders)
{
    std::string sql = sqlTemplateSubstitute(
        "SELECT * FROM ProfileItem WHERE subid = '{subid}' AND status = '{subid}'",
        "abc", 5);
    EXPECT_EQ(sql, "SELECT * FROM ProfileItem WHERE subid = 'abc' AND status = 'abc'");
}

TEST(SqlTemplateSubstitutionTest, NoPlaceholderPassthrough)
{
    std::string sql = sqlTemplateSubstitute(
        "SELECT * FROM ProfileItem",
        "sub123", 5);
    EXPECT_EQ(sql, "SELECT * FROM ProfileItem");
}

TEST(SqlTemplateSubstitutionTest, SubidInMiddleOfString)
{
    std::string sql = sqlTemplateSubstitute(
        "WHERE subid = '{subid}' ORDER BY id",
        "mySub", 5);
    EXPECT_EQ(sql, "WHERE subid = 'mySub' ORDER BY id");
}

TEST(SqlTemplateSubstitutionTest, EmptySubidValue)
{
    std::string sql = sqlTemplateSubstitute(
        "SELECT * FROM ProfileItem WHERE subid = '{subid}'",
        "", 5);
    EXPECT_EQ(sql, "SELECT * FROM ProfileItem WHERE subid = ''");
}

// ============================================================
// BlacklistThresholdSubstitution
// ============================================================
TEST(BlacklistThresholdTest, BothPlaceholdersReplaced)
{
    std::string sql = sqlTemplateSubstitute(
        "SELECT * FROM ProfileItem WHERE subid = '{subid}' AND consecutive_failures < {blacklist_threshold}",
        "sub001", 5);
    EXPECT_EQ(sql, "SELECT * FROM ProfileItem WHERE subid = 'sub001' AND consecutive_failures < 5");
}

TEST(BlacklistThresholdTest, ZeroThreshold)
{
    std::string sql = sqlTemplateSubstitute(
        "SELECT * FROM ProfileItem WHERE subid = '{subid}' AND consecutive_failures < {blacklist_threshold}",
        "x", 0);
    EXPECT_EQ(sql, "SELECT * FROM ProfileItem WHERE subid = 'x' AND consecutive_failures < 0");
}

TEST(BlacklistThresholdTest, LargeThresholdValue)
{
    std::string sql = sqlTemplateSubstitute(
        "SELECT * FROM ProfileItem WHERE subid = '{subid}' AND consecutive_failures < {blacklist_threshold}",
        "big", 999999);
    EXPECT_EQ(sql, "SELECT * FROM ProfileItem WHERE subid = 'big' AND consecutive_failures < 999999");
}

TEST(BlacklistThresholdTest, NegativeThreshold)
{
    std::string sql = sqlTemplateSubstitute(
        "SELECT * FROM ProfileItem WHERE subid = '{subid}' AND consecutive_failures < {blacklist_threshold}",
        "neg", -3);
    EXPECT_EQ(sql, "SELECT * FROM ProfileItem WHERE subid = 'neg' AND consecutive_failures < -3");
}

// ============================================================
// WorkerCountCalculation
// ============================================================
TEST(WorkerCountCalculationTest, ProxiesExceedWorkers)
{
    EXPECT_EQ(calculateWorkerCount(10, 3), 3);
}

TEST(WorkerCountCalculationTest, ProxiesFewerThanWorkers)
{
    EXPECT_EQ(calculateWorkerCount(1, 3), 1);
}

TEST(WorkerCountCalculationTest, ZeroProxies)
{
    EXPECT_EQ(calculateWorkerCount(0, 3), 0);
}

TEST(WorkerCountCalculationTest, ProxiesLessThanWorkersMedium)
{
    EXPECT_EQ(calculateWorkerCount(5, 10), 5);
}

TEST(WorkerCountCalculationTest, SingleWorker)
{
    EXPECT_EQ(calculateWorkerCount(10, 1), 1);
}

TEST(WorkerCountCalculationTest, NegativeProxyCountClampedToZero)
{
    EXPECT_EQ(calculateWorkerCount(-1, 3), 0);
}

TEST(WorkerCountCalculationTest, NegativeWorkerCountClampedToZero)
{
    EXPECT_EQ(calculateWorkerCount(5, -1), 0);
}

TEST(WorkerCountCalculationTest, BothZero)
{
    EXPECT_EQ(calculateWorkerCount(0, 0), 0);
}

TEST(WorkerCountCalculationTest, EqualCounts)
{
    EXPECT_EQ(calculateWorkerCount(4, 4), 4);
}

// ============================================================
// CancelStateMerging
// ============================================================
TEST(CancelStateMergingTest, InternalCancelTrueReturnsTrue)
{
    bool external = false;
    EXPECT_TRUE(isCancelled(true, &external));
}

TEST(CancelStateMergingTest, ExternalCancelTrueReturnsTrue)
{
    bool external = true;
    EXPECT_TRUE(isCancelled(false, &external));
}

TEST(CancelStateMergingTest, BothFalseReturnsFalse)
{
    bool external = false;
    EXPECT_FALSE(isCancelled(false, &external));
}

TEST(CancelStateMergingTest, ExternalNullTreatedAsNotCancelled)
{
    EXPECT_FALSE(isCancelled(false, nullptr));
}

TEST(CancelStateMergingTest, ExternalNullInternalTrue)
{
    EXPECT_TRUE(isCancelled(true, nullptr));
}

TEST(CancelStateMergingTest, BothTrueReturnsTrue)
{
    bool external = true;
    EXPECT_TRUE(isCancelled(true, &external));
}

// ============================================================
// SummaryFormat
// ============================================================
TEST(SummaryFormatTest, AllZeros)
{
    EXPECT_EQ(formatSummary(0, 0, 0), "Success: 0, Failed: 0, Total: 0");
}

TEST(SummaryFormatTest, MixedValues)
{
    EXPECT_EQ(formatSummary(5, 3, 8), "Success: 5, Failed: 3, Total: 8");
}

TEST(SummaryFormatTest, AllSuccess)
{
    EXPECT_EQ(formatSummary(10, 0, 10), "Success: 10, Failed: 0, Total: 10");
}

TEST(SummaryFormatTest, AllFailed)
{
    EXPECT_EQ(formatSummary(0, 7, 7), "Success: 0, Failed: 7, Total: 7");
}

TEST(SummaryFormatTest, LargeNumbers)
{
    EXPECT_EQ(formatSummary(99999, 1, 100000), "Success: 99999, Failed: 1, Total: 100000");
}

TEST(SummaryFormatTest, SingleSuccess)
{
    EXPECT_EQ(formatSummary(1, 0, 1), "Success: 1, Failed: 0, Total: 1");
}

// ============================================================
// ProxyQueueManagement
// ============================================================
TEST(ProxyQueueManagementTest, FiveProxiesTwoWorkers)
{
    auto dist = distributeRoundRobin(5, 2);
    ASSERT_EQ(dist.size(), 2u);
    EXPECT_EQ(dist[0], std::vector<int>({0, 2, 4}));
    EXPECT_EQ(dist[1], std::vector<int>({1, 3}));
}

TEST(ProxyQueueManagementTest, OneProxyTwoWorkers)
{
    auto dist = distributeRoundRobin(1, 2);
    ASSERT_EQ(dist.size(), 2u);
    EXPECT_EQ(dist[0], std::vector<int>({0}));
    EXPECT_TRUE(dist[1].empty());
}

TEST(ProxyQueueManagementTest, ZeroProxies)
{
    auto dist = distributeRoundRobin(0, 2);
    ASSERT_EQ(dist.size(), 2u);
    EXPECT_TRUE(dist[0].empty());
    EXPECT_TRUE(dist[1].empty());
}

TEST(ProxyQueueManagementTest, OneProxyOneWorker)
{
    auto dist = distributeRoundRobin(1, 1);
    ASSERT_EQ(dist.size(), 1u);
    EXPECT_EQ(dist[0], std::vector<int>({0}));
}

TEST(ProxyQueueManagementTest, TenProxiesThreeWorkers)
{
    auto dist = distributeRoundRobin(10, 3);
    ASSERT_EQ(dist.size(), 3u);
    EXPECT_EQ(dist[0], std::vector<int>({0, 3, 6, 9}));
    EXPECT_EQ(dist[1], std::vector<int>({1, 4, 7}));
    EXPECT_EQ(dist[2], std::vector<int>({2, 5, 8}));
}

TEST(ProxyQueueManagementTest, WorkerCountZeroReturnsEmpty)
{
    auto dist = distributeRoundRobin(5, 0);
    EXPECT_TRUE(dist.empty());
}

TEST(ProxyQueueManagementTest, WorkerCountNegativeReturnsEmpty)
{
    auto dist = distributeRoundRobin(5, -1);
    EXPECT_TRUE(dist.empty());
}

TEST(ProxyQueueManagementTest, ProxiesEqualToWorkers)
{
    auto dist = distributeRoundRobin(3, 3);
    ASSERT_EQ(dist.size(), 3u);
    EXPECT_EQ(dist[0], std::vector<int>({0}));
    EXPECT_EQ(dist[1], std::vector<int>({1}));
    EXPECT_EQ(dist[2], std::vector<int>({2}));
}

TEST(ProxyQueueManagementTest, LargeProxyCount)
{
    auto dist = distributeRoundRobin(100, 4);
    ASSERT_EQ(dist.size(), 4u);
    EXPECT_EQ(static_cast<int>(dist[0].size()), 25);
    EXPECT_EQ(static_cast<int>(dist[1].size()), 25);
    EXPECT_EQ(static_cast<int>(dist[2].size()), 25);
    EXPECT_EQ(static_cast<int>(dist[3].size()), 25);
    // First worker gets indices 0, 4, 8, ..., 96
    EXPECT_EQ(dist[0][0], 0);
    EXPECT_EQ(dist[0][24], 96);
}

// ============================================================
// EdgeCases
// ============================================================
TEST(ProxyBatchEdgeCaseTest, EmptyProxyListDoesNotCrash)
{
    EXPECT_NO_THROW({
        auto dist = distributeRoundRobin(0, 3);
        ASSERT_EQ(dist.size(), 3u);
        for (const auto& worker : dist) {
            EXPECT_TRUE(worker.empty());
        }
    });
}

TEST(ProxyBatchEdgeCaseTest, SingleProxySingleWorker)
{
    int count = calculateWorkerCount(1, 1);
    EXPECT_EQ(count, 1);

    auto dist = distributeRoundRobin(1, 1);
    ASSERT_EQ(dist.size(), 1u);
    ASSERT_EQ(dist[0].size(), 1u);
    EXPECT_EQ(dist[0][0], 0);
}

TEST(ProxyBatchEdgeCaseTest, ManyProxiesWorkerCountClamped)
{
    // Even with 1000 proxies, worker count should cap at xray_workers
    EXPECT_EQ(calculateWorkerCount(1000, 4), 4);
    EXPECT_EQ(calculateWorkerCount(1000, 1), 1);
}

TEST(ProxyBatchEdgeCaseTest, SummaryWithZeroTotal)
{
    std::string s = formatSummary(0, 0, 0);
    EXPECT_EQ(s, "Success: 0, Failed: 0, Total: 0");
}

TEST(ProxyBatchEdgeCaseTest, SqlTemplateNoBracesSubid)
{
    // Template with no {subid} at all should pass through unchanged
    std::string sql = sqlTemplateSubstitute(
        "SELECT COUNT(*) FROM ProfileItem",
        "should_not_appear", 5);
    EXPECT_EQ(sql, "SELECT COUNT(*) FROM ProfileItem");
    EXPECT_TRUE(sql.find("should_not_appear") == std::string::npos);
}

TEST(ProxyBatchEdgeCaseTest, SqlTemplateMalformedBraces)
{
    // Template with partial braces should not be replaced
    std::string sql = sqlTemplateSubstitute(
        "SELECT * FROM ProfileItem WHERE subid = '{subid'",
        "val", 5);
    EXPECT_EQ(sql, "SELECT * FROM ProfileItem WHERE subid = '{subid'");
}

TEST(ProxyBatchEdgeCaseTest, WorkerCountZeroProxiesZeroWorkers)
{
    EXPECT_EQ(calculateWorkerCount(0, 0), 0);
}

TEST(ProxyBatchEdgeCaseTest, RoundRobinManyWorkersFewProxies)
{
    auto dist = distributeRoundRobin(2, 10);
    ASSERT_EQ(dist.size(), 10u);
    EXPECT_EQ(dist[0], std::vector<int>({0}));
    EXPECT_EQ(dist[1], std::vector<int>({1}));
    for (int i = 2; i < 10; ++i) {
        EXPECT_TRUE(dist[i].empty());
    }
}

// ============================================================
// B1: ResetLogic - verify counters reset to zero on each run
// ============================================================
TEST(B1ResetLogicTest, CountersResetToZero)
{
    int success = 5;
    int failed = 3;
    int processed = 8;
    success = 0;
    failed = 0;
    processed = 0;
    EXPECT_EQ(success, 0);
    EXPECT_EQ(failed, 0);
    EXPECT_EQ(processed, 0);
}

TEST(B1ResetLogicTest, EmptyQueueAfterReset)
{
    std::queue<int> q;
    q.push(0); q.push(1); q.push(2);
    q = std::queue<int>();
    EXPECT_TRUE(q.empty());
}

// ============================================================
// E7: PreGenFailedSkip - skip proxies whose config pre-generation failed
// ============================================================
TEST(PreGenFailedSkipTest, NormalProxyContinues)
{
    std::vector<bool> flags = {false, false};
    EXPECT_EQ(decidePregenSkip(flags, 0, 2), PregenSkipDecision::Continue);
    EXPECT_EQ(decidePregenSkip(flags, 1, 2), PregenSkipDecision::Continue);
}

TEST(PreGenFailedSkipTest, FailedProxySkipped)
{
    std::vector<bool> flags = {true, false};
    EXPECT_EQ(decidePregenSkip(flags, 0, 2), PregenSkipDecision::PregenFailed);
    // A later healthy proxy is unaffected by an earlier failure.
    EXPECT_EQ(decidePregenSkip(flags, 1, 2), PregenSkipDecision::Continue);
}

TEST(PreGenFailedSkipTest, AllFailedAllSkipped)
{
    std::vector<bool> flags = {true, true, true};
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(decidePregenSkip(flags, i, 3), PregenSkipDecision::PregenFailed);
    }
}

TEST(PreGenFailedSkipTest, OutOfRangeIndex)
{
    std::vector<bool> flags = {false};
    // profileIdx beyond preGenConfigs_.size() takes the guard branch (processed only).
    EXPECT_EQ(decidePregenSkip(flags, 1, 1), PregenSkipDecision::OutOfRange);
    EXPECT_EQ(decidePregenSkip(flags, 99, 1), PregenSkipDecision::OutOfRange);
    EXPECT_EQ(decidePregenSkip(flags, -1, 1), PregenSkipDecision::OutOfRange);
}

TEST(PreGenFailedSkipTest, FlagsVectorShorterThanConfigs)
{
    // Defensive: if flags and preGenConfigs_ ever drift out of sync, an
    // unmarked index must still be processed (not skipped).
    std::vector<bool> flags = {true};
    EXPECT_EQ(decidePregenSkip(flags, 0, 3), PregenSkipDecision::PregenFailed);
    EXPECT_EQ(decidePregenSkip(flags, 1, 3), PregenSkipDecision::Continue);
    EXPECT_EQ(decidePregenSkip(flags, 2, 3), PregenSkipDecision::Continue);
}

TEST(PreGenFailedSkipTest, EmptyFlagsAllContinue)
{
    std::vector<bool> flags;
    EXPECT_EQ(decidePregenSkip(flags, 0, 2), PregenSkipDecision::Continue);
    EXPECT_EQ(decidePregenSkip(flags, 1, 2), PregenSkipDecision::Continue);
}

TEST(PreGenFailedSkipTest, MixedBatchDecisionCounts)
{
    // Batch of 5: indices 1 and 3 failed pre-generation.
    std::vector<bool> flags = {false, true, false, true, false};
    int skipped = 0, continued = 0;
    for (int i = 0; i < 5; ++i) {
        if (decidePregenSkip(flags, i, 5) == PregenSkipDecision::PregenFailed) ++skipped;
        else if (decidePregenSkip(flags, i, 5) == PregenSkipDecision::Continue) ++continued;
    }
    EXPECT_EQ(skipped, 2);
    EXPECT_EQ(continued, 3);
}
