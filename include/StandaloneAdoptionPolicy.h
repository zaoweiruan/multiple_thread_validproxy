#ifndef STANDALONE_ADOPTION_POLICY_H
#define STANDALONE_ADOPTION_POLICY_H

// Pure policy helpers for dangling standalone-proxy adoption.
//
// Extracted from AppController so the port-priority decision can be unit
// tested without linking the full UI dependency chain (same approach as
// src/ui/UnifiedMonitorRows.h and include/FloatingWidgetPolicy.h).
//
// Resolution chain (spec v1.3):
//
//   1. proxy_runtime_history  (indexId + pid + started_at triple)
//   2. command-line config file, then the known config directory
//   3. live OS evidence (TCP table of the pid + SOCKS5 greeting)
//
// The live facts (osPorts / socksPorts) are collected ONCE up front and are
// used twice: to produce the OS tier candidates and to D2-validate whichever
// candidate the chain selects. The validation is a pure in-memory set
// comparison, so it costs no extra I/O.
//
// D2 verdict semantics (the "disproven" vs "unprovable" distinction matters):
//
//   Verified   - the port is owned by this pid AND answered a SOCKS5 greeting
//   Unverified - the port is owned by this pid, handshake inconclusive
//   Unknown    - no live facts available (OS table unreadable / no LISTEN)
//   Disproven  - the port is contradicted by the live facts
//
// Only a Disproven candidate is rejected (the chain then falls through to
// the next source). An Unverified / Unknown candidate is ACCEPTED (with a
// warning) because rejecting it would silently drop a healthy proxy.
//
// A result with port == 0 means "not resolvable" and the caller must discard
// the process instead of adopting it.

#include <algorithm>
#include <string>
#include <vector>

