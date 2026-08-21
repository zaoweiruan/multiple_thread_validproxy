#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <unordered_map>
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