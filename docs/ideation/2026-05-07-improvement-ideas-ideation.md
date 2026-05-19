---
date: 2026-05-07
topic: improvement-ideas
focus: surprise-me mode - discovered subjects from codebase
mode: repo-grounded
---

# Ideation: validproxy Improvement Ideas

## Grounding Context

**Codebase Context**:
- Project: validproxy, C++17/20, Windows-only, CMake/Ninja build
- Core: multithreaded proxy testing with xray-core, dedup + blacklist mechanism
- Recent work: Logger fixes (mutex, LOG_ERROR parse), ConfigReader type validation, resource leak fixes
- Strategy tracks: (1) Proxy testing engine, (2) Subscription management, (3) Config & logging
- Key metrics: valid proxy ratio, avg test time, network requests/test, blacklisted proxy count

**Interesting Subjects Discovered**:
1. Extract protocol parsers from 400-line parseSubscription() monolith
2. Add CURL RAII wrapper (raw handles without exception safety)
3. Implement config hot-reload via file watcher
4. Add proxy performance trend analytics (point-in-time only)
5. Unify grok_app/ parsers with main codebase

**Past Learnings**:
- Logger system evolved through multiple fixes (double timestamps, LOG_ERROR parse bug)
- ConfigReader initially only parsed database section, causing xray_executable to be empty
- Blacklist logic simplified by removing redundant blacklisted field
- Resource leaks fixed in main.cpp error paths

---

## Ranked Survivors

### 1. CURL RAII Wrapper ⭐⭐⭐⭐
**Description:** Replace raw `curl_easy_init/cleanup` calls with a RAII wrapper (CurlHandle class) that guarantees cleanup in destructor.

**Warrant:** `direct: AGENTS.md "resource leak fixes"; Phase 1 "raw CURL handles without exception safety"`

**Rationale:** Prevents handle leaks in multithreaded testing where exceptions/early-returns are common. Makes CURL usage exception-safe.

**Downsides:** Requires refactoring all CURL call sites (~42 instances)

**Confidence:** 95% | **Complexity:** Medium | **Status:** Unexplored

---

### 2. CURL Handle Pool ⭐⭐⭐
**Description:** Implement thread-local handle pool that reuses initialized CURL handles across tests.

**Warrant:** `reasoned: 5% per-test setup time reduction; handle churn in multithreaded environment`

**Rationale:** Reduces per-test setup time, lowers memory allocation overhead, improves overall throughput.

**Downsides:** Requires state reset logic between test uses

**Confidence:** 90% | **Complexity:** Medium | **Status:** Unexplored%

---

### 3. ShareLinkParser Class (Extract from Monolith) ⭐⭐⭐⭐⭐
**Description:** Break 400-line parseSubscription() into ShareLinkParser class with per-protocol methods.

**Warrant:** `direct: Phase 1 "parseSubscription() is monolithic"`

**Rationale:** Makes each protocol parser independently testable. Adding new protocols no longer requires modifying a 400-line function.

**Downsides:** Requires significant refactoring of existing code

**Confidence:** 95% | **Complexity:** High | **Status:** Unexplored%

---

### 4. Protocol Parser Registry (Plugin Architecture) ⭐⭐⭐⭐
**Description:** Replace monolithic parser with registry of per-protocol plugins implementing common interface.

**Warrant:** `reasoned: Open-closed principle; eliminates duplication with grok_app/`

**Rationale:** Reduces future protocol addition effort from modifying monolithic code to writing single plugin.

**Downsides:** Requires defining stable plugin interface

**Confidence:** 90% | **Complexity:** High | **Status:** Unexplored%

---

### 5. Proxy Performance Trend Analytics ⭐⭐⭐⭐
**Description:** Extend point-in-time latency tracking with rolling window of historical measurements stored in SQLite.

**Warrant:** `direct: Phase 1 "only point-in-time values tracked"`

**Rationale:** Identifies degrading proxies before they become unusable. Operators can distinguish stable proxies from flaky ones.

**Downsides:** Adds SQLite table and storage requirements

**Confidence:** 90% | **Complexity:** Medium | **Status:** Unexplored%

---

### 6. Config Hot-Reload via FileWatcher ⭐⭐⭐⭐
**Description:** Implement Windows ReadDirectoryChangesW or polling-based file watcher to detect config.json changes.

**Warrant:** `direct: Phase 1 "config loaded once at startup"`

**Rationale:** Eliminates restart requirement for enterprise users running long tests who need to tweak parameters.

**Downsides:** Adds background thread overhead (~1MB memory)

**Confidence:** 85% | **Complexity:** Medium | **Status:** Unexplored%

---

### 7. Live Reactive Config Object ⭐⭐⭐
**Description:** Reframe static startup config to live, observable object that triggers automatic pipeline reconfiguration.

**Warrant:** `reasoned: Enterprise users need frequent tuning without downtime`

**Rationale:** Removes need to restart tool for config changes, reduces operational overhead.

**Downsides:** Requires significant architecture change from static to reactive

**Confidence:** 80% | **Complexity:** High | **Status:** Unexplored%

---

### 8. Passive Xray Telemetry for Validation ⭐⭐⭐
**Description:** Eliminate active proxy testing by using passive xray-core connection logs to infer proxy validity.

**Warrant:** `reasoned: Xray-core already logs success/failure; active testing wastes 100% traffic for <1% valid ratio`

