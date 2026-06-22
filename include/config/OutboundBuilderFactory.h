#ifndef OUTBOUND_BUILDER_FACTORY_H
#define OUTBOUND_BUILDER_FACTORY_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class OutboundBuilderFactory {
public:
    boost::json::object create(const db::models::Profileitem& profile, const std::string& tag = "proxy") const;
};

} // namespace config

#endif // OUTBOUND_BUILDER_FACTORY_H
