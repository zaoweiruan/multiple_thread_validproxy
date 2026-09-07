// tests/ui/TestStandalonePortClose.cpp
//
// Decisive experiment for the user-reported bug: "启动独立代理测试连通性失败后
// 关闭进程，为啥立刻重启其它代理，提示原端口被占用需要用顺延端口？"
//
// Hypothesis under test: after force-killing a standalone proxy
// (stopStandaloneProxy does TerminateProcess + CloseHandle with NO graceful
// close and NO wait), what does the OS TCP table report for the proxy's fixed
// SOCKS port (socks_base_port, default 10808)?  Utils::isPortOccupiedInTable
// treats any entry with dwLocalPort==port AND dwState in {LISTEN,TIME_WAIT} as
// OCCUPIED (enumerated over BOTH AF_INET and AF_INET6).  If a force-kill leaves
// such an entry behind, isPortAvailable(port) returns false and the app's
// onStartProxy pops the "端口已被占用, 是否使用顺延端口" prompt.
//
// This test reproduces the exact lifecycle with the REAL xray binary:
//   1. start a real xray with a standalone-style config (0.0.0.0:PORT, protocol
//      socks/mixed) on a fixed port
//   2. establish a REAL connection to that port (mirrors waitForPort's connect
//      + the connectivity check's SOCKS connections)
//   3. force-kill the xray process via TerminateProcess + CloseHandle (mirrors
//      stopStandaloneProxy:1563-1583 / shutdownStandaloneProxies:1585-1605)
//   4. IMMEDIATELY enumerate GetExtendedTcpTable (AF_INET + AF_INET6,
//      TCP_TABLE_OWNER_PID_ALL) for that port and report every row's dwState
//     (LISTEN / TIME_WAIT / ESTABLISHED / gone) + whether the
//     isPortAvailable-style predicate would call it occupied
//
// Because UITests is a pure Win32 target (does NOT link project Utils.cpp,
// see CMakeLists UNITest link), the isPortOccupiedInTable semantics are
// re-implemented verbatim here against the raw Win32 API, not reused.
//
// The test SKIPs when xray cannot start in the environment (same gate as
// TestStandaloneProxyPool::xrayCanStart), so it never pollutes a sandbox.

#include <catch2/catch_test_macros.hpp>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include "framework/Fixtures.h"

using namespace uitest;

namespace {

// ---------------------------------------------------------------------------
// Lifted verbatim from src/Utils.cpp isPortOccupiedInTable (AF_INET+AF_INET6,
// LOCAL port match, LISTEN|TIME_WAIT treated as occupied). Kept identical so
// the test observes exactly what the app's isPortAvailable(port) would see.
// ---------------------------------------------------------------------------

struct TcpRow {
    unsigned long localAddr;   // 0 for IPv6 rows (use localAddr6)
    unsigned char localAddr6[16];
    bool isIpv6;
    unsigned short localPort;  // host order
    unsigned long state;       // MIB_TCP_STATE_*
    unsigned long pid;
};

std::vector<TcpRow> tcpRowsForPort(int port) {
    std::vector<TcpRow> out;
    const unsigned short want = static_cast<unsigned short>(port);

    // IPv4
    {
        DWORD size = 0;
        if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                                TCP_TABLE_OWNER_PID_ALL, 0) == ERROR_INSUFFICIENT_BUFFER
            && size > 0) {
            std::vector<unsigned char> buf(size);
            PMIB_TCPTABLE_OWNER_PID table =
                reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buf.data());
            if (GetExtendedTcpTable(table, &size, FALSE, AF_INET,
                                    TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const MIB_TCPROW_OWNER_PID& r = table->table[i];
                    if (ntohs(static_cast<u_short>(r.dwLocalPort)) == want) {
                        TcpRow row{};
                        row.isIpv6 = false;
                        row.localAddr = r.dwLocalAddr;
                        row.localPort = want;
                        row.state = r.dwState;
                        row.pid = r.dwOwningPid;
                        out.push_back(row);
                    }
                }
            }
        }
    }

    // IPv6
    {
        DWORD size = 0;
        if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET6,
                                TCP_TABLE_OWNER_PID_ALL, 0) == ERROR_INSUFFICIENT_BUFFER
            && size > 0) {
            std::vector<unsigned char> buf(size);
            PMIB_TCP6TABLE_OWNER_PID table =
                reinterpret_cast<PMIB_TCP6TABLE_OWNER_PID>(buf.data());
            if (GetExtendedTcpTable(table, &size, FALSE, AF_INET6,
                                    TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const MIB_TCP6ROW_OWNER_PID& r = table->table[i];
                    if (ntohs(static_cast<u_short>(r.dwLocalPort)) == want) {
                        TcpRow row{};
                        row.isIpv6 = true;
                        std::memcpy(row.localAddr6, r.ucLocalAddr, 16);
                        row.localPort = want;
                        row.state = r.dwState;
                        row.pid = r.dwOwningPid;
                        out.push_back(row);
                    }
                }
            }
        }
    }
    return out;
}

