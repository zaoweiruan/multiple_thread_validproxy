#include "config/OutboundBuilderFactory.h"
#include "config/outbound/VLESSOutboundBuilder.h"
#include "config/outbound/VMessOutboundBuilder.h"
#include "config/outbound/SSOutboundBuilder.h"
#include "config/outbound/TrojanOutboundBuilder.h"
#include "config/outbound/SOCKSOutboundBuilder.h"
#include "config/outbound/HTTPOutboundBuilder.h"
#include "config/outbound/Hysteria2OutboundBuilder.h"
#include "config/outbound/TUICOutboundBuilder.h"
#include "config/outbound/WireGuardOutboundBuilder.h"
#include <stdexcept>

namespace config {

boost::json::object OutboundBuilderFactory::create(const db::models::Profileitem& profile, const std::string& tag) const {
    int type = std::stoi(profile.configtype);

    switch (type) {
        case 3: {
            SSOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 1: {
            VMessOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 5: {
            VLESSOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 6: {
            TrojanOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 4: {
            SOCKSOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 10: {
            HTTPOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 7: {
            Hysteria2OutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 8: {
            TUICOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        case 9: {
            WireGuardOutboundBuilder builder;
            return builder.build(profile, tag);
        }
        default:
            throw std::runtime_error("不支持的协议类型: " + profile.configtype);
    }
}

} // namespace config
