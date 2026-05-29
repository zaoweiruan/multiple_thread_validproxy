---
title: "fix: Merge log level standardization — error levels + WARN usage + sync details"
type: fix
status: completed
date: 2026-05-10
supersedes:
  - "2026-05-08-003-standardize-proxy-test-error-levels-plan.md"
  - "2026-05-09-012-fix-log-level-mismatches-from-audit-plan.md"
origin: Merge of plans #003 and #012 after source code audit
---

# fix: Merge log level standardization — error levels, WARN usage, sync details

## Summary

Merge plans 003 and 012 into a single pass that:

1. Ensures all `Logger::write` calls across proxy-testing modules have explicit `LogLevel` parameters.
2. Standardizes network errors → `INFO`, config/inject errors → `ERR`.
3. Adds `LogLevel::WARN` for boundary conditions (no proxies, failed instance starts).
4. Fixes missing error detail on `syncDatabases()` failure.
5. Adds explicit `LogLevel` params in `XrayInstance.cpp` and `XrayManager.cpp` (found during audit).

---

## Previous Plan Status

### Plan 003 — ✅ Already applied before this merge
| Task | Status |
|------|--------|
| U1 ProxyBatchTester explicit ERR (lines 89, 124) | Already `LogLevel::ERR` |
| U2 ProxyFinder network ERR→INFO (lines 79, 146) | Already `LogLevel::INFO` |
| U3 SubitemUpdaterV2 fetch ERR→INFO (old ~374, ~397) | Already `LogLevel::INFO` |
| U4 XrayApi addOutbound DEBUG→ERR (lines 81-82) | Already `LogLevel::ERR` |

### Plan 012 — ✅ All tasks completed in this merge
| Task | Status |
|------|--------|
| U1 ProxyFinder network ERR→INFO | ✅ Already fixed |
| U2 ProxyBatchTester explicit ERR params | ✅ Already present on main paths |
| U3 Add WARN level usage | ✅ **Completed** (see Execution Log below) |
| U4 syncDatabases error detail | ✅ **Completed** (see Execution Log below) |
| U5 Add missing LogLevel params in ConfigGenerator | ✅ **Completed** |

### U6 — ✅ Remove residual std::cerr/std::cout in ProxyBatchTester.cpp (completed)

| # | Location | Before | After |
|---|----------|--------|-------|
| U6a | `workerThreadFunc` ×2 (lines ~148, ~157) | `logToConsole()` via `std::cout` | `Logger::write(..., LogLevel::INFO)` |
| U6b | `workerThreadFunc` exception (line ~165) | `std::cerr << ...` | `Logger::write(..., LogLevel::ERR)` |
| U6c | `run()` no-proxies (line ~211) | `std::cerr << ...` | `Logger::write("No proxies to test", LogLevel::WARN)` |
| U6d | `run()` xray start fail (line ~222) | `std::cerr << ...` | `Logger::write("Failed to start xray instances", LogLevel::WARN)` |
| U6e | `runWithSubId()` no-proxies (line ~243) | `std::cerr << ...` | `Logger::write("No proxies to test for subscription: ...", LogLevel::WARN)` |
| U6f | `runWithSubId()` header (line ~248) | `std::cout << ...` | `Logger::write("Testing ...", LogLevel::INFO)` |
| U6g | `runWithSubId()` xray start fail (line ~255) | `std::cerr << ...` | `Logger::write("Failed to start xray instances", LogLevel::WARN)` |

Also removed `logToConsole()` helper function and `#include <iostream>` from `ProxyBatchTester.cpp`.
Removed unused `#include <iostream>` from `XrayManager.cpp`.

---

## Remaining Tasks (deduplicated, verified against current source)

### U1: `src/ProxyBatchTester.cpp` — Add WARN level to boundary-condition messages

All currently call `Logger::write(msg)` **without** explicit `LogLevel`, defaulting to `INFO`. Should be `WARN`.

**U1a. Line ~214 — "No proxies to test"**
```cpp
// Current:
Logger::write("ERROR: No proxies to test");
// Target:
Logger::write("WARN: No proxies to test", LogLevel::WARN);
```

**U1b. Line ~225 — "Failed to start xray instances" (run())**
```cpp
// Current:
Logger::write("ERROR: Failed to start xray instances");
// Target:
Logger::write("WARN: Failed to start xray instances", LogLevel::WARN);
```

