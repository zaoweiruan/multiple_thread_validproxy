#ifndef TUIC_OUTBOUND_BUILDER_H
#define TUIC_OUTBOUND_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class TUICOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile, const std::string& outboundTag) const;
};

} // namespace config

#endif // TUIC_OUTBOUND_BUILDER_H
