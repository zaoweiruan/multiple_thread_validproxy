#ifndef SS_URI_BUILDER_H
#define SS_URI_BUILDER_H

#include <string>
#include "Profileitem.h"

namespace share {

class SSUriBuilder {
public:
    std::string build(const db::models::Profileitem& profile);
};

} // namespace share

#endif // SS_URI_BUILDER_H
