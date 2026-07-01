#include "PortManager.h"
#include "Utils.h"

std::vector<int> PortManager::usedPorts_;

int PortManager::findAvailable(int startPort, int maxAttempts) {
    for (int i = 0; i < maxAttempts; ++i) {
        int port = startPort + i;
        if (port > 65535) port = 10000 + (port - 10000) % 50000;
        
        bool isUsed = false;
        for (int used : usedPorts_) {
            if (used == port) {
                isUsed = true;
                break;
            }
        }
        
        if (!isUsed && !isInUse(port)) {
            usedPorts_.push_back(port);
            return port;
        }
    }
    return -1;
}

bool PortManager::isInUse(int port) {
    // Delegate to the corrected connect-based detection in Utils
    return !utils::isPortAvailable(port);
}

std::vector<int> PortManager::allocateRange(int startPort, int count) {
    std::vector<int> ports;
    for (int i = 0; i < count; ++i) {
        int port = findAvailable(startPort + i, 100);
        if (port > 0) ports.push_back(port);
    }
    return ports;
}

// Clear all tracked ports - call when stopping all Xray instances
void PortManager::clearPorts() {
    usedPorts_.clear();
}