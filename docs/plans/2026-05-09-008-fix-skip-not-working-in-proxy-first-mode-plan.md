---
title: "fix: shouldSkipUpdate not working in proxy_first mode (run() + runSingleWithProxy())"
type: fix
status: completed
date: 2026-05-09
origin: "Bug report: subscription skip logic still bypassed in proxy-first code path"
---

# fix: shouldSkipUpdate not working in proxy_first mode

## Problem

`shouldSkipUpdate()` is only checked inside the `runDirectPhase` block of `SubitemUpdaterV2::run()`. When `priority_mode = proxy_first`:

- `runDirectPhase = false` — Phase 1 is entirely bypassed
- Line 189 `else if (runProxyPhase)` pushes **all** enabled subscriptions into `failedSubs` with **zero `shouldSkipUpdate()` filtering**
- Phase 2 proxy loop (lines 203-229) processes all `failedSubs` with **no skip check**

Result: `-UA` fetches every subscription every time, ignoring `AutoUpdateInterval`. Additionally, `runSingleWithProxy()` also lacks the check entirely.

| Code Path | has `shouldSkipUpdate`? |
|-----------|------------------------|
| `run()` — `runDirectPhase` | ✅ (line 158) |
| `run()` — `else if (runProxyPhase)` | ❌ (line 189) |
| `run()` — Phase 2 proxy loop | ❌ (line 203) |
| `runSingle()` | ✅ (line 287) |
| `runSingleWithProxy()` | ❌ (line 301) |

---

## Scope Boundaries

- Modify: `src/SubitemUpdaterV2.cpp`
- NOT modify: configs, DB, other files

---

## Detailed Changes

### F1. `run()` line 189 — Filter `shouldSkipUpdate` in proxy_first stacking

```cpp
} else if (runProxyPhase) {
    for (const auto& sub : enabledSubs) {
        if (shouldSkipUpdate(sub)) {
            Logger::write("INFO: Skipping sub " + sub.id + " (within update interval)", LogLevel::INFO);
            continue;
        }
        failedSubs.push_back({sub.id, sub.remarks, sub.url});
    }
    ...
```

### F2. `runSingleWithProxy()` — Add check at function entry

Insert after the enabled check (line 314), before fetching:

```cpp
if (shouldSkipUpdate(sub)) {
    Logger::write("INFO: Skipping sub " + subId + " (within update interval)", LogLevel::INFO);
    return true;
}
```

---

## Verification

- Build: `cmake --build . --parallel 8`
- Run `-UA` against production config or test config; subscriptions whose `UpdateTime` is within `AutoUpdateInterval` should be skipped even in `proxy_first` mode
- Confirm "Skipping sub ... (within update interval)" appears in logs for recently updated subs
- Run `-U <id>` to verify `runSingle()` still works (previously correct)
- Unit tests: `.\bin\test_dedup.exe`