// Returns true if ANY row for `port` has state LISTEN or TIME_WAIT (exactly
// Utils::isPortOccupiedInTable's predicate => isPortAvailable(port) would be
// false).
bool predicateSaysOccupied(int port) {
    const std::vector<TcpRow> rows = tcpRowsForPort(port);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].state == MIB_TCP_STATE_LISTEN
            || rows[i].state == MIB_TCP_STATE_TIME_WAIT) {
            return true;
        }
    }
    return false;
}

const char* stateName(unsigned long st) {
    switch (st) {
        case MIB_TCP_STATE_CLOSED:     return "CLOSED";
        case MIB_TCP_STATE_LISTEN:     return "LISTEN";
        case MIB_TCP_STATE_SYN_SENT:   return "SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD:   return "SYN_RCVD";
        case MIB_TCP_STATE_ESTAB:      return "ESTAB";
        case MIB_TCP_STATE_FIN_WAIT1:  return "FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2:  return "FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING:    return "CLOSING";
        case MIB_TCP_STATE_LAST_ACK:   return "LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT:  return "TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return "DELETE_TCB";
        default:                       return "?";
    }
}

// Authoritative capability probe: can a real xray bind ANY localhost socket in
// this environment?  (Mirror of TestStandaloneProxyPool::xrayCanStart.)
bool xrayCanStart() {
    const std::wstring xray = L"E:\\v2rayN-windows-64\\bin\\xray\\xray.exe";
    const std::wstring cfg  =
        L"E:\\eclipse_workspace\\multiple_thread_validproxy\\bin\\config\\probe2.json";
    std::wstring cmd = L"\"" + xray + L"\" run -c \"" + cfg + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    HANDLE nul = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    si.hStdInput  = nul;
    si.hStdOutput = nul;
    si.hStdError  = nul;
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, const_cast<wchar_t*>(cmd.c_str()), nullptr, nullptr,
                          TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (nul) ::CloseHandle(nul);
        return false;
    }
    if (nul) ::CloseHandle(nul);

    bool listening = false;
    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s != INVALID_SOCKET) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(45000);
            for (int i = 0; i < 25 && !listening; ++i) {
                if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
                    listening = true; break;
                }
                ::Sleep(100);
            }
            ::closesocket(s);
        }
        ::WSACleanup();
    }
    ::TerminateProcess(pi.hProcess, 0);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return listening;
}

// Pick the fixed port. Defaults to the app's socks_base_port (10808) but if it
// is currently OCCUPIED (some real proxy is using it), we fall back to the
// first free port >= 10808 so the test never collides with a live listener.
// Returns -1 if no free port in range is found.
int pickTestPort() {
    for (int port = 10808; port < 10808 + 50; ++port) {
        if (!predicateSaysOccupied(port)) return port;
    }
    return -1;
}

// Write a minimal standalone-style xray config: a socks/mixed inbound listening
// on 0.0.0.0:PORT and a freedom outbound (no upstream proxy needed — the point
// is purely the LISTEN socket, exactly like probe2.json but with our port).
bool writeStandaloneConfig(int port, const std::wstring& path) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{\"log\":{\"loglevel\":\"error\"},"
        "\"inbounds\":[{\"tag\":\"s-in\",\"listen\":\"0.0.0.0\",\"port\":%d,"
        "\"protocol\":\"mixed\",\"settings\":{\"auth\":\"noauth\",\"udp\":true}}],"
        "\"outbounds\":[{\"tag\":\"direct\",\"protocol\":\"freedom\"}]}",
        port);
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = ::WriteFile(h, buf, static_cast<DWORD>(std::strlen(buf)),
                                &written, nullptr);
    ::CloseHandle(h);
    return ok != FALSE;
}

