// Unit tests for the pure adoption policy helpers.
//
// The policy is the "which port do we adopt, and is it trustworthy" decision:
//   chain   = runtime history -> command-line config -> config dir -> live OS
//   D2 gate = classify the chain-selected candidate against the live facts
//
// Pure/unit-testable: no process, no socket, no database.

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <vector>

#include "StandaloneAdoptionPolicy.h"

namespace {

using standalone_adoption::PortCandidate;
using standalone_adoption::PortResolution;
using standalone_adoption::PortSource;
using standalone_adoption::PortVerdict;
using standalone_adoption::buildOsCandidates;
using standalone_adoption::classifyPortCandidate;
using standalone_adoption::containsPort;
using standalone_adoption::orderOsCandidatesBySocks;
using standalone_adoption::portSourceLabel;
using standalone_adoption::portVerdictLabel;
using standalone_adoption::resolveChainPort;
using standalone_adoption::RecordVerdict;
using standalone_adoption::ProxyRecordSnapshot;
using standalone_adoption::RecordCompareResult;
using standalone_adoption::compareProxyRecord;
using standalone_adoption::recordVerdictLabel;

const int kApiPort = 10085;

// ---------------------------------------------------------------------------
// containsPort
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, ContainsPort_FindsExistingPort) {
    const std::vector<int> ports;
    std::vector<int> list;
    list.push_back(10808);
    list.push_back(10809);
    EXPECT_TRUE(containsPort(list, 10808));
    EXPECT_TRUE(containsPort(list, 10809));
    EXPECT_FALSE(containsPort(list, 10810));
    EXPECT_FALSE(containsPort(list, 0));
    EXPECT_FALSE(containsPort(ports, 10808));
}

// ---------------------------------------------------------------------------
// classifyPortCandidate -- the D2 verdict
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, Classify_RejectsOutOfRange) {
    std::vector<int> osPorts;
    osPorts.push_back(10808);
    EXPECT_EQ(PortVerdict::Disproven, classifyPortCandidate(0, osPorts, osPorts, kApiPort, true));
    EXPECT_EQ(PortVerdict::Disproven, classifyPortCandidate(-1, osPorts, osPorts, kApiPort, true));
    EXPECT_EQ(PortVerdict::Disproven, classifyPortCandidate(70000, osPorts, osPorts, kApiPort, true));
}

TEST(StandaloneAdoptionPolicyTest, Classify_RejectsApiPort) {
    std::vector<int> osPorts;
    osPorts.push_back(kApiPort);
    std::vector<int> socksPorts;
    // The api inbound answers nothing, but even if it did it must never be
    // adopted as the SOCKS port.
    EXPECT_EQ(PortVerdict::Disproven, classifyPortCandidate(kApiPort, osPorts, socksPorts, kApiPort, true));
}

TEST(StandaloneAdoptionPolicyTest, Classify_UnknownWhenNoLiveFacts) {
    // No live evidence at all: the candidate can neither be confirmed nor
    // disproven -> Unknown (accept, do not drop a healthy proxy).
    const std::vector<int> emptyOs;
    const std::vector<int> emptySocks;
    EXPECT_EQ(PortVerdict::Unknown, classifyPortCandidate(10808, emptyOs, emptySocks, kApiPort, false));
}

TEST(StandaloneAdoptionPolicyTest, Classify_DisprovenWhenPortNotOwnedByPid) {
    std::vector<int> osPorts;
    osPorts.push_back(10809);
    std::vector<int> socksPorts;
    socksPorts.push_back(10809);
    // 10810 is the stale recorded port; the pid listens on 10809 only.
    EXPECT_EQ(PortVerdict::Disproven, classifyPortCandidate(10810, osPorts, socksPorts, kApiPort, true));
}

TEST(StandaloneAdoptionPolicyTest, Classify_VerifiedWhenSocksAnswered) {
    std::vector<int> osPorts;
    osPorts.push_back(10808);
    std::vector<int> socksPorts;
    socksPorts.push_back(10808);
    EXPECT_EQ(PortVerdict::Verified, classifyPortCandidate(10808, osPorts, socksPorts, kApiPort, true));
}

TEST(StandaloneAdoptionPolicyTest, Classify_UnverifiedWhenListeningButHandshakeInconclusive) {
    std::vector<int> osPorts;
    osPorts.push_back(10808);
    std::vector<int> socksPorts;
    EXPECT_EQ(PortVerdict::Unverified, classifyPortCandidate(10808, osPorts, socksPorts, kApiPort, true));
}

