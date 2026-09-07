#include "config/StreamSettingsBuilder.h"
#include "Profileitem.h"
#include <sstream>
#include <algorithm>

namespace config {

boost::json::object StreamSettingsBuilder::build(const db::models::Profileitem& p) const {
    boost::json::object streamSettings;
    
    if (!p.network.empty()) {
        streamSettings["network"] = p.network;
    }
    
    // Normalize streamsecurity: v2rayN stores "false" to mean "no security",
    // which is NOT a valid Xray security value and causes Xray to fail with
    // `Unknown security "false"`. Valid Xray values: "none", "tls", "reality",
    // "xtls". Treat "false" (and any other invalid value) as "no security" by
    // omitting the security field entirely (Xray defaults to "none").
    const std::string security = p.streamsecurity;
    const bool hasValidSecurity = (security == "tls" || security == "reality" ||
                                   security == "xtls" || security == "none");

    if (!security.empty() && hasValidSecurity) {
        streamSettings["security"] = security;
        
        if (security == "tls") {
            boost::json::object tlsSettings;
            tlsSettings["allowInsecure"] = (p.allowinsecure == "1");
            if (!p.sni.empty()) {
                tlsSettings["serverName"] = p.sni;
            }
            if (!p.alpn.empty()) {
                std::vector<std::string> alpnList;
                std::stringstream ss(p.alpn);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    alpnList.push_back(item);
                }
                boost::json::array alpnArr;
                for (const std::string& a : alpnList) {
                    alpnArr.push_back(boost::json::value(a));
                }
                tlsSettings["alpn"] = alpnArr;
            }
            if (!p.fingerprint.empty()) {
                tlsSettings["fingerprint"] = p.fingerprint;
            }
            if (!p.echconfiglist.empty()) {
                tlsSettings["echConfigList"] = boost::json::value(p.echconfiglist);
            }
            if (!p.echforcequery.empty()) {
                tlsSettings["echForceQuery"] = (p.echforcequery == "1");
            }
            if (!p.cert.empty()) {
                boost::json::array certsArr;
                boost::json::object certObj;
                std::stringstream certSs(p.cert);
                std::string line;
                boost::json::array certLines;
                while (std::getline(certSs, line)) {
                    certLines.push_back(boost::json::value(line));
                }
                certObj["certificate"] = certLines;
                certObj["usage"] = "verify";
                certsArr.push_back(certObj);
                
                boost::json::object certSettings;
                certSettings["certificates"] = certsArr;
                certSettings["disableSystemRoot"] = true;
                tlsSettings["allowInsecure"] = false;
                tlsSettings["disableSystemRoot"] = true;
            }
            if (!p.certsha.empty()) {
                tlsSettings["pinnedPeerCertSha256"] = p.certsha;
                tlsSettings["allowInsecure"] = false;
            }
            streamSettings["tlsSettings"] = tlsSettings;
        } else if (security == "reality") {
            boost::json::object realitySettings;
            if (p.publickey.empty()) {
                throw std::runtime_error("REALITY配置错误：publicKey不能为空");
            }
            if (p.sni.empty()) {
                throw std::runtime_error("REALITY配置错误：sni(serverName)不能为空");
            }

            realitySettings["publicKey"] = p.publickey;
            realitySettings["serverName"] = p.sni;

            if (!p.shortid.empty()) {
                realitySettings["shortId"] = p.shortid;
            } else {
                realitySettings["shortId"] = "";
            }
            
            if (!p.spiderx.empty()) {
                realitySettings["spiderX"] = p.spiderx;
            } else {
                realitySettings["spiderX"] = "";
            }

            if (!p.fingerprint.empty()) {
                realitySettings["fingerprint"] = p.fingerprint;
            } else {
                realitySettings["fingerprint"] = "chrome";
            }

            streamSettings["realitySettings"] = realitySettings;
        }
    }
    
    if (p.network == "grpc") {
        boost::json::object grpcSettings;
        grpcSettings["serviceName"] = p.path;
        grpcSettings["multiMode"] = p.grpcMultiMode == 1;
        grpcSettings["idle_timeout"] = 60;
        grpcSettings["health_check_timeout"] = 20;
        grpcSettings["permit_without_stream"] = false;
        grpcSettings["initial_windows_size"] = 0;
        streamSettings["grpcSettings"] = grpcSettings;
    }

    if (p.network == "ws") {
        boost::json::object wsSettings;
        if (!p.path.empty()) {
            wsSettings["path"] = p.path;
        }
        if (!p.requesthost.empty()) {
            wsSettings["host"] = p.requesthost;
            boost::json::object headers;
            headers["host"] = p.requesthost;
            wsSettings["headers"] = headers;
        }
        streamSettings["wsSettings"] = wsSettings;
    }

    if (p.network == "xhttp") {
        // XHTTP (SplitHTTP) config: `host` and `mode` are TOP-LEVEL fields of
        // xhttpSettings. xray (infra/conf SplitHTTPConfig.Build) hard-errors with
        // `"headers" can't contain "host"` if `host` is placed inside `headers`,
        // and only reads `mode` from the top level (valid: auto/packet-up/
        // stream-up/stream-one; empty defaults to auto). See xray-core
        // infra/conf/transport_internet.go.
        boost::json::object xhttpSettings;
        if (!p.path.empty()) {
            xhttpSettings["path"] = p.path;
        }
        if (!p.requesthost.empty()) {
            xhttpSettings["host"] = p.requesthost;
        }
        if (!p.headertype.empty()) {
            xhttpSettings["mode"] = p.headertype;
        }
        streamSettings["xhttpSettings"] = xhttpSettings;
    }

    if (p.network == "kcp") {
        boost::json::object kcpSettings;
        kcpSettings["mtu"] = p.kcpMtu;
        kcpSettings["tti"] = p.kcpTti;
        kcpSettings["uplinkCapacity"] = p.kcpUplink;
        kcpSettings["downlinkCapacity"] = p.kcpDownlink;
        kcpSettings["congestion"] = p.kcpCongestion == 1;
        kcpSettings["readBufferSize"] = p.kcpReadBufferSize;
        kcpSettings["writeBufferSize"] = p.kcpWriteBufferSize;
        if (!p.headertype.empty()) {
            kcpSettings["header"] = boost::json::object{ { "type", p.kcpHeaderType } };
        }
        streamSettings["kcpSettings"] = kcpSettings;
    }

    return streamSettings;
}

} // namespace config
