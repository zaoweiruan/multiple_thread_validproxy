#include <gtest/gtest.h>
#include "NetworkMonitor.h"
#include "CurlEasyHandle.h"
#include "test_utils.h"
#include <thread>
#include <chrono>
#include <cstdlib>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

static const std::vector<std::string> kBadUrls = {
    "http://127.0.0.1:1"
};

static const int kShortIntervalMs = 50;
static const int kShortTimeoutMs = 50;
static const int kWaitMs = 500;

namespace {

/// Minimal loopback HTTP responder so the "connected then disconnected"
/// transition can be produced without touching the real internet.
///
/// LoggingOnConnectionLost used to reach "connected" by probing
/// https://www.example.com and then guarding phase 2 with
/// `if (nm.IsConnected())`. On a slow link — or simply under parallel ctest
/// load — phase 1 never connected, the guard silently skipped the body, and the
/// test then asserted a LOST line that was never emitted. Roughly 1 run in 12
/// failed. A loopback listener makes the precondition deterministic.
class LoopbackHttpServer {
public:
    bool start() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return false;
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // let the OS pick a free port
        if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
            listen(s, 8) == SOCKET_ERROR) {
            closesocket(s);
            return false;
        }
        sockaddr_in bound = {};
        int len = sizeof(bound);
        if (getsockname(s, reinterpret_cast<struct sockaddr*>(&bound), &len) == SOCKET_ERROR) {
            closesocket(s);
            return false;
        }
        listenSock_ = s;
        port_ = ntohs(bound.sin_port);
        running_ = true;
        thread_ = std::thread(&LoopbackHttpServer::serve, this);
        return true;
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        if (listenSock_ != INVALID_SOCKET) {
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
        }
    }

    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/";
    }

private:
    void serve() {
        // The listener MUST be non-blocking: a blocking accept() would never
        // return once running_ flips to false, so stop() would hang on join().
        u_long mode = 1;
        ioctlsocket(listenSock_, FIONBIO, &mode);

        while (running_) {
            sockaddr_in peer = {};
            int len = sizeof(peer);
            SOCKET client = accept(listenSock_, reinterpret_cast<struct sockaddr*>(&peer), &len);
            if (client == INVALID_SOCKET) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            // On Windows an accepted socket inherits the listener's non-blocking
            // mode, so restore blocking before reading the request head.
            u_long blocking = 0;
            ioctlsocket(client, FIONBIO, &blocking);

            char buf[1024];
            recv(client, buf, sizeof(buf), 0);
            const char* resp =
                "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client, resp, static_cast<int>(strlen(resp)), 0);
            closesocket(client);
        }
    }

    SOCKET listenSock_{INVALID_SOCKET};
    int port_{0};
    bool running_{false};
    std::thread thread_;
};

/// Temporarily replaces a proxy environment variable and restores it on scope
/// exit. Both the CRT copy (`_putenv`, what libcurl's getenv reads) and the
/// Win32 block (`SetEnvironmentVariableA`) are updated.
class ScopedEnvVar {
public:
    ScopedEnvVar(const char* name, const std::string& value) : name_(name) {
        const char* current = std::getenv(name);
        hadValue_ = (current != nullptr);
        if (hadValue_) {
            saved_ = current;
        }
        apply(value);
    }

    ~ScopedEnvVar() {
        if (hadValue_) {
            apply(saved_);
        } else {
            remove();
        }
    }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

private:
    void apply(const std::string& value) const {
        std::string assignment = name_ + "=" + value;
        _putenv(assignment.c_str());
        SetEnvironmentVariableA(name_.c_str(), value.c_str());
    }

    void remove() const {
        std::string assignment = name_ + "=";
        _putenv(assignment.c_str());
        SetEnvironmentVariableA(name_.c_str(), nullptr);
    }

    std::string name_;
    std::string saved_;
    bool hadValue_{false};
};

}  // namespace

TEST(NetworkMonitorTest, EnabledFalse_ReturnsTrue) {
    NetworkMonitor nm(false);
    EXPECT_TRUE(nm.IsConnected());
    EXPECT_FALSE(nm.IsEnabled());
}

TEST(NetworkMonitorTest, IsConnected_Default_False) {
    NetworkMonitor nm;
    EXPECT_FALSE(nm.IsConnected());
    EXPECT_TRUE(nm.IsEnabled());
}

