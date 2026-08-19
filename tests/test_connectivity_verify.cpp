// ConnectivityVerifyTest — R2 启动后连通性验证（Spec §3.3.2）
//
// 通过本地极简 SOCKS5 代理 + HTTP 服务器（127.0.0.1，无外部网络）端到端验证
// ConnectivityVerifier::verify 的探测行为：
//   - SocksProxyOk   : 经 socks5 端口访问本地 URL 成功 → 判定连通
//   - SocksProxyFail : 代理端口无 SOCKS 响应 → 判定不连通
//   - RetryPolicy    : 重试控制流（注入探针，不依赖网络）
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "ProxyConnectivityVerifier.h"

namespace {

// --- tiny local test servers (loopback only, no external network) ---

class WinsockGuard {
public:
    WinsockGuard() {
        WSADATA wsa;
        initialized_ = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    }
    ~WinsockGuard() {
        if (initialized_) {
            WSACleanup();
        }
    }
    bool ok() const { return initialized_; }

private:
    bool initialized_ = false;
};

// Binds a listener on 127.0.0.1:0 and writes the chosen port into outPort.
SOCKET bindLoopback(int& outPort) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    if (listen(s, 8) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    sockaddr_in bound = {};
    int len = sizeof(bound);
    if (getsockname(s, reinterpret_cast<struct sockaddr*>(&bound), &len) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    outPort = ntohs(bound.sin_port);
    return s;
}

void setNonBlocking(SOCKET s) {
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
}

// On Windows, sockets returned by accept() inherit the listener's
// non-blocking mode; restore blocking so recv() waits for real data.
void setBlocking(SOCKET s) {
    u_long mode = 0;
    ioctlsocket(s, FIONBIO, &mode);
}

// Reads until the HTTP header terminator is observed (or an error occurs).
void readHttpHeader(SOCKET s) {
    char buf[4096];
    std::string acc;
    while (acc.find("\r\n\r\n") == std::string::npos) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) {
            return;
        }
        acc.append(buf, static_cast<size_t>(n));
        if (acc.size() > 65536) {
            return;
        }
    }
}

// HTTP server thread: answers every request with a 200 OK body so the
// cURL write callback receives data (a 204 No Content would leave the
// response body empty and the probe would be treated as failed).
// When noContent is true the server replies 204 No Content with an empty
// body, mimicking test.url = https://www.google.com/generate_204.
void httpServerLoop(SOCKET listener, std::atomic<bool>& stop, bool noContent = false) {
    while (!stop.load()) {
        SOCKET c = accept(listener, nullptr, nullptr);
        if (c == INVALID_SOCKET) {
            if (stop.load()) {
                break;
            }
            Sleep(5);
            continue;
        }
        setBlocking(c);
        readHttpHeader(c);
        const char* resp;
        if (noContent) {
            resp = "HTTP/1.1 204 No Content\r\n"
                   "Content-Length: 0\r\n"
                   "Connection: close\r\n\r\n";
        } else {
            resp = "HTTP/1.1 200 OK\r\n"
                   "Content-Length: 2\r\n"
                   "Connection: close\r\n\r\n"
                   "ok";
        }
        send(c, resp, static_cast<int>(std::strlen(resp)), 0);
        closesocket(c);
    }
}

// Bidirectional relay between client and target until either side closes.
void relay(SOCKET a, SOCKET b) {
    fd_set fds;
    char buf[8192];
    bool openA = true;
    bool openB = true;
    while (openA && openB) {
        FD_ZERO(&fds);
        if (openA) {
            FD_SET(a, &fds);
        }
        if (openB) {
            FD_SET(b, &fds);
        }
        timeval tv = {};
        tv.tv_usec = 200000; // 200ms poll
        int r = select(0, &fds, nullptr, nullptr, &tv);
        if (r <= 0) {
            continue;
        }
        if (openA && FD_ISSET(a, &fds)) {
            int n = recv(a, buf, sizeof(buf), 0);
            if (n <= 0) {
                openA = false;
                shutdown(b, SD_SEND);
            } else if (send(b, buf, n, 0) == SOCKET_ERROR) {
                openB = false;
            }
        }
        if (openB && FD_ISSET(b, &fds)) {
            int n = recv(b, buf, sizeof(buf), 0);
            if (n <= 0) {
                openB = false;
                shutdown(a, SD_SEND);
            } else if (send(a, buf, n, 0) == SOCKET_ERROR) {
                openA = false;
            }
        }
    }
    shutdown(a, SD_BOTH);
    shutdown(b, SD_BOTH);
}

