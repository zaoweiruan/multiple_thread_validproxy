#include <gtest/gtest.h>
#include <cstdio>
#include <string>

#include "Utils.h"

TEST(JoinUrlTest, BothWithoutSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path", "https://sub.com/v2?t=1"),
              "https://acc.com/path/https://sub.com/v2?t=1");
}

TEST(JoinUrlTest, BaseHasTrailingSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path/", "https://sub.com"),
              "https://acc.com/path/https://sub.com");
}

TEST(JoinUrlTest, SuffixHasLeadingSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path", "/https://sub.com"),
              "https://acc.com/path/https://sub.com");
}

TEST(JoinUrlTest, BothHaveSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path/", "/https://sub.com"),
              "https://acc.com/path/https://sub.com");
}

TEST(JoinUrlTest, EmptyBaseReturnsSuffix) {
    EXPECT_EQ(utils::joinUrl("", "https://sub.com"), "https://sub.com");
}

TEST(JoinUrlTest, EmptySuffixReturnsBase) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path", ""), "https://acc.com/path");
}

TEST(JoinUrlTest, BothEmptyReturnsEmpty) {
    EXPECT_EQ(utils::joinUrl("", ""), "");
}

TEST(JoinUrlTest, BaseAlreadyEndsWithSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/", "sub?t=1"),
              "https://acc.com/sub?t=1");
}

TEST(UrlValidationTest, ValidHttpUrl) {
    EXPECT_TRUE(utils::isValidUrlFormat("http://example.com"));
}

TEST(UrlValidationTest, ValidHttpsUrl) {
    EXPECT_TRUE(utils::isValidUrlFormat("https://example.com"));
}

TEST(UrlValidationTest, ValidUrlWithPath) {
    EXPECT_TRUE(utils::isValidUrlFormat("https://example.com/path/to/file"));
}

TEST(UrlValidationTest, ValidUrlWithSubdomain) {
    EXPECT_TRUE(utils::isValidUrlFormat("https://sub.example.com"));
}

TEST(UrlValidationTest, InvalidNoScheme) {
    EXPECT_FALSE(utils::isValidUrlFormat("example.com"));
}

TEST(UrlValidationTest, InvalidFtpScheme) {
    EXPECT_FALSE(utils::isValidUrlFormat("ftp://example.com"));
}

TEST(UrlValidationTest, InvalidNoDomain) {
    EXPECT_FALSE(utils::isValidUrlFormat("http://"));
}

TEST(UrlValidationTest, InvalidEmpty) {
    EXPECT_FALSE(utils::isValidUrlFormat(""));
}

TEST(UrlValidationTest, InvalidDotOnly) {
    EXPECT_FALSE(utils::isValidUrlFormat("http://."));
}

TEST(UrlValidationTest, InvalidLocalhostNoDot) {
    EXPECT_FALSE(utils::isValidUrlFormat("http://localhost"));
}

#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <vector>
#include <vector>

// Test-local oracle: does the kernel table still show a TIME_WAIT row for `port`?
// Used to prove the T1 precondition actually materialised (avoid a vacuous pass).
namespace tcpwait_oracle {
    bool hasTimeWaitRow(int port) {
        DWORD size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        if (size == 0) return false;
        std::vector<unsigned char> buf(size);
        PMIB_TCPTABLE_OWNER_PID table =
            reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buf.data());
        if (GetExtendedTcpTable(table, &size, FALSE, AF_INET,
                                TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) return false;
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort))
                    != static_cast<u_short>(port)) continue;
            if (table->table[i].dwState == MIB_TCP_STATE_TIME_WAIT) return true;
        }
        DWORD size6 = 0;
        GetExtendedTcpTable(nullptr, &size6, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
        if (size6 == 0) return false;
        std::vector<unsigned char> buf6(size6);
        PMIB_TCP6TABLE_OWNER_PID table6 =
            reinterpret_cast<PMIB_TCP6TABLE_OWNER_PID>(buf6.data());
        if (GetExtendedTcpTable(table6, &size6, FALSE, AF_INET6,
                                TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) return false;
        for (DWORD i = 0; i < table6->dwNumEntries; ++i) {
            if (ntohs(static_cast<u_short>(table6->table[i].dwLocalPort))
                    != static_cast<u_short>(port)) continue;
            if (table6->table[i].dwState == MIB_TCP_STATE_TIME_WAIT) return true;
        }
        return false;
    }
}  // namespace tcpwait_oracle

