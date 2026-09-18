#include "PortManager.h"
#include "Utils.h"

std::set<int> PortManager::usedPorts_;
std::mutex PortManager::mutex_;

int PortManager::findAvailable(int startPort, int maxAttempts) {
    std::lock_guard<std::mutex> lock(mutex_);
    return findAvailableUnlocked(startPort, maxAttempts);
}

int PortManager::findAvailableUnlocked(int startPort, int maxAttempts) {
    // Clamp startPort into the valid range [10000, 65535].
    int begin = startPort;
    if (begin < 10000) begin = 10000;
    if (begin > 65535) begin = 10000;

    // Scan always starts at begin (deterministic: callers pass explicit
    // start ports and expect ports near them). usedPorts_ gives an O(log n)
    // check, and the wraparound stop below guarantees each port is visited
    // at most once per call (no O(n^2) rescanning).
    int candidate = begin;
    int stop = begin;  // stop when we loop back to begin (each port checked at most once)

    for (int i = 0; i < maxAttempts; ++i) {
        // Check the physical port availability via the OS bind probe in
        // utils::isPortAvailable — which now binds the WILDCARD address
        // (0.0.0.0 / [::]) on both TCP families, so a real listen() target
        // such as an xray SOCKS5 inbound bound to the wildcard is correctly
        // reported as occupied. usedPorts_ only tracks what we allocated,
        // not what the OS has already reserved for another process.
        bool systemFree = !isInUseUnlocked(candidate);
        bool oursFree = (usedPorts_.find(candidate) == usedPorts_.end());

        if (systemFree && oursFree) {
            usedPorts_.insert(candidate);
            return candidate;
        }

        // Advance candidate with wraparound; stop before revisiting begin.
        ++candidate;
        if (candidate > 65535) candidate = 10000;
        if (candidate == stop) break;  // full cycle completed, no port found
    }
    return -1;
}

bool PortManager::isInUse(int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    return isInUseUnlocked(port);
}

bool PortManager::isInUseUnlocked(int port) {
    // Check both the internal tracking set and the OS-level binding status.
    if (usedPorts_.find(port) != usedPorts_.end()) return true;
    return !utils::isPortAvailable(port);
}

std::vector<int> PortManager::allocateRange(int startPort, int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> ports;
    for (int i = 0; i < count; ++i) {
        int port = findAvailableUnlocked(startPort + i, 100);
        if (port > 0) ports.push_back(port);
    }
    return ports;
}

bool PortManager::freePort(int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = usedPorts_.find(port);
    if (it != usedPorts_.end()) {
        usedPorts_.erase(it);
        return true;
    }
    return false;
}

// Clear all tracked ports - call when stopping all Xray instances
void PortManager::clearPorts() {
    std::lock_guard<std::mutex> lock(mutex_);
    usedPorts_.clear();
}