// Minimal SOCKS5 no-auth + CONNECT proxy (RFC 1928 subset, IPv4 only).
void socks5ServerLoop(SOCKET listener, std::atomic<bool>& stop) {
    while (!stop.load()) {
        SOCKET c = accept(listener, nullptr, nullptr);
        if (c == INVALID_SOCKET) {
            if (stop.load()) {
                break;
            }
            Sleep(5);
            continue;
        }
        setBlocking(c);
        char buf[512];
        int n = recv(c, buf, sizeof(buf), 0); // 05 01 00
        if (n < 3 || static_cast<unsigned char>(buf[0]) != 0x05) {
            closesocket(c);
            continue;
        }
        const char replySel = 0x05;
        const char replyAuth = 0x00;
        send(c, &replySel, 1, 0);
        send(c, &replyAuth, 1, 0);
        n = recv(c, buf, sizeof(buf), 0); // 05 01 00 01 <4B IP> <2B port>
        if (n < 10 || static_cast<unsigned char>(buf[0]) != 0x05
            || static_cast<unsigned char>(buf[1]) != 0x01
            || static_cast<unsigned char>(buf[3]) != 0x01) {
            closesocket(c);
            continue;
        }
        sockaddr_in target = {};
        target.sin_family = AF_INET;
        std::memcpy(&target.sin_addr, buf + 4, 4);
        std::memcpy(&target.sin_port, buf + 8, 2);
        SOCKET t = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (t == INVALID_SOCKET
            || connect(t, reinterpret_cast<struct sockaddr*>(&target), sizeof(target)) == SOCKET_ERROR) {
            const char rep[] = {0x05, 0x05, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
            send(c, rep, sizeof(rep), 0);
            if (t != INVALID_SOCKET) {
                closesocket(t);
            }
            closesocket(c);
            continue;
        }
        const char rep[] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        send(c, rep, sizeof(rep), 0);
        relay(c, t);
        closesocket(t);
        closesocket(c);
    }
}

// RAII environment: local HTTP server + local SOCKS5 proxy on ephemeral ports.
class TestProxyEnv {
public:
    explicit TestProxyEnv(bool http204 = false)
        : wsa_(),
          httpListener_(INVALID_SOCKET),
          socksListener_(INVALID_SOCKET),
          httpPort_(0),
          socksPort_(0),
          http204_(http204) {
        httpListener_ = bindLoopback(httpPort_);
        socksListener_ = bindLoopback(socksPort_);
        if (httpListener_ == INVALID_SOCKET || socksListener_ == INVALID_SOCKET) {
            return;
        }
        setNonBlocking(httpListener_);
        setNonBlocking(socksListener_);
        httpThread_ = std::thread(httpServerLoop, httpListener_, std::ref(stop_), http204_);
        socksThread_ = std::thread(socks5ServerLoop, socksListener_, std::ref(stop_));
    }
    ~TestProxyEnv() {
        stop_.store(true);
        closesocket(httpListener_);
        closesocket(socksListener_);
        if (httpThread_.joinable()) {
            httpThread_.join();
        }
        if (socksThread_.joinable()) {
            socksThread_.join();
        }
    }
    bool ready() const {
        return httpListener_ != INVALID_SOCKET && socksListener_ != INVALID_SOCKET;
    }
    int httpPort() const { return httpPort_; }
    int socksPort() const { return socksPort_; }

private:
    WinsockGuard wsa_;
    SOCKET httpListener_;
    SOCKET socksListener_;
    int httpPort_;
    int socksPort_;
    bool http204_;
    std::atomic<bool> stop_{false};
    std::thread httpThread_;
    std::thread socksThread_;
};

} // namespace

TEST(ConnectivityVerifyTest, SocksProxyOk) {
    TestProxyEnv env;
    ASSERT_TRUE(env.ready());

    const std::string url = "http://127.0.0.1:" + std::to_string(env.httpPort()) + "/";
    EXPECT_TRUE(proxy::ConnectivityVerifier::verify(env.socksPort(), url, 3000, 3));
}

// Regression: test.url = https://www.google.com/generate_204 returns HTTP 204
// No Content (empty body). The verifier must judge success by the HTTP status
// code (200/204, like ProxyTester), not by a non-empty response body, or every
// standalone proxy verification would fail against generate_204.
TEST(ConnectivityVerifyTest, SocksProxyOk_NoContentBody) {
    TestProxyEnv env(/*http204=*/true);
    ASSERT_TRUE(env.ready());

    const std::string url = "http://127.0.0.1:" + std::to_string(env.httpPort()) + "/";
    EXPECT_TRUE(proxy::ConnectivityVerifier::verify(env.socksPort(), url, 3000, 3));
}

