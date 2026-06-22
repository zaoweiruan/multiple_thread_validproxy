#ifndef IPV6_FORMATTER_H
#define IPV6_FORMATTER_H

#include <string>

namespace share {

class Ipv6Formatter {
public:
    static std::string formatAddress(const std::string& address);
    static bool isValidIpv6(const std::string& addr);
};

}

#endif
