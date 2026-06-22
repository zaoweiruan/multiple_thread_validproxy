#ifndef VLESS_URI_BUILDER_H
#define VLESS_URI_BUILDER_H

#include <string>
#include "Profileitem.h"

namespace share {

class VLESSUriBuilder {
public:
    std::string build(const db::models::Profileitem& profile);
};

} // namespace share

#endif // VLESS_URI_BUILDER_H