// ---------------------------------------------------------------------------
// buildOsCandidates
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, BuildOsCandidates_ExcludesApiPort) {
    std::vector<int> osPorts;
    osPorts.push_back(10808);
    osPorts.push_back(kApiPort);
    const std::vector<int> candidates = buildOsCandidates(osPorts, kApiPort);
    ASSERT_EQ(static_cast<size_t>(1), candidates.size());
    EXPECT_EQ(10808, candidates[0]);
}

TEST(StandaloneAdoptionPolicyTest, BuildOsCandidates_FiltersInvalidAndDeduplicates) {
    std::vector<int> osPorts;
    osPorts.push_back(0);
    osPorts.push_back(10809);
    osPorts.push_back(-5);
    osPorts.push_back(10808);
    osPorts.push_back(10809);
    osPorts.push_back(70000);
    const std::vector<int> candidates = buildOsCandidates(osPorts, kApiPort);
    ASSERT_EQ(static_cast<size_t>(2), candidates.size());
    EXPECT_EQ(10808, candidates[0]);
    EXPECT_EQ(10809, candidates[1]);
}

TEST(StandaloneAdoptionPolicyTest, BuildOsCandidates_EmptyWhenOnlyApiPort) {
    std::vector<int> osPorts;
    osPorts.push_back(kApiPort);
    EXPECT_TRUE(buildOsCandidates(osPorts, kApiPort).empty());
}

// ---------------------------------------------------------------------------
// orderOsCandidatesBySocks
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, OrderOsCandidates_PutsSocksAnsweringFirst) {
    std::vector<int> candidates;
    candidates.push_back(10809);
    candidates.push_back(10808);
    std::vector<int> socksPorts;
    socksPorts.push_back(10808);
    const std::vector<int> ordered = orderOsCandidatesBySocks(candidates, socksPorts);
    ASSERT_EQ(static_cast<size_t>(2), ordered.size());
    EXPECT_EQ(10808, ordered[0]);
    EXPECT_EQ(10809, ordered[1]);
}

TEST(StandaloneAdoptionPolicyTest, OrderOsCandidates_KeepsRemainderAscending) {
    std::vector<int> candidates;
    candidates.push_back(10810);
    candidates.push_back(10807);
    candidates.push_back(10808);
    std::vector<int> socksPorts;
    socksPorts.push_back(10808);
    const std::vector<int> ordered = orderOsCandidatesBySocks(candidates, socksPorts);
    ASSERT_EQ(static_cast<size_t>(3), ordered.size());
    EXPECT_EQ(10808, ordered[0]);
    EXPECT_EQ(10807, ordered[1]);
    EXPECT_EQ(10810, ordered[2]);
}

TEST(StandaloneAdoptionPolicyTest, OrderOsCandidates_EmptyInput) {
    const std::vector<int> empty;
    const std::vector<int> socksPorts;
    EXPECT_TRUE(orderOsCandidatesBySocks(empty, socksPorts).empty());
}

