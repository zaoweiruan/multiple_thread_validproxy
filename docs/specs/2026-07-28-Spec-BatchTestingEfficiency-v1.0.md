---
title: "Batch Testing Efficiency Optimization - gRPC API, async pipeline, batch DB writes"
type: spec
status: draft
date: 2026-07-28
---

# Batch Testing Efficiency Optimization Spec

## 1. Background & Problem Statement

### Requirements (User-Confirmed)

| Requirement | Decision | Notes |
|-------------|----------|-------|
| Xray minimum version | **v1.8.0+** | HandlerService gRPC stable from v1.8.0 |
| API implementation | **gRPC only** | Direct gRPC via HandlerService, no HTTP fallback |
| Feature flag | **Compile-time** | `#ifdef USE_GRPC_API` in CMakeLists.txt |
| DB flush interval | **Configurable** | Default 5s, `config.json` → `test.db_flush_interval_ms` |
| Worker count cap | **Configurable** | Auto-scale = `min(hardware_concurrency, config_max_workers)`, default max = 16 |

### 1.1 Current Behavior

`ProxyBatchTester` is the core engine for batch-testing proxy availability. For each proxy in the queue, a worker thread executes a sequential pipeline:

```
Phase 1: removeOutbound (×2, redundant)
  → subprocess CreateProcessA + WaitForSingleObject(5s) + 200ms sleep × 2
  → Fixed sleep loops: 100ms + 400ms
Phase 2: addOutbound (with 3× retry)
  → subprocess CreateProcessA + WaitForSingleObject(5s) + 200ms sleep
  → Fixed sleep loop: 300ms
Phase 3: curl SOCKS5 test
  → proxyTester_->test() — up to timeout_ms (default 2000ms)
Phase 4: DB write
  → exItemDao.updateTestResult() under dbMutex_
```

Each worker owns a single XrayInstance (separate xray.exe process). All workers share one `XrayManager` singleton, one `sqlite3* db_` handle, and one `dbMutex_`.

### 1.2 Per-Proxy Overhead Breakdown

| Component | Overhead | Notes |
|-----------|----------|-------|
| removeOutbound ×2 | 2 × subprocess ≈ 600-900ms | **Redundant double-call** |
| addOutbound | 1 × subprocess ≈ 300-500ms | With optional lso verification |
| Fixed sleeps | 1400ms total | Hardcoded 10ms loops between API calls |
| Subtotal (API + sleep) | **~1700-2300ms** | BEFORE actual curl test |
| curl test | 200-2000ms | Depends on server response |
| DB write | 1-5ms | Under mutex serialization |
| **Total per proxy** | **~2000-4300ms** | |

### 1.3 Measured Impact

With config `xray.workers=4`, testing 100 proxies:
- **Current**: 4 workers × 25 proxies × ~3500ms = **~87.5 seconds**
- **With Xray warmup**: +2s (500ms × 4 instances)
- Testing 500 proxies: **~7+ minutes**

### 1.4 Root Causes

| # | Bottleneck | Location | Impact |
|---|-----------|----------|--------|
| B1 | **Subprocess-based XrayApi** | `XrayApi.cpp` — `runProcess()`: CreateProcessA + pipe I/O + WaitForSingleObject(5s) | 3 subprocess calls per proxy, each 300-500ms. **Primary bottleneck.** |
| B2 | **Redundant double removeOutbound** | `ProxyBatchTester.cpp:617-646` — calls removeOutbound twice with identical tag | Doubles Phase 1 overhead for no benefit |
| B3 | **Hardcoded sleep durations** | `ProxyBatchTester.cpp` — 100ms, 300ms, 400ms fixed sleep loops | 1400ms per proxy wasted on polling, not event-driven |
| B4 | **Serial DB writes** | `ProxyBatchTester.cpp` — per-proxy `updateTestResult()` under `dbMutex_` | Worker threads block on DB lock; `updateTestResultBatch()` exists but unused |
| B5 | **Low worker count** | `config.json`: `"workers": 4` | Under-utilizes multi-core CPUs (user has 6-8+ cores) |
| B6 | **No Xray config reuse** | Each proxy test generates fresh outbound JSON, sends via subprocess | Config change is cheap; subprocess invocation is not |

---

## 2. Optimization Proposals

### 2.1 Phase A: gRPC Direct Client (High Impact, Medium Risk)

**Goal**: Replace subprocess-based `XrayApi` with direct gRPC calls to running Xray instances.

