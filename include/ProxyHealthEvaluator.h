#ifndef PROXY_HEALTH_EVALUATOR_H
#define PROXY_HEALTH_EVALUATOR_H

#include <vector>
#include <string>

#include "Profileitem.h"

namespace proxy {

// A member to be health-checked: enough of its upstream proxy config to build
// a direct cURL proxy URL. configtype matches db::models::Profileitem.configtype
// (4 = SOCKS, 10 = HTTP, ...). For SOCKS/HTTP the xray builder maps
// user = p.security and pass = p.id, so we mirror that here.
struct MemberProbeTarget {
    std::string tag;
    int configtype = 0;
    std::string address;
    std::string port;
    std::string username;   // p.security
    std::string password;   // p.id
    // Full upstream profile. Used by the resident xray probe pool to build the
    // outbound to inject (vmess/vless/trojan/ss/hysteria2/tuic/wireguard).
    db::models::Profileitem profile;
};

// Per-member health snapshot returned by a single probe.
struct MemberHealth {
    std::string tag;
    bool alive = false;
    bool tested = false;     // false => protocol not directly probeable (e.g. vmess/vless)
    long long delayMs = -1;
    std::string lastError;
};

// Probes pool member health by measuring each member's *upstream* proxy
// directly with cURL (socks5/http). This replaces the previous xray
// ObservatoryService path, which only reports outbounds that existed at xray
// Start() and therefore never sees members injected at runtime — leaving every
// pool member frozen at its injection state (active / -1 / 否 / failStreak 0).
//
// Protocols that require xray to speak (vmess/vless/trojan/ss/hysteria2/tuic/
// wireguard) cannot be probed with a bare cURL proxy URL; those return
// tested=false and must be measured via a separate xray-side probe (out of
// scope for this iteration). SOCKS (4) and HTTP (10) are probed directly.
class ProxyHealthEvaluator {
public:
    ProxyHealthEvaluator();
    ~ProxyHealthEvaluator();

    // Probe each target's upstream proxy. Returns one MemberHealth per target
    // (tag always set; tested/alive filled only for directly-probeable types).
    std::vector<MemberHealth> probe(const std::vector<MemberProbeTarget>& targets,
                                    const std::string& testUrl,
                                    long connectTimeoutMs,
                                    long totalTimeoutMs);

private:
    // Build a cURL proxy URL for a SOCKS/HTTP target (empty if unsupported).
    static std::string proxyUrlFor(const MemberProbeTarget& t);
    // Probe a single SOCKS/HTTP target through its upstream proxy.
    MemberHealth probeSocksHttp(const MemberProbeTarget& t,
                                const std::string& testUrl,
                                long connectTimeoutMs,
                                long totalTimeoutMs);
};

} // namespace proxy

#endif // PROXY_HEALTH_EVALUATOR_H
