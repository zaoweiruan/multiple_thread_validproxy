#include "ProxyConnectivityVerifier.h"

#include "Logger.h"
#include "UrlFetcher.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace proxy {

bool ConnectivityVerifier::verify(int socksPort, const std::string& url,
                                  int timeoutMs, int maxAttempts) {
    UrlFetcher fetcher;
    ProbeFn probe = [&fetcher, socksPort, &url, timeoutMs]() -> bool {
        // Success is judged by the HTTP status code (200/204), not by a
        // non-empty body: the configured test URL
        // (https://www.google.com/generate_204) answers 204 No Content.
        const std::optional<long> status =
            fetcher.fetchViaProxyStatus(url, socksPort, timeoutMs, timeoutMs);
        return status.has_value() && (*status == 200 || *status == 204);
    };
    // Sleep half a timeout between failed attempts: the xray process takes
    // several seconds to become fully ready, and a too-early probe would
    // otherwise exhaust all attempts before the port serves traffic.
    return verifyWithProbe(probe, maxAttempts, timeoutMs / 2);
}

bool ConnectivityVerifier::verifyWithProbe(ProbeFn probe, int maxAttempts,
                                           int retryDelayMs) {
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        if (probe()) {
            return true;
        }
        if (retryDelayMs > 0 && attempt + 1 < maxAttempts) {
            Sleep(static_cast<DWORD>(retryDelayMs));
        }
    }
    return false;
}

bool ConnectivityVerifier::waitForPort(int port, int timeoutMs) {
    return waitForPort(port, timeoutMs, nullptr);
}

bool ConnectivityVerifier::waitForPort(int port, int timeoutMs, void* processHandle) {
    const int intervalMs = 200;
    const int maxIter = (timeoutMs <= 0) ? 1 : (timeoutMs / intervalMs + 1);
    for (int iter = 0; iter < maxIter; ++iter) {
        // Crash-aware: if the backing xray/sing-box process has already exited
        // (flash-crash), the SOCKS port will never open — stop polling and fail
        // fast instead of burning the entire timeout window (the
        // "启动闪崩却长时间等待" bug).
        HANDLE h = static_cast<HANDLE>(processHandle);
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) {
                // Surface the real exit code so the log shows WHY the proxy died
                // (config error, address-in-use, missing asset, ...), not just that
                // it died. The xray/sing-box stderr text is captured by the caller
                // (see AppController::startStandaloneProxy).
                std::string extra;
                DWORD code = 0;
                if (GetExitCodeProcess(h, &code) && code != STILL_ACTIVE) {
                    extra = " (exitCode=" + std::to_string(code) + ")";
                }
                Logger::write("[ConnectivityVerifier] backing process exited before SOCKS "
                              "port " + std::to_string(port) + " became ready (flash-crash)" + extra
                              + "; bailing out early instead of waiting the full timeout",
                              LogLevel::ERR);
                return false;
            }
        }
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(static_cast<u_short>(port));
            const int rc = connect(s, reinterpret_cast<struct sockaddr*>(&addr),
                                   static_cast<int>(sizeof(addr)));
            closesocket(s);
            if (rc == 0) {
                return true;
            }
        }
        Sleep(intervalMs);
    }
    return false;
}

} // namespace proxy