**Current flow** (per API call):
```
C++ → CreateProcessA("xray.exe api ...") → pipe stdin JSON → WaitForSingleObject(5s) → read pipe stdout → parse JSON → sleep(200ms)
```

**Proposed flow**:
```
C++ → gRPC channel → Xray.Core.App.Statscommand → response
```

**Xray gRPC API endpoints**:
- `xray.core.app.stats.command.StatsService/GetStats` — query traffic stats
- `xray.core.app.stats.command.StatsService/QueryStats` — query with filter
- `xray.core.app.proxyman.command.HandlerService/AddOutbound` — add outbound (gRPC message)
- `xray.core.app.proxyman.command.HandlerService/RemoveOutbound` — remove outbound

**Implementation approach**:

Since the project uses MinGW/GCC on Windows and linking protobuf+gRPC C++ libraries adds significant build complexity, a **lightweight HTTP/gRPC-web fallback** is preferred:

**Option A (Recommended): HTTP API via xray's inbound API**
- Xray exposes gRPC on `apiPort` (default 10080)
- Use `curl` or raw Winsock to send gRPC-web or JSON-over-HTTP requests
- Avoids protobuf dependency entirely

**Option B: Named pipe / Win32 named pipe**
- Xray on Windows can communicate via named pipes
- Lower overhead than process creation
- Less portable

**Option C: Full gRPC C++ client**
- Link `libprotobuf`, `libgrpc++`, `libgrpc`
- Heaviest build dependency change
- Most performant

**Recommendation**: Start with **Option A (HTTP API)** — Xray's statshandler already exposes JSON-compatible endpoints. If insufficient, upgrade to Option C.

**Affected files**:
- `include/XrayApi.h`, `src/XrayApi.cpp` — Add `addOutboundDirect()` / `removeOutboundDirect()` methods using HTTP/gRPC
- New: `include/XrayGrpcClient.h`, `src/XrayGrpcClient.cpp` — gRPC/HTTP client wrapper
- `src/ProxyBatchTester.cpp` — Switch worker calls from subprocess to direct API

**Expected improvement**: 3 subprocess calls × 400ms = **~1200ms saved per proxy** (60-70% of API overhead eliminated).

---

### 2.2 Phase B: Remove Redundant removeOutbound (High Impact, Low Risk)

**Goal**: Eliminate the duplicate `removeOutbound` call in Phase 1.

**Current code** (`ProxyBatchTester.cpp:617-646`):
```cpp
// FIRST remove (line ~617)
xrayApi.removeOutbound(tag);           // subprocess + sleep
std::this_thread::sleep_for(std::chrono::milliseconds(200));
for (int i = 0; i < 10; i++) { ... }  // 100ms loop

// SECOND remove (line ~635) — SAME TAG, SAME OUTBOUND
xrayApi.removeOutbound(tag);           // subprocess + sleep AGAIN
std::this_thread::sleep_for(std::chrono::milliseconds(200));
for (int i = 0; i < 40; i++) { ... }  // 400ms loop
```

**Proposed**: Single `removeOutbound` call + minimal wait.

**Affected files**:
- `src/ProxyBatchTester.cpp:617-646` — Remove duplicate call and associated sleep loop

**Expected improvement**: ~600-900ms + 500ms sleep saved per proxy. Low risk — single remove is idempotent.

---

### 2.3 Phase C: Sleep Reduction via Event-Driven Readiness (Medium Impact, Medium Risk)

**Goal**: Replace hardcoded sleep loops with event-driven readiness detection.

**Current pattern** (repeated 3-4 times per proxy):
```cpp
std::this_thread::sleep_for(std::chrono::milliseconds(200));
for (int i = 0; i < N; i++) {
    // poll something
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
```

**Proposed approaches**:

| Approach | Mechanism | Tradeoff |
|----------|-----------|----------|
| **Xray API readiness poll** | Query Xray stats endpoint until outbound is registered | Replaces blind sleep with meaningful check |
| **Shorter fixed sleep** | 50ms instead of 200ms (API calls are near-instant with gRPC) | Simple, low risk, immediate win |
| **Adaptive backoff** | Start at 10ms, exponential to 100ms max | More complex, handles slow machines |

**Recommendation**: After Phase A (gRPC), API calls become near-instant. Reduce sleeps to **50ms fixed** (simple, safe) initially, then upgrade to readiness polling if needed.

