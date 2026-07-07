#ifndef SINGBOX_OUTBOUND_BUILDER_FACTORY_H
#define SINGBOX_OUTBOUND_BUILDER_FACTORY_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

// Factory for sing-box outbound JSON format
// sing-box uses "type" field instead of "protocol" in outbounds
class SingBoxOutboundBuilderFactory {
public:
    boost::json::object create(const db::models::Profileitem& profile, const std::string& tag = "proxy") const;
};

} // namespace config

#endif // SINGBOX_OUTBOUND_BUILDER_FACTORY_H