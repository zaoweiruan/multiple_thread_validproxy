---
title: "fix: Clean up dead config fields - use_whitelist, whitelist, advanced.worker_count"
type: fix
status: completed
date: 2026-05-08
origin: User request (via opencode session)
---

# fix: Clean up dead config fields

## Summary

Remove three dead config fields (`dedup.use_whitelist`, `dedup.whitelist`, `advanced.worker_count`) from all JSON config files. These fields exist in one or more config files but are never declared in `AppConfig`, never parsed by `ConfigReader`, and never used in any `.cpp` file.

---

## Problem

| Field | Exists in | Status |
|-------|-----------|--------|
| `dedup.use_whitelist` | `config.json` + test configs | ❌ Dead code — never declared/parsed/used |
| `dedup.whitelist` | `config.json` + test configs | ❌ Dead code — never declared/parsed/used |
| `advanced.worker_count` | only `config.json` | ❌ Dead code — redundant with `xray.workers` |

These fields mislead developers into thinking they control behavior. `advanced.worker_count` also creates a false conflict with `xray.workers`.

---

## Scope Boundaries

- Modify: `bin/config.json`, `bin/test_config.json`, `bin/test_config_full.json`
- Will NOT change: any `.h` / `.cpp` file (no code references these fields)

---

## Targeted Changes

### U1. `bin/config.json`

Remove from `dedup` object:
```json
"use_whitelist": false,
"whitelist": []
```

Remove entire `advanced` object:
```json
"advanced": {
    "worker_count": 4
}
```

### U2. `bin/test_config.json`

Remove from `dedup` object:
```json
"use_whitelist": false,
"whitelist": []
```

### U3. `bin/test_config_full.json`

Remove from `dedup` object:
```json
"use_whitelist": false,
"whitelist": []
```

---

## Verification

- Build: `cmake --build . --parallel 8` (no code changes, should pass trivially)
- Run: smoke test with each config file to confirm parsing unaffected
- Confirm: `grep` for `use_whitelist` / `whitelist` / `advanced` across all `bin/*.json` returns nothing
