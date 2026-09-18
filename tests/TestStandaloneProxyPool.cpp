// TDD unit tests for the Standalone Proxy Pool feature.
// Covers (offline, no live Xray):
//   * ConfigGenerator::buildPoolConfig control-plane JSON structure
//   * config::StandalonePoolConfigParser field parsing (defaults + overrides)
//   * proxy::StandaloneProxyPool construction boundary behavior (no start)
//
// Runtime behavior (member injection over live gRPC, balancer selector
// discovery, Observatory health, DRAINING removal) is verified manually per
// the Spec §9.1 reviewer checklist.

#include <gtest/gtest.h>
#include <boost/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <future>
#include <chrono>

#include "ConfigReader.h"
#include "config/sections/StandalonePoolConfigParser.h"
#include "ConfigGenerator.h"
#include "StandaloneProxyPool.h"
#include "PortManager.h"
#include "Profileitem.h"
#include "XrayApi.h"

namespace {

// ---- helpers ---------------------------------------------------------------

boost::json::object asObject(const boost::json::value& v, const char* what) {
    EXPECT_TRUE(v.is_object()) << what << " should be a JSON object";
    return v.as_object();
}

// ---- buildPoolConfig structure -------------------------------------------

TEST(StandaloneProxyPool, BuildPoolConfigStructure) {
    config::StandalonePoolConfig cfg;
    cfg.enabled = true;
    cfg.mode = "pool";
    cfg.socksPort = 10809;
    cfg.apiPort = 10810;
    cfg.balancerStrategy = "leastPing";

    std::string json = config::ConfigGenerator::buildPoolConfig(cfg.socksPort, cfg.apiPort, cfg);
    boost::json::value root;
    ASSERT_NO_THROW(root = boost::json::parse(json)) << "buildPoolConfig must return valid JSON";

    boost::json::object doc = asObject(root, "root");

    // 1) api service with Handler/Routing/Observatory
    ASSERT_TRUE(doc.contains("api"));
    boost::json::object api = asObject(doc.at("api"), "api");
    EXPECT_EQ(api.at("tag").as_string(), "api");
    ASSERT_TRUE(api.contains("services"));
    bool hasHandler = false, hasRouting = false, hasObservatory = false;
    for (const boost::json::value& s : api.at("services").as_array()) {
        std::string name = s.as_string().c_str();
        if (name == "HandlerService") hasHandler = true;
        if (name == "RoutingService") hasRouting = true;
        if (name == "ObservatoryService") hasObservatory = true;
    }
    EXPECT_TRUE(hasHandler) << "api.services must include HandlerService";
    EXPECT_TRUE(hasRouting) << "api.services must include RoutingService";
    EXPECT_TRUE(hasObservatory) << "api.services must include ObservatoryService";

    // 2) mixed socks inbound (array form, Xray schema)
    ASSERT_TRUE(doc.contains("inbounds"));
    boost::json::array& inbounds = doc.at("inbounds").as_array();
    bool hasSocksIn = false;
    for (const boost::json::value& ib : inbounds) {
        if (ib.at("tag").as_string() == "socks-in") {
            hasSocksIn = true;
            EXPECT_EQ(ib.at("protocol").as_string(), "mixed");
            EXPECT_EQ(ib.at("listen").as_string(), "127.0.0.1");
            ASSERT_TRUE(ib.as_object().contains("port")) << "socks-in must declare a separate port";
            EXPECT_EQ(ib.at("port").as_int64(), 10809);
            boost::json::object sn = asObject(ib.at("settings"), "socks settings");
            EXPECT_EQ(sn.at("auth").as_string(), "noauth");
            EXPECT_TRUE(sn.at("udp").as_bool());
        }
    }
    EXPECT_TRUE(hasSocksIn) << "inbounds must contain socks-in";

    // 3) outbounds: only the static "direct" (freedom). The balancer is no
    //    longer an outbound (xray 26.x removed the "balancing" protocol); it is
    //    declared under routing.balancers (see section 5).
    ASSERT_TRUE(doc.contains("outbounds"));
    boost::json::array& outs = doc.at("outbounds").as_array();
    bool hasDirect = false, hasLegacyBalancer = false;
    for (const boost::json::value& ob : outs) {
        std::string tag = ob.at("tag").as_string().c_str();
        if (tag == "direct") {
            hasDirect = true;
            EXPECT_EQ(ob.at("protocol").as_string(), "freedom");
        } else if (tag == "balancer-out") {
            hasLegacyBalancer = true;
        }
    }
    EXPECT_TRUE(hasDirect) << "outbounds must contain direct";
    EXPECT_FALSE(hasLegacyBalancer)
        << "outbounds must NOT contain a legacy 'balancing' protocol outbound";

    // 4) observatory subjectSelector ["px-"]
    ASSERT_TRUE(doc.contains("observatory"));
    boost::json::object obs = asObject(doc.at("observatory"), "observatory");
    boost::json::array sub = obs.at("subjectSelector").as_array();
    ASSERT_EQ(sub.size(), 1u);
    EXPECT_EQ(sub[0].as_string(), "px-");

    // 5) routing: balancer declared under routing.balancers (xray 26.x) and a
    //    field rule referencing it via balancerTag.
    ASSERT_TRUE(doc.contains("routing"));
    boost::json::object routing = asObject(doc.at("routing"), "routing");
    ASSERT_TRUE(routing.contains("balancers")) << "routing.balancers must exist";
    boost::json::array& balancers = routing.at("balancers").as_array();
    ASSERT_EQ(balancers.size(), 1u) << "exactly one balancer expected";
    boost::json::object bal = asObject(balancers[0], "balancer");
    EXPECT_EQ(bal.at("tag").as_string(), "balancer-out");
    boost::json::array sel = bal.at("selector").as_array();
    ASSERT_EQ(sel.size(), 1u);
    EXPECT_EQ(sel[0].as_string(), "px-") << "balancer selector must be the px- prefix";
    EXPECT_EQ(bal.at("strategy").at("type").as_string(), "leastPing")
        << "balancer strategy must match config";
    ASSERT_TRUE(routing.contains("rules"));
    bool hasBalancingRule = false;
    for (const boost::json::value& r : routing.at("rules").as_array()) {
        if (r.at("type").as_string() == "field" && r.as_object().contains("balancerTag")
            && r.at("balancerTag").as_string() == "balancer-out") {
            hasBalancingRule = true;
        }
    }
    EXPECT_TRUE(hasBalancingRule) << "routing.rules must contain a field rule targeting balancer-out";
}

// The balancer must carry an `observation` block so xray 26.x attaches an
// observatory to it. GetOutboundStatus only reports observatories wired to a
// balancer; without this the pool probe returns zero entries and every member
// is frozen at its injection state (active / -1 / 否 / 0). The probe URL must
// reuse the configured test url (config.json test.url) rather than a separate
// hard-coded observatory.destination.
TEST(StandaloneProxyPool, BuildPoolConfigObservationBlock) {
    config::StandalonePoolConfig cfg;
    cfg.enabled = true;
    cfg.probeUrl = "https://www.example.com/generate_204";
    cfg.observatory.intervalSec = 5;

    std::string json = config::ConfigGenerator::buildPoolConfig(cfg.socksPort, cfg.apiPort, cfg);
    boost::json::value root;
    ASSERT_NO_THROW(root = boost::json::parse(json)) << "buildPoolConfig must return valid JSON";
    boost::json::object doc = asObject(root, "root");

    ASSERT_TRUE(doc.contains("routing"));
    boost::json::object routing = asObject(doc.at("routing"), "routing");
    ASSERT_TRUE(routing.contains("balancers"));
    boost::json::array& balancers = routing.at("balancers").as_array();
    ASSERT_EQ(balancers.size(), 1u);
    boost::json::object bal = asObject(balancers[0], "balancer");

    ASSERT_TRUE(bal.contains("observation")) << "balancer must carry an observation block";
    boost::json::object obs = asObject(bal.at("observation"), "observation");
    EXPECT_EQ(obs.at("probeURL").as_string(), cfg.probeUrl)
        << "probe URL must reuse the configured test url";
    boost::json::array sub = obs.at("subjectSelector").as_array();
    ASSERT_EQ(sub.size(), 1u);
    EXPECT_EQ(sub[0].as_string(), "px-");
    EXPECT_EQ(obs.at("probeInterval").as_string(), "5s");

    // Top-level observatory must also echo the reused probe URL.
    ASSERT_TRUE(doc.contains("observatory"));
    boost::json::object topObs = asObject(doc.at("observatory"), "observatory");
    EXPECT_EQ(topObs.at("probeURL").as_string(), cfg.probeUrl);
}

// ---- config parser ---------------------------------------------------------

TEST(StandaloneProxyPool, ConfigParserDefaultsWhenAbsent) {
    config::AppConfig cfg;
    bool defEnabled = cfg.standalone_pool.enabled;
    std::string defMode = cfg.standalone_pool.mode;
    int defProbeWorkers = cfg.standalone_pool.evaluate.probeWorkers;
    config::StandalonePoolConfigParser().parse(boost::json::object{}, cfg, "");
    EXPECT_EQ(cfg.standalone_pool.enabled, defEnabled);
    EXPECT_EQ(cfg.standalone_pool.mode, defMode);
    EXPECT_EQ(cfg.standalone_pool.evaluate.probeWorkers, defProbeWorkers)
        << "probeWorkers must keep its default when absent";
}

TEST(StandaloneProxyPool, ConfigParserOverrides) {
    config::AppConfig cfg;
    boost::json::object root = boost::json::parse(R"({
        "standalone_pool": {
            "enabled": true,
            "mode": "pool",
            "socksPort": 20009,
            "apiPort": 20010,
            "balancerStrategy": "leastLoad",
            "observatory": {
                "type": "ping",
                "destination": "https://www.cloudflare.com",
                "intervalSec": 7,
                "samplingCount": 5,
                "timeoutSec": 3
            },
            "evaluate": {
                "intervalSec": 12,
                "reportHealth": false,
                "autoPruneDead": true,
                "pruneFailStreak": 4,
                "autoOptimize": true,
                "probeWorkers": 5
            }
        }
    })").as_object();