**U1c. Line ~246 — "No proxies to test for subscription" (runWithSubId())**
```cpp
// Current:
Logger::write("ERROR: No proxies to test for subscription: " + subId);
// Target:
Logger::write("WARN: No proxies to test for subscription: " + subId, LogLevel::WARN);
```

**U1d. Line ~258 — "Failed to start xray instances" (runWithSubId())**
```cpp
// Current:
Logger::write("ERROR: Failed to start xray instances");
// Target:
Logger::write("WARN: Failed to start xray instances", LogLevel::WARN);
```

---

### U2: `src/SubitemUpdaterV2.cpp` — Add error detail before sync failure return

**U2a. Line ~1743 — Add summary error log before returning false**
```cpp
// Current:
    return failCount == 0;
}
// Target:
    if (failCount > 0) {
        Logger::write("Sync failed: " + std::to_string(failCount) + " proxy(es) failed to migrate", LogLevel::ERR);
    }
    return failCount == 0;
}
```

---

### U3: `src/XrayInstance.cpp` — Add explicit LogLevel params on error calls (discovered during audit)

**U3a. Line 23 — Config file creation failure**
```cpp
// Current:
Logger::write("[XrayInstance] Failed to create config file");
// Target:
Logger::write("[XrayInstance] Failed to create config file", LogLevel::ERR);
```

**U3b. Line 29 — Job object creation failure**
```cpp
// Current:
Logger::write("[XrayInstance] Failed to create job object: " + std::to_string(GetLastError()));
// Target:
Logger::write("[XrayInstance] Failed to create job object: " + std::to_string(GetLastError()), LogLevel::ERR);
```

**U3c. Line 47 — Process creation failure**
```cpp
// Current:
Logger::write("[XrayInstance] Failed to create process: " + std::to_string(err));
// Target:
Logger::write("[XrayInstance] Failed to create process: " + std::to_string(err), LogLevel::ERR);
```

**U3d. Line 54 — Job assignment failure**
```cpp
// Current:
Logger::write("[XrayInstance] Failed to assign to job: " + std::to_string(GetLastError()));
// Target:
Logger::write("[XrayInstance] Failed to assign to job: " + std::to_string(GetLastError()), LogLevel::ERR);
```

---

### U4: `src/XrayManager.cpp` — Add explicit LogLevel params (discovered during audit)

**U4a. Line 56 — Failed to find available ports**
```cpp
Logger::write("XrayManager: failed to find available ports", LogLevel::ERR);
```

**U4b. Line 66 — Failed to start instance**
```cpp
Logger::write("XrayManager: failed to start instance " + std::to_string(i), LogLevel::WARN);
```

---

### U5: `src/ConfigGenerator.cpp` — Add missing LogLevel params (discovered during final audit)

All 4 calls used `Logger::write(msg)` without explicit `LogLevel`:

**U5a. Line 31 — SQL result count** → `LogLevel::INFO`
**U5b. Line 36 — Default network fallback** → `LogLevel::WARN`
**U5c. Line 41 — Invalid network skip** → `LogLevel::WARN`
**U5d. Line 48 — Valid profiles count** → `LogLevel::INFO`

---

## Execution Log

### U1: `ProxyBatchTester.cpp` — WARN for boundary conditions
| Sub-task | Change | Verified |
|----------|--------|----------|
| U1a. "No proxies to test" (run) | `LogLevel::WARN`, msg prefix "WARN:" | ✅ `-T 99999999` shows `[WARN]` |
| U1b. "Failed to start xray instances" (run) | `LogLevel::WARN` | ✅ Code verified |
| U1c. "No proxies to test for subscription" (runWithSubId) | `LogLevel::WARN` | ✅ `-T 99999999` shows `[WARN]` |
| U1d. "Failed to start xray instances" (runWithSubId) | `LogLevel::WARN` | ✅ Code verified |

### U2: `SubitemUpdaterV2.cpp` — Sync failure detail
| Sub-task | Change | Verified |
|----------|--------|----------|
| U2a. failCount summary before return false | Added `Logger::write("Sync failed: N proxy(es) failed to migrate", LogLevel::ERR)` | ✅ Sync test passes (703/703) |

### U3: `XrayInstance.cpp` — ERR for error paths
| Sub-task | Change | Verified |
|----------|--------|----------|
| U3a. Config file creation failure | `LogLevel::ERR` | ✅ Code verified |
| U3b. Job object creation failure | `LogLevel::ERR` | ✅ Code verified |
| U3c. Process creation failure | `LogLevel::ERR` | ✅ Code verified |
| U3d. Job assignment failure | `LogLevel::ERR` | ✅ Code verified |

