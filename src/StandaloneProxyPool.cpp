#include "StandaloneProxyPool.h"
#include "ProxyHealthEvaluator.h"
#include "ConfigGenerator.h"
#include "config/OutboundBuilderFactory.h"
#include "PortManager.h"
#include "Logger.h"
#include <boost/json.hpp>
#include <cstdlib>

namespace proxy {

StandaloneProxyPool::StandaloneProxyPool(const config::StandalonePoolConfig& cfg,
                                         const std::string& xrayPath,
                                         const std::string& configDir)
    : cfg_(cfg),
      xrayPath_(xrayPath),
      configDir_(configDir),
      instance_(std::make_shared<XrayInstance>(xrayPath, cfg.socksPort, cfg.apiPort, configDir)),
      api_(new xray::XrayApi(xrayPath, std::string("127.0.0.1:") + std::to_string(cfg.apiPort))),
      running_(false),
      stopFlag_(false),
      reportHealth_(cfg.evaluate.reportHealth),
      autoPruneDead_(cfg.evaluate.autoPruneDead),
      autoOptimize_(cfg.evaluate.autoOptimize) {
}

StandaloneProxyPool::~StandaloneProxyPool() {
    stop();
}

bool StandaloneProxyPool::start() {
    if (running_) return true;
    // 2026-09-18 Spec §3.1: 每次 start 重置停止标志（stop() 会置 true）。
    stopFlag_.store(false, std::memory_order_release);
    std::string json = config::ConfigGenerator::buildPoolConfig(cfg_.socksPort, cfg_.apiPort, cfg_);
    instance_->setExplicitConfig(json);
    if (!instance_->start()) {
        Logger::write("[StandaloneProxyPool] failed to start pool instance", LogLevel::ERR);
        return false;
    }
    running_ = true;
    reportHealth_ = cfg_.evaluate.reportHealth;
    autoPruneDead_ = cfg_.evaluate.autoPruneDead;
    autoOptimize_ = cfg_.evaluate.autoOptimize;
    // Start the resident probe workers (non-direct member health probing).
    // Failure is non-fatal for the pool: the evaluator keeps reported
    // tested=false on such members (and start failure here is logged as WARN
    // rather than aborting the pool, since direct protocols still probe fine).
    probePool_.reset(new ProxyProbePool(xrayPath_, cfg_.evaluate.probeWorkers, configDir_));
    if (!probePool_->start()) {
        Logger::write("[StandaloneProxyPool] probe workers failed to start; "
                      "non-direct members will report untested", LogLevel::WARN);
    }
    evaluator_.setProbePool(probePool_.get());
    evaluatorThread_ = std::thread(&StandaloneProxyPool::evaluatorLoop, this);
    Logger::write("[StandaloneProxyPool] started", LogLevel::INFO);
    return true;
}

void StandaloneProxyPool::stop() {
    if (!running_) return;
    running_ = false;
    // 2026-09-18 Spec §3.1: 让正在跑的 doProbe() 循环感知到停止请求，
    // 在下一个 target 之前提前返回。pre-fix 必须等整个串行 probe 循环
    // 跑完（30-120s）才能返回；post-fix 最多等一个 target 探测完成（~ms）。
    stopFlag_.store(true, std::memory_order_release);
    if (evaluatorThread_.joinable()) {
        evaluatorThread_.join();
    }
    evaluator_.setProbePool(nullptr);
    if (probePool_) {
        probePool_->stop();
        probePool_.reset();
    }
    instance_->stop();
}

bool StandaloneProxyPool::isRunning() const {
    return running_;
}

bool StandaloneProxyPool::injectMember(const db::models::Profileitem& profile) {
    if (!running_) return false;
    // Reject duplicate indexIds up front: Xray would refuse the second
    // addOutboundDirect with "existing tag found: px-<id>", but an explicit
    // check gives a clear WARN log and skips the wasted gRPC call.
    const long long indexId = std::atoll(profile.indexid.c_str());
    {
        std::lock_guard<std::mutex> lock(membersMutex_);
        if (members_.find(indexId) != members_.end()) {
            Logger::write("[StandaloneProxyPool] inject rejected: " + profile.indexid
                          + " already in pool", LogLevel::WARN);
            return false;
        }
    }
    const std::string tag = std::string("px-") + profile.indexid;
    config::OutboundBuilderFactory factory;
    // Build the outbound object and pin its tag to the pool member tag so the
    // injected Xray handler is addressable as "px-<id>" (matches PoolMember.tag
    // and the balancer/observatory subjectSelector "px-").
    boost::json::object ob = factory.create(profile, tag);
    ob["tag"] = tag;
    // addOutboundDirect (gRPC) expects the wrapped {"outbounds":[...]} form
    // produced by ConfigGenerator / ProxyBatchTester, NOT a bare outbound.
    boost::json::object root;
    boost::json::array arr;
    arr.push_back(boost::json::value(ob));
    root["outbounds"] = arr;
    std::string json = boost::json::serialize(root);
    std::string resultOut;
    // Prefer the direct gRPC path (correct AddOutboundRequest protobuf, no
    // subprocess). Fall back to the legacy subprocess path for completeness.
    bool ok = api_->addOutboundDirect(json, tag, resultOut);
    if (!ok) {
        ok = api_->addOutbound(json, tag, resultOut);
    }
    if (!ok) {
        Logger::write("[StandaloneProxyPool] inject failed for " + tag, LogLevel::ERR);
        return false;
    }
    PoolMember m;
    // 2026-09-11 真实 indexId 为 int64 级字符串（如 5720942700011514210），旧 std::atoi 截断
    // 导致 members_ map 键与 View 均为垃圾值（bugfix #82）；改用 std::atoll 保真。
    m.indexId = std::atoll(profile.indexid.c_str());
    m.tag = tag;
    m.profile = profile;
    m.state = MemberLifecycleState::ACTIVE;
    m.failStreak = 0;
    m.lastDelayMs = -1;
    m.lastAlive = false;
    m.lastError = "";
    m.statsRecorded = false;
    m.lastProbe = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(membersMutex_);
        members_[m.indexId] = m;
    }
    notifyChanged();
    Logger::write("[StandaloneProxyPool] injected " + tag, LogLevel::INFO);
    return true;
}

