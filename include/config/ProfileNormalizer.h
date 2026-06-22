#ifndef PROFILE_NORMALIZER_H
#define PROFILE_NORMALIZER_H

#include <string>
#include "Profileitem.h"

namespace config {

class ProfileNormalizer {
public:
    void normalize(db::models::Profileitem& profile) const;
    static std::string normalizeNetwork(const std::string& network);
};

} // namespace config

#endif // PROFILE_NORMALIZER_H
