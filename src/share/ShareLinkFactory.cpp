#include "share/ShareLinkFactory.h"
#include "share/VLESSUriBuilder.h"
#include "share/VMessUriBuilder.h"
#include "share/TrojanUriBuilder.h"
#include "share/SSUriBuilder.h"
#include "share/Hysteria2UriBuilder.h"
#include "share/TUICUriBuilder.h"
#include <iostream>

namespace share {

std::string ShareLinkFactory::toShareUri(const db::models::Profileitem& profile) {
    int type = std::stoi(profile.configtype);

    switch (type) {
        case 1: { // VMess
            VMessUriBuilder builder;
            return builder.build(profile);
        }
        case 3: { // Shadowsocks
            SSUriBuilder builder;
            return builder.build(profile);
        }
        case 4: // Socks - not supported
            std::cerr << "Socks not supported: " << profile.configtype << std::endl;
            return "";
        case 5: { // VLESS
            VLESSUriBuilder builder;
            return builder.build(profile);
        }
        case 6: { // Trojan
            TrojanUriBuilder builder;
            return builder.build(profile);
        }
        case 7: { // Hysteria2
            Hysteria2UriBuilder builder;
            return builder.build(profile);
        }
        case 8: { // TUIC
            TUICUriBuilder builder;
            return builder.build(profile);
        }
        case 9: // WireGuard - not supported
            std::cerr << "WireGuard not supported: " << profile.configtype << std::endl;
            return "";
        case 10: // HTTP - not supported
            std::cerr << "HTTP not supported: " << profile.configtype << std::endl;
            return "";
        default:
            std::cerr << "Unsupported config type: " << profile.configtype << std::endl;
            return "";
    }
}

} // namespace share
