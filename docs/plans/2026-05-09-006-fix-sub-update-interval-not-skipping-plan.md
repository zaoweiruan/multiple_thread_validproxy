---
title: "fix: Fix subscription auto-update interval check not skipping recently updated subs"
type: fix
status: completed
date: 2026-05-09
origin: Bug report (test config missing subscription section, Unix timestamp incompatibility, missing UpdateTime write-back in runSingleWithProxy)
---

# fix: Fix subscription auto-update interval check not skipping recently updated subs

## Problem

The `shouldSkipUpdate()` feature (implemented in plan 005) does NOT skip recently updated subscriptions due to three independent bugs:

| # | Bug | Root Cause |
|---|-----|------------|
| 1 | Test configs missing `subscription` section | `ConfigReader.cpp` else branch sets `check_auto_update_interval = false` |
| 2 | Existing `UpdateTime` values are Unix timestamps (length=10), fail `length() != 15` check | Historical data from v2rayN, format mismatch |
| 3 | `runSingleWithProxy()` never writes back `UpdateTime` | Implementation omission in original 005 plan |

---

## Scope Boundaries

- Modify: `bin/test_config_full.json`, `bin/test_config.json`, `src/SubitemUpdaterV2.cpp`
- NOT modify: `bin/config.json`, `bin/worker/config.json` (already correct)

---

## Detailed Changes

### U1. Add `subscription` section to test configs

**`bin/test_config_full.json`** — Add after `"log"` block:
```json
"subscription": {
    "priority_mode": "proxy_first",
    "check_auto_update_interval": true
},
```

**`bin/test_config.json`** — Same addition.

### U2. Add Unix timestamp compatibility to `shouldSkipUpdate()`

In `src/SubitemUpdaterV2.cpp`, after the existing `length() != 15` check, add a branch to handle 10-digit Unix timestamps:

```cpp
if (sub.updatetime == "0" || sub.updatetime.length() != 15) {
    // Compat: 10-digit Unix timestamp (from v2rayN)
    if (sub.updatetime.length() == 10 &&
        sub.updatetime.find_first_not_of("0123456789") == std::string::npos) {
        try {
            auto lastUpdate = std::chrono::system_clock::from_time_t(std::stoll(sub.updatetime));
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastUpdate).count();
            return elapsed < intervalMinutes;
        } catch (...) {
            return false;
        }
    }
    return false;
}
```

### U3. Add UpdateTime write-back to `runSingleWithProxy()`

In `src/SubitemUpdaterV2.cpp`, modify `runSingleWithProxy()` to write back UpdateTime after successful update:

```cpp
auto profiles = parseSubscription(content, sub.id);
bool result = updateProfileItems(sub.id, profiles);
if (result) {
    std::string newTime = getCurrentTimestamp();
    std::string updateSql = "UPDATE SubItem SET UpdateTime = '" + newTime + "' WHERE Id = '" + sub.id + "'";
    sqlite3_exec(db_, updateSql.c_str(), nullptr, nullptr, nullptr);
}
return result;
```

---

## Verification

- Build: `cmake --build . --parallel 8`
- Run `-U <subid>` with `-c bin/test_config_full.json`; if subscription has `UpdateTime` within `AutoUpdateInterval`, log should show `"Skipping sub ... (within update interval)"`
- Run `-U <subid>` with a subscription that has a Unix timestamp `UpdateTime`; should now be properly recognized and skipped if within interval
- Run `-U <subid>` through proxy (if proxy path is triggered); after success, `UpdateTime` should be updated in database