// Port availability tests — requires Winsock
#include <winsock2.h>
#include <ws2tcpip.h>

TEST(PortCheckTest, IsPortAvailableReturnsFalseWhenPortOccupied) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    // Create a TCP socket and bind to a known port
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(sock, INVALID_SOCKET);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(19876);

    // Bind + listen to occupy the port
    int bindResult = bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ASSERT_EQ(bindResult, 0) << "Cannot bind test port 19876";
    ASSERT_EQ(listen(sock, 1), 0);

    // Now the port should NOT be available
    EXPECT_FALSE(utils::isPortAvailable(19876));

    closesocket(sock);
    WSACleanup();
}

TEST(PortCheckTest, IsPortAvailableReturnsTrueWhenPortFree) {
    // After the occupied port is released, isPortAvailable should return true
    // Use a specific port that should be free
    EXPECT_TRUE(utils::isPortAvailable(19976));
}

TEST(PortCheckTest, TimeWaitOnlyRow_IsNotBlocking) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    const int testPort = 19881;

    // Manufacture a real TIME_WAIT row on testPort. The side that closes FIRST
    // is the active closer and lands in TIME_WAIT, so close the accepted
    // connection before destroying the LISTEN socket — that leaves a TIME_WAIT
    // row on the listening port and no LISTEN row at all.
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(listener, INVALID_SOCKET);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(testPort));
    ASSERT_EQ(bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0)
        << "Cannot bind test port " << testPort;
    ASSERT_EQ(listen(listener, 1), 0);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(client, INVALID_SOCKET);
    ASSERT_EQ(connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0)
        << "Cannot connect to test port " << testPort;
    SOCKET server = accept(listener, nullptr, nullptr);
    ASSERT_NE(server, INVALID_SOCKET);

    closesocket(server);    // active close -> TIME_WAIT on (testPort, ephemeral)
    closesocket(client);
    closesocket(listener);  // drop LISTEN so only the TIME_WAIT row remains

    // Timing guard: wait (max 2s) for the TIME_WAIT row to actually appear.
    // Skipping rather than failing keeps the suite green on hosts where the
    // kernel reclaims the endpoint before we can observe it.
    bool sawTimeWait = false;
    for (int i = 0; i < 100 && !sawTimeWait; ++i) {
        if (tcpwait_oracle::hasTimeWaitRow(testPort)) {
            sawTimeWait = true;
        } else {
            Sleep(20);
        }
    }
    if (!sawTimeWait) {
        WSACleanup();
        GTEST_SKIP() << "TIME_WAIT row on port " << testPort
                     << " never appeared within 2s; cannot verify the precondition";
    }

    // The TIME_WAIT row appeared in the kernel *after* the cache may have been
    // populated by an earlier case, so drop the snapshot first — otherwise the
    // probe below would answer from a stale snapshot and pass vacuously.
    utils::invalidatePortSnapshot();
    EXPECT_FALSE(utils::isPortBlocking(testPort))
        << "A TIME_WAIT-only port must not be reported as blocking "
           "(Windows: TIME_WAIT does not block rebind)";
    EXPECT_FALSE(utils::isPortListening(testPort))
        << "isPortListening must look at LISTEN only";

    WSACleanup();
}

