#ifndef SHARE_LINK_FACTORY_H
#define SHARE_LINK_FACTORY_H

#include <string>
#include "Profileitem.h"

namespace share {

class ShareLinkFactory {
public:
    std::string toShareUri(const db::models::Profileitem& profile);
};

} // namespace share

#endif // SHARE_LINK_FACTORY_H
