#include "StandaloneConfigPort.h"

namespace standalone_config {

int findSocksInboundIndex(const boost::json::array& inbounds) {
    if (inbounds.empty()) {
        return -1;
    }
    // First pass: exact "tag" == "socks" (most explicit, unambiguous).
    for (std::size_t i = 0; i < inbounds.size(); ++i) {
        if (!inbounds[i].is_object()) {
            continue;
        }
        const boost::json::object& o = inbounds[i].as_object();
        boost::json::object::const_iterator tag = o.find("tag");
        if (tag != o.end() && tag->value().is_string() &&
            tag->value().as_string() == "socks") {
            return static_cast<int>(i);
        }
    }
    // Second pass: protocol/type in {socks, mixed}.
    for (std::size_t i = 0; i < inbounds.size(); ++i) {
        if (!inbounds[i].is_object()) {
            continue;
        }
        const boost::json::object& o = inbounds[i].as_object();
        boost::json::object::const_iterator it = o.find("protocol");  // xray
        if (it == o.end() || !it->value().is_string()) {
            it = o.find("type");  // sing-box
        }
        if (it != o.end() && it->value().is_string()) {
            boost::json::string_view kind = it->value().as_string();
            if (kind == "socks" || kind == "mixed") {
                return static_cast<int>(i);
            }
        }
    }
    return 0;  // fallback: first inbound
}

void applySocksPort(boost::json::object& config, int socksPort) {
    boost::json::object::iterator it = config.find("inbounds");
    if (it == config.end() || !it->value().is_array()) {
        return;
    }
    boost::json::array& arr = it->value().as_array();
    if (arr.empty()) {
        return;
    }
    int idx = findSocksInboundIndex(arr);
    int target = (idx >= 0) ? idx : 0;
    if (!arr[target].is_object()) {
        return;
    }
    boost::json::object& inbound = arr[target].as_object();
    if (inbound.contains("listen_port")) {
        inbound["listen_port"] = socksPort;  // sing-box format
    } else {
        inbound["port"] = socksPort;  // xray format
    }
}

}  // namespace standalone_config
