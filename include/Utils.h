#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <windows.h>

namespace utils {
    std::string getExecutableDir();
    std::string generateUniqueId();
    std::string getProtocolName(const std::string& configType);
}

#endif // UTILS_H