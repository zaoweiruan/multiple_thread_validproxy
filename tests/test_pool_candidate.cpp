// Test: PoolCandidateItem 排序比较器与健康度计算（AddPoolMemberDialog 增强）
#include <gtest/gtest.h>
#include "PoolCandidate.h"

using pool_candidate::CandidateSortKey;
using pool_candidate::PoolCandidateItem;
using pool_candidate::compareCandidates;
using pool_candidate::computeHealth;

// -------------------------------------------------------------------
// computeHealth：贝叶斯平滑公式（与 ProxyListModel::rebuildMaps 一致）
// -------------------------------------------------------------------
TEST(PoolCandidateHealthTest, ColdStartZero) {
    EXPECT_DOUBLE_EQ(0.0, computeHealth(0, 0));
    EXPECT_DOUBLE_EQ(0.0, computeHealth(0, 3));
}

TEST(PoolCandidateHealthTest, StableProxy) {
    // start=5, crash=0 → stable=5 → (5+1)/(5+2) = 6/7
    EXPECT_NEAR(6.0 / 7.0, computeHealth(5, 0), 1e-9);
}

TEST(PoolCandidateHealthTest, CrashClampedToZero) {
    // start=3, crash=5 → stable=0 → (0+1)/(3+2) = 1/5
    EXPECT_NEAR(0.2, computeHealth(3, 5), 1e-9);
}

TEST(PoolCandidateHealthTest, PartialCrash) {
    // start=10, crash=4 → stable=6 → (6+1)/(10+2) = 7/12
    EXPECT_NEAR(7.0 / 12.0, computeHealth(10, 4), 1e-9);
}

// -------------------------------------------------------------------
// compareCandidates：升/降序 + 数值/字符串语义
// -------------------------------------------------------------------
TEST(PoolCandidateSortTest, DelayAscendingDescending) {
    PoolCandidateItem a; a.delay = "100";
    PoolCandidateItem b; b.delay = "50";
    // 升序：50 排在 100 前
    EXPECT_TRUE(compareCandidates(b, a, CandidateSortKey::Delay, true));
    EXPECT_FALSE(compareCandidates(a, b, CandidateSortKey::Delay, true));
    // 降序：100 排在 50 前
    EXPECT_TRUE(compareCandidates(a, b, CandidateSortKey::Delay, false));
    EXPECT_FALSE(compareCandidates(b, a, CandidateSortKey::Delay, false));
}

TEST(PoolCandidateSortTest, DelayInvalidLast) {
    PoolCandidateItem a; a.delay = "100";
    PoolCandidateItem b; b.delay = "";   // 无效（未测试）
    PoolCandidateItem c; c.delay = "-1"; // 无效
    // 升序：有效值排在无效值前
    EXPECT_TRUE(compareCandidates(a, b, CandidateSortKey::Delay, true));
    EXPECT_FALSE(compareCandidates(b, a, CandidateSortKey::Delay, true));
    EXPECT_TRUE(compareCandidates(a, c, CandidateSortKey::Delay, true));
    // 无效值之间稳定（都不排前）
    EXPECT_FALSE(compareCandidates(b, c, CandidateSortKey::Delay, true));
    EXPECT_FALSE(compareCandidates(c, b, CandidateSortKey::Delay, true));
}

TEST(PoolCandidateSortTest, StringCaseInsensitive) {
    PoolCandidateItem a; a.remarks = "abc";
    PoolCandidateItem b; b.remarks = "ABC";
    // 大小写不敏感 → 相等 → 互不排前
    EXPECT_FALSE(compareCandidates(a, b, CandidateSortKey::Remarks, true));
    EXPECT_FALSE(compareCandidates(b, a, CandidateSortKey::Remarks, true));
    // 不同字符串按大小写不敏感序
    PoolCandidateItem c; c.remarks = "abd";
    EXPECT_TRUE(compareCandidates(a, c, CandidateSortKey::Remarks, true));
    EXPECT_TRUE(compareCandidates(c, a, CandidateSortKey::Remarks, false));
}

TEST(PoolCandidateSortTest, HealthNumeric) {
    PoolCandidateItem a; a.start_count = 5; a.crash_count = 0; // 6/7 ≈ 0.857
    PoolCandidateItem b; b.start_count = 1; b.crash_count = 0; // 2/3 ≈ 0.667
    // 升序：低健康在前
    EXPECT_TRUE(compareCandidates(b, a, CandidateSortKey::Health, true));
    EXPECT_FALSE(compareCandidates(a, b, CandidateSortKey::Health, true));
    // 降序：高健康在前
    EXPECT_TRUE(compareCandidates(a, b, CandidateSortKey::Health, false));
    EXPECT_FALSE(compareCandidates(b, a, CandidateSortKey::Health, false));
}

TEST(PoolCandidateSortTest, ProtocolByName) {
    PoolCandidateItem a; a.configtype = "1"; // VMess
    PoolCandidateItem b; b.configtype = "5"; // VLESS
    // "VLESS" < "VMess"（大小写不敏感：vless < vmess）→ 升序 VLESS 在前
    EXPECT_TRUE(compareCandidates(b, a, CandidateSortKey::Protocol, true));
    EXPECT_TRUE(compareCandidates(a, b, CandidateSortKey::Protocol, false));
}

TEST(PoolCandidateSortTest, IndexIdStringOrder) {
    PoolCandidateItem a; a.indexid = "100";
    PoolCandidateItem b; b.indexid = "99";
    // 字符串序："100" < "99"（'1' < '9'）
    EXPECT_TRUE(compareCandidates(a, b, CandidateSortKey::IndexId, true));
    EXPECT_TRUE(compareCandidates(b, a, CandidateSortKey::IndexId, false));
}

TEST(PoolCandidateSortTest, EqualItemsStable) {
    PoolCandidateItem a; a.indexid = "42"; a.delay = "100";
    PoolCandidateItem b; b.indexid = "42"; b.delay = "100";
    EXPECT_FALSE(compareCandidates(a, b, CandidateSortKey::IndexId, true));
    EXPECT_FALSE(compareCandidates(a, b, CandidateSortKey::Delay, false));
}