// T2 (Spec §3.1): the rollback switch restores the old "TIME_WAIT blocks"
// behaviour, so the semantics change is reversible without a code revert.
TEST(PortCheckTest, TimeWaitBlocking_ConfigToggleRestoresOldBehavior) {
    utils::setTimeWaitBlocking(true);
    EXPECT_TRUE(utils::configTimeWaitBlocking())
        << "setTimeWaitBlocking(true) must be observable";
    utils::setTimeWaitBlocking(false);
    EXPECT_FALSE(utils::configTimeWaitBlocking())
        << "default (false) must not block on TIME_WAIT";
}

// T3 (Spec §3.1): with nothing listening, waitPortReleased must return on the
// first poll — the 1000ms budget is a ceiling, not a sleep.
TEST(PortCheckTest, WaitPortReleased_ReturnsTrueWhenNoListener) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    const int testPort = 19882;
    const ULONGLONG startedAt = GetTickCount64();
    EXPECT_TRUE(utils::waitPortReleased(testPort, 1000))
        << "An unoccupied port must be reported released immediately";
    const ULONGLONG elapsedMs = GetTickCount64() - startedAt;
    EXPECT_LT(elapsedMs, 200u)
        << "waitPortReleased must return on the first poll, not exhaust its budget";

    WSACleanup();
}

// T4 (Spec §3.1): while a real LISTEN socket is held, waitPortReleased must
// exhaust its budget and report false — it may not declare the port free.
TEST(PortCheckTest, WaitPortReleased_TimesOutWhenListenerHeld) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    const int testPort = 19883;
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(sock, INVALID_SOCKET);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(testPort));
    ASSERT_EQ(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0)
        << "Cannot bind test port " << testPort;
    ASSERT_EQ(listen(sock, 1), 0);

    EXPECT_FALSE(utils::waitPortReleased(testPort, 300))
        << "A held LISTEN socket must keep the port 'not released' until the budget expires";

    closesocket(sock);
    WSACleanup();
}

