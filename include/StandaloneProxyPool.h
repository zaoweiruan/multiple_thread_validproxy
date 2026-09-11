#ifndef STANDALONE_PROXY_POOL_H
#define STANDALONE_PROXY_POOL_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>

#include "ConfigReader.h"
#include "XrayInstance.h"
#include "XrayApi.h"
#include "Profileitem.h"
#include "ProxyHealthEvaluator.h"

namespace proxy {

// Lifecycle of a pool member. ACTIVE = serving; REMOVE_REQUESTED = a graceful
// removal was asked and the evaluator will perform the actual HandlerService
// RemoveHandler on the next cycle (two-phase, avoids racing the probe read);
// DRAINING = removal submitted, pending map erase (transient UI label; Xray
// itself drops the handler immediately — see Spec §5.8).
enum class MemberLifecycleState {
    ACTIVE,
    REMOVE_REQUESTED,
    DRAINING
};

struct PoolMember {
    long long indexId;
    std::string tag;                 // px-<indexId>
    db::models::Profileitem profile; // snapshot used to (re)build outbound JSON
    MemberLifecycleState state;
    int failStreak;
    long long lastDelayMs;
    bool lastAlive;
    std::string lastError;
    bool statsRecorded;             // autoOptimize record-only marker
    bool probed = false;            // true once the health evaluator has measured this member
    std::chrono::steady_clock::time_point lastProbe;
};

struct PoolMemberView {
    long long indexId;
    std::string tag;
    std::string host;               // member proxy address (profile.address)
    std::string state;
    long long lastDelayMs;
    bool lastAlive;
    std::string lastError;
    int failStreak;
    bool probed = false;            // true once the health evaluator has measured this member
    int socksPort = 0;              // pool listen port (0 = unknown)
    DWORD pid = 0;                  // pool xray process pid (0 = unknown)
};

// Resolve socks/api listen ports for the pool via PortManager so the pool
// never collides with an already-running standalone proxy or Xray instance
// (both of which allocate ports through the same manager). Updates
// cfg.socksPort / cfg.apiPort in place. Returns false (freeing any reserved
// port) if no free port can be found within `attempts` scans. Does not throw.
bool resolvePoolPorts(config::StandalonePoolConfig& cfg, int attempts = 200);

// Single xray process that hosts dynamically injected member proxies (tag
// px-<indexId>), balances them via a balancer+observatory control plane, and
// evaluates health on an interval applying the a/b/c policy switches.
class StandaloneProxyPool {
public:
    // cfg: parsed standalone_pool config. xrayPath: xray-core binary.
    // configDir: working dir for the pool's xray instance (writes config + logs).
    StandaloneProxyPool(const config::StandalonePoolConfig& cfg,
                        const std::string& xrayPath,
                        const std::string& configDir);
    ~StandaloneProxyPool();

    bool start();
    void stop();
    bool isRunning() const;

    // Inject a profile as a pool member. Returns false if the pool is not
    // running or the addOutbound call fails.
    bool injectMember(const db::models::Profileitem& profile);
    // Remove a member. graceful=true defers the actual handler removal to the
    // evaluator (two-phase); graceful=false removes immediately.
    bool removeMember(long long indexId, bool graceful);
    std::vector<PoolMemberView> getMembers() const;

    void setReportHealth(bool on);
    void setAutoPruneDead(bool on);
    void setAutoOptimize(bool on);

    // Synchronously run one health probe cycle and update member state. Used
    // for immediate feedback after injection and by tests; the background
    // evaluatorLoop also probes on its interval.
    void probeNow();

    // Invoked after every evaluation cycle with a fresh snapshot (UI refresh).
    std::function<void(const std::vector<PoolMemberView>&)> onMembersChanged;

private:
    void evaluatorLoop();
    void snapshotMembers(std::vector<PoolMemberView>& out) const;
    void notifyChanged();
    // Re-add every ACTIVE member's outbound (used after a relaunch).
    void reinjectAll();
    // Detect instance death and relaunch + reinject (Spec §5.7).
    void relaunchIfNeeded();
    // Build probe targets from current ACTIVE members and run the evaluator.
    std::vector<MemberHealth> doProbe();
    // Merge probe results into members_ (caller must hold membersMutex_).
    void mergeHealth(const std::vector<MemberHealth>& health);

    config::StandalonePoolConfig cfg_;
    std::shared_ptr<XrayInstance> instance_;
    std::unique_ptr<xray::XrayApi> api_;
    ProxyHealthEvaluator evaluator_;
    mutable std::mutex membersMutex_;
    std::map<long long, PoolMember> members_;
    std::atomic<bool> running_;
    std::atomic<bool> reportHealth_;
    std::atomic<bool> autoPruneDead_;
    std::atomic<bool> autoOptimize_;
    std::thread evaluatorThread_;
};

} // namespace proxy

#endif // STANDALONE_PROXY_POOL_H
