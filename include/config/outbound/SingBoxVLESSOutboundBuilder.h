#ifndef SINGBOX_VLESS_OUTBOUND_BUILDER_H
#define SINGBOX_VLESS_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class SingBoxVLESSOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // SINGBOX_VLESS_OUTBOUND_BUILDER_H