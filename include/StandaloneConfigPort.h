#ifndef STANDALONE_CONFIG_PORT_H
#define STANDALONE_CONFIG_PORT_H

#include <boost/json.hpp>

namespace standalone_config {

// Locate the SOCKS inbound inside a parsed standalone config's "inbounds" array.
// In xray it is marked with "protocol": "socks" | "mixed"; in sing-box with
// "type": "socks" | "mixed". Falls back to index 0 when no such inbound is
// found (single-inbound templates where inbounds[0] is the SOCKS listener).
// Returns -1 if the array is missing/empty.
int findSocksInboundIndex(const boost::json::array& inbounds);

// Set the SOCKS inbound's listening port on a parsed standalone config object.
// Handles both xray ("port") and sing-box ("listen_port") field names.
// No-op if there is no "inbounds" array.
void applySocksPort(boost::json::object& config, int socksPort);

}  // namespace standalone_config

#endif  // STANDALONE_CONFIG_PORT_H