### U3b: `XrayInstance.cpp` — INFO for xray lifecycle (networking)
| Sub-task | Change | Verified |
|----------|--------|----------|
| Creating config | `LogLevel::INFO` (was REPORT) | ✅ Code verified |
| Executing command | `LogLevel::INFO` (was REPORT) | ✅ Code verified |
| Started successfully | `LogLevel::INFO` (was REPORT) | ✅ Code verified |

### U3c: `ProxyBatchTester.cpp` — INFO for xray instances and test results (networking)
| Sub-task | Change | Verified |
|----------|--------|----------|
| Loaded N proxies | `LogLevel::INFO` (was REPORT) | ✅ `-S` shows `[INFO]` |
| Started N xray instances | `LogLevel::INFO` (was REPORT) | ✅ Code verified |
| Proxy test SUCCESS | `LogLevel::INFO` (was REPORT) | ✅ Code verified |
| Proxy test FAILED | `LogLevel::INFO` (was REPORT) | ✅ Code verified |

### U4: `XrayManager.cpp` — Explicit LogLevel
| Sub-task | Change | Verified |
|----------|--------|----------|
| Failed to find ports | `LogLevel::ERR` | ✅ Code verified |
| Failed to start instance | `LogLevel::WARN` | ✅ Code verified |

### U5: `ConfigGenerator.cpp` — Missing LogLevel params
| Sub-task | Change | Verified |
|----------|--------|----------|
| SQL returned N profiles | `LogLevel::INFO` | ✅ `-T 99999999` shows `[INFO]` |
| Using default network 'tcp' | `LogLevel::WARN` | ✅ `-T` shows `[WARN]` when triggered |
| Skipping invalid network | `LogLevel::WARN` | ✅ Code verified |
| Valid profiles: N | `LogLevel::INFO` | ✅ `-T 99999999` shows `[INFO]` |

---

## Verification Results

| Test | Result |
|------|--------|
| Build | ✅ 0 errors, 0 new warnings |
| Unit tests (15/15) | ✅ All passed (`test_dedup` 11, `test_model` 3, `test_curl_easy_handle` 1) |
| Sync test (-S, 703 proxies) | ✅ All migrated successfully |
| Log-level spot check (-T) | ✅ `[INFO]` `[WARN]` `[REPORT]` `[ERR]` 正确显示 |
| Grep audit: all Logger::write have LogLevel | ✅ 0 violations |

---

## File Change List

| File | Changes |
|------|---------|
| `src/ProxyBatchTester.cpp` | Add `LogLevel::WARN` to 4 boundary-condition calls; change 4 REPORT→INFO (xray/test logging) |
| `src/SubitemUpdaterV2.cpp` | Add error-detail summary log (`LogLevel::ERR`) before `syncDatabases()` failure return |
| `src/XrayInstance.cpp` | Add `LogLevel::ERR` to 4 error-path calls; change 3 REPORT→INFO (lifecycle logging) |
| `src/XrayManager.cpp` | Add `LogLevel::ERR` (port failure) and `LogLevel::WARN` (instance start failure) |
| `src/ConfigGenerator.cpp` | Add missing `LogLevel` params to 4 calls (2×INFO, 2×WARN) |

---

## Risks

| Risk | Mitigation |
|------|------------|
| INFO-level warnings disappear if console_level later raised above INFO | Default console_level = INFO; file_level = DEBUG catches everything |
| Changing "ERROR:" prefix to WARN may confuse grep-based log parsing | Keep "WARN:" prefix consistent but note in code comments |

---

## Subsequent Modification Policy

> **Effective immediately**: All future source code modifications must be accompanied by a corresponding plan document in `docs/plans/`.
>
> **Process**:
> 1. Create or update a plan document with `status: draft` before making changes
> 2. Execute changes and update plan status to `completed` with verification evidence
> 3. Peer review the plan document before merging
> 4. No ad-hoc code changes without a plan

---

## Links

- Superseded plans: `2026-05-08-003-standardize-proxy-test-error-levels-plan.md`, `2026-05-09-012-fix-log-level-mismatches-from-audit-plan.md`
- Test config: `bin/worker/config_test.json`
- Test databases: `test/guiNDB.db` (source), `test/guiNDB_empty.db` (target)