// ---------------------------------------------------------------------------
// resolveChainPort -- chain order + D2 gate + fall-through
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, ResolveChain_TakesFirstCandidate) {
    std::vector<PortCandidate> chain;
    PortCandidate history;
    history.port = 10809;
    history.source = PortSource::RuntimeHistory;
    chain.push_back(history);
    std::vector<int> osPorts;
    osPorts.push_back(10809);
    std::vector<int> socksPorts;
    socksPorts.push_back(10809);
    const PortResolution result = resolveChainPort(chain, osPorts, socksPorts, kApiPort, true);
    EXPECT_EQ(10809, result.port);
    EXPECT_EQ(PortSource::RuntimeHistory, result.source);
    EXPECT_EQ(PortVerdict::Verified, result.verdict);
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_SkipsDisprovenAndFallsThrough) {
    // Core v1.3 case: the recorded history port is stale (10810) while the pid
    // listens on 10809 -> the stale value is disproven and the chain falls
    // through to the live OS candidate.
    std::vector<PortCandidate> chain;
    PortCandidate history;
    history.port = 10810;
    history.source = PortSource::RuntimeHistory;
    chain.push_back(history);
    PortCandidate live;
    live.port = 10809;
    live.source = PortSource::OsTcpTable;
    chain.push_back(live);

    std::vector<int> osPorts;
    osPorts.push_back(10809);
    std::vector<int> socksPorts;
    socksPorts.push_back(10809);

    const PortResolution result = resolveChainPort(chain, osPorts, socksPorts, kApiPort, true);
    EXPECT_EQ(10809, result.port);
    EXPECT_EQ(PortSource::OsTcpTable, result.source);
    EXPECT_EQ(PortVerdict::Verified, result.verdict);
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_SkipsZeroCandidates) {
    std::vector<PortCandidate> chain;
    PortCandidate empty;
    empty.port = 0;
    empty.source = PortSource::RuntimeHistory;
    chain.push_back(empty);
    PortCandidate live;
    live.port = 10809;
    live.source = PortSource::OsTcpTable;
    chain.push_back(live);

    std::vector<int> osPorts;
    osPorts.push_back(10809);
    const PortResolution result = resolveChainPort(chain, osPorts, osPorts, kApiPort, true);
    EXPECT_EQ(10809, result.port);
    EXPECT_EQ(PortSource::OsTcpTable, result.source);
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_AllZeroYieldsNoCandidate) {
    std::vector<PortCandidate> chain;
    PortCandidate empty;
    empty.port = 0;
    empty.source = PortSource::DirProbe;
    chain.push_back(empty);
    const std::vector<int> noFacts;
    const PortResolution result = resolveChainPort(chain, noFacts, noFacts, kApiPort, false);
    EXPECT_EQ(0, result.port);
    EXPECT_EQ(PortSource::None, result.source);
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_AllDisprovenYieldsNoCandidate) {
    std::vector<PortCandidate> chain;
    PortCandidate stale;
    stale.port = 10810;
    stale.source = PortSource::RuntimeHistory;
    chain.push_back(stale);
    PortCandidate staleCfg;
    staleCfg.port = 10811;
    staleCfg.source = PortSource::CmdlineConfig;
    chain.push_back(staleCfg);

    std::vector<int> osPorts;
    osPorts.push_back(10809);
    std::vector<int> socksPorts;
    socksPorts.push_back(10809);

    const PortResolution result = resolveChainPort(chain, osPorts, socksPorts, kApiPort, true);
    EXPECT_EQ(0, result.port);
    EXPECT_EQ(PortSource::None, result.source);
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_PassesThroughWhenNoLiveFacts) {
    // Regression guard: with no live evidence the recorded value must be
    // accepted (verdict Unknown), never rejected.
    std::vector<PortCandidate> chain;
    PortCandidate history;
    history.port = 10809;
    history.source = PortSource::RuntimeHistory;
    chain.push_back(history);
    const std::vector<int> noFacts;
    const PortResolution result = resolveChainPort(chain, noFacts, noFacts, kApiPort, false);
    EXPECT_EQ(10809, result.port);
    EXPECT_EQ(PortSource::RuntimeHistory, result.source);
    EXPECT_EQ(PortVerdict::Unknown, result.verdict);
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_RejectsApiPortCandidate) {
    std::vector<PortCandidate> chain;
    PortCandidate api;
    api.port = kApiPort;
    api.source = PortSource::OsTcpTable;
    chain.push_back(api);
    std::vector<int> osPorts;
    osPorts.push_back(kApiPort);
    const PortResolution result = resolveChainPort(chain, osPorts, osPorts, kApiPort, true);
    EXPECT_EQ(0, result.port);
    EXPECT_EQ(PortSource::None, result.source);
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_AcceptsUnverifiedCandidate) {
    // Listening but the handshake was inconclusive -> accept (with a warning),
    // never drop the proxy.
    std::vector<PortCandidate> chain;
    PortCandidate history;
    history.port = 10808;
    history.source = PortSource::RuntimeHistory;
    chain.push_back(history);
    std::vector<int> osPorts;
    osPorts.push_back(10808);
    const std::vector<int> noSocks;
    const PortResolution result = resolveChainPort(chain, osPorts, noSocks, kApiPort, true);
    EXPECT_EQ(10808, result.port);
    EXPECT_EQ(PortVerdict::Unverified, result.verdict);
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_EmptyChainYieldsNoCandidate) {
    const std::vector<PortCandidate> chain;
    std::vector<int> osPorts;
    osPorts.push_back(10808);
    const PortResolution result = resolveChainPort(chain, osPorts, osPorts, kApiPort, true);
    EXPECT_EQ(0, result.port);
    EXPECT_EQ(PortSource::None, result.source);
}

