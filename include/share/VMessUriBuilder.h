#ifndef SHARE_VMESS_URI_BUILDER_H
#define SHARE_VMESS_URI_BUILDER_H

#include <string>
#include "Profileitem.h"

namespace share {

class VMessUriBuilder {
public:
    std::string build(const db::models::Profileitem& profile);
};

} // namespace share

#endif // SHARE_VMESS_URI_BUILDER_H