// Spawn real xray on `port`. Returns true + fills pi when the port becomes
// reachable (connect succeeds), else false (xray failed / env cannot bind).
struct XrayProc {
    bool ok = false;
    PROCESS_INFORMATION pi{};
    std::wstring cfgPath;
};

XrayProc startXrayStandalone(int port) {
    XrayProc out;
    const std::wstring xray = L"E:\\v2rayN-windows-64\\bin\\xray\\xray.exe";
    wchar_t tmp[MAX_PATH];
    ::GetTempPathW(MAX_PATH, tmp);
    out.cfgPath = std::wstring(tmp) + L"portclose_" + std::to_wstring(port) + L".json";
    if (!writeStandaloneConfig(port, out.cfgPath)) return out;

    std::wstring cmd = L"\"" + xray + L"\" run -c \"" + out.cfgPath + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    HANDLE nul = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    si.hStdInput  = nul;
    si.hStdOutput = nul;
    si.hStdError  = nul;
    if (!::CreateProcessW(nullptr, const_cast<wchar_t*>(cmd.c_str()), nullptr, nullptr,
                          TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &out.pi)) {
        if (nul) ::CloseHandle(nul);
        return out;
    }
    if (nul) ::CloseHandle(nul);

    WSADATA wsa{};
    bool listening = false;
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        for (int attempt = 0; attempt < 40; ++attempt) { // up to ~4s
            SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
            if (s != INVALID_SOCKET) {
                sockaddr_in a{};
                a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a.sin_port = htons(static_cast<u_short>(port));
                if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
                    listening = true;
                    ::closesocket(s);
                    break;
                }
                ::closesocket(s);
            }
            ::Sleep(100);
        }
        ::WSACleanup();
    }
    // NOTE: we keep the process alive here (caller decides when/how to kill).
    out.ok = listening;
    return out;
}

// Report every TCP row on `port` to stdout so ctest -V shows the decisive state.
void reportTcpState(int port, const char* phase) {
    const std::vector<TcpRow> rows = tcpRowsForPort(port);
    std::printf("[portclose] %-28s port=%d rows=%zu occupied(predicate)=%s\n",
                phase, port, rows.size(),
                predicateSaysOccupied(port) ? "YES(占用)" : "no(空闲)");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const TcpRow& r = rows[i];
        if (r.isIpv6) {
            std::printf("   [v6] local=[%02x%02x:%02x%02x:%02x%02x:%02x%02x]"
                        ":%d state=%-10s(%s) pid=%lu\n",
                        r.localAddr6[0], r.localAddr6[1], r.localAddr6[2],
                        r.localAddr6[3], r.localAddr6[4], r.localAddr6[5],
                        r.localAddr6[6], r.localAddr6[7], r.localPort,
                        stateName(r.state), stateName(r.state), r.pid);
        } else {
            const unsigned char* a =
                reinterpret_cast<const unsigned char*>(&r.localAddr);
            std::printf("   [v4] local=%u.%u.%u.%u:%d state=%-10s pid=%lu\n",
                        a[0], a[1], a[2], a[3], r.localPort,
                        stateName(r.state), r.pid);
        }
    }
}

