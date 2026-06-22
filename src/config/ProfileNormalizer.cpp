#include "config/ProfileNormalizer.h"
#include "Utils.h"

namespace config {

std::string ProfileNormalizer::normalizeNetwork(const std::string& network) {
    if (network.empty()) {
        return "tcp";
    }
    if (network == "splithttp") {
        return "xhttp";
    }
    if (!utils::isValidNetwork(network)) {
        return "tcp";
    }
    return network;
}

void ProfileNormalizer::normalize(db::models::Profileitem& profile) const {
    profile.network = normalizeNetwork(profile.network);
}

} // namespace config
