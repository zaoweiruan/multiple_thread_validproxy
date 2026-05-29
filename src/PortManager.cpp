#include "PortManager.h"
#include <winsock2.h>
#include <ws2tcpip.h>

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
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    closesocket(sock);
    return result == SOCKET_ERROR;
}

std::vector<int> PortManager::allocateRange(int startPort, int count) {
    std::vector<int> ports;
    for (int i = 0; i < count; ++i) {
        int port = findAvailable(startPort + i, 100);
        if (port > 0) ports.push_back(port);
    }
    return ports;
}