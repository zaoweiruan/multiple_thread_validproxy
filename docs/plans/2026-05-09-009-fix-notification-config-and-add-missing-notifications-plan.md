---
title: "fix: Complete notification config in all config files + add notification for default batch test mode"
type: fix
status: completed
date: 2026-05-09
origin: "User request: notification.enabled/on_update/on_test config standardization"
---

# fix: Complete notification config + add missing batch test notification

## Problem

Although the code supports Windows toast notifications (`Utils::sendNotification()`) and `main.cpp` already sends them for `-U`, `-UA`, and `-T <id>`, three issues remain:

1. `bin/config.json` has `"notification"` block but missing `on_update`/`on_test` flags, and has an unused `webhook_url` field
2. `bin/test_config*.json` have no `notification` block at all — test runs get no notifications even if desired
3. Default mode (batch test all, `main.cpp` line 766) does not trigger notification, but `-T <id>` does — inconsistent

| Config File | Has `notification`? | `on_update`? | `on_test`? |
|---|---|---|---|
| `bin/config.json` | ✅ (but has stale `webhook_url`) | ❌ | ❌ |
| `bin/test_config_full.json` | ❌ | — | — |
| `bin/test_config.json` | ❌ | — | — |

---

## Scope Boundaries

- Modify: `bin/config.json`, `bin/test_config_full.json`, `bin/test_config.json`, `src/main.cpp`
- NOT modify: `ConfigReader`, `Utils`, `SubitemUpdaterV2`, `ProxyBatchTester`

---

## Detailed Changes

### U1. `bin/config.json` — Fix notification block

Replace:
```json
"notification": {
    "enabled": false,
    "webhook_url": ""
},
```
With:
```json
"notification": {
    "enabled": false,
    "on_update": true,
    "on_test": true
},
```

(Remove stale `webhook_url`, add `on_update`/`on_test`.)

### U2. `bin/test_config_full.json` — Add notification block

Insert after the `sync` block (line 37 `}`), before the file closing `}`:
```json
"notification": {
    "enabled": false,
    "on_update": true,
    "on_test": true
},
```

### U3. `bin/test_config.json` — Add notification block

Same as U2, insert after `sync` block, before closing `}`:
```json
"notification": {
    "enabled": false,
    "on_update": true,
    "on_test": true
},
```

### U4. `src/main.cpp` — Add notification after default batch test

Insert after line 766 (`bool testResult = tester.run();`), before the XrayManager release:
```cpp
if (appConfig->notification_enabled && appConfig->notification_on_test) {
    utils::sendNotification("Proxy Test Complete", testResult ? "All tests completed successfully" : "Some tests failed");
}
```

This mirrors the existing pattern at lines 689-691.

---

## Verification

- Build: `cmake --build . --parallel 8`
- Verify all 3 config files parse correctly (run `-show-sub` or any command against each)
- No functional change to existing notification behavior for `-U`, `-UA`, `-T <id>`
