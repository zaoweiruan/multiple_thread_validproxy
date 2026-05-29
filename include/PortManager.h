#ifndef PORT_MANAGER_H
#define PORT_MANAGER_H

#include <vector>
#include <string>

class PortManager {
public:
    static int findAvailable(int startPort, int maxAttempts = 100);
    static bool isInUse(int port);
    static std::vector<int> allocateRange(int startPort, int count);

private:
    static std::vector<int> usedPorts_;
};

#endif // PORT_MANAGER_H