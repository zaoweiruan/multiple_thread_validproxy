---
title: "fix: Resolve sync failure caused by config path errors"
type: fix
status: completed
date: 2026-05-11
origin: "Log analysis of sync_20260511_161426.log — 703 proxies failed to migrate due to wrong target_db path"
---

# fix: Resolve sync failure caused by config path errors

## Problem

Log analysis of `bin/log/sync_20260511_161426.log` revealed that the `-S` sync operation was failing for all proxies with `no such table: ProfileItem` errors. Root cause investigation found configuration path errors in `bin/worker/config.json`.

| # | Severity | Issue | File | Field | Wrong Value | Correct Value |
|---|----------|-------|------|-------|-------------|---------------|
| 1 | 🔴 Critical | `sync.target_db` points to non-existent 0-byte file | `bin/worker/config.json` | `sync.target_db` | `E:/eclipse_workspace/multiple_thread_validproxy/guiNDB.db` | `E:/eclipse_workspace/multiple_thread_validproxy/test/guiNDB_empty.db` |
| 2 | 🔴 Critical | `database.path` uses relative path resolving to wrong location | `bin/worker/config.json` | `database.path` | `guiNDB.db` (relative) | `E:/eclipse_workspace/multiple_thread_validproxy/bin/worker/guiNDB.db` (absolute) |
| 3 | 🟡 Medium | `log.file_level` too restrictive, loses INFO/WARN logs | `bin/config.json` | `log.file_level` | `ERROR` | `DEBUG` |
| 4 | 🟡 Medium | `log.network_failures` disabled, hides connection issues | `bin/config.json` | `log.network_failures` | `false` | `true` |

## Changes Made

### U1: `bin/worker/config.json` — Fix database paths
- `database.path`: `"guiNDB.db"` → full absolute path `"E:/eclipse_workspace/multiple_thread_validproxy/bin/worker/guiNDB.db"`
- `sync.target_db`: `"E:/eclipse_workspace/multiple_thread_validproxy/guiNDB.db"` → `"E:/eclipse_workspace/multiple_thread_validproxy/test/guiNDB_empty.db"`

### U2: `bin/config.json` — Fix log level configuration
- `log.file_level`: `"ERROR"` → `"DEBUG"`
- `log.network_failures`: `false` → `true`

## Verification

| Test | Result |
|------|--------|
| Sync test (-S, source=bin/worker/guiNDB.db, target=test/guiNDB_empty.db) | ✅ 706 proxies migrated successfully |
| Unit tests (15/15) | ✅ All passed |
| `[INFO]` `[WARN]` `[REPORT]` `[ERR]` log prefixes | ✅ Correct display |