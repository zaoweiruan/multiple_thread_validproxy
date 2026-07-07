#include "config/SingBoxOutboundBuilderFactory.h"
#include "config/outbound/SingBoxVLESSOutboundBuilder.h"
#include "config/outbound/SingBoxVMessOutboundBuilder.h"
#include "config/outbound/SingBoxSSOutboundBuilder.h"
#include "config/outbound/SingBoxTrojanOutboundBuilder.h"
#include "config/outbound/SingBoxHysteria2OutboundBuilder.h"
#include "config/outbound/SingBoxTUICOutboundBuilder.h"
#include "config/outbound/WireGuardOutboundBuilder.h"
#include <stdexcept>

namespace config {

boost::json::object SingBoxOutboundBuilderFactory::create(const db::models::Profileitem& profile, const std::string& tag) const {
    int type = std::stoi(profile.configtype);

    switch (type) {
        case 3: {  // SS
            SingBoxSSOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 1: {  // VMess
            SingBoxVMessOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 5: {  // VLESS
            SingBoxVLESSOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 6: {  // Trojan
            SingBoxTrojanOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 7: {  // Hysteria2
            SingBoxHysteria2OutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 8: {  // TUIC
            SingBoxTUICOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 9: {  // WireGuard
            // WireGuard format is similar between Xray and sing-box
            // Could reuse WireGuardOutboundBuilder with modifications
            WireGuardOutboundBuilder wgBuilder;
            boost::json::object outbound = wgBuilder.build(profile, tag);
            // Convert protocol -> type for sing-box
            if (outbound.contains("protocol")) {
                outbound["type"] = outbound["protocol"];
                outbound.erase("protocol");
            }
            return outbound;
        }
        default:
            throw std::runtime_error("不支持的协议类型: " + profile.configtype);
    }
}

} // namespace config