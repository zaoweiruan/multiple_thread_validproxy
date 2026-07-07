#ifndef SINGBOX_HYSTERIA2_OUTBOUND_BUILDER_H
#define SINGBOX_HYSTERIA2_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class SingBoxHysteria2OutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // SINGBOX_HYSTERIA2_OUTBOUND_BUILDER_H