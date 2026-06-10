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
}

#endif // UTILS_H