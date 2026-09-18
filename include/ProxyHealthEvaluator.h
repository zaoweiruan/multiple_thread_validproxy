#ifndef PROXY_HEALTH_EVALUATOR_H
#define PROXY_HEALTH_EVALUATOR_H

#include <atomic>
#include <vector>
#include <string>

#include "Profileitem.h"

namespace proxy {

// Forward declaration: the resident xray probe pool (ProxyProbePool.h) is
// wired in via setProbePool() to avoid a circular include (ProxyProbePool.h
// includes ProxyHealthEvaluator.h for MemberProbeTarget/MemberHealth).
class ProxyProbePool;

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
// wireguard) cannot be probed with a bare cURL proxy URL; when a resident
// ProxyProbePool is attached via setProbePool() those are routed to the pool's
// xray workers instead. Without a pool they return tested=false. SOCKS (4) and
// HTTP (10) are always probed directly.
class ProxyHealthEvaluator {
public:
    ProxyHealthEvaluator();
    ~ProxyHealthEvaluator();

    // Attach a resident xray probe pool. Non-direct protocols (vmess/vless/
    // trojan/ss/hysteria2/tuic/wireguard) are routed to it when running.
    void setProbePool(ProxyProbePool* pool);

    // Probe each target's upstream proxy. Returns one MemberHealth per target
    // (tag always set; tested/alive filled only for directly-probeable types).
    //
    // stopFlag: optional cancellation token. When non-null and set to true
    // before the call returns, the loop terminates early and returns the
    // partial results collected so far (unprobed targets appear in the
    // returned vector with tested=false, so mergeHealth leaves their state
    // untouched). Used by StandaloneProxyPool::stop() to unblock the
    // evaluator thread's join() within one probe interval instead of
    // waiting for the full serial cycle (2026-09-18 Spec §3.1).
    std::vector<MemberHealth> probe(const std::vector<MemberProbeTarget>& targets,
                                    const std::string& testUrl,
                                    long connectTimeoutMs,
                                    long totalTimeoutMs,
                                    std::atomic<bool>* stopFlag = nullptr);

private:
    // Resident xray probe pool for non-direct protocols (nullptr = disabled).
    ProxyProbePool* probePool_ = nullptr;
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
