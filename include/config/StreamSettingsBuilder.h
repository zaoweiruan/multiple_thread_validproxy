#ifndef STREAM_SETTINGS_BUILDER_H
#define STREAM_SETTINGS_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

class StreamSettingsBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile) const;
};

} // namespace config

#endif // STREAM_SETTINGS_BUILDER_H
