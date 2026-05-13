---
title: "fix: Remove dead log.level config field"
type: fix
status: completed
date: 2026-05-12
origin: Config field audit — explored confirmed log.level is parsed but never read by any code
---

# fix: Remove dead `log.level` config field

## Problem

`log.level` in `bin/config.json` maps to `AppConfig::log_level`, which is parsed by `ConfigReader` (line 130-132) but **never read or applied anywhere** in the codebase.

The actual log level is controlled by two **separate fields** that ARE used:
- `log.console_level` → `Logger::setConsoleLevel()` — 9 call sites in `main.cpp`
- `log.file_level` → `Logger::setFileLevel()` — 9 call sites in `main.cpp`

This misleads users into thinking `log.level` controls something, when in reality changing it has zero effect.

## Scope Boundaries

- **Modify**: `include/ConfigReader.h`, `src/ConfigReader.cpp`, `bin/config.json`
- **Will NOT change**: any logic in `.cpp` files outside ConfigReader (no code reads this field)
- **Will NOT change**: test config files (if they also have `log.level`, remove it too)

## Targeted Changes

### U1. `include/ConfigReader.h` — Remove field declaration

Remove line 21:
```cpp
std::string log_level;
```

### U2. `src/ConfigReader.cpp` — Remove parsing code

Remove the `log.level` parsing block (~lines 130-132):
```cpp
if (logObj.contains("level") && logObj["level"].is_string()) {
    config.log_level = logObj["level"].as_string().c_str();
}
```

### U3. `bin/config.json` — Remove dead key

Remove line 21:
```json
"level": "INFO",
```

### U4. (If present) `bin/test_config*.json` — Same removal

Check and clean any test config files that also contain `log.level`.

## Verification

1. **Build**: `cmake --build build --parallel 8` — 0 errors, 0 warnings
2. **Grep confirm**: `Select-String -Pattern "log_level" -Path src\*.cpp` returns only Logger.cpp/h (not ConfigReader)
3. **Smoke test**: Run `.\bin\validproxy.exe -h` and confirm help displays correctly
4. **Config parse test**: Run with existing config.json to confirm no parse errors

## File Changes

| File | Change |
|------|--------|
| `include/ConfigReader.h` | Remove `std::string log_level;` declaration |
| `src/ConfigReader.cpp` | Remove `log.level` JSON parsing block |
| `bin/config.json` | Remove `"level": "INFO"` key |

## Verification Results

- ✅ Build: 0 errors, 0 new warnings (5 pre-existing warnings unchanged)
- ✅ `log_level` grep: no references remain in ConfigReader.cpp, ConfigReader.h, or any other non-Logger source
- ✅ ctest: 2/2 passed (CurlEasyHandleTest, DedupTest 11/11)
- ✅ Test configs (`test_config.json`, `test_config_full.json`): cleaned of `"level"` keys

## Risk

- **Low**. The field is dead code — removing it changes no behavior. ConfigReader will simply not populate `log_level`, but since nothing reads it, there is zero functional impact.