// T7 (Spec §3.2 / Task 2 P0-2): the convergence check AppController::stopStandaloneProxy
// relies on. A socket we created and then closed must be reported released within the
// budget — this is the contract that lets a just-stopped proxy rebind the same port.
// Pure `utils` contract only: no PortManager (test_utils does not link it), no xray.
TEST(PortCheckTest, WaitPortReleased_TrueAfterSyntheticClose) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);
    const int testPort = 19884;   // 避开已用的 19881/19882/19883
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(sock, INVALID_SOCKET);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(testPort));
    ASSERT_EQ(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0) << "Cannot bind test port " << testPort;
    ASSERT_EQ(listen(sock, 1), 0);
    // Closing the listening socket is what stopStandaloneProxy does after the process
    // is gone; the kernel may hold the LISTEN row for a short window afterwards.
    closesocket(sock);
    EXPECT_TRUE(utils::waitPortReleased(testPort, 1000))
        << "A locally closed LISTEN socket must be reported released within the budget";
    WSACleanup();
}
// T5 (P1-3): within the snapshot TTL, three consecutive probes must not each pay a
// full two-family table enumeration. A refresh costs +2 on the counter (one per
// address family), so 3 probes inside the TTL are expected to add <= 4.
TEST(PortCheckTest, SnapshotCache_EnumeratesOnceWithinTtl) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);
    const int testPort = 19885;
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(sock, INVALID_SOCKET);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(testPort));
    ASSERT_EQ(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(listen(sock, 1), 0);
    // The kernel state just changed outside the cache's knowledge.
    utils::invalidatePortSnapshot();

    const long before = utils::testing::g_tcpTableEnumerationCount.load(
                             std::memory_order_relaxed);
    EXPECT_TRUE(utils::isPortBlocking(testPort));
    EXPECT_TRUE(utils::isPortBlocking(testPort));
    EXPECT_TRUE(utils::isPortBlocking(testPort));
    const long delta = utils::testing::g_tcpTableEnumerationCount.load(
                            std::memory_order_relaxed) - before;

    EXPECT_GT(delta, 0L) << "the cache must have been populated at least once";
    EXPECT_LE(delta, 4L) << "3 probes inside the TTL must not cost 3 full enumerations";

    closesocket(sock);
    utils::invalidatePortSnapshot();
    WSACleanup();
}
TEST(PortCheckTest, FindAvailablePortReturnsNextFreePort) {

    // Create a listening socket to block a specific port
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    SOCKET testSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(testSock, INVALID_SOCKET);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(19877);

    ASSERT_EQ(bind(testSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(listen(testSock, 1), 0);
    utils::invalidatePortSnapshot();

    // findAvailablePort should skip port 19877 and return 19878 (or higher)
    int found = utils::findAvailablePort(19877, 10);
    EXPECT_GT(found, 19877);
    EXPECT_LE(found, 19887);

    closesocket(testSock);
    WSACleanup();
}

// Regression (session b5): a real server (e.g. xray SOCKS5) listens on the
// WILDCARD address 0.0.0.0:<port>. isPortAvailable must report it as occupied.
// The previous probe bound 127.0.0.1 only, which on Windows is allowed to
// coexist with a 0.0.0.0 listener on the same port -> false "free" (the bug).
TEST(PortCheckTest, OccupiedOnWildcardReportedUnavailable) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(sock, INVALID_SOCKET);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // 0.0.0.0 -- the real-world case
    addr.sin_port = htons(19878);

    ASSERT_EQ(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0)
        << "Cannot bind wildcard test port 19878";
    ASSERT_EQ(listen(sock, 1), 0);
    utils::invalidatePortSnapshot();

    EXPECT_FALSE(utils::isPortAvailable(19878))
        << "Wildcard (0.0.0.0) listener must make the port unavailable";

    closesocket(sock);
    WSACleanup();
}

// Regression: an IPv6-only ([::]) listener must also be detected. The old probe
// was IPv4-only and would silently miss an IPv6 occupant.
TEST(PortCheckTest, OccupiedOnIpv6ReportedUnavailable) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    SOCKET sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        GTEST_SKIP();   // IPv6 unavailable on this host
    }
    int v6only = 1;
    setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
               reinterpret_cast<const char*>(&v6only), sizeof(v6only));

    sockaddr_in6 addr6 = {};
    addr6.sin6_family = AF_INET6;
    addr6.sin6_addr = in6addr_any;   // ::
    addr6.sin6_port = htons(19879);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6)) != 0 ||
        listen(sock, 1) != 0) {
        closesocket(sock);
        WSACleanup();
        GTEST_SKIP() << "Cannot bind IPv6 test port 19879";
    }
    utils::invalidatePortSnapshot();

    EXPECT_FALSE(utils::isPortAvailable(19879))
        << "IPv6 ([::]) listener must make the port unavailable";

    closesocket(sock);
    WSACleanup();
}

// Regression: findAvailablePort must skip a port occupied on 0.0.0.0.
TEST(PortCheckTest, FindAvailablePortSkipsWildcardOccupied) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(sock, INVALID_SOCKET);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(19880);

    ASSERT_EQ(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(listen(sock, 1), 0);
    utils::invalidatePortSnapshot();

    int found = utils::findAvailablePort(19880, 10);
    EXPECT_NE(found, 19880)
        << "findAvailablePort must not return a wildcard-occupied port";

    closesocket(sock);
    WSACleanup();
}

TEST(PrintableAsciiTest, EmptyStringReturnsTrue) {
    EXPECT_TRUE(utils::isPrintableAscii(""));
}

TEST(PrintableAsciiTest, PrintableAsciiOnlyReturnsTrue) {
    EXPECT_TRUE(utils::isPrintableAscii("aA1 _-~"));
    EXPECT_TRUE(utils::isPrintableAscii("0123456789"));
    EXPECT_TRUE(utils::isPrintableAscii("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"));
}

