// test_xray_manager_startup.cpp
// Regression test for the startup flash-crash detection gap:
//   "检测启动独立代理进程是否成功需要加上时限，防止出现启动闪崩，却长时间等待"
//
// XrayManager::start() must be CRASH-AWARE and BOUNDED: when the backing Xray
// process hangs / flash-crashes and never opens its API port, start() must fail
// fast instead of blocking for the full readiness timeout.
//
// The "fake xray" helper (fake_xray_helper.exe) selects behaviour by file name;
// the "latecrash" variant writes PANIC, sleeps ~6s, then exits 7 — i.e. it
// SURVIVES XrayInstance::start()'s 500ms spawn-grace AND the OLD 5s liveness
// poll, but never listens on its API port. Under the OLD code this reached
// pollApiPortReady and blocked for a second 5s (~10s total). Under the fixed
// code the crash-aware waitInstanceReady is bounded to 5s, so start() returns
// after ~5.5s. Any value well under the old ~10s proves the fix.
#include "XrayManager.h"
#include "Logger.h"

#include <gtest/gtest.h>

#include <cstdlib>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

std::filesystem::path makeTempDir() {
    const std::string sub = "xray_mgr_test_" + std::to_string(::GetCurrentProcessId());
    std::filesystem::path dir = std::filesystem::temp_directory_path() / sub;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string helperExePath() {
    char buf[MAX_PATH] = {};
    ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::filesystem::path self(buf);
    return (self.parent_path() / "fake_xray_helper.exe").string();
}

std::string deployHelper(const std::filesystem::path& tmp, const std::string& targetName) {
    const std::string dst = (tmp / targetName).string();
    std::error_code ec;
    std::filesystem::copy_file(helperExePath(), dst,
                               std::filesystem::copy_options::overwrite_existing, ec);
    EXPECT_FALSE(ec) << "copy helper failed: " << ec.message();
    return dst;
}

}  // namespace

TEST(XrayManagerStartupTest, FlashCrashDoesNotHang) {
    const std::filesystem::path tmp = makeTempDir();
    const std::string exePath = deployHelper(tmp, "fake_xray_latecrash.exe");

    XrayManager* mgr = XrayManager::getInstance(exePath, tmp.string(), 1);
    mgr->stopAll();

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    const int actual = mgr->start(1, 19501, 19601);
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    const long long elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // The fake xray survives the 500ms spawn-grace, then crashes before its API
    // port opens. No instance should be considered started.
    EXPECT_EQ(0, actual)
        << "no instance should start: process flash-crashed before API port ready";

    // Bounded: under the OLD code this scenario blocked ~10s (5s liveness poll
    // + 5s port poll) because the process survived the liveness poll and
    // pollApiPortReady only watched the port. The crash-aware, bounded wait must
    // return after ~5.5s (500ms grace + 5000ms readiness), plus process-launch
    // latency L. The OLD code would block 10000ms + L. So any value below the
    // OLD baseline (here 9500ms, allowing for launch latency) proves the fix and
    // would FAIL against the old implementation.
    EXPECT_LT(elapsedMs, 9500)
        << "start() must not block for the full timeout on a flash crash; elapsedMs="
        << elapsedMs;

    // Lower bound: the fake xray must have entered the readiness wait (i.e. we
    // exercised the crash-aware path, not a trivially-early failure such as port
    // allocation). Guards against the test passing vacuously.
    EXPECT_GT(elapsedMs, 400)
        << "startup unexpectedly fast; did the instance spawn at all? elapsedMs="
        << elapsedMs;

    mgr->stopAll();
    XrayManager::release();

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

// Real-xray SUCCESS-path verification.
//
// The fix (crash-aware, bounded waitInstanceReady) is universal: XrayInstance
// startup generates a fixed direct/freedom template and never reads the proxy
// profile, so the launch path is identical for EVERY indexid — including
// 4214682372947796598, which lives in bin/worker/guindb.db. This test proves
// a HEALTHY real xray instance is brought up quickly and reported ready
// (actualCount==1) with no unbounded wait, i.e. the fix did not regress the
// happy path.
//
// Uses ISOLATED ports (29501/29601) so it never collides with a concurrently
// running validproxy using the default 19501/19601.
TEST(XrayManagerStartupTest, RealXrayStartsBounded) {
    const char* envPath = std::getenv("XRAY_REAL_EXE");
    const std::string realXray =
        envPath ? std::string(envPath) : "E:/v2rayN-windows-64/bin/xray/xray.exe";
    if (!std::filesystem::exists(realXray)) {
        GTEST_SKIP() << "real xray not found at " << realXray
                     << " (set XRAY_REAL_EXE to enable this test)";
    }

    const std::filesystem::path tmp = makeTempDir();

    // Guarantee a clean singleton (clear any instance left by a prior test).
    XrayManager::release();

    XrayManager* mgr = XrayManager::getInstance(realXray, tmp.string(), 1);
    mgr->stopAll();

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    // Isolated ports: avoid the default 19501/19601 used by a live validproxy.
    const int actual = mgr->start(1, 29501, 29601);
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    const long long elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    EXPECT_EQ(1, actual)
        << "a healthy real xray instance must start and report its API port ready";
    EXPECT_LT(elapsedMs, 3000)
        << "startup of a healthy instance must be fast; elapsedMs=" << elapsedMs;

    mgr->stopAll();
    XrayManager::release();

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}