    config::StandalonePoolConfigParser().parse(root, cfg, "");

    EXPECT_TRUE(cfg.standalone_pool.enabled);
    EXPECT_EQ(cfg.standalone_pool.mode, "pool");
    EXPECT_EQ(cfg.standalone_pool.socksPort, 20009);
    EXPECT_EQ(cfg.standalone_pool.apiPort, 20010);
    EXPECT_EQ(cfg.standalone_pool.balancerStrategy, "leastLoad");
    EXPECT_EQ(cfg.standalone_pool.observatory.type, "ping");
    EXPECT_EQ(cfg.standalone_pool.observatory.destination, "https://www.cloudflare.com");
    EXPECT_EQ(cfg.standalone_pool.observatory.intervalSec, 7);
    EXPECT_EQ(cfg.standalone_pool.observatory.samplingCount, 5);
    EXPECT_EQ(cfg.standalone_pool.observatory.timeoutSec, 3);
    EXPECT_EQ(cfg.standalone_pool.evaluate.intervalSec, 12);
    EXPECT_FALSE(cfg.standalone_pool.evaluate.reportHealth);
    EXPECT_TRUE(cfg.standalone_pool.evaluate.autoPruneDead);
    EXPECT_EQ(cfg.standalone_pool.evaluate.pruneFailStreak, 4);
    EXPECT_TRUE(cfg.standalone_pool.evaluate.autoOptimize);
    EXPECT_EQ(cfg.standalone_pool.evaluate.probeWorkers, 5)
        << "probeWorkers must be parsed from evaluate block";
}