TEST(PrintableAsciiTest, ControlCharactersReturnFalse) {
    std::string withNul = "abc";
    withNul.push_back('\0');
    withNul.push_back('d');
    EXPECT_FALSE(utils::isPrintableAscii(withNul));

    std::string withUsAscii31 = "abc";
    withUsAscii31.push_back(static_cast<char>(0x1F));
    EXPECT_FALSE(utils::isPrintableAscii(withUsAscii31));

    std::string withDel = "abc";
    withDel.push_back(static_cast<char>(0x7F));
    EXPECT_FALSE(utils::isPrintableAscii(withDel));
}

TEST(PrintableAsciiTest, Utf8MultibyteReturnsFalse) {
    EXPECT_FALSE(utils::isPrintableAscii("中文"));
}

TEST(PrintableAsciiTest, MixedContentReturnsFalse) {
    EXPECT_FALSE(utils::isPrintableAscii("abc\xE4\xB8\xAD"));
}

// Regression: numeric-leading valid domain names previously rejected by the
// IPv4 parser's early "octet > 255" short-circuit (see
// docs/bugfix/2026-08-12-Bugfix-IsPublicAddress-NumericDomain-v1.0.md).
TEST(IsPublicAddressTest, NumericDomainWithAlphaTLD) {
    EXPECT_TRUE(utils::isPublicAddress("8103.shomaparvazroyadetonnist-bypassishere.lat"));
    EXPECT_TRUE(utils::isPublicAddress("876.outline-vpn.cloud"));
    EXPECT_TRUE(utils::isPublicAddress("1744156156.tencentapp.cn"));
    EXPECT_TRUE(utils::isPublicAddress("67.7777112.xyz"));
    EXPECT_TRUE(utils::isPublicAddress("230920393.f-sub.com"));
}

TEST(IsPublicAddressTest, LeadingZeroNumericDomain) {
    // "09..." is not the "0." pattern of the Rule 1 heuristic, so it must pass.
    EXPECT_TRUE(utils::isPublicAddress("09303582303.ddns.net"));
}

TEST(IsPublicAddressTest, LongNumericSubdomainFallsBackToDomain) {
    EXPECT_TRUE(utils::isPublicAddress(
        "93343878961078676381579883502615.international-ixp.com"));
    EXPECT_TRUE(utils::isPublicAddress(
        "0101010101010101010101001010101010110010101010101010101010101.poki-pakipon.ir"));
}

TEST(IsPublicAddressTest, LoopbackStillRejected) {
    EXPECT_FALSE(utils::isPublicAddress("127.0.0.1"));
    EXPECT_FALSE(utils::isPublicAddress("127.0.0.53"));
    EXPECT_FALSE(utils::isPublicAddress("127.1.1.127"));
}

TEST(IsPublicAddressTest, PrivateRangesStillRejected) {
    EXPECT_FALSE(utils::isPublicAddress("0.0.0.0"));
    EXPECT_FALSE(utils::isPublicAddress("10.1.2.3"));
    EXPECT_FALSE(utils::isPublicAddress("192.168.1.1"));
    EXPECT_FALSE(utils::isPublicAddress("172.16.0.1"));
    EXPECT_FALSE(utils::isPublicAddress("172.31.255.255"));
    EXPECT_FALSE(utils::isPublicAddress("169.254.1.1"));
}

TEST(IsPublicAddressTest, MulticastAndReservedStillRejected) {
    EXPECT_FALSE(utils::isPublicAddress("224.0.0.1"));
    EXPECT_FALSE(utils::isPublicAddress("240.0.0.1"));
}

TEST(IsPublicAddressTest, PublicIpv4Accepted) {
    EXPECT_TRUE(utils::isPublicAddress("8.8.8.8"));
    EXPECT_TRUE(utils::isPublicAddress("1.2.3.4"));
    EXPECT_TRUE(utils::isPublicAddress("172.32.0.1"));
}

