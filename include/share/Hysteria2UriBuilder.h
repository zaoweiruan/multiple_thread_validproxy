#ifndef HYSTERIA2_URI_BUILDER_H
#define HYSTERIA2_URI_BUILDER_H

#include <string>
#include "Profileitem.h"

namespace share {

class Hysteria2UriBuilder {
public:
    std::string build(const db::models::Profileitem& profile);
};

} // namespace share

#endif // HYSTERIA2_URI_BUILDER_H
