// Unit tests for ProcessInspector standalone-config command-line extraction.
// Covers both the existing bare-name extraction and the new full-path fallback
// used to resolve the listening port of proxies launched by external programs
// from a non-standard config directory.

// winsock2.h MUST precede <windows.h> (pulled in by ProcessInspector.h).
#include <winsock2.h>
#include <ws2tcpip.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ProcessInspector.h"


using proc::ProcessInspector;

// ---------------------------------------------------------------------------
// extractConfigFileName (bare name, existing behavior)
// ---------------------------------------------------------------------------

TEST(ProcessInspectorTest, ExtractConfigFileName_QuotedFullPath) {
    // The full path is passed on the command line; only the bare name is returned.
    std::wstring cmd =
        L"\"C:\\exe\\xray.exe\" run -c \"C:\\exe\\config\\standalone_abc123-xray.json\"";
    EXPECT_EQ(ProcessInspector::extractConfigFileName(cmd),
              "standalone_abc123-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFileName_SingboxBare) {
    std::wstring cmd = L"sing-box run -c standalone_myid-singbox.json";
    EXPECT_EQ(ProcessInspector::extractConfigFileName(cmd),
              "standalone_myid-singbox.json");
}

TEST(ProcessInspectorTest, ExtractConfigFileName_None) {
    std::wstring cmd = L"xray.exe run";
    EXPECT_EQ(ProcessInspector::extractConfigFileName(cmd), "");
}

// ---------------------------------------------------------------------------
// extractConfigFullPath (full path, new fallback)
// ---------------------------------------------------------------------------

TEST(ProcessInspectorTest, ExtractConfigFullPath_AbsoluteQuoted) {
    std::wstring cmd =
        L"\"D:\\other\\xray.exe\" -c \"D:\\other\\standalone_abc-xray.json\"";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd),
              "D:\\other\\standalone_abc-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_AbsoluteUnquoted) {
    std::wstring cmd = L"xray.exe -c D:\\other\\standalone_abc-xray.json";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd),
              "D:\\other\\standalone_abc-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_Relative) {
    std::wstring cmd = L"xray.exe -c config\\standalone_abc-xray.json";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd),
              "config\\standalone_abc-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_AssignmentWithEquals) {
    std::wstring cmd = L"xray.exe config=D:\\other\\standalone_abc-xray.json";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd),
              "D:\\other\\standalone_abc-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_BareNameReturnsEmpty) {
    // No directory component -> caller's directory probe handles it.
    std::wstring cmd = L"xray.exe -c standalone_abc-xray.json";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd), "");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_None) {
    std::wstring cmd = L"xray.exe run";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd), "");
}

// ---------------------------------------------------------------
// listeningTcpPorts: OS TCP table lookup by owning pid
// ---------------------------------------------------------------
namespace {

// Winsock must be initialised before any socket call.
bool ensureWinsockForTest() {
    static const bool started = []() -> bool {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return started;
}

// Opens a loopback listening socket on an ephemeral port and returns it.
SOCKET openLoopbackListener(int& outPort) {
    outPort = 0;
    if (!ensureWinsockForTest()) {
        return INVALID_SOCKET;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (s == INVALID_SOCKET) {
        outPort = 0;
        return s;
    }
    struct sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 0; // ephemeral
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(s, 4) != 0) {
        closesocket(s);
        outPort = 0;
        return INVALID_SOCKET;
    }
    struct sockaddr_in bound;
    int boundLen = sizeof(bound);
    if (getsockname(s, reinterpret_cast<struct sockaddr*>(&bound),
                    &boundLen) != 0) {
        closesocket(s);
        outPort = 0;
        return INVALID_SOCKET;
    }
    outPort = ntohs(bound.sin_port);
    return s;
}

} // namespace

TEST(ProcessInspectorTest, ListeningTcpPorts_FindsOwnListeningSocket) {
    int port = 0;
    SOCKET listener = openLoopbackListener(port);
    ASSERT_NE(listener, INVALID_SOCKET);
    ASSERT_GT(port, 0);

    const std::vector<int> ports =
        ProcessInspector::listeningTcpPorts(GetCurrentProcessId());
    closesocket(listener);

    EXPECT_TRUE(std::find(ports.begin(), ports.end(), port) != ports.end())
        << "own listening port " << port << " not found in OS TCP table";
}

TEST(ProcessInspectorTest, ListeningTcpPorts_InvalidPidYieldsEmpty) {
    EXPECT_TRUE(ProcessInspector::listeningTcpPorts(0).empty());
}

// ---------------------------------------------------------------------------
// v1.7 / D-C: the scan additionally reports whether the OS TCP table could be
// enumerated at all, so that "table unreadable" and "pid owns no LISTEN port"
// can be told apart.
// ---------------------------------------------------------------------------
TEST(ProcessInspectorTest, ScanListeningTcpPorts_ReportsReadOkForLiveSocket) {
    int port = 0;
    SOCKET listener = openLoopbackListener(port);
    ASSERT_NE(listener, INVALID_SOCKET);
    ASSERT_GT(port, 0);

    const ProcessInspector::TcpPortScan scan =
        ProcessInspector::scanListeningTcpPorts(GetCurrentProcessId());
    closesocket(listener);

    EXPECT_TRUE(scan.readOk) << "the OS TCP table must be readable here";
    EXPECT_TRUE(std::find(scan.ports.begin(), scan.ports.end(), port) !=
                scan.ports.end())
        << "own listening port " << port << " not found in OS TCP table";
}

TEST(ProcessInspectorTest, ScanListeningTcpPorts_ReportsReadOkAndEmptyForUnusedPid) {
    // 0xFFFFFFFC is far outside the Windows pid range, so this pid cannot own
    // any LISTEN endpoint. The table itself must still be readable.
    const ProcessInspector::TcpPortScan scan =
        ProcessInspector::scanListeningTcpPorts(0xFFFFFFFCu);
    EXPECT_TRUE(scan.readOk);
    EXPECT_TRUE(scan.ports.empty());
}

TEST(ProcessInspectorTest, ScanListeningTcpPorts_ReportsNotReadableForZeroPid) {
    const ProcessInspector::TcpPortScan scan =
        ProcessInspector::scanListeningTcpPorts(0);
    EXPECT_FALSE(scan.readOk);
    EXPECT_TRUE(scan.ports.empty());
}
