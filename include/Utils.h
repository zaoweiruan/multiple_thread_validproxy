#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <windows.h>

#include "Profileitem.h"
#include "ProfileExItem.h"

namespace utils {
    std::string getExecutableDir();
    std::string generateUniqueId();
    std::string getProtocolName(const std::string& configType);
    std::string getCurrentTimestamp();
    // Returns local wall-clock time as "yyyy-MM-dd HH:mm:ss". Use this (NOT
    // getCurrentTimestamp(), which is epoch seconds) whenever the value is
    // compared/parsed as a datetime, e.g. durationMsBetween() inputs or
    // proxy_runtime_history.started_at fallbacks.
    std::string getCurrentTimestampFormatted();
    void sendNotification(const std::string& title, const std::string& message);
    std::string joinUrl(const std::string& base, const std::string& suffix);
    bool isValidUrlFormat(const std::string& url);
    bool isValidNetwork(const std::string& network);
    // Returns true when every byte of s is a printable ASCII character
    // (0x20-0x7E). Used to reject binary garbage in Security/Id fields that
    // can never be injected into xray config.
    bool isPrintableAscii(const std::string& s);
    bool isPortAvailable(int port);
    int findAvailablePort(int startPort, int maxAttempts = 100);
    // ---- Port predicates (2026-10-12 Spec StandalonePortImmediateRebind) ----
    // Single authoritative occupancy predicate. Every caller (isPortAvailable,
    // PortManager::isInUseUnlocked, the ProxyListPanel conflict dialog) derives
    // from it so the `port.time_wait_blocking` rollback switch cannot be bypassed.
    // Semantics: LISTEN always blocks; TIME_WAIT blocks ONLY when the switch is on.
    bool isPortBlocking(int port);
    // LISTEN only — never blocks on TIME_WAIT. Must stay UNCACHED: it is the sole
    // dependency of waitPortReleased, and a cached read would return a stale
    // "still listening" for up to the snapshot TTL.
    bool isPortListening(int port);
    // Bounded wait for the LISTEN row to disappear from the OS TCP table.
    // Returns true as soon as the port is not listening; budgetMs is only a
    // fallback ceiling (normal path returns on the first poll, <20ms).
    bool waitPortReleased(int port, int budgetMs);
    // Drops the cached TCP-table snapshot so the next isPortBlocking() re-enumerates
    // the kernel table. Two callers: (1) waitPortReleased, the moment it observes a
    // LISTEN row disappear — the one point where we *know* the snapshot just became
    // stale, and without this a just-freed port would keep reporting "occupied" for
    // the rest of the 250ms TTL and be shifted away from; (2) tests that create or
    // destroy sockets directly (kernel state changed outside our knowledge).
    void invalidatePortSnapshot();

#ifdef TEST_BUILD
    // Test-only observability hook for T5 (P1-3 snapshot cache): incremented once
    // per address-family enumeration inside scanPortStatesRawFamily. One snapshot
    // refresh enumerates both families, so a refresh costs +2.
    namespace testing {
        extern std::atomic<long> g_tcpTableEnumerationCount;
    }
#endif
    // Process-level rollback switch for TIME_WAIT blocking. Published by
    // AppController from `port.time_wait_blocking` at startup (constructor) and
    // on config save. Struct is a plain atomic read — never touch ConfigReader
    // here: ConfigReader::load() reads from disk on every call and would put
    // blocking disk IO on the port-probe hot path.
    void setTimeWaitBlocking(bool blocking);
    bool configTimeWaitBlocking();
    // Diagnostics: log every TCP endpoint (pid + state + process image name)
    // currently holding `port`. Used to capture ground truth when a standalone-
    // proxy start is blocked by a "port occupied" check (see 2026-08-31 port
    // investigation). Safe to call from any thread.
    void logPortOccupants(int port);
    bool isPublicAddress(const std::string& address);
    bool isValidUuid(const std::string& id);
    bool isSupportedSsCipher(const std::string& method);

    // Returns true when a connectivity test result should be considered valid:
    // success must be true and latency must be strictly positive (ms > 0).
    bool isTestResultValid(bool success, long latencyMs);

    // Returns true when a delay string (e.g. "100", "-1", "") represents a
    // valid, strictly-positive latency. Empty and non-positive values are
    // treated as invalid (untested / failed).
    bool isDelayValid(const std::string& delayStr);

    // Convert forward slashes to backslashes (Windows native path format).
    // Used in log messages so file paths display with Windows separators.
    std::string toWindowsPath(const std::string& path);

    // Process management utilities
    bool isProcessRunning(const std::string& processName);
    void killProcessByName(const std::string& processName);
    std::string getProcessNameFromPath(const std::string& fullPath);

    // Proxy list maps — pre-built from exItems so the UI thread only assigns
    // them into the model instead of iterating 50k+ items on every reload.
    struct ProxyListMaps {
        std::unordered_map<std::string, std::string> delayMap;
        std::unordered_map<std::string, std::string> messageMap;
        std::unordered_map<std::string, int> failuresMap;
        std::unordered_map<std::string, int> startCountMap;
        std::unordered_map<std::string, long long> runtimeMap;
        std::unordered_map<std::string, double> healthMap;
    };

    // Build lookup maps from a vector of ProfileExItem. Safe to call from
    // background threads (no wxWidgets dependency).
    ProxyListMaps buildProxyListMaps(const std::vector<db::models::ProfileExItem>& exItems);
}

#endif // UTILS_H