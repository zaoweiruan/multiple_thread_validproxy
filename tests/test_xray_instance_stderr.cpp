// test_xray_instance_stderr.cpp
// Verification of the stderr/exit-code observability added to XrayInstance:
//   1. Runtime death: child lives longer than the start() poll window, then
//      dies with a non-zero code; isRunning() must report death and expose
//      the exit code + captured stderr tail.
//   2. Startup death: child dies inside the start() poll window; start()
//      must return false and expose the exit code + captured stderr.
//
// The "fake xray" is a compiled helper executable (fake_xray_helper.exe),
// because CreateProcessW does not execute a .bat passed as lpApplicationName.
// The helper ignores the arguments XrayInstance appends ("run -c <config>")
// and behaves by its own file name:
//   - fake_xray_runtime.exe : writes PANIC to stderr, sleeps ~8s, exits 42
//   - fake_xray_startup.exe : writes PANIC to stderr, exits 7 immediately

#include "XrayInstance.h"
#include "Logger.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::filesystem::path makeTempDir() {
    const std::string sub =
        "xray_inst_test_" + std::to_string(::GetCurrentProcessId());
    std::filesystem::path dir = std::filesystem::temp_directory_path() / sub;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// The helper exe is built into the same directory as the test exe (tests/).
std::string helperExePath() {
    char buf[MAX_PATH] = {};
    ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::filesystem::path self(buf);
    return (self.parent_path() / "fake_xray_helper.exe").string();
}

// Deploy the helper into the per-test temp dir under a mode-selecting name.
std::string deployHelper(const std::filesystem::path& tmp,
                         const std::string& targetName) {
    const std::string dst = (tmp / targetName).string();
    std::error_code ec;
    std::filesystem::copy_file(helperExePath(), dst,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    EXPECT_FALSE(ec) << "copy helper failed: " << ec.message();
    return dst;
}

std::string readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::string();
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    return content;
}

bool waitForDeath(XrayInstance& inst, int maxSeconds) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(maxSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!inst.isRunning()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return !inst.isRunning();
}

