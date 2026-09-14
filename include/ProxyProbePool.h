#ifndef PROXY_PROBE_POOL_H
#define PROXY_PROBE_POOL_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ProxyHealthEvaluator.h"
#include "XrayApi.h"
#include "XrayInstance.h"

namespace proxy {

// A small pool of resident xray instances used to health-probe pool members
// whose protocol cannot be measured with a bare cURL proxy URL (vmess/vless/
// trojan/ss/hysteria2/tuic/wireguard). Each worker runs xray with the default
// config template (socks-in mixed + api + direct/proxy outbounds + routing
// TCP->proxy), so probing a member is: inject its outbound via gRPC, then
// cURL through the worker's local socks port.
//
// The pool is intentionally independent from XrayManager (global singleton
// shared with batch testing) and from ProxyBatchTester (lifecycle/DB/verdict
// semantics do not match pool health probing).
class ProxyProbePool {
public:
    ProxyProbePool(const std::string& xrayPath, int workerCount,
                   const std::string& configDir);
    ~ProxyProbePool();

    // Start workerCount resident xray instances. Returns true if at least one
    // worker became ready. Idempotent: no-op when already running.
    bool start();
    // Stop all workers and release their ports. Idempotent.
    void stop();

    bool isRunning() const;
    int size() const;

    // Probe one member through an idle worker. Returns true when the probe
    // flow executed (out.tested reflects the outcome); returns false when no
    // worker is available or the pool is not running (caller keeps tested=false).
    bool probeMember(const MemberProbeTarget& target,
                     const std::string& testUrl,
                     long connectTimeoutMs,
                     long totalTimeoutMs,
                     MemberHealth& out);

private:
    struct Worker {
        std::shared_ptr<XrayInstance> instance;
        std::unique_ptr<xray::XrayApi> api;
        int socksPort = 0;
        int apiPort = 0;
        bool busy = false;
    };

    // Returns index of an idle worker (marks it busy) or -1 when all busy.
    int acquireWorker();
    void releaseWorker(int index);

    std::string xrayPath_;
    std::string configDir_;
    int workerCount_;
    std::vector<Worker> workers_;
    mutable std::mutex workersMutex_;
    std::atomic<bool> running_;
};

} // namespace proxy

#endif // PROXY_PROBE_POOL_H