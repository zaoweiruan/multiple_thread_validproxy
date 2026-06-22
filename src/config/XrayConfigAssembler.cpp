#include "config/XrayConfigAssembler.h"

namespace config {

boost::json::object XrayConfigAssembler::assemble(const boost::json::object& outbound) const {
    boost::json::array outboundsArr;
    outboundsArr.push_back(outbound);
    boost::json::object root;
    root["outbounds"] = outboundsArr;
    return root;
}

} // namespace config