TEST(ConnectivityVerifyTest, SocksProxyFail) {
    // Listener that accepts connections but never answers the SOCKS handshake:
    // curl connects, sends the version negotiation, then times out.
    WinsockGuard wsa;
    int port = 0;
    SOCKET l = bindLoopback(port);
    ASSERT_NE(l, INVALID_SOCKET);
    setNonBlocking(l);
    std::atomic<bool> stop{false};
    std::thread keepAlive([&stop]() {
        while (!stop.load()) {
            Sleep(10);
        }
    });

    const std::string url = "http://127.0.0.1:1/";
    EXPECT_FALSE(proxy::ConnectivityVerifier::verify(port, url, 1000, 2));

    stop.store(true);
    keepAlive.join();
    closesocket(l);
}

TEST(ConnectivityVerifyTest, RetryPolicy_ExhaustsAttempts) {
    int calls = 0;
    auto probe = [&calls]() -> bool {
        ++calls;
        return false;
    };
    EXPECT_FALSE(proxy::ConnectivityVerifier::verifyWithProbe(probe, 3));
    EXPECT_EQ(calls, 3);
}

TEST(ConnectivityVerifyTest, RetryPolicy_SucceedsOnLaterAttempt) {
    int calls = 0;
    auto probe = [&calls]() -> bool {
        ++calls;
        return calls >= 3;
    };
    EXPECT_TRUE(proxy::ConnectivityVerifier::verifyWithProbe(probe, 5));
    EXPECT_EQ(calls, 3);
}

TEST(ConnectivityVerifyTest, WaitForPort_ListeningPortTrue) {
    WinsockGuard wsa;
    int port = 0;
    SOCKET l = bindLoopback(port);
    ASSERT_NE(l, INVALID_SOCKET);
    ASSERT_EQ(listen(l, 1), 0);

    EXPECT_TRUE(proxy::ConnectivityVerifier::waitForPort(port, 2000));

    closesocket(l);
}

TEST(ConnectivityVerifyTest, WaitForPort_ClosedPortFalse) {
    WinsockGuard wsa;
    // Grab an ephemeral port then close it so nothing is listening.
    int port = 0;
    SOCKET l = bindLoopback(port);
    ASSERT_NE(l, INVALID_SOCKET);
    closesocket(l);

    EXPECT_FALSE(proxy::ConnectivityVerifier::waitForPort(port, 300));
}

TEST(ConnectivityVerifyTest, WaitForPort_BecomesAvailable) {
    // Simulates the proxy-process startup window: the port is completely free
    // at first (connect → WSAECONNREFUSED), then a delayed bind+listen makes
    // it reachable. waitForPort must poll until it appears.
    WinsockGuard wsa;
    int port = 0;
    SOCKET probe = bindLoopback(port);
    ASSERT_NE(probe, INVALID_SOCKET);
    closesocket(probe); // release the port so nothing is bound initially

    std::atomic<bool> started{false};
    std::atomic<bool> stop{false};
    std::thread binder([&]() {
        Sleep(600);
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<u_short>(port));
        bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        listen(s, 1);
        started.store(true);
        while (!stop.load()) {
            Sleep(10);
        }
        closesocket(s);
    });

    EXPECT_TRUE(proxy::ConnectivityVerifier::waitForPort(port, 5000));
    EXPECT_TRUE(started.load());

    stop.store(true);
    binder.join();
}

// RetryPolicy: a failed probe must be followed by retryDelayMs before the next
// attempt, so the total window covers the proxy process startup time (the xray
// process needs several seconds before it is fully ready). Regression test for
// the standalone connectivity failures reported in ui_20260817_112412.log.
TEST(ConnectivityVerifyTest, RetryPolicy_InsertsDelayBetweenAttempts) {
    int calls = 0;
    proxy::ConnectivityVerifier::ProbeFn probe = [&calls]() -> bool {
        ++calls;
        return false;
    };

    const auto start = std::chrono::steady_clock::now();
    const bool ok = proxy::ConnectivityVerifier::verifyWithProbe(probe, 3, 300);
    const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_FALSE(ok);
    EXPECT_EQ(calls, 3);
    // Two failed probes (attempts 1 and 2) each sleep retryDelayMs (300 ms).
    EXPECT_GE(elapsedMs, 600);
}