**Affected files**:
- `src/ProxyBatchTester.cpp` — All sleep/loop sequences

**Expected improvement**: 1400ms → ~150ms per proxy (**~1250ms saved**).

---

### 2.4 Phase D: Batch DB Writes (Medium Impact, Low Risk)

**Goal**: Replace per-proxy DB writes with batched result flushing.

**Current**: Each worker calls `exItemDao.updateTestResult()` individually under `dbMutex_`, blocking all other workers.

**Proposed**: Workers push results to a thread-safe queue; a dedicated flush thread batches writes every N results or every T seconds.

```
Worker threads → ThreadSafeQueue<TestResult> → FlushThread → updateTestResultBatch() (transaction)
```

**Key insight**: `updateTestResultBatch()` already exists in `ProfileExItemDAO` and uses `BEGIN/COMMIT` transaction with prepared statements — it just isn't used by ProxyBatchTester.

**Affected files**:
- New: `include/TestResultQueue.h` — Thread-safe result queue (or use `std::queue` + `std::mutex` + `std::condition_variable`)
- `src/ProxyBatchTester.cpp` — Workers push to queue instead of direct DB call
- New: `src/DbFlushWorker.cpp` — Dedicated flush thread consuming from queue
- `include/ProfileExItemDAO.h` — Verify `updateTestResultBatch()` signature compatibility

**Expected improvement**: Reduces `dbMutex_` contention. Workers never block on DB. Batch transaction amortizes fsync overhead across N results.

---

### 2.5 Phase E: Dynamic Worker Count (Low Impact, Low Risk)

**Goal**: Auto-scale worker count based on CPU cores.

**Current**: Hardcoded default 4 in `config.json`.

**Proposed**: Use `std::thread::hardware_concurrency()` with a configurable override:

```cpp
int workerCount = ConfigReader::instance().getWorkers();
if (workerCount <= 0) {
    unsigned int cores = std::thread::hardware_concurrency();
    workerCount = (cores > 0) ? static_cast<int>(cores) : 4;
}
```

**Affected files**:
- `src/ProxyBatchTester.cpp` — `calculateXrayInstanceCount()` or equivalent

**Expected improvement**: 20-50% throughput increase on 6-8 core CPUs. Minimal risk.

---

### 2.6 Phase F: Xray Config Caching (Low Impact, Low Risk)

**Goal**: Cache outbound JSON generation to avoid redundant config regeneration.

**Current**: Each proxy test generates outbound JSON from scratch via `ConfigGenerator`.

**Proposed**: Pre-generate all outbound configs before starting workers, store in a `std::vector<std::string>` indexed by proxy queue position. Workers read pre-generated configs.

**Affected files**:
- `src/ProxyBatchTester.cpp` — Pre-generate configs in `run()` before worker loop
- `include/ConfigGenerator.h` — Verify `generateOutbound()` is stateless

**Expected improvement**: Minor (config generation is fast), but reduces per-proxy CPU work.

---

## 3. Implementation Priority & Phasing

### Phase A + B + C (Immediate — Highest ROI)

These three phases target the **same code path** (`workerThreadFunc`) and should be implemented together:

| Phase | Savings per proxy | Risk | Effort |
|-------|-------------------|------|--------|
| A (gRPC) | ~1200ms | Medium | High |
| B (Remove redundant) | ~1100ms | Low | Low |
| C (Sleep reduction) | ~1250ms | Low | Low |
| **Combined** | **~3550ms** | — | — |

**Before**: ~4300ms per proxy (API + sleep)
**After**: ~750ms per proxy (API 300ms + sleep 150ms + curl 300ms avg)

**Speedup**: ~5.7× per proxy.

### Phase D + E (Secondary — Moderate ROI)

| Phase | Impact | Risk | Effort |
|-------|--------|------|--------|
| D (Batch DB) | Reduces lock contention | Low | Medium |
| E (Worker count) | 20-50% throughput gain | Low | Low |

### Phase F (Optional — Polish)

| Phase | Impact | Risk | Effort |
|-------|--------|------|--------|
| F (Config cache) | Minor CPU savings | Low | Low |

---

## 4. Estimated Performance After All Phases

### Current (4 workers, 100 proxies):
- Per proxy: ~3500ms (2300ms API/sleep + 1200ms curl avg)
- Total: 4 × 25 × 3500ms = **~87.5s**

