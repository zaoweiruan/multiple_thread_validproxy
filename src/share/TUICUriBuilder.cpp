#include "share/TUICUriBuilder.h"
#include "share/VLESSUriBuilder.h"

namespace share {

std::string TUICUriBuilder::build(const db::models::Profileitem& profile) {
    VLESSUriBuilder vlessBuilder;
    return vlessBuilder.build(profile);
}

} // namespace share
