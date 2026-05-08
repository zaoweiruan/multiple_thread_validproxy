---
title: "fix: improve CLI argument handling - print help on invalid arguments"
type: fix
status: completed
date: 2026-05-08
origin: User request (via opencode session)
---

# fix: Improve CLI argument handling - print help on invalid arguments

## Summary

Improve the command-line argument handling in `main.cpp` to print usage/help information when invalid or unrecognized arguments are provided, instead of silently stopping with an error. This makes the CLI more user-friendly and consistent with standard Unix/Linux conventions.

---

## Problem Frame

Previously, when users provided invalid or unrecognized command-line arguments to `validproxy.exe`, the program would:
1. **Silently stop** without clear feedback
2. **Print an error** but no guidance on correct usage
3. **Force users to guess** the correct `-h` or `--help` flag

This violates the principle of **least surprise** — users expect helpful feedback when they make mistakes.

---

## Requirements

- **R1**: When unrecognized arguments are provided, print the usage/help text (same as `-h` flag)
- **R2**: Preserve existing `-h` / `--help` behavior (no regression)
- **R3**: Log the invalid argument for debugging purposes (optional, using `Logger::write`)
- **R4**: Must compile on Windows/MinGW-w64 with C++17

**Origin actors:** N/A (usability improvement)  
**Origin flows:** N/A  
**Origin acceptance examples:** AE1 (better UX), AE2 (consistent with `-h` output)

---

## Scope Boundaries

- Modify: `src/main.cpp` (argument parsing loop)
- Modify: `src/main.cpp` (help text printing function, if centralized)
- **Will NOT change** the actual command functionality (`-S`, `-U`, etc.)
- **Will NOT change** the config file parsing logic
- **Will NOT modify** other source files

---

## Context & Research

### Current Behavior (before fix)

From `ce-code-review` skill analysis (and code review):
- When CLI arguments don't match any known branch/mode, the program **stops immediately** with an error
- No fallback to current branch or help text
- User must manually run `-h` to discover correct usage

### Desired Behavior (after fix)

```
$ validproxy.exe invalidarg
Usage: validproxy [options]
Options:
  -c, --config <path>   Config file path (default: config.json)
  -show-sub, --show-sub  Show all subscriptions
  ... (full help text)
```

### Prior Art

- Git: `git invalidcommand` → `git: 'invalidcommand' is not a git command. See 'git --help'.`
- CMake: `cmake --invalid` → `Unknown argument --invalid...`
- Most CLI tools: Print usage + error message

---

## Key Technical Decisions

- **Decision**: Print help text on invalid arguments (not just error)
  - **Rationale**: User-friendly, follows Unix convention
  - **Implementation**: Call the same help-printing function as `-h` flag

- **Decision**: Log the invalid argument via `Logger::write`
  - **Rationale**: Helps debug user issues in production
  - **Example**: `Logger::write("WARN: Unrecognized argument: invalidarg", LogLevel::WARN);`

- **Decision**: Status `completed` (already implemented in prior commits)
  - **Prior commits**:
    - `2fb583b` - "feat: support -S with no args, read from config; update help text"
    - `71e9fcd` - "fix: add error handling for -S with missing argument"
    - `3f73d0a` - "fix: CLI参数解析支持单横线，添加未知参数检测，修复show-sub模式退出问题"

---

## Implementation (Already Completed)

### Changes Made (in prior commits)

1. **`src/main.cpp`** - Unknown argument detection
   - Added detection for unrecognized arguments in the parsing loop
   - When argument doesn't match any known flag, print help + error
   - Uses existing help text printing logic

2. **`src/main.cpp`** - Improved help text
   - Updated help text to be more comprehensive (commit `2fb583b`)
   - Added missing options to help output

3. **`src/main.cpp`** - Single-dash argument support
   - Added support for single-dash variants (commit `3f73d0a`)
   - Example: `-show-sub` now works in addition to `--show-sub`

---

## System-Wide Impact

- **User experience**: ✅ Dramatically improved — users get help instead of silent failures
- **Error propagation**: ⚠️ Invalid args now print help (was: stop with error)
- **State lifecycle risks**: None — no state changes, only output changes
- **Unchanged invariants**:
  - All existing command modes (`-S`, `-U`, etc.) unchanged
  - Config file parsing logic unchanged
  - Help text content is identical to `-h` flag output

---

## Risks & Mitigation

| Risk | Mitigation |
|------|------------|
| Help text becomes out-of-date | Update help text when adding new commands (commit `2fb583b` shows this practice) |
| Accidental argument suppression | Test all commands after help text changes |
| Logger not initialized when invalid arg provided | Print to stdout (fallback) + log if Logger is ready |

---

## Documentation / Operational Notes

- **Plan status**: `completed` — all work implemented in prior commits
- **Commits to reference**:
  - `2fb583b` - "feat: support -S with no args, read from config; update help text"
  - `71e9fcd` - "fix: add error handling for -S with missing argument"
  - `3f73d0a` - "fix: CLI参数解析支持单横线，添加未知参数检测"
- **User-facing change**: When providing invalid arguments, users now see help text instead of error messages

---

## Sources & References

- **Origin**: User request during opencode session (2026-05-08)
- **Related code**: `src/main.cpp` (argument parsing loop, lines ~92-210)
- **Related commits**: `2fb583b`, `71e9fcd`, `3f73d0a`
- **Unix convention**: Standard practice for CLI tools — print usage on invalid input
