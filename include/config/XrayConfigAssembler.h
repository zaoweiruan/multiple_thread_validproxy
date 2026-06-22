#ifndef XRAY_CONFIG_ASSEMBLER_H
#define XRAY_CONFIG_ASSEMBLER_H

#include <string>
#include <boost/json.hpp>

namespace config {

class XrayConfigAssembler {
public:
    boost::json::object assemble(const boost::json::object& outbound) const;
};

} // namespace config

#endif // XRAY_CONFIG_ASSEMBLER_H
