#ifndef WIREGUARD_OUTBOUND_BUILDER_H
#define WIREGUARD_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class WireGuardOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // WIREGUARD_OUTBOUND_BUILDER_H