// ---------------------------------------------------------------------------
// Labels
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, PortSourceLabel_AllValues) {
    EXPECT_STREQ("none", portSourceLabel(PortSource::None));
    EXPECT_STREQ("runtime-history", portSourceLabel(PortSource::RuntimeHistory));
    EXPECT_STREQ("cmdline-config", portSourceLabel(PortSource::CmdlineConfig));
    EXPECT_STREQ("dir-probe", portSourceLabel(PortSource::DirProbe));
    EXPECT_STREQ("os-tcp-table", portSourceLabel(PortSource::OsTcpTable));
}

TEST(StandaloneAdoptionPolicyTest, PortVerdictLabel_AllValues) {
    EXPECT_STREQ("disproven", portVerdictLabel(PortVerdict::Disproven));
    EXPECT_STREQ("unknown", portVerdictLabel(PortVerdict::Unknown));
    EXPECT_STREQ("unverified", portVerdictLabel(PortVerdict::Unverified));
    EXPECT_STREQ("verified", portVerdictLabel(PortVerdict::Verified));
}

// ---------------------------------------------------------------------------
// D5 instance identity gate
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, CheckInstance_MatchWhenTimestampsEqual) {
    EXPECT_EQ(standalone_adoption::InstanceCheck::Match,
              standalone_adoption::checkInstanceIdentity("2026-10-21 08:39:08",
                                    "2026-10-21 08:39:08"));
}

TEST(StandaloneAdoptionPolicyTest, CheckInstance_RecycledWhenTimestampsDiffer) {
    EXPECT_EQ(standalone_adoption::InstanceCheck::Recycled,
              standalone_adoption::checkInstanceIdentity("2026-10-21 08:39:08",
                                    "2026-10-21 08:41:52"));
}

TEST(StandaloneAdoptionPolicyTest, CheckInstance_UnverifiableWhenEnumeratedTimeEmpty) {
    // An unreadable creation time is NOT evidence of a mismatch: the caller
    // must keep the process instead of dropping a healthy proxy.
    EXPECT_EQ(standalone_adoption::InstanceCheck::Unverifiable,
              standalone_adoption::checkInstanceIdentity("", "2026-10-21 08:39:08"));
}

TEST(StandaloneAdoptionPolicyTest, CheckInstance_UnverifiableWhenHandleTimeEmpty) {
    EXPECT_EQ(standalone_adoption::InstanceCheck::Unverifiable,
              standalone_adoption::checkInstanceIdentity("2026-10-21 08:39:08", ""));
}

TEST(StandaloneAdoptionPolicyTest, CheckInstance_UnverifiableWhenBothEmpty) {
    EXPECT_EQ(standalone_adoption::InstanceCheck::Unverifiable, standalone_adoption::checkInstanceIdentity("", ""));
}

TEST(StandaloneAdoptionPolicyTest, InstanceCheckLabel_AllValues) {
    EXPECT_STREQ("match", standalone_adoption::instanceCheckLabel(standalone_adoption::InstanceCheck::Match));
    EXPECT_STREQ("recycled", standalone_adoption::instanceCheckLabel(standalone_adoption::InstanceCheck::Recycled));
    EXPECT_STREQ("unverifiable", standalone_adoption::instanceCheckLabel(standalone_adoption::InstanceCheck::Unverifiable));
}

// ---------------------------------------------------------------------------
// v1.6 adoption routing (bypass the live detection for proxies started by
// this session)
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, Route_SkipsForeignProxyAlreadyManaged) {
    EXPECT_EQ(standalone_adoption::AdoptionRoute::Skip,
              standalone_adoption::decideAdoptionRoute(true, false));
}

TEST(StandaloneAdoptionPolicyTest, Route_FullDetectsForeignProxyNotManaged) {
    EXPECT_EQ(standalone_adoption::AdoptionRoute::FullDetect,
              standalone_adoption::decideAdoptionRoute(false, false));
}

TEST(StandaloneAdoptionPolicyTest, Route_BypassesDetectForSessionStartedProxy) {
    // Started by this session but not yet in the registry: the live
    // detection is pointless, the port is already known.
    EXPECT_EQ(standalone_adoption::AdoptionRoute::BypassDetect,
              standalone_adoption::decideAdoptionRoute(false, true));
}

