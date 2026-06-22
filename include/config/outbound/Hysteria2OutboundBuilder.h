#ifndef HYSTERIA2_OUTBOUND_BUILDER_H
#define HYSTERIA2_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class Hysteria2OutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // HYSTERIA2_OUTBOUND_BUILDER_H