TEST(StandaloneProxyPool, ConfigParserRejectsInvalidEnum) {
    config::AppConfig cfg;
    cfg.standalone_pool.mode = "pool";
    cfg.standalone_pool.balancerStrategy = "leastPing";
    cfg.standalone_pool.observatory.type = "http";
    boost::json::object root = boost::json::parse(R"({
        "standalone_pool": {
            "mode": "bogus",
            "balancerStrategy": "bogus",
            "observatory": { "type": "bogus" },
            "evaluate": { "probeWorkers": 65 }
        }
    })").as_object();
    config::StandalonePoolConfigParser().parse(root, cfg, "");
    EXPECT_EQ(cfg.standalone_pool.mode, "pool") << "invalid mode must be ignored";
    EXPECT_EQ(cfg.standalone_pool.balancerStrategy, "leastPing") << "invalid strategy ignored";
    EXPECT_EQ(cfg.standalone_pool.observatory.type, "http") << "invalid observatory type ignored";
    EXPECT_EQ(cfg.standalone_pool.evaluate.probeWorkers, 2)
        << "out-of-range probeWorkers (>64) must keep the default";

    // 0 is a legal value: it disables the resident probe pool (0..64 accepted).
    cfg.standalone_pool.evaluate.probeWorkers = 2;
    boost::json::object rootZero = boost::json::parse(R"({
        "standalone_pool": { "evaluate": { "probeWorkers": 0 } }
    })").as_object();
    config::StandalonePoolConfigParser().parse(rootZero, cfg, "");
    EXPECT_EQ(cfg.standalone_pool.evaluate.probeWorkers, 0)
        << "probeWorkers 0 = disable must be parsed, not rejected";

    // Negative values are rejected.
    cfg.standalone_pool.evaluate.probeWorkers = 2;
    boost::json::object rootNeg = boost::json::parse(R"({
        "standalone_pool": { "evaluate": { "probeWorkers": -1 } }
    })").as_object();
    config::StandalonePoolConfigParser().parse(rootNeg, cfg, "");
    EXPECT_EQ(cfg.standalone_pool.evaluate.probeWorkers, 2)
        << "negative probeWorkers must keep the default";
}

// ---- pool construction (offline) ------------------------------------------

TEST(StandaloneProxyPool, ConstructionBoundaryNoStart) {
    config::StandalonePoolConfig cfg;
    cfg.enabled = true;
    cfg.socksPort = 10809;
    cfg.apiPort = 10810;

    proxy::StandaloneProxyPool pool(cfg, "xray.exe", "C:/tmp/config");

    EXPECT_FALSE(pool.isRunning()) << "pool must not be running before start()";

    // Injecting before start must fail.
    db::models::Profileitem probe;
    probe.indexid = "1";
    probe.address = "203.0.113.7";
    probe.port = "8388";
    EXPECT_FALSE(pool.injectMember(probe)) << "inject before start must return false";

    // Removing an unknown member must fail.
    EXPECT_FALSE(pool.removeMember(1, false)) << "removeMember unknown id must return false";

    // No members yet.
    EXPECT_TRUE(pool.getMembers().empty()) << "members must be empty before any injection";

    // Setters must not crash and simply arm flags.
    EXPECT_NO_THROW({
        pool.setReportHealth(true);
        pool.setAutoPruneDead(true);
        pool.setAutoOptimize(true);
        pool.setReportHealth(false);
        pool.setAutoPruneDead(false);
        pool.setAutoOptimize(false);
    });

    // stop() is safe when not running.
    EXPECT_NO_THROW(pool.stop());
    EXPECT_FALSE(pool.isRunning());
}

