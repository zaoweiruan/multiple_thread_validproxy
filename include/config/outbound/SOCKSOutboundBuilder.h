#ifndef SOCKS_OUTBOUND_BUILDER_H
#define SOCKS_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class SOCKSOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // SOCKS_OUTBOUND_BUILDER_H
