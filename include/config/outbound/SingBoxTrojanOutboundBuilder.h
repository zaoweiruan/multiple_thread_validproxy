#ifndef SINGBOX_TROJAN_OUTBOUND_BUILDER_H
#define SINGBOX_TROJAN_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class SingBoxTrojanOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // SINGBOX_TROJAN_OUTBOUND_BUILDER_H