// Real SOCKS5 handshake to 127.0.0.1:port — mirrors the connectivity test's
// UrlFetcher::fetchViaProxyStatus, which connects THROUGH the SOCKS proxy on
// `port` to a detection target URL. xray ACCEPTS this inbound session (its local
// port = `port`); when the caller closes it, xray's side enters TIME_WAIT on
// `port`. Returns the open client socket (caller closes to trigger server-side
// TIME_WAIT) or INVALID_SOCKET on failure.
SOCKET doSocksHandshake(int port) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(static_cast<u_short>(port));
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        ::closesocket(s); return INVALID_SOCKET;
    }
    unsigned char greet[3] = { 0x05, 0x01, 0x00 };
    if (::send(s, reinterpret_cast<const char*>(greet), 3, 0) != 3) {
        ::closesocket(s); return INVALID_SOCKET;
    }
    unsigned char m[2] = {0};
    if (::recv(s, reinterpret_cast<char*>(m), 2, 0) != 2) {
        ::closesocket(s); return INVALID_SOCKET;
    }
    // CONNECT to 127.0.0.1:80 (loopback target; outbound need not succeed for the
    // inbound TIME_WAIT to form).
    unsigned char conn[10] = { 0x05, 0x01, 0x00, 0x01, 127, 0, 0, 1, 0x00, 80 };
    if (::send(s, reinterpret_cast<const char*>(conn), 10, 0) != 10) {
        ::closesocket(s); return INVALID_SOCKET;
    }
    unsigned char rep[10] = {0};
    (void)::recv(s, reinterpret_cast<char*>(rep), sizeof(rep), 0); // success/fail
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// The decisive experiment. Runs three sub-scenarios on the SAME port as long
// as xray can bind it; each force-kills and immediately re-checks the table.
TEST_CASE("Standalone SOCKS port state immediately after force-kill",
          "[portclose]") {
    if (!xrayCanStart()) {
        SKIP("xray cannot start in this environment (minimal xray config exits "
             "immediately, unable to bind any localhost socket). The force-kill "
             "port-state experiment requires a real bindable xray.");
    }
    const int port = pickTestPort();
    REQUIRE(port > 0); // a free port must exist in range
    std::printf("[portclose] fixed SOCKS port selected = %d\n", port);

    // Scenario A: LISTEN only, no connection -> force-kill.
    {
        XrayProc p = startXrayStandalone(port);
        REQUIRE(p.ok); // xray bound the port (standalone listener live)
        // Give the parent-connection a beat so the LISTEN row is settled.
        ::Sleep(200);
        reportTcpState(port, "A: LISTEN-only, before kill");
        // Force-kill exactly like stopStandaloneProxy (TerminateProcess + no wait).
        ::TerminateProcess(p.pi.hProcess, 1);
        ::CloseHandle(p.pi.hThread);
        ::CloseHandle(p.pi.hProcess);
        // IMMEDIATELY (no sleep) re-check the table.
        reportTcpState(port, "A: IMMEDIATELY after force-kill");
        std::printf("[portclose] A result: occupied=%s\n",
                    predicateSaysOccupied(port) ? "YES -> would prompt 顺延" : "no -> port free for reuse");
        // The LISTEN entry should be GONE because TerminateProcess tears the
        // process down; a LISTEN socket itself cannot linger in TIME_WAIT.
        // Ordinarily predicate must be false here; if it is true we log which state.
    }

    // Scenario B: LISTEN + a HELD-open real connection -> force-kill.
    {
        XrayProc p = startXrayStandalone(port);
        REQUIRE(p.ok);
        SOCKET client = INVALID_SOCKET;
        WSADATA wsa{};
        REQUIRE(::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
        {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(static_cast<u_short>(port));
            client = ::socket(AF_INET, SOCK_STREAM, 0);
            if (client != INVALID_SOCKET) {
                // retry-connect until up (parallel to waitForPort)
                for (int i = 0; i < 40; ++i) {
                    if (::connect(client, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) break;
                    ::Sleep(100);
                }
            }
        }
        // Hold the connection open so it is genuinely ESTABLISHED.
        ::Sleep(300);
        reportTcpState(port, "B: LISTEN + held conn, before kill");
        // Force-kill while the connection is STILL OPEN (client socket not yet closed).
        ::TerminateProcess(p.pi.hProcess, 1);
        ::CloseHandle(p.pi.hThread);
        ::CloseHandle(p.pi.hProcess);
        reportTcpState(port, "B: IMMEDIATELY after force-kill (conn open)");
        if (client != INVALID_SOCKET) { ::closesocket(client); ::Sleep(200); }
        reportTcpState(port, "B: 200ms after closing client");
        std::printf("[portclose] B result: occupied=%s\n",
                    predicateSaysOccupied(port) ? "YES -> would prompt 顺延" : "no -> port free");
        ::WSACleanup();
    }

    // Scenario C: LISTEN + client CONNECTION CLOSED (client-side FIN) -> force-kill.
    {
        XrayProc p = startXrayStandalone(port);
        REQUIRE(p.ok);
        WSADATA wsa{};
        REQUIRE(::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
        {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(static_cast<u_short>(port));
            for (int attempt = 0; attempt < 40; ++attempt) {
                SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
                if (s != INVALID_SOCKET) {
                    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
                        ::Sleep(150);      // exchange; let it establish
                        ::closesocket(s);  // client-side close (normal FIN)
                        break;
                    }
                    ::closesocket(s);
                }
                ::Sleep(100);
            }
        }
        ::Sleep(150); // allow the close/FIN state to settle on the server side
        reportTcpState(port, "C: LISTEN + closed conn, before kill");
        ::TerminateProcess(p.pi.hProcess, 1);
        ::CloseHandle(p.pi.hThread);
        ::CloseHandle(p.pi.hProcess);
        reportTcpState(port, "C: IMMEDIATELY after force-kill (conn closed)");
        std::printf("[portclose] C result: occupied=%s\n",
                    predicateSaysOccupied(port) ? "YES -> would prompt 顺延" : "no -> port free");
        // TIME_WAIT observation: if present, poll how long it lingers.
        if (predicateSaysOccupied(port)) {
            const DWORD startMs = ::GetTickCount();
            bool gone = false;
            for (int i = 0; i < 150; ++i) { // up to 15s
                ::Sleep(100);
                if (!predicateSaysOccupied(port)) { gone = true; break; }
            }
            std::printf("[portclose] C: TIME_WAIT/LISTEN lingered for ~%lu ms before releasing (gone=%s)\n",
                        static_cast<unsigned long>(::GetTickCount() - startMs),
                        gone ? "true" : "false (still busy after 15s)");
        }
        ::WSACleanup();
    }

    // Scenario D: the FIX verification. LISTEN only, force-kill, but WAIT for the
    // process to actually exit (WaitForSingleObject) before checking the table.
    // Mirrors the proposed fix to AppController::stopStandaloneProxy (add a wait
    // after TerminateProcess). If the residue in A/B/C was caused by the process
    // not yet being torn down, waiting should make occupied=false here.
    {
        XrayProc p = startXrayStandalone(port);
        REQUIRE(p.ok);
        ::Sleep(200);
        reportTcpState(port, "D: LISTEN-only, before kill");
        ::TerminateProcess(p.pi.hProcess, 1);
        // THE FIX: wait for the process to fully exit before releasing the handle.
        const DWORD waitRes = ::WaitForSingleObject(p.pi.hProcess, 5000);
        reportTcpState(port, "D: after kill + WaitForSingleObject(exit)");
        ::CloseHandle(p.pi.hThread);
        ::CloseHandle(p.pi.hProcess);
        std::printf("[portclose] D waitRes=%s occupied=%s\n",
                    (waitRes == WAIT_OBJECT_0) ? "exited" : "TIMEOUT",
                    predicateSaysOccupied(port) ? "YES -> would prompt 顺延" : "no -> port free");
    }
}

// ---------------------------------------------------------------------------
// Scenario E: the REAL connectivity-test path. The app's ConnectivityVerifier
// connects THROUGH the SOCKS proxy (port) to a detection target URL, establishing
// an inbound SOCKS session on xray whose local port IS `port`. When the test ends
// and curl closes the session, xray's side enters TIME_WAIT on `port`. We confirm
// that a force-kill (as the app does) does NOT clear that TIME_WAIT, so the port
// stays "occupied" for the full 2*MSL and the next start is prompted to 顺延.
TEST_CASE("Standalone SOCKS session leaves TIME_WAIT on listen port", "[portclose]") {
    if (!xrayCanStart()) {
        SKIP("xray cannot start in this environment; SOCKS-session TIME_WAIT "
             "experiment requires a real bindable xray.");
    }
    const int port = pickTestPort();
    REQUIRE(port > 0);
    std::printf("[portclose] E: fixed SOCKS port selected = %d\n", port);

    XrayProc p = startXrayStandalone(port);
    REQUIRE(p.ok);
    WSADATA wsa{};
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

    SOCKET s = doSocksHandshake(port);
    REQUIRE(s != INVALID_SOCKET);
    ::Sleep(300); // let xray process CONNECT / open the outbound
    reportTcpState(port, "E: SOCKS session live, before kill");

    ::closesocket(s);           // client closes the session (curl does this on test end)
    // KEY: in the real app there is a dialog gap ("是否关闭该进程") between the
    // connectivity test ending and the user clicking YES, during which xray stays
    // ALIVE and completes its side of the close -> TIME_WAIT on `port`. Mirror that
    // by letting xray live ~2.5s so its inbound socket moves CLOSE_WAIT -> TIME_WAIT
    // BEFORE we kill it (a too-early kill would ABORT the socket and hide TIME_WAIT).
    ::Sleep(2500);
    reportTcpState(port, "E: after SOCKS session close (before kill)");

    // Force-kill the proxy EXACTLY as the app's fail-cleanup path does
    // (TerminateProcess + WaitForSingleObject(5000) + CloseHandle). TIME_WAIT is a
    // kernel TCP state and survives the process — the wait only frees the LISTEN.
    ::TerminateProcess(p.pi.hProcess, 1);
    ::WaitForSingleObject(p.pi.hProcess, 5000);
    ::CloseHandle(p.pi.hThread);
    ::CloseHandle(p.pi.hProcess);

    reportTcpState(port, "E: after SOCKS-close + force-kill");
    std::printf("[portclose] E: occupied(predicate)=%s\n",
                predicateSaysOccupied(port) ? "YES(占用 -> 顺延提示)" : "no");

    // How long does the TIME_WAIT linger? (2*MSL ~ up to 120s on Windows)
    const DWORD startMs = ::GetTickCount();
    bool gone = false;
    for (int i = 0; i < 300; ++i) { // up to 30s
        ::Sleep(100);
        if (!predicateSaysOccupied(port)) { gone = true; break; }
    }
    std::printf("[portclose] E: TIME_WAIT lingered ~%lu ms before releasing (gone=%s)\n",
                static_cast<unsigned long>(::GetTickCount() - startMs),
                gone ? "true" : "false (>30s, i.e. full 2*MSL)");
    ::WSACleanup();
}

// ---------------------------------------------------------------------------
// Scenario F: reboot a 2nd standalone proxy on the SAME port while a TIME_WAIT
// from the 1st proxy's SOCKS session still occupies it. This directly probes
// whether xray's inbound listener sets SO_REUSEADDR: if the 2nd xray comes up
// LISTEN on `port` it rebinds over TIME_WAIT (no 顺延 needed); if it fails to
// bind (xray exits) it does NOT set SO_REUSEADDR and 顺延 is unavoidable.
TEST_CASE("Standalone SOCKS port reuse over TIME_WAIT (xray SO_REUSEADDR probe)",
          "[portclose]") {
    if (!xrayCanStart()) {
        SKIP("xray cannot start in this environment; SO_REUSEADDR probe requires "
             "a real bindable xray.");
    }
    const int port = pickTestPort();
    REQUIRE(port > 0);
    std::printf("[portclose] F: fixed SOCKS port selected = %d\n", port);

    XrayProc p1 = startXrayStandalone(port);
    REQUIRE(p1.ok);
    WSADATA wsa{};
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

    SOCKET s = doSocksHandshake(port);
    REQUIRE(s != INVALID_SOCKET);
    ::Sleep(300);
    ::closesocket(s);   // end the SOCKS session -> xray side TIME_WAIT on `port`
    ::Sleep(200);

    // Kill the 1st proxy; LISTEN goes, TIME_WAIT stays.
    ::TerminateProcess(p1.pi.hProcess, 1);
    ::CloseHandle(p1.pi.hThread);
    ::CloseHandle(p1.pi.hProcess);
    ::Sleep(200);
    reportTcpState(port, "F: 1st proxy killed, TIME_WAIT only");

    // OS-level probe: can we bind over the TIME_WAIT? (Windows allows it only
    // with SO_REUSEADDR.) This tells us what an xray listener needs.
    {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(static_cast<u_short>(port));
        SOCKET no = ::socket(AF_INET, SOCK_STREAM, 0);
        const int rNo = ::bind(no, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        std::printf("[portclose] F: bind WITHOUT SO_REUSEADDR -> %s\n",
                    rNo == 0 ? "OK(unexpected)" : "FAIL(=WSAEADDRINUSE, TIME_WAIT blocks)");
        ::closesocket(no);
        SOCKET yes = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(yes, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&one), sizeof(one));
        const int rYes = ::bind(yes, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        std::printf("[portclose] F: bind WITH SO_REUSEADDR -> %s\n",
                    rYes == 0 ? "OK(rebind over TIME_WAIT works)" : "FAIL");
        ::closesocket(yes);
    }

    // Now the decisive real-world probe: reboot a 2nd xray on the SAME port.
    XrayProc p2 = startXrayStandalone(port);
    reportTcpState(port, "F: 2nd xray booted over TIME_WAIT");
    std::printf("[portclose] F: 2nd xray ok=%s -> %s\n",
                p2.ok ? "YES" : "NO",
                p2.ok ? "xray has SO_REUSEADDR, 10810 reusable, no 顺延"
                      : "xray bind failed (no SO_REUSEADDR), 顺延 required");
    if (p2.ok) {
        ::TerminateProcess(p2.pi.hProcess, 1);
        ::CloseHandle(p2.pi.hThread);
        ::CloseHandle(p2.pi.hProcess);
    }
    ::WSACleanup();
}
