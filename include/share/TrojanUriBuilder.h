#ifndef SHARE_TROJAN_URI_BUILDER_H
#define SHARE_TROJAN_URI_BUILDER_H

#include <string>
#include "Profileitem.h"

namespace share {

class TrojanUriBuilder {
public:
    std::string build(const db::models::Profileitem& profile);
};

} // namespace share

#endif // SHARE_TROJAN_URI_BUILDER_H