bool StandaloneProxyPool::isDeadMember(const PoolMember& m, int pruneFailStreak) {
    return m.failStreak >= pruneFailStreak || !m.lastAlive;
}

bool StandaloneProxyPool::removeMember(long long indexId, bool graceful) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(membersMutex_);
        std::map<long long, PoolMember>::iterator it = members_.find(indexId);
        if (it != members_.end()) {
            found = true;
            if (!graceful) {
                if (api_) api_->removeOutbound(it->second.tag);
                if (isDeadMember(it->second, cfg_.evaluate.pruneFailStreak) && onMemberRemoved) {
                    onMemberRemoved(it->second.indexId);
                }
                members_.erase(it);
            } else if (it->second.state == MemberLifecycleState::ACTIVE) {
                it->second.state = MemberLifecycleState::REMOVE_REQUESTED;
            }
        }
    }
    if (found) notifyChanged();
    return found;
}

std::vector<PoolMemberView> StandaloneProxyPool::getMembers() const {
    std::vector<PoolMemberView> out;
    snapshotMembers(out);
    return out;
}

void StandaloneProxyPool::setReportHealth(bool on) {
    reportHealth_ = on;
}

void StandaloneProxyPool::setAutoPruneDead(bool on) {
    autoPruneDead_ = on;
}

void StandaloneProxyPool::setAutoOptimize(bool on) {
    autoOptimize_ = on;
}

void StandaloneProxyPool::snapshotMembers(std::vector<PoolMemberView>& out) const {
    std::lock_guard<std::mutex> lock(membersMutex_);
    const int port = cfg_.socksPort;
    const DWORD pid = (instance_ && instance_->isRunning()) ? instance_->getPid() : 0;
    for (std::map<long long, PoolMember>::const_iterator it = members_.begin(); it != members_.end(); ++it) {
        const PoolMember& m = it->second;
        PoolMemberView v;
        v.indexId = m.indexId;
        v.tag = m.tag;
        v.host = m.profile.address;
        if (m.state == MemberLifecycleState::ACTIVE) v.state = "active";
        else if (m.state == MemberLifecycleState::REMOVE_REQUESTED) v.state = "remove-requested";
        else v.state = "draining";
        v.lastDelayMs = m.lastDelayMs;
        v.lastAlive = m.lastAlive;
        v.lastError = m.lastError;
        v.failStreak = m.failStreak;
        v.probed = m.probed;
        v.socksPort = port;
        v.pid = pid;
        out.push_back(v);
    }
}

