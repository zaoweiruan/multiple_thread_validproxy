#include "share/Ipv6Formatter.h"

namespace share {

std::string Ipv6Formatter::formatAddress(const std::string& address) {
    if (address.empty()) {
        return address;
    }
    if (address.find(':') == std::string::npos) {
        return address;
    }
    std::string result = address;
    if (result.front() != '[') {
        result = "[" + result;
    }
    if (result.back() != ']') {
        result += "]";
    }
    return result;
}

bool Ipv6Formatter::isValidIpv6(const std::string& addr) {
    return addr.find(':') != std::string::npos && addr.find('[') != std::string::npos;
}

}
