#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <windows.h>

namespace utils {
    std::string getExecutableDir();
    std::string generateUniqueId();
    std::string getProtocolName(const std::string& configType);
    std::string getCurrentTimestamp();
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

    // Process management utilities
    bool isProcessRunning(const std::string& processName);
    void killProcessByName(const std::string& processName);
    std::string getProcessNameFromPath(const std::string& fullPath);
}

#endif // UTILS_H