#ifndef HTTP_OUTBOUND_BUILDER_H
#define HTTP_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class HTTPOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // HTTP_OUTBOUND_BUILDER_H