TEST(StandaloneAdoptionPolicyTest, Route_BypassesDetectEvenWhenAlreadyManaged) {
    // Already managed AND started by this session: the registry entry may
    // have lost its runtime history row (e.g. after a database switch), so
    // the sweep must still re-write the history instead of skipping.
    EXPECT_EQ(standalone_adoption::AdoptionRoute::BypassDetect,
              standalone_adoption::decideAdoptionRoute(true, true));
}

TEST(StandaloneAdoptionPolicyTest, BypassPort_PrefersHistoryPort) {
    EXPECT_EQ(10808, standalone_adoption::resolveBypassPort(10808, 10809, 10810));
}

TEST(StandaloneAdoptionPolicyTest, BypassPort_FallsBackToMemoryPort) {
    EXPECT_EQ(10809, standalone_adoption::resolveBypassPort(0, 10809, 10810));
}

TEST(StandaloneAdoptionPolicyTest, BypassPort_FallsBackToSessionPort) {
    EXPECT_EQ(10810, standalone_adoption::resolveBypassPort(0, 0, 10810));
}

TEST(StandaloneAdoptionPolicyTest, BypassPort_ZeroWhenNoSourceHasPort) {
    EXPECT_EQ(0, standalone_adoption::resolveBypassPort(0, 0, 0));
}

TEST(StandaloneAdoptionPolicyTest, BypassPort_IgnoresNonPositiveValues) {
    EXPECT_EQ(10809, standalone_adoption::resolveBypassPort(-1, 10809, -5));
}

TEST(StandaloneAdoptionPolicyTest, AdoptionRouteLabel_AllValues) {
    EXPECT_STREQ("skip", standalone_adoption::adoptionRouteLabel(standalone_adoption::AdoptionRoute::Skip));
    EXPECT_STREQ("bypass-detect",
                 standalone_adoption::adoptionRouteLabel(standalone_adoption::AdoptionRoute::BypassDetect));
    EXPECT_STREQ("full-detect",
                 standalone_adoption::adoptionRouteLabel(standalone_adoption::AdoptionRoute::FullDetect));
}

// ---------------------------------------------------------------------------
// v1.7: D-C — the OS facts carry their own readability.
//
// "the TCP table could not be enumerated" and "the pid owns no LISTEN port"
// are two different situations and must not lead to the same verdict.
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, Classify_UnknownWhenFactsUnreadable) {
    const std::vector<int> osPorts(1, 10808);
    const std::vector<int> socksPorts(1, 10808);
    EXPECT_EQ(PortVerdict::Unknown,
              classifyPortCandidate(10808, osPorts, socksPorts, kApiPort, false));
}

TEST(StandaloneAdoptionPolicyTest, Classify_DisprovenWhenTableReadableAndPidHasNoListener) {
    const std::vector<int> emptyOs;
    const std::vector<int> emptySocks;
    EXPECT_EQ(PortVerdict::Disproven,
              classifyPortCandidate(10808, emptyOs, emptySocks, kApiPort, true));
}

TEST(StandaloneAdoptionPolicyTest, Classify_UnknownWhenFactsUnreadableAndOsPortsEmpty) {
    const std::vector<int> emptyOs;
    const std::vector<int> emptySocks;
    EXPECT_EQ(PortVerdict::Unknown,
              classifyPortCandidate(10808, emptyOs, emptySocks, kApiPort, false));
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_DisprovesAllWhenTableReadableAndPidHasNoListener) {
    std::vector<PortCandidate> chain;
    PortCandidate cand;
    cand.port = 10808;
    cand.source = PortSource::RuntimeHistory;
    chain.push_back(cand);
    const std::vector<int> emptyOs;
    const std::vector<int> emptySocks;
    const PortResolution result =
        resolveChainPort(chain, emptyOs, emptySocks, kApiPort, true);
    EXPECT_EQ(0, result.port);
    EXPECT_EQ(PortSource::None, result.source);
}

TEST(StandaloneAdoptionPolicyTest, ResolveChain_PassesThroughWhenTableUnreadable) {
    std::vector<PortCandidate> chain;
    PortCandidate cand;
    cand.port = 10808;
    cand.source = PortSource::RuntimeHistory;
    chain.push_back(cand);
    const std::vector<int> emptyOs;
    const std::vector<int> emptySocks;
    const PortResolution result =
        resolveChainPort(chain, emptyOs, emptySocks, kApiPort, false);
    EXPECT_EQ(10808, result.port);
    EXPECT_EQ(PortSource::RuntimeHistory, result.source);
    EXPECT_EQ(PortVerdict::Unknown, result.verdict);
}

