#ifndef VLESS_OUTBOUND_BUILDER_H
#define VLESS_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class VLESSOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // VLESS_OUTBOUND_BUILDER_H
