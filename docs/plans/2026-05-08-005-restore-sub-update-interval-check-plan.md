---
title: "feat: Restore subscription auto-update interval check mechanism"
type: feat
status: completed
date: 2026-05-08
origin: User request (via opencode session)
---

# feat: Restore subscription auto-update interval check

## Summary

Reimplement the auto-update interval check for subscription updates. The database schema (`SubItem.AutoUpdateInterval`, `SubItem.UpdateTime`) and C++ `Subitem` struct fields still exist, but the runtime check logic was never implemented. This plan adds time-based skip logic to `SubitemUpdaterV2::run()` and `runSingle()`.

---

## Problem

- `SubitemUpdaterV2::run()` and `runSingle()` always fetch subscription URLs unconditionally
- `AutoUpdateInterval` and `UpdateTime` columns in `SubItem` table are populated (defaults: `1440` min, `0`) but never read for decisions
- In a previous cleanup (commit `709972e`), `AppConfig` fields `update_subscription` and `check_auto_update_interval` were removed as dead code since no runtime logic existed

---

## Scope Boundaries

- Modify: `include/ConfigReader.h`, `src/ConfigReader.cpp`, `src/SubitemUpdaterV2.cpp`, `include/SubitemUpdaterV2.h`, `bin/config.json`
- NOT modify: database schema, `Subitem.h` struct (fields already exist)

---

## Detailed Changes

### U1. `include/ConfigReader.h` — Restore `check_auto_update_interval` in `AppConfig`

Add field to `AppConfig` struct:

```cpp
bool check_auto_update_interval = false;
```

### U2. `src/ConfigReader.cpp` — Parse the field from `subscription` JSON object

In `load()` method, inside `subscription` parsing block:

```cpp
config.check_auto_update_interval = root["subscription"]["check_auto_update_interval"].asBool();
```

### U3. `bin/config.json` — Restore config field

```json
"subscription": {
    "enable": false,
    "check_auto_update_interval": true,
    "priority_mode": "proxy_first"
}
```

Default to `true` so the feature is active by default.

### U4. `include/SubitemUpdaterV2.h` — Add helper method

```cpp
class SubitemUpdaterV2 {
    // ...
private:
    bool shouldSkipUpdate(const db::models::Subitem& sub) const;
};
```

### U5. `src/SubitemUpdaterV2.cpp` — Add `shouldSkipUpdate()` and integrate into `run()` / `runSingle()`

#### 5a. `shouldSkipUpdate()` implementation

```cpp
bool SubitemUpdaterV2::shouldSkipUpdate(const db::models::Subitem& sub) const
{
    if (!config_.check_auto_update_interval)
        return false;

    if (sub.autoupdateinterval.empty() || sub.updatetime.empty())
        return false;

    int intervalMinutes = std::stoi(sub.autoupdateinterval);
    if (intervalMinutes <= 0)
        return false;

    // Parse updatetime in format "YYYYmmdd_HHMMSS" (same as getCurrentTimestamp())
    if (sub.updatetime == "0" || sub.updatetime.length() != 15)
        return false;

    try {
        std::tm tm = {};
        std::istringstream ss(sub.updatetime);
        ss >> std::get_time(&tm, "%Y%m%d_%H%M%S");
        if (ss.fail())
            return false;
        auto lastUpdate = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastUpdate).count();
        return elapsed < intervalMinutes;
    } catch (...) {
        return false;
    }
}
```

#### 5b. `run()` — Add skip check before `fetchUrl()`

```cpp
for (size_t i = 0; i < enabledSubs.size(); ++i) {
    const auto& sub = enabledSubs[i];
    
    if (shouldSkipUpdate(sub)) {
        Logger::write("INFO: Skipping sub " + sub.id + " (within update interval)", LogLevel::INFO);
        continue;
    }
    
    // ... existing fetchUrl() logic ...
```

#### 5c. `runSingle()` — Add skip check before `updateWithStrategy()`

```cpp
SubitemUpdaterV2::UpdateResult SubitemUpdaterV2::runSingle(const db::models::Subitem& sub, const std::string& strategy)
{
    std::lock_guard<std::mutex> lock(mtx_balance_);

    if (shouldSkipUpdate(sub)) {
        Logger::write("INFO: Skipping sub " + sub.id + " (within update interval)", LogLevel::INFO);
        return UpdateResult::SUCCESS;
    }

    // ... existing updateWithStrategy() logic ...
```

#### 5d. After successful profile update — write back `UpdateTime`

In both `run()` and `runSingle()`, after `updateProfileItems()` succeeds, update the `UpdateTime` column:

```cpp
// After successful profile update:
std::string newTime = getCurrentTimestamp();  // returns "YYYYmmdd_HHMMSS" format
db.execute("UPDATE SubItem SET UpdateTime = ? WHERE Id = ?", newTime, sub.id);
```

---

## Verification

- Build: `cmake --build . --parallel 8`
- Run a full update (`-UA` or `-U <id>`) with the test config and confirm:
  - Subscriptions within interval are skipped (log shows "Skipping sub ... (within update interval)")
  - Subscriptions past interval are fetched normally
  - `UpdateTime` is updated in DB after successful fetch
- Disable `check_auto_update_interval: false` and confirm all subscriptions update unconditionally
- Run `-T` (proxy testing) to verify unrelated code paths unaffected