### After Phases A+B+C (4 workers, 100 proxies):
- Per proxy: ~750ms (300ms API + 150ms sleep + 300ms curl avg)
- Total: 4 × 25 × 750ms = **~18.75s**

### After All Phases (6 workers, 100 proxies):
- Per proxy: ~600ms (faster API + batch DB + no contention)
- Total: 6 × 17 × 600ms = **~10.2s**

**Overall improvement**: ~8.5× speedup (87.5s → 10.2s).

---

## 5. Affected Files Summary

| File | Changes | Phase |
|------|---------|-------|
| `include/XrayApi.h` | Add `addOutboundDirect()` / `removeOutboundDirect()` | A |
| `src/XrayApi.cpp` | Implement gRPC/HTTP direct API calls | A |
| `include/XrayGrpcClient.h` | New: gRPC client wrapper | A |
| `src/XrayGrpcClient.cpp` | New: HTTP/gRPC implementation | A |
| `src/ProxyBatchTester.cpp` | Switch to direct API, remove duplicate removeOutbound, reduce sleeps, batch DB, pre-gen configs | A+B+C+D+F |
| `include/ProxyBatchTester.h` | Add result queue, flush thread members | D |
| `src/DbFlushWorker.cpp` | New: dedicated DB flush thread | D |
| `include/TestResultQueue.h` | New: thread-safe result queue | D |
| `bin/config.json` | Dynamic worker count default | E |
| `src/ConfigGenerator.cpp` | Verify stateless outbound generation | F |
| `CMakeLists.txt` | Add new source files | A+D |

---

## 6. Risk Assessment

| Phase | Risk | Mitigation |
|-------|------|------------|
| A (gRPC) | Xray API compatibility; build dependency changes | HTTP fallback if gRPC protobuf linking fails; feature flag to revert to subprocess |
| B (Remove redundant) | Existing code may rely on double-remove for timing | Add `sleep(100)` single wait; test with slow Xray instances |
| C (Sleep reduction) | Race condition if Xray hasn't processed outbound change | Keep 50ms minimum; add API readiness check as fallback |
| D (Batch DB) | Data loss on crash (unflushed results) | Flush on worker join; periodic flush every 5s |
| E (Worker count) | Too many workers overwhelm system | Cap at `min(hardware_concurrency, 16)`; keep configurable |

---

## 7. Testing Strategy

### Unit Tests
- `XrayGrpcClient` mock tests (verify request format, response parsing)
- `TestResultQueue` thread-safety tests (concurrent push/pop)
- `DbFlushWorker` batch correctness tests

### Integration Tests
- Full pipeline test with mock Xray (or real xray.exe on test port)
- Verify correct number of API calls per proxy (should be 2, not 3)
- Verify DB batch write correctness (compare individual vs batch results)

### Performance Benchmarks
- Before/after timing for 100/500/1000 proxy batches
- Measure per-phase overhead breakdown
- CPU/memory profiling during batch test

---

## 8. Open Questions

1. **gRPC dependency**: ✅ Go directly to gRPC. Use `#ifdef USE_GRPC_API` compile-time flag in CMakeLists.txt. No HTTP fallback phase — full gRPC implementation from start.
2. **Xray version compatibility**: ✅ Minimum Xray v1.8.0 (HandlerService stable). Document as requirement in spec header.
3. **Batch flush interval**: ✅ Configurable. Default 5s, user-adjustable via `config.json` → `test.db_flush_interval_ms`.
4. **Worker count cap**: ✅ Configurable. Auto-scale = `min(hardware_concurrency, config_max_workers)`, default max = 16.
5. **Backward compatibility**: Should the gRPC direct API be a compile-time flag (`#ifdef USE_GRPC_API`) or runtime feature flag (config.json)?

---

## 9. References

- Xray-core gRPC API: `xray-core/app/proxyman/command/command.proto`
- Xray-core Stats API: `xray-core/app/stats/command/command.proto`
- Existing spec: `docs/specs/2026-07-24-Spec-SubscriptionWritePerformance-v1.0.md`
- Bugfix history: `docs/bugfix/2026-06-12-Bugfix-ProxyBatchTester-Worker0-JoinTimeout-v1.0.md`
- Bugfix history: `docs/bugfix/2026-06-15-Bugfix-ProxyBatchTester-WorkerTimeoutLog-v1.0.md`
- Bugfix history: `docs/bugfix/2026-06-15-Bugfix-ProxyBatchTester-ZeroProxyEarlyReturn-v1.0.md`
