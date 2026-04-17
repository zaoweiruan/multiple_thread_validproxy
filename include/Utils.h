#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <windows.h>

namespace utils {
    std::string getExecutableDir();
    std::string generateUniqueId();
    std::string getProtocolName(const std::string& configType);
    void sendNotification(const std::string& title, const std::string& message);
}

#endif // UTILS_H