// ---------------------------------------------------------------------------
// v1.7: D-B — the discard fingerprint.
//
// The empty-string convention is the opposite of checkInstanceIdentity: an
// empty fingerprint means "no evidence" and counts as the SAME instance so
// the log level does not flap.
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, Instance_SameFingerprint_IsSame) {
    EXPECT_TRUE(standalone_adoption::isSameProcessFingerprint(
        "2026-10-21 08:39:08", "2026-10-21 08:39:08"));
}

TEST(StandaloneAdoptionPolicyTest, Instance_DifferentFingerprint_IsNotSame) {
    EXPECT_FALSE(standalone_adoption::isSameProcessFingerprint(
        "2026-10-21 08:39:08", "2026-10-21 09:12:44"));
}

TEST(StandaloneAdoptionPolicyTest, Instance_EmptyFingerprint_IsSame) {
    EXPECT_TRUE(standalone_adoption::isSameProcessFingerprint("", "2026-10-21 08:39:08"));
    EXPECT_TRUE(standalone_adoption::isSameProcessFingerprint("2026-10-21 08:39:08", ""));
    EXPECT_TRUE(standalone_adoption::isSameProcessFingerprint("", ""));
}

// ---------------------------------------------------------------------------
// v1.7: D-A1 — the adoption in-flight guard.
//
// The flag lives on the caller's side so this stays a pure function.
// ---------------------------------------------------------------------------
TEST(StandaloneAdoptionPolicyTest, AdoptionGate_AcquiresWhenFree) {
    std::atomic<bool> flag(false);
    EXPECT_TRUE(standalone_adoption::tryBeginAdoption(flag));
    EXPECT_TRUE(flag.load());
}

TEST(StandaloneAdoptionPolicyTest, AdoptionGate_RejectsWhenHeld) {
    std::atomic<bool> flag(false);
    EXPECT_TRUE(standalone_adoption::tryBeginAdoption(flag));
    EXPECT_FALSE(standalone_adoption::tryBeginAdoption(flag));
}

TEST(StandaloneAdoptionPolicyTest, AdoptionGate_ReleasesThenAcquires) {
    std::atomic<bool> flag(false);
    EXPECT_TRUE(standalone_adoption::tryBeginAdoption(flag));
    flag.store(false);
    EXPECT_TRUE(standalone_adoption::tryBeginAdoption(flag));
}

// ---------------------------------------------------------------------------
// v1.8: config / database record cross-check.
//
// The comparison is diagnostic only: a mismatch must never block the
// adoption, it only makes a stale configuration visible.
// ---------------------------------------------------------------------------
namespace {

ProxyRecordSnapshot makeSnapshot(const std::string& protocol,
                                 const std::string& address,
                                 const std::string& port,
                                 const std::string& userId) {
    ProxyRecordSnapshot snapshot;
    snapshot.protocol = protocol;
    snapshot.address = address;
    snapshot.port = port;
    snapshot.userId = userId;
    return snapshot;
}

} // namespace

TEST(StandaloneAdoptionPolicyTest, RecordCheck_MatchWhenAllRequiredEqual) {
    const ProxyRecordSnapshot cfg =
        makeSnapshot("vless", "example.com", "443", "uuid-1");
    const ProxyRecordSnapshot db =
        makeSnapshot("vless", "example.com", "443", "uuid-1");
    const RecordCompareResult result = compareProxyRecord(cfg, db);
    EXPECT_EQ(RecordVerdict::Match, result.verdict);
    EXPECT_TRUE(result.mismatchedFields.empty());
}

TEST(StandaloneAdoptionPolicyTest, RecordCheck_MismatchWhenProtocolDiffers) {
    const ProxyRecordSnapshot cfg =
        makeSnapshot("vless", "example.com", "443", "uuid-1");
    const ProxyRecordSnapshot db =
        makeSnapshot("vmess", "example.com", "443", "uuid-1");
    const RecordCompareResult result = compareProxyRecord(cfg, db);
    EXPECT_EQ(RecordVerdict::Mismatch, result.verdict);
    ASSERT_EQ(1u, result.mismatchedFields.size());
    EXPECT_EQ("protocol", result.mismatchedFields[0]);
}

