#ifndef VMESS_OUTBOUND_BUILDER_H
#define VMESS_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class VMessOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // VMESS_OUTBOUND_BUILDER_H