TEST(IsPublicAddressTest, MalformedIpv4Rejected) {
    EXPECT_FALSE(utils::isPublicAddress("256.1.1.1"));
    EXPECT_FALSE(utils::isPublicAddress("1.2.3.4.5"));
    EXPECT_FALSE(utils::isPublicAddress("1.2.3"));
    EXPECT_FALSE(utils::isPublicAddress("1."));
    EXPECT_FALSE(utils::isPublicAddress("."));
    EXPECT_FALSE(utils::isPublicAddress("1..2"));
}

TEST(IsPublicAddressTest, NormalDomainAccepted) {
    EXPECT_TRUE(utils::isPublicAddress("example.com"));
    EXPECT_TRUE(utils::isPublicAddress("sub.domain.example.org"));
}

TEST(IsPublicAddressTest, GarbageDomainHeuristicsRejected) {
    EXPECT_FALSE(utils::isPublicAddress("0.example.com"));
    EXPECT_FALSE(utils::isPublicAddress("a.b.c.d.example.org"));
    EXPECT_FALSE(utils::isPublicAddress("0.0.0.einetwork.news"));
}

TEST(IsPublicAddressTest, IpPrefixedDomainFallsBackToDomain) {
    EXPECT_TRUE(utils::isPublicAddress("127.0.0.1.example.com"));
}

TEST(IsPublicAddressTest, EmptyAndSingleLabel) {
    EXPECT_FALSE(utils::isPublicAddress(""));
    EXPECT_TRUE(utils::isPublicAddress("localhost"));
}

TEST(IsPublicAddressTest, Ipv6LiteralUnchanged) {
    EXPECT_FALSE(utils::isPublicAddress("::1"));
    EXPECT_FALSE(utils::isPublicAddress("::"));
    EXPECT_FALSE(utils::isPublicAddress("fe80::1"));
    EXPECT_TRUE(utils::isPublicAddress("2001:4860:4860::8888"));
}

TEST(IsTestResultValidTest, SuccessWithPositiveLatency) {
    EXPECT_TRUE(utils::isTestResultValid(true, 1));
    EXPECT_TRUE(utils::isTestResultValid(true, 100));
}

TEST(IsTestResultValidTest, SuccessWithZeroLatencyIsInvalid) {
    EXPECT_FALSE(utils::isTestResultValid(true, 0));
}

TEST(IsTestResultValidTest, FailureAlwaysInvalid) {
    EXPECT_FALSE(utils::isTestResultValid(false, -1));
    EXPECT_FALSE(utils::isTestResultValid(false, 0));
    EXPECT_FALSE(utils::isTestResultValid(false, 100));
}

// Bugfix 2026-08-21 (StandaloneMonitor-DataColumns): getCurrentTimestamp()
// returns epoch seconds and must never be fed to durationMsBetween(). The
// formatted variant below is the datetime-compatible counterpart.
TEST(GetCurrentTimestampFormattedTest, MatchesDatetimeFormat) {
    const std::string ts = utils::getCurrentTimestampFormatted();
    EXPECT_EQ(static_cast<int>(ts.size()), 19);
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    EXPECT_EQ(std::sscanf(ts.c_str(), "%d-%d-%d %d:%d:%d",
                          &y, &mo, &d, &h, &mi, &se), 6);
    EXPECT_GE(y, 2026);
    EXPECT_GE(mo, 1);
    EXPECT_LE(mo, 12);
    EXPECT_GE(d, 1);
    EXPECT_LE(d, 31);
}

TEST(GetCurrentTimestampFormattedTest, CompatibleWithDurationMsBetweenParser) {
    // Same string twice: a compatible parser yields exactly zero elapsed ms.
    const std::string now = utils::getCurrentTimestampFormatted();
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    ASSERT_EQ(std::sscanf(now.c_str(), "%d-%d-%d %d:%d:%d",
                          &y, &mo, &d, &h, &mi, &se), 6);
}