namespace standalone_adoption {

// Where the adopted SOCKS port came from.
enum class PortSource {
    None = 0,        // nothing resolved -> discard
    RuntimeHistory,  // socks_port column of proxy_runtime_history
    CmdlineConfig,   // config file located from the process command line
    DirProbe,        // config file found by probing the known config dir
    OsTcpTable       // live LISTEN port of the pid
};

// Outcome of validating one candidate against the live facts.
enum class PortVerdict {
    Disproven = 0,   // contradicted by the live facts -> reject this candidate
    Unknown,         // no live facts -> cannot verify -> accept (no warning)
    Unverified,      // listening but handshake inconclusive -> accept + warn
    Verified         // listening and SOCKS5 confirmed -> accept
};

// One candidate port offered by one resolution source.
struct PortCandidate {
    int port = 0;
    PortSource source = PortSource::None;
};

// The adopted port plus how it was found and how it was validated.
struct PortResolution {
    int port = 0;                        // 0 = not resolved
    PortSource source = PortSource::None;
    PortVerdict verdict = PortVerdict::Unknown;
};

// True when `port` is contained in `ports`.
inline bool containsPort(const std::vector<int>& ports, int port) {
    return std::find(ports.begin(), ports.end(), port) != ports.end();
}

// Classifies one candidate against the live facts.
//
//   candidate     - the port to validate
//   osPorts       - every LISTEN port owned by the pid (empty = no live facts)
//   socksPorts    - subset of osPorts that replied 0x05 to a SOCKS5 greeting
//   excludedPort  - the xray/sing-box api inbound port (0 = nothing excluded)
//
// Rules:
//   - out of range or equal to the api port        -> Disproven
//   - no live facts (osPorts empty)                -> Unknown (accept)
//   - not owned by this pid                         -> Disproven
//   - owned by this pid and SOCKS5 confirmed        -> Verified
//   - owned by this pid, handshake inconclusive     -> Unverified
// Pure/unit-testable.
inline PortVerdict classifyPortCandidate(int candidate,
                                         const std::vector<int>& osPorts,
                                         const std::vector<int>& socksPorts,
                                         int excludedPort) {
    if (candidate <= 0 || candidate > 65535) {
        return PortVerdict::Disproven;
    }
    if (excludedPort > 0 && candidate == excludedPort) {
        return PortVerdict::Disproven;
    }
    if (osPorts.empty()) {
        // No live evidence at all: nothing can be disproven.
        return PortVerdict::Unknown;
    }
    if (!containsPort(osPorts, candidate)) {
        // The pid does not listen on this port -> the value is stale.
        return PortVerdict::Disproven;
    }
    if (containsPort(socksPorts, candidate)) {
        return PortVerdict::Verified;
    }
    return PortVerdict::Unverified;
}

// Builds the OS tier candidate list: every valid LISTEN port of the pid
// except the api inbound, de-duplicated and sorted ascending.
// Pure/unit-testable.
inline std::vector<int> buildOsCandidates(const std::vector<int>& osPorts,
                                           int excludedPort) {
    std::vector<int> valid;
    for (std::vector<int>::const_iterator it = osPorts.begin();
         it != osPorts.end(); ++it) {
        const int port = *it;
        if (port <= 0 || port > 65535) {
            continue;
        }
        if (excludedPort > 0 && port == excludedPort) {
            continue;
        }
        if (!containsPort(valid, port)) {
            valid.push_back(port);
        }
    }
    std::sort(valid.begin(), valid.end());
    return valid;
}

// Orders the OS candidates so that the ports that answered the SOCKS5
// greeting come first (ascending), followed by the remaining ones (ascending).
// Pure/unit-testable.
inline std::vector<int> orderOsCandidatesBySocks(
    const std::vector<int>& candidates,
    const std::vector<int>& socksPorts) {
    std::vector<int> answering;
    std::vector<int> rest;
    for (std::vector<int>::const_iterator it = candidates.begin();
         it != candidates.end(); ++it) {
        if (containsPort(socksPorts, *it)) {
            answering.push_back(*it);
        } else {
            rest.push_back(*it);
        }
    }
    std::sort(answering.begin(), answering.end());
    std::sort(rest.begin(), rest.end());
    answering.insert(answering.end(), rest.begin(), rest.end());
    return answering;
}

// Walks the candidate chain in priority order and returns the first
// candidate that is not disproven by the live facts.
//
//   candidates    - chain in priority order (history, cmdline, dir, os...)
//   osPorts       - every LISTEN port owned by the pid
//   socksPorts    - subset that answered a SOCKS5 greeting
//   excludedPort  - the api inbound port to ignore
//
// A Disproven candidate is skipped and the chain falls through to the next
// source (this is what corrects a stale recorded port). Unknown / Unverified
// candidates are accepted so that a healthy proxy is never dropped.
// Pure/unit-testable.
inline PortResolution resolveChainPort(
    const std::vector<PortCandidate>& candidates,
    const std::vector<int>& osPorts,
    const std::vector<int>& socksPorts,
    int excludedPort) {
    PortResolution result;
    for (std::vector<PortCandidate>::const_iterator it = candidates.begin();
         it != candidates.end(); ++it) {
        const int port = it->port;
        if (port <= 0) {
            continue;
        }
        const PortVerdict verdict =
            classifyPortCandidate(port, osPorts, socksPorts, excludedPort);
        if (verdict == PortVerdict::Disproven) {
            continue;
        }
        result.port = port;
        result.source = it->source;
        result.verdict = verdict;
        return result;
    }
    return result;
}

// Stable label used in adoption log lines. Pure/unit-testable.
inline const char* portSourceLabel(PortSource source) {
    switch (source) {
        case PortSource::OsTcpTable:
            return "os-tcp-table";
        case PortSource::CmdlineConfig:
            return "cmdline-config";
        case PortSource::DirProbe:
            return "dir-probe";
        case PortSource::RuntimeHistory:
            return "runtime-history";
        case PortSource::None:
        default:
            return "none";
    }
}

// Stable label for the D2 verdict. Pure/unit-testable.
inline const char* portVerdictLabel(PortVerdict verdict) {
    switch (verdict) {
        case PortVerdict::Verified:
            return "verified";
        case PortVerdict::Unverified:
            return "unverified";
        case PortVerdict::Unknown:
            return "unknown";
        case PortVerdict::Disproven:
        default:
            return "disproven";
    }
}

// ---------------------------------------------------------------
// D5 instance identity gate (spec v1.5).
//
// The candidate process list is produced by enumerating the running
// xray / sing-box images. Between that enumeration and the adoption
// the original process may die and a NEW process may reuse the same
// pid. Comparing the creation timestamp observed during the
// enumeration with the one read from a freshly opened handle proves
// whether the handle still belongs to the enumerated instance.
//
// The check is a pure function so it can be unit-tested without a real
// process handle.
// ---------------------------------------------------------------
enum class InstanceCheck {
    Match = 0,   // same instance (both creation timestamps agree)
    Recycled,    // pid was reused -> the handle is a DIFFERENT instance
    Unverifiable // creation time unavailable -> identity cannot be proven
};

// Classifies the instance identity of an enumerated process.
//  - An empty timestamp on EITHER side means the identity cannot be
//    proven: the result is Unverifiable and the caller MUST NOT drop the
//    process (an unreadable creation time is NOT evidence of a mismatch).
//  - Two equal non-empty timestamps => Match.
//  - Two different non-empty timestamps => Recycled (pid reuse).
inline InstanceCheck checkInstanceIdentity(const std::string& enumeratedCreatedAt,
                                           const std::string& handleCreatedAt) {
    if (enumeratedCreatedAt.empty() || handleCreatedAt.empty()) {
        return InstanceCheck::Unverifiable;
    }
    return (enumeratedCreatedAt == handleCreatedAt) ? InstanceCheck::Match
                                                    : InstanceCheck::Recycled;
}

// Stable label for the D5 instance check result. Pure/unit-testable.
inline const char* instanceCheckLabel(InstanceCheck check) {
    switch (check) {
        case InstanceCheck::Match:
            return "match";
        case InstanceCheck::Recycled:
            return "recycled";
        case InstanceCheck::Unverifiable:
            return "unverifiable";
        default:
            return "unknown";
    }
}

// ---------------------------------------------------------------
// v1.6 adoption routing.
//
// The adoption sweep enumerates every running xray / sing-box image
// and has to decide what to do with each candidate:
//
//   Skip        - the proxy is already tracked by this application.
//                 Nothing to do (this is the historic behaviour).
//   BypassDetect- the proxy was started by THIS session, so the
//                 live detection (TCP table + SOCKS5 handshake) is
//                 pointless: we already know the port we asked for.
//                 The proxy is adopted / re-adopted directly and the
//                 runtime history row is (re)written.
//   FullDetect  - a foreign (dangling) proxy: run the full live
//                 detection and the D2 gate as usual.
//
// The routing decision is a pure function so it can be unit-tested
// without touching processes, handles or the database.
// ---------------------------------------------------------------
enum class AdoptionRoute {
    Skip = 0,      // already tracked -> nothing to do
    BypassDetect,  // started by this session -> skip live detection
    FullDetect     // foreign process -> full live detection
};

// Decides how the adoption sweep must treat one enumerated candidate.
//   alreadyManaged        - the indexId is already present in the
//                           standalone proxy registry (or has a watcher).
//   startedByThisSession  - the process was launched by this application
//                           during the current session.
//
// A proxy started by this session never goes through the live
// detection: its port is already known (it was written into the
// runtime history when the proxy was started), and the detection
// would only re-derive the very same value. It is still adopted /
// re-adopted so that the runtime history row exists (for example
// after the active database was switched).
inline AdoptionRoute decideAdoptionRoute(bool alreadyManaged,
                                         bool startedByThisSession) {
    if (startedByThisSession) {
        return AdoptionRoute::BypassDetect;
    }
    if (alreadyManaged) {
        return AdoptionRoute::Skip;
    }
    return AdoptionRoute::FullDetect;
}

// Resolves the SOCKS port for the bypass (no live detection) route.
// The sources are ordered from the most to the least authoritative:
//   1. the runtime history row of the live session (already verified)
//   2. the in-memory registry entry (already verified)
//   3. the port recorded when this session started the proxy
// Returns 0 when no source carries a usable port.
inline int resolveBypassPort(int historyPort, int memoryPort, int sessionPort) {
    if (historyPort > 0) {
        return historyPort;
    }
    if (memoryPort > 0) {
        return memoryPort;
    }
    if (sessionPort > 0) {
        return sessionPort;
    }
    return 0;
}

// Stable label for the adoption route. Pure/unit-testable.
inline const char* adoptionRouteLabel(AdoptionRoute route) {
    switch (route) {
        case AdoptionRoute::Skip:
            return "skip";
        case AdoptionRoute::BypassDetect:
            return "bypass-detect";
        case AdoptionRoute::FullDetect:
            return "full-detect";
        default:
            return "unknown";
    }
}

} // namespace standalone_adoption

#endif // STANDALONE_ADOPTION_POLICY_H
