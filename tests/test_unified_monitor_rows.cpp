#ifdef HAS_WXWIDGETS
// test_unified_monitor_rows.cpp
// Unit tests for buildUnifiedMonitorRows() pure merge function
// (standalone watched proxies + proxy-pool members -> unified rows).

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "UnifiedMonitorRows.h"

namespace {

StandaloneMonitorRow makeStandaloneRow(const std::string& indexId,
                                       const std::string& host,
                                       int socksPort,
                                       int64_t pid,
                                       int64_t durationMs) {
    StandaloneMonitorRow row;
    row.indexId = indexId;
    row.host = host;
    row.socksPort = socksPort;
    row.pid = pid;
    row.durationMs = durationMs;
    return row;
}

proxy::PoolMemberView makePoolMember(long long indexId,
                                     const std::string& tag,
                                     const std::string& host,
                                     const std::string& state,
                                     long long delay,
                                     bool alive,
                                     int failStreak,
                                     const std::string& err) {
    proxy::PoolMemberView v;
    v.indexId = indexId;
    v.tag = tag;
    v.host = host;
    v.state = state;
    v.lastDelayMs = delay;
    v.lastAlive = alive;
    v.failStreak = failStreak;
    v.lastError = err;
    v.probed = true;
    return v;
}

} // namespace

TEST(UnifiedMonitorRows, EmptySourcesYieldEmptyRows) {
    const std::vector<StandaloneMonitorRow> standalone;
    const std::vector<proxy::PoolMemberView> members;
    const std::vector<UnifiedMonitorRow> rows =
        buildUnifiedMonitorRows(standalone, members);
    EXPECT_TRUE(rows.empty());
}

TEST(UnifiedMonitorRows, StandaloneRowsMappedFirst) {
    std::vector<StandaloneMonitorRow> standalone;
    standalone.push_back(makeStandaloneRow("idx-1", "1.2.3.4", 10808, 1234, 60000));
    std::vector<proxy::PoolMemberView> members;
    members.push_back(makePoolMember(7, "px-7", "5.6.7.8", "active", 120, true, 0, ""));

    const std::vector<UnifiedMonitorRow> rows =
        buildUnifiedMonitorRows(standalone, members);

    ASSERT_EQ(rows.size(), 2u);
    // Standalone row first.
    EXPECT_EQ(rows[0].type, MonitorType::Standalone);
    EXPECT_EQ(rows[0].indexId, "idx-1");
    EXPECT_EQ(rows[0].host, "1.2.3.4");
    EXPECT_EQ(rows[0].socksPort, 10808);
    EXPECT_EQ(rows[0].pid, 1234);
    EXPECT_EQ(rows[0].durationMs, 60000);
    EXPECT_EQ(rows[0].state, "\xe8\xbf\x90\xe8\xa1\x8c\xe4\xb8\xad"); // "运行中"
    EXPECT_EQ(rows[0].lastDelayMs, -1);
    EXPECT_FALSE(rows[0].lastAlive);
    EXPECT_EQ(rows[0].failStreak, 0);
    EXPECT_TRUE(rows[0].lastError.empty());
    // Pool row second.
    EXPECT_EQ(rows[1].type, MonitorType::Pool);
    EXPECT_EQ(rows[1].indexId, "7"); // std::to_string(long long)
    EXPECT_EQ(rows[1].tag, "px-7");
    EXPECT_EQ(rows[1].host, "5.6.7.8");
    EXPECT_EQ(rows[1].state, "active");
    EXPECT_EQ(rows[1].lastDelayMs, 120);
    EXPECT_TRUE(rows[1].lastAlive);
    EXPECT_EQ(rows[1].failStreak, 0);
    EXPECT_TRUE(rows[1].lastError.empty());
    EXPECT_EQ(rows[1].socksPort, 0);
    EXPECT_EQ(rows[1].pid, 0);
    EXPECT_EQ(rows[1].durationMs, 0);
}

TEST(UnifiedMonitorRows, PoolMemberFieldsMapped) {
    std::vector<StandaloneMonitorRow> standalone;
    std::vector<proxy::PoolMemberView> members;
    members.push_back(makePoolMember(42, "px-42", "9.9.9.9", "draining", -1, false, 3, "timeout"));

    const std::vector<UnifiedMonitorRow> rows =
        buildUnifiedMonitorRows(standalone, members);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].type, MonitorType::Pool);
    EXPECT_EQ(rows[0].indexId, "42");
    EXPECT_EQ(rows[0].tag, "px-42");
    EXPECT_EQ(rows[0].host, "9.9.9.9");
    EXPECT_EQ(rows[0].state, "draining");
    EXPECT_EQ(rows[0].lastDelayMs, -1);
    EXPECT_FALSE(rows[0].lastAlive);
    EXPECT_EQ(rows[0].failStreak, 3);
    EXPECT_EQ(rows[0].lastError, "timeout");
}

// 2026-09-11 池成员 indexId int64 化回归（bugfix #82）：
// 真实 indexId 为 int64 级字符串（如 5720942700011514210），旧 int 截断后
// row.indexId 渲染为垃圾值；改造后必须完整渲染 19 位串，且不携带 px- 前缀。
TEST(UnifiedMonitorRows, Int64IndexIdRenderedFully) {
    std::vector<StandaloneMonitorRow> standalone;
    std::vector<proxy::PoolMemberView> members;
    members.push_back(makePoolMember(5720942700011514210LL, "px-5720942700011514210",
                                     "1.2.3.4", "active", 120, true, 0, ""));

    const std::vector<UnifiedMonitorRow> rows =
        buildUnifiedMonitorRows(standalone, members);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].type, MonitorType::Pool);
    EXPECT_EQ(rows[0].indexId, "5720942700011514210"); // 完整 19 位 int64 串，非截断
    EXPECT_EQ(rows[0].tag, "px-5720942700011514210");
}

TEST(UnifiedMonitorRows, OrderingStandaloneBeforePool) {
    std::vector<StandaloneMonitorRow> standalone;
    standalone.push_back(makeStandaloneRow("a", "h1", 1, 1, 1));
    standalone.push_back(makeStandaloneRow("b", "h2", 2, 2, 2));
    std::vector<proxy::PoolMemberView> members;
    members.push_back(makePoolMember(1, "px-1", "h3", "active", 10, true, 0, ""));
    members.push_back(makePoolMember(2, "px-2", "h4", "active", 20, true, 0, ""));

    const std::vector<UnifiedMonitorRow> rows =
        buildUnifiedMonitorRows(standalone, members);

    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].type, MonitorType::Standalone);
    EXPECT_EQ(rows[1].type, MonitorType::Standalone);
    EXPECT_EQ(rows[2].type, MonitorType::Pool);
    EXPECT_EQ(rows[3].type, MonitorType::Pool);
    EXPECT_EQ(rows[2].indexId, "1");
    EXPECT_EQ(rows[3].indexId, "2");
}

#else
#include <gtest/gtest.h>
TEST(UnifiedMonitorRows, SkippedWhenWxWidgetsUnavailable) {
    SUCCEED() << "UnifiedMonitorRows test skipped: wxWidgets not available";
}
#endif