**Rationale:** Cuts all active test network requests, avoids triggering anti-scraping protections.

**Downsides:** Requires parsing xray-core logs; may miss proxies that xray never attempts

**Confidence:** 75% | **Complexity:** High | **Status:** Unexplored%

---

### 9. Raw Socket Proxy Handshake Validation ⭐⭐
**Description:** Remove xray-core dependency by implementing lightweight raw socket handshakes for common proxy protocols.

**Warrant:** `external: SOCKS5/HTTP/VMess handshake protocols are publicly documented`

**Rationale:** Reduces tool footprint by ~10MB, removes xray-core single point of failure.

**Downsides:** Reimplements functionality that xray-core already provides

**Confidence:** 70% | **Complexity:** High | **Status:** Unexplored%

---

### 10. Parser Unit Test Suite ⭐⭐⭐
**Description:** Build shared test suite for all protocol parsers using common test vectors.

**Warrant:** `reasoned: Monolithic parsers cannot be unit-tested per-protocol without extraction`

**Rationale:** Catches parsing regressions early, directly improves valid proxy ratio metric.

**Downsides:** Requires parser extraction first (idea #3 or #4)

**Confidence:** 90% | **Complexity:** Medium | **Status:** Unexplored%

---

### 11. Versioned Parser Profiles ⭐⭐
**Description:** Add versioned parser profiles for subscription protocols, detecting protocol version from metadata.

**Warrant:** `external: Chrome Blink renderer version detection analogy`

**Rationale:** Prevents parsing failures for legacy subscription formats still used by smaller providers.

**Downsides:** Increases parser complexity with version detection logic

**Confidence:** 80% | **Complexity:** Medium | **Status:** Unexplored%

---

### 12. Streaming Incremental Parsing ⭐⭐
**Description:** Reframe parseSubscription to emit results immediately as subscription payload downloads.

**Warrant:** `reasoned: Monolithic parseSubscription blocks testing until full payload downloaded`

**Rationale:** Reduces time-to-first-valid-proxy result for large subscriptions.

**Downsides:** Requires async streaming architecture change

**Confidence:** 80% | **Complexity:** High | **Status:** Unexplored%

---

### 13. Parser Caching (Persistent Across Runs) ⭐⭐⭐
**Description:** Store parsed proxy configs as SQLite BLOBs, caching parsing results across tool runs.

**Warrant:** `external: Compiler lexer token buffer caching analogy`

**Rationale:** Avoids redundant parsing for repeated test runs on same subscriptions, cutting startup time by 50%.

**Downsides:** Adds cache invalidation logic

**Confidence:** 85% | **Complexity:** Medium | **Status:** Unexplored%

---

### 14. CURL Metrics Instrumentation ⭐⭐⭐
**Description:** Add lightweight instrumentation to CURL RAII wrapper tracking per-handle metrics.

**Warrant:** `external: Network interface driver instrumentation analogy`

**Rationale:** Provides granular data to optimize test batches, enables automatic blacklisting.

**Downsides:** Adds slight overhead to each CURL operation

**Confidence:** 85% | **Complexity:** Medium | **Status:** Unexplored%

---

### 15. Modular Parser + CURL Pool Integration ⭐⭐⭐ (Cross-Cutting)
**Description:** Parser output feeds directly into CURL handle pool, creating unified validation pipeline.

**Warrant:** `reasoned: Parsers output configs that need CURL handles; unification reduces translation layer`

**Rationale:** Compounds value of parser modularity and CURL pool, creates seamless validation pipeline.

**Downsides:** Tight coupling between parser and testing modules

**Confidence:** 85% | **Complexity:** High | **Status:** Unexplored%

---

### 16. Zero-Copy Parallel ShareLinkParser ⭐⭐ (Constraint-Flip)
**Description:** Invert proxy count constraint to 1M, forcing lock-free parallel parsing across 100+ threads.

**Warrant:** `reasoned: 1M proxies require parsing throughput 1000x current levels`

**Rationale:** Enables processing 1M+ proxy share links in <10 seconds.

**Downsides:** Very high complexity, only relevant for extreme scale

**Confidence:** 70% | **Complexity:** Very High | **Status:** Unexplored%

---

## Rejection Summary

| # | Idea | Reason Rejected |
|---|------|-----------------|
| 1 | Async-Aware CURL Wrapper | too expensive relative to value (long-running test scenarios limited) |
| 2 | Generic URI Parser | duplicates idea #4 (Registry pattern more flexible), introduces external dependency |
| 3 | CLI Flag Overrides | subject-replacement (CLI tool → general CLI framework) |
| 4 | Config Schema Auto-Generation | too expensive (requires major build system changes) |
| 5 | Persistent Proxy Management Service | subject-replacement (CLI tool → background service) |
| 6 | Parser Fuzzing Harness | already covered by idea #10 (test suite includes malformed inputs) |

---

## Next Steps

**Recommended**: Pick idea #1 (CURL RAII Wrapper) or #3 (ShareLinkParser) for brainstorming - both have high confidence and directly serve strategy track 1 (Proxy testing engine).

**Phase 6 Options**:
1. **Refine in conversation** - add ideas, re-evaluate, or deepen analysis
2. **Open and iterate in Proof** - save to Proof for HTML review loop
3. **Brainstorm a selected idea** - load `ce-brainstorm` with chosen idea as seed
4. **Save and end** - persist to `docs/ideation/` (already done ✅)
