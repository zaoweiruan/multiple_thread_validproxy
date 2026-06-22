#ifndef TUIC_URI_BUILDER_H
#define TUIC_URI_BUILDER_H

#include <string>
#include "Profileitem.h"

namespace share {

class TUICUriBuilder {
public:
    std::string build(const db::models::Profileitem& profile);
};

} // namespace share

#endif // TUIC_URI_BUILDER_H