TEST(StandaloneAdoptionPolicyTest, RecordCheck_MismatchWhenAddressDiffers) {
    const ProxyRecordSnapshot cfg =
        makeSnapshot("trojan", "old.example.com", "443", "pw-1");
    const ProxyRecordSnapshot db =
        makeSnapshot("trojan", "new.example.com", "443", "pw-1");
    const RecordCompareResult result = compareProxyRecord(cfg, db);
    EXPECT_EQ(RecordVerdict::Mismatch, result.verdict);
    ASSERT_EQ(1u, result.mismatchedFields.size());
    EXPECT_EQ("address", result.mismatchedFields[0]);
}

TEST(StandaloneAdoptionPolicyTest, RecordCheck_MismatchWhenPortDiffers) {
    const ProxyRecordSnapshot cfg =
        makeSnapshot("vless", "example.com", "443", "uuid-1");
    const ProxyRecordSnapshot db =
        makeSnapshot("vless", "example.com", "8443", "uuid-1");
    const RecordCompareResult result = compareProxyRecord(cfg, db);
    EXPECT_EQ(RecordVerdict::Mismatch, result.verdict);
    ASSERT_EQ(1u, result.mismatchedFields.size());
    EXPECT_EQ("port", result.mismatchedFields[0]);
}

TEST(StandaloneAdoptionPolicyTest, RecordCheck_MismatchWhenUserIdDiffers) {
    const ProxyRecordSnapshot cfg =
        makeSnapshot("vless", "example.com", "443", "uuid-old");
    const ProxyRecordSnapshot db =
        makeSnapshot("vless", "example.com", "443", "uuid-new");
    const RecordCompareResult result = compareProxyRecord(cfg, db);
    EXPECT_EQ(RecordVerdict::Mismatch, result.verdict);
    ASSERT_EQ(1u, result.mismatchedFields.size());
    EXPECT_EQ("userId", result.mismatchedFields[0]);
}

TEST(StandaloneAdoptionPolicyTest, RecordCheck_MismatchWhenRealityPublicKeyDiffers) {
    ProxyRecordSnapshot cfg =
        makeSnapshot("vless", "example.com", "443", "uuid-1");
    ProxyRecordSnapshot db =
        makeSnapshot("vless", "example.com", "443", "uuid-1");
    cfg.publicKey = "pbk-old";
    db.publicKey = "pbk-new";
    const RecordCompareResult result = compareProxyRecord(cfg, db);
    EXPECT_EQ(RecordVerdict::Mismatch, result.verdict);
    ASSERT_EQ(1u, result.mismatchedFields.size());
    EXPECT_EQ("publicKey", result.mismatchedFields[0]);
}

TEST(StandaloneAdoptionPolicyTest, RecordCheck_IgnoresOptionalFieldMissingOnOneSide) {
    ProxyRecordSnapshot cfg =
        makeSnapshot("vless", "example.com", "443", "uuid-1");
    const ProxyRecordSnapshot db =
        makeSnapshot("vless", "example.com", "443", "uuid-1");
    cfg.requestHost = "cdn.example.com";
    cfg.publicKey = "pbk-1";
    const RecordCompareResult result = compareProxyRecord(cfg, db);
    EXPECT_EQ(RecordVerdict::Match, result.verdict);
    EXPECT_TRUE(result.mismatchedFields.empty());
}

TEST(StandaloneAdoptionPolicyTest, RecordCheck_UnverifiableWhenRequiredFieldEmpty) {
    const ProxyRecordSnapshot cfg =
        makeSnapshot("vless", "example.com", "443", "uuid-1");
    const ProxyRecordSnapshot db =
        makeSnapshot("vless", "", "443", "uuid-1");
    const RecordCompareResult result = compareProxyRecord(cfg, db);
    EXPECT_EQ(RecordVerdict::Unverifiable, result.verdict);
    EXPECT_TRUE(result.mismatchedFields.empty());
}

TEST(StandaloneAdoptionPolicyTest, RecordVerdictLabel_AllValues) {
    EXPECT_STREQ("match", recordVerdictLabel(RecordVerdict::Match));
    EXPECT_STREQ("mismatch", recordVerdictLabel(RecordVerdict::Mismatch));
    EXPECT_STREQ("unverifiable", recordVerdictLabel(RecordVerdict::Unverifiable));
}

} // namespace
