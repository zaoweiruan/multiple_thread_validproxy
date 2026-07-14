#include "utils/DnsCache.h"
#include "Logger.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#include <future>

namespace utils {

std::unordered_map<std::string, DnsCache::CacheEntry>& DnsCache::getCache() {
    static std::unordered_map<std::string, CacheEntry> cache;
    return cache;
}

std::mutex& DnsCache::getMutex() {
    static std::mutex mtx;
    return mtx;
}

namespace {
    // Thread-safe WinSock initialization (call_once ensures only one init)
    void ensureWsaStartup() {
        static std::once_flag wsaFlag;
        std::call_once(wsaFlag, []() {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                Logger::write("[DnsCache] WSAStartup failed", LogLevel::ERR);
            }
        });
    }
}

// ---------------------------------------------------------------
// Run getaddrinfo in a separate thread with a timeout.
// Blocks indefinitely in getaddrinfo for certain slow/unreachable DNS servers
// on Windows, so we wrap it with std::async + wait_for(10s).
// ---------------------------------------------------------------
std::string DnsCache::resolve(const std::string& hostname) {
    if (hostname.empty()) return {};

    // --- Check cache first (reads are mutex-protected) ---
    {
        std::lock_guard<std::mutex> lock(getMutex());
        auto it = getCache().find(hostname);
        if (it != getCache().end()) {
            return it->second.ip;
        }
    }

    // --- Ensure WinSock initialized ---
    ensureWsaStartup();

    // --- DNS resolution with timeout ---
    std::string ip;
    auto future = std::async(std::launch::async, [&hostname]() -> std::string {
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;       // IPv4 only
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* result = nullptr;
        int ret = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);

        std::string resolvedIp;
        if (ret == 0 && result) {
            struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
            char ipStr[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &(addr->sin_addr), ipStr, sizeof(ipStr))) {
                resolvedIp = ipStr;
            }
            freeaddrinfo(result);
        }
        return resolvedIp;
    });

    const int DNS_TIMEOUT_SEC = 10;
    if (future.wait_for(std::chrono::seconds(DNS_TIMEOUT_SEC)) == std::future_status::timeout) {
        Logger::write("[DnsCache] DNS resolution timed out (" + std::to_string(DNS_TIMEOUT_SEC)
                      + "s) for " + hostname, LogLevel::WARN);
    } else {
        ip = future.get();
        if (ip.empty()) {
            Logger::write("[DnsCache] Failed to resolve " + hostname, LogLevel::TRACE);
        } else {
            Logger::write("[DnsCache] Resolved " + hostname + " -> " + ip, LogLevel::DEBUG);
        }
    }

    // --- Cache result (empty or resolved) to avoid retry storms ---
    {
        std::lock_guard<std::mutex> lock(getMutex());
        getCache()[hostname] = {ip};
    }

    return ip;
}

} // namespace utils
