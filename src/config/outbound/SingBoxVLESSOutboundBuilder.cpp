#include "config/outbound/SingBoxVLESSOutboundBuilder.h"
#include "Profileitem.h"
#include "utils/Base64Utils.h"
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace config {

boost::json::object SingBoxVLESSOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["type"] = "vless";
    outbound["tag"] = outboundTag;

    outbound["server"] = p.address;
    outbound["server_port"] = std::stoi(p.port);

    outbound["uuid"] = p.id;

    // Packet encoding: xudp enables UDP over VLESS (sing-box standard)
    outbound["packet_encoding"] = "xudp";

    // TLS/REALITY settings
    if (p.streamsecurity == "tls" || p.streamsecurity == "reality") {
        boost::json::object tlsObj;
        tlsObj["enabled"] = true;
        
        if (p.streamsecurity == "reality") {
            boost::json::object realityObj;
            realityObj["enabled"] = true;
            if (!p.publickey.empty()) {
                realityObj["public_key"] = p.publickey;
            }
            if (!p.sni.empty()) {
                tlsObj["server_name"] = p.sni;
            }
            if (!p.shortid.empty()) {
                realityObj["short_id"] = p.shortid;
            } else {
                realityObj["short_id"] = "";
            }
            tlsObj["reality"] = realityObj;
        } else if (p.streamsecurity == "tls") {
            if (!p.sni.empty()) {
                tlsObj["server_name"] = p.sni;
            }
        }

        if (!p.allowinsecure.empty()) {
            tlsObj["insecure"] = (p.allowinsecure == "1");
        }
        if (!p.alpn.empty()) {
            boost::json::array alpnArr;
            std::string alpnStr = p.alpn;
            size_t pos = 0;
            while ((pos = alpnStr.find(',')) != std::string::npos) {
                alpnArr.push_back(boost::json::value(alpnStr.substr(0, pos)));
                alpnStr = alpnStr.substr(pos + 1);
            }
            if (!alpnStr.empty()) {
                alpnArr.push_back(boost::json::value(alpnStr));
            }
            tlsObj["alpn"] = alpnArr;
        }
if (!p.fingerprint.empty()) {
             tlsObj["utls"] = boost::json::object{ {"enabled", true}, {"fingerprint", p.fingerprint} };
         }

          // ECH (Encrypted Client Hello)
          if (!p.echconfiglist.empty()) {
              const std::string& configList = p.echconfiglist;
              
              // Detect format: DNS URL format is "domain+https://dns-server/query"
              // Base64 format may contain '+' but won't have "domain+http" pattern
              bool isDnsUrlFormat = false;
              size_t plusPos = configList.find('+');
              if (plusPos != std::string::npos && plusPos > 0) {
                  std::string beforePlus = configList.substr(0, plusPos);
                  std::string afterPlus = configList.substr(plusPos + 1);
                  // DNS URL: part before '+' is a domain name (starts with letter/digit, contains only alphanum + hyphen + dot)
                  bool domainLike = !beforePlus.empty() && 
                      ((beforePlus[0] >= 'a' && beforePlus[0] <= 'z') || 
                       (beforePlus[0] >= 'A' && beforePlus[0] <= 'Z') || 
                       (beforePlus[0] >= '0' && beforePlus[0] <= '9'));
                  for (size_t i = 1; domainLike && i < beforePlus.size(); ++i) {
                      char c = beforePlus[i];
                      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                            (c >= '0' && c <= '9') || c == '-' || c == '.')) {
                          domainLike = false;
                      }
                  }
                  // Part after '+' must start with http:// or https://
                  bool httpLike = (afterPlus.rfind("http://", 0) == 0 || afterPlus.rfind("https://", 0) == 0);
                  if (domainLike && httpLike) {
                      isDnsUrlFormat = true;
                  }
              }
              
              if (isDnsUrlFormat) {
                  // DNS URL format: "domain+https://dns-server/query"
                  // Parse the domain part (before '+') for query_server_name
                  // sing-box needs config: [] to trigger DNS-based ECH discovery
                  size_t plusPos = configList.find('+');
                  std::string queryServerName;
                  if (plusPos != std::string::npos && plusPos > 0) {
                      queryServerName = configList.substr(0, plusPos);
                  }
                  
                  if (!queryServerName.empty()) {
                      // Both config and query_server_name present
                      boost::json::array emptyConfigArr;
                      tlsObj["ech"] = boost::json::object{
                          {"enabled", true},
                          {"config", emptyConfigArr},
                          {"query_server_name", queryServerName}
                      };
                  } else {
                      // No valid query_server_name, just enable with empty config
                      boost::json::array emptyConfigArr;
                      tlsObj["ech"] = boost::json::object{
                          {"enabled", true},
                          {"config", emptyConfigArr}
                      };
                  }
              } else {
                  // Base64-encoded ECH config: decode and populate ech.config array
                  std::vector<uint8_t> decodedBytes = utils::base64Decode(configList);
                  if (!decodedBytes.empty()) {
                      // Convert to hex string for sing-box ech.config
                      std::ostringstream hexStream;
                      for (size_t i = 0; i < decodedBytes.size(); ++i) {
                          hexStream << std::hex << std::setw(2) << std::setfill('0') 
                                    << static_cast<int>(decodedBytes[i]);
                      }
                      std::string hexStr = hexStream.str();
                      boost::json::array configArr;
                      configArr.push_back(boost::json::value(hexStr));
                      tlsObj["ech"] = boost::json::object{
                          {"enabled", true},
                          {"config", configArr}
                      };
                  } else {
                      // Decode failed, fall back to enabled: true
                      tlsObj["ech"] = boost::json::object{ {"enabled", true} };
                  }
              }
          }

         outbound["tls"] = tlsObj;
    }

    // Transport settings
    if (p.network == "ws") {
        boost::json::object transport;
        transport["type"] = "ws";
        if (!p.path.empty()) {
            transport["path"] = p.path;
        }
        if (!p.requesthost.empty()) {
            boost::json::object headers;
            headers["Host"] = p.requesthost;
            transport["headers"] = headers;
        }
        outbound["transport"] = transport;
    } else if (p.network == "h2" || p.network == "http2") {
        boost::json::object transport;
        transport["type"] = "http";
        outbound["transport"] = transport;
    } else if (p.network == "grpc") {
        boost::json::object transport;
        transport["type"] = "grpc";
        if (!p.path.empty()) {
            transport["service"] = p.path;
        }
        outbound["transport"] = transport;
    } else if (!p.network.empty() && p.network != "tcp") {
        // Other transports (tcp is default)
        if (p.network == "tcp") {
            // TCP is default, no transport needed
        }
    }

    // Flow (XTLS/Vision)
    if (!p.flow.empty()) {
        outbound["flow"] = p.flow;
    }

    // Note: UDP over TCP is handled at the proxy level, not outbound level in sing-box

    return outbound;
}

} // namespace config