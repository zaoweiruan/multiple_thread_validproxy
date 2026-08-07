#ifndef PORT_MANAGER_H
#define PORT_MANAGER_H

#include <set>
#include <string>
#include <mutex>
#include <climits>
#include <vector>

class PortManager {
public:
    static int findAvailable(int startPort, int maxAttempts = 100);
    static bool isInUse(int port);
    static bool freePort(int port);
    static std::vector<int> allocateRange(int startPort, int count);
    static void clearPorts();  // Release all tracked ports for reuse

private:
    // Locked internal helpers: callers must hold mutex_.
    static int findAvailableUnlocked(int startPort, int maxAttempts);
    static bool isInUseUnlocked(int port);

    static std::set<int> usedPorts_;
    static std::mutex mutex_;
};

#endif // PORT_MANAGER_H