bool anyCapturedContains(const std::vector<std::string>& msgs,
                         const std::string& needle) {
    for (const std::string& m : msgs) {
        if (m.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(XrayInstanceStderrTest, RuntimeDeathCapture) {
    const std::filesystem::path tmp = makeTempDir();
    const std::string exePath = deployHelper(tmp, "fake_xray_runtime.exe");

    std::vector<std::string> errMessages;
    Logger::pushCallback(
        [&errMessages](const std::string& msg, LogLevel level) {
            if (level == LogLevel::ERR) {
                errMessages.push_back(msg);
            }
        });

    XrayInstance inst(exePath, 19001, 19101, tmp.string());

    EXPECT_TRUE(inst.start()) << "start() should succeed: fake xray lives > 5s";
    EXPECT_TRUE(inst.isRunning());
    EXPECT_TRUE(waitForDeath(inst, 25))
        << "fake xray should die on its own after ~8s";

    EXPECT_EQ(42u, inst.lastExitCode());

    const std::string stderrLog = (tmp / "xray_stderr_19001.log").string();
    const std::string stdoutLog = (tmp / "xray_stdout_19001.log").string();
    EXPECT_TRUE(std::filesystem::exists(stderrLog));
    EXPECT_TRUE(std::filesystem::exists(stdoutLog));

    const std::string stderrContent = readWholeFile(stderrLog);
    EXPECT_NE(std::string::npos, stderrContent.find("PANIC"))
        << "stderr redirect file should contain the child's stderr";
    EXPECT_TRUE(anyCapturedContains(errMessages, "PANIC"))
        << "the death log message should embed the stderr tail";

    inst.stop();
    Logger::popCallback();
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

// Regression (issue ui_20260831_151924.log, exit code 23): the default config
// template declared "ObservatoryService" in its api.services list, but shipped
// no corresponding observatory block. Xray 26.x resolves API service
// dependencies eagerly at startup and aborts with "core: not all dependencies
// are resolved" (exit code 23) when an api service has no backing config. The
// template must NOT declare ObservatoryService (it is unused by the batch
// tester, which only drives HandlerService/StatsService over gRPC). This test
// asserts the generated config omits it.
//
// Note: earlier analysis suspected the routing rule referencing outbound tag
// "proxy" ({"type":"field","outboundTag":"proxy","network":"tcp"}) was the
// culprit. Bisection against real Xray 26.3.27 showed that is NOT fatal: a
// template without ObservatoryService loads fine even with the proxy rule and
// only a "direct" outbound. The proxy placeholder is kept defensively so the
// tag resolves at load, and the routing rule itself is unchanged.
TEST(XrayInstanceStderrTest, ConfigTemplateOmitsObservatoryService) {
    const std::filesystem::path tmp = makeTempDir();
    // startup-mode helper exits immediately, but createConfigFile() already
    // ran synchronously inside start() before the process spawn, so the config
    // file is guaranteed present regardless of the exit code.
    const std::string exePath = deployHelper(tmp, "fake_xray_startup.exe");

    XrayInstance inst(exePath, 19003, 19103, tmp.string());
    (void)inst.start();  // returns false (child dies fast); config still written

    const std::string configContent =
        readWholeFile((tmp / "xray_config_19003.json").string());
    ASSERT_FALSE(configContent.empty())
        << "start() must have written the config file to the temp dir";

    // THE root cause: ObservatoryService must NOT be advertised without a
    // backing observatory block or Xray 26.x fails with exit code 23.
    EXPECT_EQ(std::string::npos, configContent.find("ObservatoryService"))
        << "template must not declare ObservatoryService in api.services: it "
           "has no corresponding observatory block and makes Xray 26.x abort "
           "with 'core: not all dependencies are resolved' (exit code 23)";

    // The services the batch tester actually drives over gRPC must still be
    // advertised so outbound injection and stats keep working.
    EXPECT_NE(std::string::npos, configContent.find("HandlerService"));
    EXPECT_NE(std::string::npos, configContent.find("StatsService"));

    // The routing rule that routes socks-in traffic through the proxy tag.
    EXPECT_NE(std::string::npos,
              configContent.find("\"outboundTag\": \"proxy\""))
        << "routing must still reference the proxy outbound tag";

    // The placeholder outbound that resolves that tag at load time.
    EXPECT_NE(std::string::npos,
              configContent.find("\"tag\": \"proxy\", \"protocol\": \"freedom\""))
        << "template must declare a placeholder 'proxy' outbound so the "
           "routing rule's proxy tag resolves at load";

    // Sanity: the direct outbound baseline is still present.
    EXPECT_NE(std::string::npos,
              configContent.find("\"tag\": \"direct\", \"protocol\": \"freedom\""));

    inst.stop();
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST(XrayInstanceStderrTest, StartupDeathCapture) {
    const std::filesystem::path tmp = makeTempDir();
    const std::string exePath = deployHelper(tmp, "fake_xray_startup.exe");

    std::vector<std::string> errMessages;
    Logger::pushCallback(
        [&errMessages](const std::string& msg, LogLevel level) {
            if (level == LogLevel::ERR) {
                errMessages.push_back(msg);
            }
        });

    XrayInstance inst(exePath, 19002, 19102, tmp.string());

    EXPECT_FALSE(inst.start())
        << "start() should fail: fake xray dies inside the poll window";
    EXPECT_EQ(7u, inst.lastExitCode());

    const std::string stderrLog = (tmp / "xray_stderr_19002.log").string();
    EXPECT_TRUE(std::filesystem::exists(stderrLog));

    const std::string stderrContent = readWholeFile(stderrLog);
    EXPECT_NE(std::string::npos, stderrContent.find("PANIC"))
        << "stderr redirect file should contain the child's stderr";
    EXPECT_TRUE(anyCapturedContains(errMessages, "PANIC"))
        << "the death log message should embed the stderr tail";

    inst.stop();
    Logger::popCallback();
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}
