#ifndef DNS_CACHE_H
#define DNS_CACHE_H

#include <string>
#include <unordered_map>
#include <mutex>

namespace utils {

/**
 * DnsCache - Resolve domain names to IP addresses with in-memory caching.
 *
 * Uses getaddrinfo() (WinSock2) for DNS resolution. Results are cached
 * to avoid repeated DNS queries for the same domain.
 *
 * Thread-safe: internal cache is protected by a mutex.
 */
class DnsCache {
public:
    /**
     * Resolve a hostname to an IPv4 address string.
     * Returns the first resolved IPv4 address, or empty string on failure.
     * Failed resolutions are also cached (empty string) to avoid retry storms.
     *
     * @param hostname The domain name to resolve (e.g. "example.com")
     * @return IP address string (e.g. "93.184.216.34") or empty on failure
     */
    static std::string resolve(const std::string& hostname);

private:
    struct CacheEntry {
        std::string ip;
    };

    static std::unordered_map<std::string, CacheEntry>& getCache();
    static std::mutex& getMutex();
};

} // namespace utils

#endif // DNS_CACHE_H
