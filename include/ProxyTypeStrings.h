#ifndef PROXY_TYPE_STRINGS_H
#define PROXY_TYPE_STRINGS_H

#include <string_view>

namespace ProxyTypeStrings {
    constexpr std::string_view protocolName(int configType) {
        switch (configType) {
            case 1: return "VMess";
            case 2: return "Custom";
            case 3: return "Shadowsocks";
            case 4: return "Socks";
            case 5: return "VLESS";
            case 6: return "Trojan";
            case 7: return "Hysteria2";
            case 8: return "TUIC";
            case 9: return "WireGuard";
            case 10: return "HTTP";
            case 11: return "Anytls";
            default: return "Unknown";
        }
    }
}

#endif // PROXY_TYPE_STRINGS_H