void StandaloneProxyPool::notifyChanged() {
    if (!onMembersChanged) return;
    std::vector<PoolMemberView> snap;
    snapshotMembers(snap);
    onMembersChanged(snap);
}

void StandaloneProxyPool::reinjectAll() {
    std::vector<PoolMember> snapshot;
    {
        std::lock_guard<std::mutex> lock(membersMutex_);
        for (std::map<long long, PoolMember>::iterator it = members_.begin(); it != members_.end(); ++it) {
            snapshot.push_back(it->second);
        }
    }
    for (std::size_t i = 0; i < snapshot.size(); ++i) {
        const PoolMember& m = snapshot[i];
        if (m.state != MemberLifecycleState::ACTIVE) continue;
        config::OutboundBuilderFactory factory;
        boost::json::object ob = factory.create(m.profile, m.tag);
        ob["tag"] = m.tag;
        boost::json::object root;
        boost::json::array arr;
        arr.push_back(boost::json::value(ob));
        root["outbounds"] = arr;
        std::string json = boost::json::serialize(root);
        std::string resultOut;
        bool ok = api_->addOutboundDirect(json, m.tag, resultOut);
        if (!ok) {
            ok = api_->addOutbound(json, m.tag, resultOut);
        }
        if (!ok) {
            Logger::write("[StandaloneProxyPool] reinject failed: " + m.tag, LogLevel::ERR);
        }
    }
}

void StandaloneProxyPool::relaunchIfNeeded() {
    if (instance_->isRunning()) return;
    Logger::write("[StandaloneProxyPool] instance not running; relaunching", LogLevel::WARN);
    if (!instance_->start()) {
        Logger::write("[StandaloneProxyPool] relaunch failed", LogLevel::ERR);
        return;
    }
    reinjectAll();
}

void StandaloneProxyPool::probeNow() {
    if (!running_) return;
    std::vector<MemberHealth> health = doProbe();
    {
        std::lock_guard<std::mutex> lock(membersMutex_);
        mergeHealth(health);
    }
    notifyChanged();
}

std::vector<MemberHealth> StandaloneProxyPool::doProbe() {
    std::vector<MemberProbeTarget> targets;
    {
        std::lock_guard<std::mutex> lock(membersMutex_);
        for (std::map<long long, PoolMember>::const_iterator it = members_.begin();
             it != members_.end(); ++it) {
            const PoolMember& m = it->second;
            if (m.state != MemberLifecycleState::ACTIVE) continue;
            MemberProbeTarget t;
            t.tag = m.tag;
            t.configtype = std::atoi(m.profile.configtype.c_str());
            t.address = m.profile.address;
            t.port = m.profile.port;
            t.username = m.profile.security;   // user = security (SOCKS/HTTP builders)
            t.password = m.profile.id;         // pass = id
            t.profile = m.profile;             // full snapshot for probe-pool outbound injection
            targets.push_back(t);
        }
    }
    std::string testUrl = cfg_.probeUrl.empty() ? cfg_.observatory.destination : cfg_.probeUrl;
    long totalMs = cfg_.observatory.timeoutSec > 0
                       ? static_cast<long>(cfg_.observatory.timeoutSec * 1000)
                       : 10000L;
    long connectMs = (totalMs < 3000L) ? totalMs : 3000L;
    // 2026-09-18 Spec §3.1: 传递停止标志，让 evaluator 的串行 probe 循环
    // 可被 stop() 提前打断（避免 join() 阻塞 30-120s）。
    return evaluator_.probe(targets, testUrl, connectMs, totalMs, &stopFlag_);
}

