// TestProxyProbePool.cpp
// ProxyProbePool 单元测试：常驻 Xray 探针池（非直连成员健康探测）。
// 离线：构造边界 / 未启动不可用；live（XRAY_REAL_EXE）：注入并探测。
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "ProxyProbePool.h"
#include "ProxyHealthEvaluator.h"
#include "Profileitem.h"
#include "PortManager.h"

namespace {

std::string getXrayExe() {
    const char* exe = std::getenv("XRAY_REAL_EXE");
    if (exe == nullptr) {
        return std::string();
    }
    return std::string(exe);
}

db::models::Profileitem makeVlessProfile() {
    db::models::Profileitem p;
    p.indexid = "900012356";
    p.configtype = "5";          // VLESS（非直连，非 4/10）
    p.address = "1.2.3.4";
    p.port = "443";
    p.security = "reality";
    p.id = "123e4567-e89b-12d3-a456-426614174000";
    p.streamsecurity = "reality";
    p.publickey = "test-public-key";
    p.sni = "example.com";
    return p;
}

proxy::MemberProbeTarget makeTarget(const db::models::Profileitem& p) {
    proxy::MemberProbeTarget t;
    t.tag = "px-" + p.indexid;
    t.configtype = std::atoi(p.configtype.c_str());
    t.address = p.address;
    t.port = p.port;
    t.username = p.security;
    t.password = p.id;
    t.profile = p;
    return t;
}

}  // namespace

class ProxyProbePoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        xrayExe_ = getXrayExe();
        tempDir_ = std::filesystem::temp_directory_path() /
                   ("probe_pool_test_" + std::to_string(::GetCurrentProcessId()));
        std::filesystem::create_directories(tempDir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tempDir_, ec);
    }

    std::string xrayExe_;
    std::filesystem::path tempDir_;
};TEST_F(ProxyProbePoolTest, ConstructionBoundaryNoStart) {
    proxy::ProxyProbePool pool(xrayExe_, 2, tempDir_.string());
    EXPECT_FALSE(pool.isRunning());
    EXPECT_EQ(pool.size(), 0);
    pool.stop();  // 未 start 时 stop 必须安全

    db::models::Profileitem profile = makeVlessProfile();
    proxy::MemberProbeTarget target = makeTarget(profile);
    proxy::MemberHealth out;
    EXPECT_FALSE(pool.probeMember(target, "https://www.google.com", 3000, 10000, out));
}

TEST_F(ProxyProbePoolTest, ProbeMemberUnavailableWhenNotStarted) {
    proxy::ProxyProbePool pool(xrayExe_, 0, tempDir_.string());
    EXPECT_FALSE(pool.isRunning());
    EXPECT_EQ(pool.size(), 0);

    db::models::Profileitem profile = makeVlessProfile();
    proxy::MemberProbeTarget target = makeTarget(profile);
    proxy::MemberHealth out;
    EXPECT_FALSE(pool.probeMember(target, "https://www.google.com", 3000, 10000, out));
}

TEST_F(ProxyProbePoolTest, LiveProbeMemberInjectsAndProbes) {
    if (xrayExe_.empty()) {
        GTEST_SKIP() << "XRAY_REAL_EXE not set; skipping live probe pool test";
    }

    proxy::ProxyProbePool pool(xrayExe_, 1, tempDir_.string());
    ASSERT_TRUE(pool.start());
    EXPECT_TRUE(pool.isRunning());
    EXPECT_EQ(pool.size(), 1);

    db::models::Profileitem profile = makeVlessProfile();
    proxy::MemberProbeTarget target = makeTarget(profile);

    proxy::MemberHealth out;
    bool ok = pool.probeMember(target, "https://www.google.com", 3000, 10000, out);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(out.tested);
    EXPECT_EQ(out.tag, "px-900012356");

    pool.stop();
    EXPECT_FALSE(pool.isRunning());
}

TEST_F(ProxyProbePoolTest, EvaluatorWithoutProbePoolLeavesNonDirectUntested) {
    proxy::ProxyHealthEvaluator evaluator;
    std::vector<proxy::MemberProbeTarget> targets;
    targets.push_back(makeTarget(makeVlessProfile()));

    std::vector<proxy::MemberHealth> health =
        evaluator.probe(targets, "https://www.google.com", 3000, 10000);

    ASSERT_EQ(health.size(), 1u);
    EXPECT_FALSE(health[0].tested);
    EXPECT_EQ(health[0].tag, "px-900012356");
}

TEST_F(ProxyProbePoolTest, LiveEvaluatorWithProbePoolRoutesNonDirectToPool) {
    if (xrayExe_.empty()) {
        GTEST_SKIP() << "XRAY_REAL_EXE not set; skipping live evaluator wiring test";
    }

    proxy::ProxyProbePool pool(xrayExe_, 1, tempDir_.string());
    ASSERT_TRUE(pool.start());

    proxy::ProxyHealthEvaluator evaluator;
    evaluator.setProbePool(&pool);
    std::vector<proxy::MemberProbeTarget> targets;
    targets.push_back(makeTarget(makeVlessProfile()));

    std::vector<proxy::MemberHealth> health =
        evaluator.probe(targets, "https://www.google.com", 3000, 10000);

    pool.stop();

    ASSERT_EQ(health.size(), 1u);
    EXPECT_TRUE(health[0].tested);
    EXPECT_EQ(health[0].tag, "px-900012356");
}