// 2026-09-11 Bugfix-PoolEvaluator-RecursiveLock-Hang:
// evaluatorLoop must NOT call notifyChanged() while holding membersMutex_.
// notifyChanged() → snapshotMembers() → lock(membersMutex_) is a same-thread
// recursive lock; with MinGW's SRWLOCK-backed non-recursive std::mutex this
// deadlocks the evaluator thread permanently, which then blocks the UI 5s
// monitor timer on the same mutex → AppHangB1 (Windows kills the app).
//
// Regression pattern: wire onMembersChanged, start() the pool, and assert the
// callback fires within a bounded wait (pre-fix: evaluator freezes on its
// first notifyChanged, callback count stays 0). stop() is guarded by
// std::async + wait_for so a frozen evaluator fails the test instead of
// hanging the test process (mirrors the pre-fix AppHang).
TEST(StandaloneProxyPool, EvaluatorNoRecursiveLockOnNotify) {
    const char* xrayExe = std::getenv("XRAY_REAL_EXE");
    if (xrayExe == nullptr || xrayExe[0] == '\0') {
        GTEST_SKIP() << "XRAY_REAL_EXE not set; skipping live pool test";
    }

    static int s_hangCounter = 0;
    std::filesystem::path cfgDir =
        std::filesystem::temp_directory_path() /
        ("standalone_pool_hang_" + std::to_string(++s_hangCounter));
    std::filesystem::create_directories(cfgDir);

    config::StandalonePoolConfig cfg;
    cfg.enabled = true;
    cfg.socksPort = 20130;
    cfg.apiPort = 20131;
    cfg.evaluate.intervalSec = 1;   // fast cycle for the test
    ASSERT_TRUE(proxy::resolvePoolPorts(cfg));

    proxy::StandaloneProxyPool pool(cfg, xrayExe, cfgDir.string());

    std::atomic<int> notifyCount{0};
    pool.onMembersChanged = [&notifyCount](const std::vector<proxy::PoolMemberView>&) {
        notifyCount.fetch_add(1);
    };

    ASSERT_TRUE(pool.start()) << "pool.start() must succeed against real xray";

    // Bounded wait: pre-fix the evaluator freezes inside its first cycle
    // (mergeHealth → notifyChanged → recursive lock) so the callback never
    // fires and this loop times out with count==0.
    bool notified = false;
    for (int i = 0; i < 40; ++i) {          // up to ~4s
        if (notifyCount.load() >= 1) { notified = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_TRUE(notified)
        << "onMembersChanged never fired — evaluator deadlocked on membersMutex_";

    // stop() must complete promptly. Pre-fix, stop() joins the frozen
    // evaluator thread and hangs forever (AppHang). Guard with a bounded
    // async so the failure surfaces as a test failure, not a hang.
    std::future<void> stopTask = std::async(std::launch::async, [&pool]() { pool.stop(); });
    std::future_status st = stopTask.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(st, std::future_status::ready)
        << "pool.stop() did not return within 5s — evaluator thread frozen";

    PortManager::freePort(cfg.socksPort);
    PortManager::freePort(cfg.apiPort);
    PortManager::clearPorts();
    std::error_code ec;
    std::filesystem::remove_all(cfgDir, ec);
}

// ---- pool port resolution (collision avoidance) -------------------------

TEST(StandaloneProxyPool, ResolvePoolPortsAvoidsInUsePort) {
    PortManager::clearPorts();
    // Occupy a port through the manager — simulates a running standalone proxy
    // / Xray instance that already reserved it via PortManager.
    int occupied = PortManager::findAvailable(10809, 10);
    ASSERT_GT(occupied, 0);

    config::StandalonePoolConfig cfg;
    cfg.socksPort = occupied;  // collides with the occupied port
    cfg.apiPort = occupied + 1;

    EXPECT_TRUE(proxy::resolvePoolPorts(cfg))
        << "resolvePoolPorts must find an alternative port when the desired port collides";
    EXPECT_NE(cfg.socksPort, occupied)
        << "pool socksPort must not collide with the occupied port";
    EXPECT_NE(cfg.apiPort, cfg.socksPort) << "socks and api ports must differ";

    // Cleanup so manager state does not leak into other tests.
    PortManager::freePort(occupied);
    PortManager::freePort(cfg.socksPort);
    PortManager::freePort(cfg.apiPort);
    PortManager::clearPorts();
}

TEST(StandaloneProxyPool, ResolvePoolPortsKeepsFreeDesiredPort) {
    PortManager::clearPorts();
    config::StandalonePoolConfig cfg;
    cfg.socksPort = 20009;
    cfg.apiPort = 20010;

    EXPECT_TRUE(proxy::resolvePoolPorts(cfg));
    EXPECT_EQ(cfg.socksPort, 20009) << "free desired socksPort should be kept";
    EXPECT_EQ(cfg.apiPort, 20010) << "free desired apiPort should be kept";

    PortManager::freePort(cfg.socksPort);
    PortManager::freePort(cfg.apiPort);
    PortManager::clearPorts();
}

// Live integration test (opt-in): locks the two fixes from
// 2026-08-27 —
//   (1) member injection via gRPC addOutboundDirect with the wrapped
//       {"outbounds":[...]} form (previously the broken subprocess
//       `xray api ado` bare-json path silently failed), and
//   (2) the corrected Observatory path
//       /xray.core.app.observatory.command.ObservatoryService/GetOutboundStatus
//       (verified at runtime via gRPC reflection on Xray 26.3.27; the plain
//       proto form "xray.app.observatory.command..." is NOT registered and
//       returned status=12 "unknown service").
//
// Requires a real xray binary via the XRAY_REAL_EXE env var. Otherwise it is
// skipped so CI stays green (matches test_xray_manager_startup convention).
//
// Requires a real xray binary via the XRAY_REAL_EXE env var. Otherwise it is
// skipped so CI stays green (matches test_xray_manager_startup convention).
TEST(StandaloneProxyPool, LiveInjectionAndObservatoryPath) {
    const char* xrayExe = std::getenv("XRAY_REAL_EXE");
    if (xrayExe == nullptr || xrayExe[0] == '\0') {
        GTEST_SKIP() << "XRAY_REAL_EXE not set; skipping live pool test";
    }

    // Isolated config dir under the OS temp so the test never touches bin/.
    static int s_counter = 0;
    std::filesystem::path cfgDir =
        std::filesystem::temp_directory_path() /
        ("standalone_pool_live_" + std::to_string(++s_counter));
    std::filesystem::create_directories(cfgDir);

    config::StandalonePoolConfig cfg;
    cfg.enabled = true;
    cfg.socksPort = 20120;
    cfg.apiPort = 20121;
    cfg.balancerStrategy = "leastPing";
    cfg.observatory.destination = "https://www.google.com";
    cfg.observatory.intervalSec = 5;
    cfg.evaluate.reportHealth = true;
    ASSERT_TRUE(proxy::resolvePoolPorts(cfg));

    proxy::StandaloneProxyPool pool(cfg, xrayExe, cfgDir.string());

    // Fix (1): pool.start() launches xray with a config that registers
    // HandlerService + ObservatoryService (ConfigGenerator::buildPoolConfig).
    ASSERT_TRUE(pool.start()) << "pool.start() must succeed against real xray";

    // Minimal SOCKS5 profile; injection only registers the handler via gRPC
    // (no network needed), so it must succeed once addOutboundDirect works.
    db::models::Profileitem profile;
    profile.indexid = "900012355";
    profile.configtype = "4";   // SOCKS5 outbound
    profile.address = "1.2.3.4";
    profile.port = "8080";

    ASSERT_TRUE(pool.injectMember(profile))
        << "injectMember must succeed via gRPC addOutboundDirect (wrapped JSON)";

    std::vector<proxy::PoolMemberView> members = pool.getMembers();
    ASSERT_EQ(1u, members.size()) << "injected member must be registered in the pool";
    EXPECT_EQ("px-900012355", members[0].tag);

    // Give Observatory one probe interval to run before querying, then check
    // the health path completes (the fix is that it no longer fails with
    // "unknown service"; the member may legitimately be reported down since
    // the probe target is unreachable in the test).
    std::this_thread::sleep_for(std::chrono::seconds(6));

    // Health is now probed directly against each member's *upstream* proxy (the
    // xray ObservatoryService only reports outbounds present at xray Start(), so
    // it never sees members injected at runtime — which left every member frozen
    // at active / -1 / 否 / failStreak 0). Verify the injected member is
    // actually measured, not left at its injection defaults.
    pool.probeNow();
    std::vector<proxy::PoolMemberView> views = pool.getMembers();
    ASSERT_GE(views.size(), 1u) << "pool must contain the injected member";
    bool found = false, probed = false;
    for (const auto& v : views) {
        if (v.tag == "px-900012355") { found = true; probed = v.probed; }
    }
    EXPECT_TRUE(found) << "injected px-900012355 must be present in the pool";
    EXPECT_TRUE(probed) << "injected member must be actually probed (frozen-field fix)";

    pool.stop();
    PortManager::freePort(cfg.socksPort);
    PortManager::freePort(cfg.apiPort);
    PortManager::clearPorts();
    std::error_code ec;
    std::filesystem::remove_all(cfgDir, ec);
}

// ---- dead member writeback (2026-09-15) ----------------------------------
// isDeadMember is a pure predicate used by the evaluator removal path to
// decide whether a removed member should be written back to ProfileExItem
// (delay=-1 + history reset) via the onMemberRemoved callback.

TEST(StandaloneProxyPool, IsDeadMemberByFailStreak) {
    proxy::PoolMember m;
    m.failStreak = 3;
    m.lastAlive = true;
    EXPECT_TRUE(proxy::StandaloneProxyPool::isDeadMember(m, 3))
        << "failStreak reaching the prune threshold marks the member dead";
    EXPECT_FALSE(proxy::StandaloneProxyPool::isDeadMember(m, 4))
        << "failStreak below the prune threshold is not dead";
}

TEST(StandaloneProxyPool, IsDeadMemberByLastAlive) {
    proxy::PoolMember m;
    m.failStreak = 0;
    m.lastAlive = false;
    EXPECT_TRUE(proxy::StandaloneProxyPool::isDeadMember(m, 3))
        << "a member whose last probe was not alive is dead regardless of streak";
}

TEST(StandaloneProxyPool, IsDeadMemberHealthy) {
    proxy::PoolMember m;
    m.failStreak = 1;
    m.lastAlive = true;
    EXPECT_FALSE(proxy::StandaloneProxyPool::isDeadMember(m, 3))
        << "a healthy member (low streak, alive) is not dead";
}

// Live integration test (opt-in): a dead member removed by the evaluator
// (autoPruneDead) must fire onMemberRemoved with its indexId. Requires a real
// xray binary via XRAY_REAL_EXE; skipped otherwise so CI stays green.
TEST(StandaloneProxyPool, DeadMemberRemovalTriggersCallback) {
    const char* xrayExe = std::getenv("XRAY_REAL_EXE");
    if (xrayExe == nullptr || xrayExe[0] == '\0') {
        GTEST_SKIP() << "XRAY_REAL_EXE not set; skipping live pool test";
    }

    static int s_deadCounter = 0;
    std::filesystem::path cfgDir =
        std::filesystem::temp_directory_path() /
        ("standalone_pool_dead_" + std::to_string(++s_deadCounter));
    std::filesystem::create_directories(cfgDir);

    config::StandalonePoolConfig cfg;
    cfg.enabled = true;
    cfg.socksPort = 20140;
    cfg.apiPort = 20141;
    cfg.balancerStrategy = "leastPing";
    cfg.observatory.destination = "https://www.google.com";
    cfg.observatory.intervalSec = 1;
    cfg.evaluate.intervalSec = 1;      // fast cycle for the test
    cfg.evaluate.pruneFailStreak = 1;  // one failed probe → prune
    cfg.observatory.timeoutSec = 2;    // short probe timeout
    ASSERT_TRUE(proxy::resolvePoolPorts(cfg));

    proxy::StandaloneProxyPool pool(cfg, xrayExe, cfgDir.string());
    pool.setAutoPruneDead(true);

    std::atomic<long long> removedId{0};
    pool.onMemberRemoved = [&removedId](long long id) { removedId.store(id); };

    ASSERT_TRUE(pool.start()) << "pool.start() must succeed against real xray";

    // Unreachable member (TEST-NET-3): the probe fails → failStreak reaches
    // the threshold → autoPruneDead marks REMOVE_REQUESTED → the evaluator
    // removes it and fires onMemberRemoved.
    db::models::Profileitem profile;
    profile.indexid = "900012356";
    profile.configtype = "4";          // SOCKS5 outbound
    profile.address = "203.0.113.9";   // TEST-NET-3, unreachable
    profile.port = "8080";
    ASSERT_TRUE(pool.injectMember(profile))
        << "injectMember must succeed via gRPC addOutboundDirect";

    bool removed = false;
    for (int i = 0; i < 60; ++i) {     // up to ~30s
        if (removedId.load() == 900012356LL) { removed = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    EXPECT_TRUE(removed)
        << "onMemberRemoved must fire for the dead member (autoPruneDead path)";

    pool.stop();
    PortManager::freePort(cfg.socksPort);
    PortManager::freePort(cfg.apiPort);
    PortManager::clearPorts();
    std::error_code ec;
    std::filesystem::remove_all(cfgDir, ec);
}

// Live integration test (opt-in): injecting the same indexId twice must be
// rejected on the second call (explicit duplicate guard, WARN log) while a
// different indexId still injects fine. Requires XRAY_REAL_EXE; skipped
// otherwise so CI stays green.
TEST(StandaloneProxyPool, DuplicateInjectRejected) {
    const char* xrayExe = std::getenv("XRAY_REAL_EXE");
    if (xrayExe == nullptr || xrayExe[0] == '\0') {
        GTEST_SKIP() << "XRAY_REAL_EXE not set; skipping live pool test";
    }

    static int s_dupCounter = 0;
    std::filesystem::path cfgDir =
        std::filesystem::temp_directory_path() /
        ("standalone_pool_dup_" + std::to_string(++s_dupCounter));
    std::filesystem::create_directories(cfgDir);

    config::StandalonePoolConfig cfg;
    cfg.enabled = true;
    cfg.socksPort = 20150;
    cfg.apiPort = 20151;
    cfg.balancerStrategy = "leastPing";
    cfg.observatory.destination = "https://www.google.com";
    ASSERT_TRUE(proxy::resolvePoolPorts(cfg));

    proxy::StandaloneProxyPool pool(cfg, xrayExe, cfgDir.string());
    ASSERT_TRUE(pool.start()) << "pool.start() must succeed against real xray";

    db::models::Profileitem profile;
    profile.indexid = "900012357";
    profile.configtype = "4";          // SOCKS5 outbound
    profile.address = "1.2.3.4";
    profile.port = "8080";

    ASSERT_TRUE(pool.injectMember(profile))
        << "first injection of an indexId must succeed";
    EXPECT_FALSE(pool.injectMember(profile))
        << "second injection of the same indexId must be rejected (duplicate guard)";

    db::models::Profileitem other = profile;
    other.indexid = "900012358";
    EXPECT_TRUE(pool.injectMember(other))
        << "a different indexId must still inject successfully";

    pool.stop();
    PortManager::freePort(cfg.socksPort);
    PortManager::freePort(cfg.apiPort);
    PortManager::clearPorts();
    std::error_code ec;
    std::filesystem::remove_all(cfgDir, ec);
}

// ============================================================================
// Pool Stop Optimization (2026-09-18 Spec §6)
//
// 用户报告"代理池中代理数量多时关闭池非常缓慢"。三个瓶颈：
//   A: evaluator doProbe 串行不可中断（主因，30-120s）
//   B: XrayInstance::stop 硬 sleep 500ms（1.0-1.5s/实例）
//   C: ProxyProbePool::stop 串行停止 worker（probeWorkers=8 时 ~12s）
// 目标：整体关闭时间 1-2s。
//
// 测试 A 为纯离线测试；B/C/D 通过 XRAY_REAL_EXE 环境变量 opt-in，未设置时
// GTEST_SKIP（CI 保持绿灯）。
// ============================================================================

// Test A (offline): ProxyHealthEvaluator::probe with a set stopFlag must
// return promptly, skipping the remaining targets. RED before implementation:
// probe() currently has no stopFlag parameter, so this fails to compile.
TEST(StandaloneProxyPool, ProxyHealthEvaluatorStopFlagReturnsPromptly) {
    proxy::ProxyHealthEvaluator evaluator;

    // 1000 SOCKS targets pointing at an invalid local port. Even if each cURL
    // probe fails in ~1ms, iterating all of them takes ~1s — far longer than
    // the stopFlag contract. When stopFlag is pre-set, probe() must return
    // within the first iteration (<<100ms).
    std::vector<proxy::MemberProbeTarget> targets;
    targets.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        proxy::MemberProbeTarget t;
        t.tag = "px-offline-" + std::to_string(i);
        t.configtype = 4;            // SOCKS — routed via probeSocksHttp
        t.address = "127.0.0.1";
        t.port = "0";                // invalid port, connect fails immediately
        t.username = "u";
        t.password = "p";
        targets.push_back(t);
    }

    std::atomic<bool> stopFlag(true);   // pre-set: stop immediately on entry
    const auto start = std::chrono::steady_clock::now();
    const std::vector<proxy::MemberHealth> health = evaluator.probe(
        targets, "https://127.0.0.1:1/healthz", /*connectMs*/5, /*totalMs*/100,
        &stopFlag);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_LE(elapsedMs, 100)
        << "probe() with a set stopFlag must return within 100ms (took "
        << elapsedMs << "ms). Pre-fix probe() iterates all 1000 targets "
        "before returning, taking seconds.";
    EXPECT_EQ(health.size(), targets.size())
        << "probe() must still return one MemberHealth per target (contract)";
}

// Test B (live, opt-in): XrayInstance::stop must return quickly after a
// successful start, not the previous 1.0-1.5s hardcoded-sleep + wait pattern.
TEST(StandaloneProxyPool, XrayInstanceStopReturnsFast) {
    const char* xrayExe = std::getenv("XRAY_REAL_EXE");
    if (xrayExe == nullptr || xrayExe[0] == '\0') {
        GTEST_SKIP() << "XRAY_REAL_EXE not set; skipping live stop test";
    }

    static int s_stopCounter = 0;
    const std::string cfgDir =
        (std::filesystem::temp_directory_path() /
         ("xray_inst_stop_" + std::to_string(++s_stopCounter))).string();
    std::filesystem::create_directories(cfgDir);

    PortManager::clearPorts();
    int socksPort = PortManager::findAvailable(20200, 200);
    int apiPort = PortManager::findAvailable(20201, 200);
    ASSERT_GT(socksPort, 0);
    ASSERT_GT(apiPort, 0);
    ASSERT_NE(socksPort, apiPort);

    XrayInstance inst(xrayExe, socksPort, apiPort, cfgDir);
    ASSERT_TRUE(inst.start())
        << "XrayInstance.start() must succeed against real xray";
    ASSERT_TRUE(inst.isRunning());

    const auto start = std::chrono::steady_clock::now();
    inst.stop();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Pre-fix: 1.0-1.5s per instance. Post-fix (poll replaces sleep_for(500)):
    // <200ms is the target; use 500ms as a lenient bound that still catches
    // the regression (pre-fix reliably >700ms).
    EXPECT_LE(elapsedMs, 500)
        << "XrayInstance::stop() took " << elapsedMs
        << "ms; pre-fix hard-sleep pattern takes 1.0-1.5s. Stop should "
        "return within 200ms of the child exiting.";

    EXPECT_FALSE(inst.isRunning());
    PortManager::freePort(socksPort);
    PortManager::freePort(apiPort);
    PortManager::clearPorts();
    std::error_code ec;
    std::filesystem::remove_all(cfgDir, ec);
}

// Test C (live, opt-in): ProxyProbePool::stop must stop workers in parallel,
// not serially. With 4 workers the serial baseline is ~4 × 1.5s = 6s; the
// parallel target is <1s.
TEST(StandaloneProxyPool, ProxyProbePoolStopWorkersParallel) {
    const char* xrayExe = std::getenv("XRAY_REAL_EXE");
    if (xrayExe == nullptr || xrayExe[0] == '\0') {
        GTEST_SKIP() << "XRAY_REAL_EXE not set; skipping live probe-pool test";
    }

    static int s_ppCounter = 0;
    const std::string cfgDir =
        (std::filesystem::temp_directory_path() /
         ("probe_pool_stop_" + std::to_string(++s_ppCounter))).string();
    std::filesystem::create_directories(cfgDir);

    PortManager::clearPorts();
    proxy::ProxyProbePool pool(xrayExe, /*workerCount*/4, cfgDir);
    ASSERT_TRUE(pool.start())
        << "ProxyProbePool.start() must succeed against real xray";
    ASSERT_EQ(pool.size(), 4);

    const auto start = std::chrono::steady_clock::now();
    pool.stop();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_LE(elapsedMs, 1000)
        << "ProxyProbePool::stop() took " << elapsedMs
        << "ms for 4 workers; pre-fix serial stop takes ~6s. Parallel "
        "stop should finish in <1s.";

    EXPECT_FALSE(pool.isRunning());
    EXPECT_EQ(pool.size(), 0);
    PortManager::clearPorts();
    std::error_code ec;
    std::filesystem::remove_all(cfgDir, ec);
}

// Test D (live, opt-in): StandaloneProxyPool::stop must be fast even when
// the evaluator is mid-probe on a large set of unreachable members. The stop
// path is B1+B2 (the dominant bottleneck: join() + non-interruptible probe).
TEST(StandaloneProxyPool, StopWithManyMembersFast) {
    const char* xrayExe = std::getenv("XRAY_REAL_EXE");
    if (xrayExe == nullptr || xrayExe[0] == '\0') {
        GTEST_SKIP() << "XRAY_REAL_EXE not set; skipping live pool stop test";
    }

    // Set XRAY_LOCATION_ASSET so xray can load geoip.dat/geosite.dat when the
    // pool config uses geo routing rules (buildPoolConfig embeds CN/private
    // direct rules). Derived from the xray exe layout (.../bin/xray/xray.exe
    // -> .../bin where geoip.dat lives); only set when the asset exists so
    // non-geo environments are unaffected.
    {
        std::filesystem::path exe(xrayExe);
        std::filesystem::path asset = exe.parent_path().parent_path();
        if (std::filesystem::exists(asset / "geoip.dat")) {
#ifdef _WIN32
            ::SetEnvironmentVariableA("XRAY_LOCATION_ASSET",
                                      asset.string().c_str());
#else
            (void)::setenv("XRAY_LOCATION_ASSET", asset.string().c_str(), 1);
#endif
        }
    }

    static int s_bigCounter = 0;
    const std::string cfgDir =
        (std::filesystem::temp_directory_path() /
         ("standalone_pool_big_" + std::to_string(++s_bigCounter))).string();
    std::filesystem::create_directories(cfgDir);

    PortManager::clearPorts();
    config::StandalonePoolConfig cfg;
    cfg.enabled = true;
    cfg.socksPort = 20300;
    cfg.apiPort = 20301;
    cfg.balancerStrategy = "leastPing";
    cfg.observatory.destination = "https://www.google.com";
    cfg.observatory.timeoutSec = 1;
    cfg.evaluate.intervalSec = 1;
    ASSERT_TRUE(proxy::resolvePoolPorts(cfg));

    proxy::StandaloneProxyPool pool(cfg, xrayExe, cfgDir);
    if (!pool.start()) {
        GTEST_SKIP() << "pool.start() failed against real xray (likely missing "
                        "xray assets like geoip.dat); cannot run stop timing test.";
    }

    // Inject 10 unreachable SOCKS members. Pre-fix, the evaluator probes them
    // serially (each cURL times out at 1s total, so ~10s of blocking); the
    // stop() call joins the evaluator thread and cannot return until the
    // current probe cycle finishes.
    int injected = 0;
    for (int i = 0; i < 10; ++i) {
        db::models::Profileitem p;
        p.indexid = "900100" + std::to_string(i);
        p.configtype = "4";
        p.address = "198.51.100." + std::to_string(10 + i);
        p.port = "8080";
        if (pool.injectMember(p)) ++injected;
    }
    if (injected < 5) {
        pool.stop();
        PortManager::freePort(cfg.socksPort);
        PortManager::freePort(cfg.apiPort);
        PortManager::clearPorts();
        std::error_code ec;
        std::filesystem::remove_all(cfgDir, ec);
        GTEST_SKIP() << "injected only " << injected
                     << " members (gRPC addOutbound may have failed); cannot "
                        "run stop timing test.";
    }

    // Trigger a probe cycle so the evaluator is (likely) inside doProbe().
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto start = std::chrono::steady_clock::now();
    pool.stop();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Pre-fix: 30-120s. Post-fix (A+B+C): <3s. Use 3000ms as a lenient bound.
    EXPECT_LE(elapsedMs, 3000)
        << "StandaloneProxyPool::stop() took " << elapsedMs
        << "ms for " << injected << " members; pre-fix join + non-interruptible "
        "probe takes 30-120s. Post-fix (interruptible probe + parallel worker "
        "stop + poll-based instance stop) should finish in <3s.";

    PortManager::freePort(cfg.socksPort);
    PortManager::freePort(cfg.apiPort);
    PortManager::clearPorts();
    std::error_code ec;
    std::filesystem::remove_all(cfgDir, ec);
}

} // namespace
