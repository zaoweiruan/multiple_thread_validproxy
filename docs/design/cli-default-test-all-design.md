---
title: "feat: CLI no-argument default to test-all mode"
type: feat
status: draft
date: 2026-06-02
---

# CLI No-Argument Default Test-All Design

## Feature Summary

When `validproxy-cli.exe` is launched without arguments, it now defaults to `-TA` (test-all) mode instead of displaying help.

## Current Behavior

`src/main_cli.cpp:787-790`:
```cpp
if (commandMode.empty()) {
    printHelp();
    return 0;
}
```

## Proposed Behavior

```cpp
if (commandMode.empty()) {
    commandMode = "test-all";  // Silent default batch test
}
```

## Changes Required

| File | Change |
|------|--------|
| `src/main_cli.cpp` | Remove help fallback, set `commandMode = "test-all"` |

## Behavioral Matrix

| Args | Behavior |
|------|----------|
| (none) | Silent `test-all` |
| `-h` | Silent `test-all` (ignored) |
| `--help` | Silent `test-all` (ignored) |
| `-TA` | Explicit `test-all` |
| Other modes | Normal handling |

## Rationale

- CLI users typically run batch tests in scripts/automation
- Silent start avoids console spam in unattended usage
- Consistent with headless server tool expectations