TEST(NetworkMonitorTest, StartStop_NoCrash) {
    NetworkMonitor nm;
    EXPECT_TRUE(nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nm.Stop();
}

TEST(NetworkMonitorTest, StartWithUnreachableUrl_ConnectedBecomesFalse) {
    NetworkMonitor nm;
    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
    nm.Stop();
    EXPECT_FALSE(nm.IsConnected());
}

TEST(NetworkMonitorTest, StopWithoutStart_NoCrash) {
    NetworkMonitor nm;
    nm.Stop();
}

TEST(NetworkMonitorTest, DoubleStop_NoCrash) {
    NetworkMonitor nm;
    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nm.Stop();
    nm.Stop();
}

TEST(NetworkMonitorTest, RestartAfterStop_ResetsConnection) {
    NetworkMonitor nm;
    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
    nm.Stop();
    EXPECT_FALSE(nm.IsConnected());

    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
    nm.Stop();
    EXPECT_FALSE(nm.IsConnected());
}

TEST(NetworkMonitorTest, StartWithEmptyUrls_ConnectedBecomesFalse) {
    NetworkMonitor nm;
    std::vector<std::string> emptyUrls;
    nm.Start(emptyUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
    nm.Stop();
    EXPECT_FALSE(nm.IsConnected());
}

TEST(NetworkMonitorTest, LoggingOnConnectionLost) {
    LogCapture capture;
    LoopbackHttpServer server;
    ASSERT_TRUE(server.start()) << "could not bind a loopback listener";
    {
        NetworkMonitor nm;

        // Phase 1: reach a REAL connected state (locally, no internet needed).
        nm.Start({server.url()}, kShortIntervalMs, 3000);
        for (int i = 0; i < 40 && !nm.IsConnected(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ASSERT_TRUE(nm.IsConnected()) << "loopback probe never reported connected";

        // Phase 2: switch to unreachable URLs to force the LOST transition.
        nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
        nm.Stop();
    }
    server.stop();

    bool foundLost = false;
    std::vector<LogCapture::Entry> entries = capture.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].message.find("LOST") != std::string::npos &&
            entries[i].level == LogLevel::ERR) {
            foundLost = true;
            break;
        }
    }
    EXPECT_TRUE(foundLost);
}

// --- F8: majority vote so a single flaky URL cannot declare an outage ---------

TEST(NetworkMonitorTest, MajorityVerdict_TwoOfThreeStaysConnected) {
    // The jitter regression: 2 of 3 check URLs reachable must stay connected.
    // Before F8 any single failure meant disconnected.
    EXPECT_TRUE(NetworkMonitor::majorityVerdict(2, 1, 3));
}

TEST(NetworkMonitorTest, MajorityVerdict_OneOfThreeIsDown) {
    EXPECT_FALSE(NetworkMonitor::majorityVerdict(1, 2, 3));
}

TEST(NetworkMonitorTest, MajorityVerdict_AllProbesAbstainedStaysConnected) {
    // All-DNS-failure round: legacy semantics (2026-08-06 fix) must be preserved.
    EXPECT_TRUE(NetworkMonitor::majorityVerdict(0, 0, 3));
}

TEST(NetworkMonitorTest, MajorityVerdict_NoUrlsIsDown) {
    EXPECT_FALSE(NetworkMonitor::majorityVerdict(0, 0, 0));
}

TEST(NetworkMonitorTest, MajorityVerdict_SingleUrlNeedsSuccess) {
    EXPECT_TRUE(NetworkMonitor::majorityVerdict(1, 0, 1));
    EXPECT_FALSE(NetworkMonitor::majorityVerdict(0, 1, 1));
}

TEST(NetworkMonitorTest, ConsecutiveFailures_CountsCorrectly) {
    NetworkMonitor nm;
    nm.resetProbeCount();

    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs * 2));
    nm.Stop();

    // Should have some consecutive failures recorded
    int failures = nm.getConsecutiveFailures();
    EXPECT_GE(failures, 1);
}

// --- RCA 2026-10-23: probes must NOT inherit env HTTP_PROXY/HTTPS_PROXY ------
// A stale session proxy (e.g. http://127.0.0.1:10808 with nothing listening)
// made every probe fail with curl_error=7 and flipped the app into "network
// LOST" / "AutoTask aborted - network disconnected" while the link was fine.

TEST(NetworkMonitorTest, DisableProxy_OverridesExplicitProxy) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.start()) << "could not bind a loopback listener";

    CurlEasyHandle curl;
    ASSERT_TRUE(curl.valid());
    // The dead proxy would make this fail if disableProxy() did not take effect.
    curl.setUrl(server.url())
        .setProxy("http://127.0.0.1:1")
        .disableProxy()
        .setNoBody(true)
        .setTimeoutMs(3000);

    CURLcode res = curl_easy_perform(curl.get());
    long httpCode = curl.getResponseCode();
    server.stop();

    EXPECT_EQ(res, CURLE_OK) << "curl_easy_strerror: " << curl_easy_strerror(res);
    EXPECT_EQ(httpCode, 200L);
}

TEST(NetworkMonitorTest, EnvProxyIsIgnored_LoopbackStaysConnected) {
    ScopedEnvVar httpProxy("HTTP_PROXY", "http://127.0.0.1:1");
    ScopedEnvVar httpsProxy("HTTPS_PROXY", "http://127.0.0.1:1");
    if (std::getenv("HTTPS_PROXY") == nullptr) {
        GTEST_SKIP() << "CRT does not observe environment changes; cannot simulate a stale proxy";
    }

    LoopbackHttpServer server;
    ASSERT_TRUE(server.start()) << "could not bind a loopback listener";

    NetworkMonitor nm;
    nm.Start({server.url()}, kShortIntervalMs, 3000);
    for (int i = 0; i < 40 && !nm.IsConnected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    bool connected = nm.IsConnected();
    nm.Stop();
    server.stop();

    EXPECT_TRUE(connected)
        << "probe inherited the stale HTTP_PROXY/HTTPS_PROXY and reported the network as down";
}
