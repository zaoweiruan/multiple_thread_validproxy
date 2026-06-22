#ifndef TROJAN_OUTBOUND_BUILDER_H
#define TROJAN_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class TrojanOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // TROJAN_OUTBOUND_BUILDER_H
