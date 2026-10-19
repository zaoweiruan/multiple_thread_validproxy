#ifndef XRAY_MANAGER_H
#define XRAY_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <sqlite3.h>
#include "XrayInstance.h"
#include "BatchWorkerPolicy.h"

class XrayManager {
public:
    static XrayManager* getInstance();
    static XrayManager* getInstance(const std::string& xrayPath, const std::string& configDir, int workers, sqlite3* db = nullptr);
    static void release();
    
    XrayManager(const std::string& xrayPath, const std::string& configDir, int workers = 4, sqlite3* db = nullptr);
    ~XrayManager();
    
    int start(int testCount, int startPort, int apiPort);
    int start(int testCount);
    void stopAll();
    XrayInstance* getInstance(int index);
    int getInstanceCount() const;
    std::vector<std::pair<int, int>> getPortPairs();
    int getWorkers() const { return workers_; }
    bool isRunning() const { return getInstanceCount() > 0; }

    // Lifecycle management: locate the instance bound to apiPort and evaluate
    // whether its process is still healthy. If the process is dead or hung
    // (API port unresponsive), the instance is stopped, removed from the pool,
    // its ports freed, and a fresh instance is relaunched immediately with the
    // original config (same ports → same config file). Returns true if the
    // instance was found and successfully relaunched (ready to accept tests);
    // false if no instance is bound to apiPort or the relaunch failed.
    bool evaluateInstanceHealth(int apiPort);

    // P2-1: rate-limited wrapper around evaluateInstanceHealth (Spec §3.7).
    // Two independent reporters can fire for the same apiPort — the legacy
    // setConnectFailureHook (2nd consecutive gRPC connect failure) and the new
    // instanceHealthHook_ (CLI spawn failure / gRPC failure) — and both funnel
    // into ProxyBatchTester::onXrayEndpointUnreachable. Without the cooldown a
    // single flaky worker could relaunch the same instance twice within
    // milliseconds, and a persistently broken instance would restart on every
    // single queue item (50000+ items = a restart storm).
    //
    // Semantics: per-apiPort single-flight + 30s cooldown. The timestamp is
    // written UNCONDITIONALLY on every attempt (a failed relaunch must also
    // arm the cooldown, otherwise a persistently dead instance restarts on
    // every report). Returns true only when the underlying evaluation actually
    // ran and succeeded.
    //
    // Lock contract (four-tier order, see Spec §2.5):
    //   per-port mutex -> instancesMutex_ -> PortManager::mutex_ -> g_snapMutex
    // The per-port mutex must NEVER be acquired while holding any other lock.
    // D3 (2026-10-19): the previous bool contract could not distinguish "hit the 30s
    // cooldown, another reporter already restarted this instance" from "the
    // relaunch was attempted and failed". Callers treated both as failure, so a
    // perfectly healthy instance retired its worker.
    //
    // The enum lives in BatchWorkerPolicy.h (pure, unit-testable) and is aliased
    // here rather than redeclared, so production and the unit tests share one
    // type instead of two that can drift apart.
    using InstanceHealthOutcome = batch_policy::InstanceHealthOutcome;
    /// D3: full three-state wrapper. See InstanceHealthOutcome.
    InstanceHealthOutcome evaluateInstanceHealthThrottledEx(int apiPort);

    /// D3 (2026-10-19): compatibility shim kept so the existing call sites and
    /// tests keep their exact behaviour. It reports ONLY a successful relaunch;
    /// both "throttled" and "relaunch failed" collapse to false, as before.
    bool evaluateInstanceHealthThrottled(int apiPort);

 private:
    std::vector<std::unique_ptr<XrayInstance>> instances_;
    std::string xrayPath_;
    std::string configDir_;
    int workers_;
    sqlite3* db_;  // Optional: for ConfigBuilder template loading

    // Running flag for lifecycle tracking
    bool stopped_{false};

    // Protects instances_ (T1.5). Never hold this lock while calling
    // XrayInstance::start() (contains 2s sleep) — it is only taken for
    // short read/commit sections and around stop()/clear in stopAll().
    mutable std::mutex instancesMutex_;

    // P2-1 rate-limit state for evaluateInstanceHealthThrottled (Spec §3.7).
    // g_portLockMapMutex is only held long enough to look up / create the
    // per-apiPort mutex; it is released before that mutex is taken, so the
    // map lock never nests with any other lock.
    static const ULONGLONG kHealthRestartCooldownMs = 30000;  // 30s
    std::map<int, ULONGLONG> lastHealthRestart_;  // guarded by the per-port mutex
    static std::mutex g_portLockMapMutex;
    static std::map<int, std::shared_ptr<std::mutex>> g_portLocks;

    static XrayManager* instance_;
    static std::mutex instanceMutex_;
};

#endif // XRAY_MANAGER_H