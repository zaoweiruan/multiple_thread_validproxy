#include "ProxyConnectivityVerifier.h"

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
    const int intervalMs = 200;
    const int maxIter = (timeoutMs <= 0) ? 1 : (timeoutMs / intervalMs + 1);
    for (int iter = 0; iter < maxIter; ++iter) {
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