void StandaloneProxyPool::mergeHealth(const std::vector<MemberHealth>& health) {
    // Caller must hold membersMutex_.
    for (std::size_t i = 0; i < health.size(); ++i) {
        const std::string& t = health[i].tag;
        if (t.find("px-") != 0) continue;
        // bugfix #82: 真实 indexId 为 int64 级数字串，atoi 会截断导致 find 永远 miss
        long long id = std::atoll(t.substr(3).c_str());
        std::map<long long, PoolMember>::iterator it = members_.find(id);
        if (it == members_.end()) continue;
        it->second.probed = health[i].tested;
        if (health[i].tested) {
            it->second.lastDelayMs = health[i].delayMs;
            it->second.lastAlive = health[i].alive;
            it->second.lastError = health[i].lastError;
            if (health[i].alive) it->second.failStreak = 0;
            else it->second.failStreak += 1;
        }
        it->second.lastProbe = std::chrono::steady_clock::now();
    }
}

void StandaloneProxyPool::evaluatorLoop() {
    while (running_) {
        relaunchIfNeeded();

        std::vector<MemberHealth> health = doProbe();

        {
            std::lock_guard<std::mutex> lock(membersMutex_);

            // Merge probe results into member state (caller holds the lock).
            mergeHealth(health);

            // Policy (b): autoPruneDead — mark dead members for graceful removal.
            if (autoPruneDead_) {
                for (std::map<long long, PoolMember>::iterator it = members_.begin(); it != members_.end(); ++it) {
                    if (it->second.state == MemberLifecycleState::ACTIVE &&
                        it->second.failStreak >= cfg_.evaluate.pruneFailStreak) {
                        it->second.state = MemberLifecycleState::REMOVE_REQUESTED;
                        Logger::write("[StandaloneProxyPool] prune dead: " + it->second.tag, LogLevel::WARN);
                    }
                }
            }

            // Policy (c): autoOptimize — Phase 1 record-only (no OverrideBalancerTarget,
            // no remove+add rebalance). Mark preferred members for analytics.
            if (autoOptimize_) {
                for (std::map<long long, PoolMember>::iterator it = members_.begin(); it != members_.end(); ++it) {
                    if (it->second.state == MemberLifecycleState::ACTIVE && it->second.lastAlive) {
                        it->second.statsRecorded = true;
                    }
                }
            }

            // Two-phase graceful removal: perform HandlerService RemoveHandler now
            // (coordinated with probe, not mid-injection).
            std::vector<long long> done;
            for (std::map<long long, PoolMember>::iterator it = members_.begin(); it != members_.end(); ++it) {
                if (it->second.state == MemberLifecycleState::REMOVE_REQUESTED) {
                    if (api_->removeOutbound(it->second.tag)) {
                        it->second.state = MemberLifecycleState::DRAINING;
                        done.push_back(it->first);
                    } else {
                        Logger::write("[StandaloneProxyPool] graceful remove failed: " + it->second.tag, LogLevel::ERR);
                    }
                }
            }
            for (std::size_t k = 0; k < done.size(); ++k) {
                std::map<long long, PoolMember>::iterator it = members_.find(done[k]);
                if (it != members_.end()) {
                    if (isDeadMember(it->second, cfg_.evaluate.pruneFailStreak) && onMemberRemoved) {
                        onMemberRemoved(it->second.indexId);
                    }
                    members_.erase(it);
                }
            }
        }

        if (reportHealth_) {
            std::vector<PoolMemberView> snap;
            snapshotMembers(snap);
            Logger::write("[StandaloneProxyPool] health: " + std::to_string(snap.size()) + " members",
                          LogLevel::DEBUG);
        }

        notifyChanged();

        for (int s = 0; s < cfg_.evaluate.intervalSec && running_; ++s) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

bool resolvePoolPorts(config::StandalonePoolConfig& cfg, int attempts) {
    const int desiredSocks = cfg.socksPort > 0 ? cfg.socksPort : 10809;
    const int socks = PortManager::findAvailable(desiredSocks, attempts);
    if (socks <= 0) {
        return false;
    }
    const int desiredApi = cfg.apiPort > 0 ? cfg.apiPort : 10810;
    int api = PortManager::findAvailable(desiredApi, attempts);
    if (api <= 0) {
        PortManager::freePort(socks);
        return false;
    }
    if (api == socks) {
        // Defensive: desired api equals desired socks (ill-formed config).
        PortManager::freePort(api);
        api = PortManager::findAvailable(socks + 1, attempts);
        if (api <= 0) {
            PortManager::freePort(socks);
            return false;
        }
    }
    cfg.socksPort = socks;
    cfg.apiPort = api;
    return true;
}

} // namespace proxy
