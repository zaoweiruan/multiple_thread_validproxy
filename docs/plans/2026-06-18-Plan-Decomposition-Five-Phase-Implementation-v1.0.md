---
title: "Five-Phase Implementation Plan — Responsibility Decomposition for 5 Core Modules"
type: plan
status: completed
date: 2026-06-18
depends_on: docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md
completed: 2026-06-22
---

# Five-Phase Implementation Plan

## Overview

Implements the decomposition spec [`docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md`](../specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md).

**Goal**: Extract single-responsibility components from 5 overburdened modules while keeping existing facade APIs intact.

**Total estimated new files**: ~70 `.h` + `.cpp` pairs across 6 phases.

| Phase | Module | Risk | Est. New Files | Est. Tasks | Depends On | Status |
|-------|--------|------|---------------|-----------|------------|--------|
| 0 | — (characterization tests) | None | 0 | 5 | — | ✅ Completed |
| 1 | ConfigReader | Low | 16 | 7 | Phase 0.1, 0.2 | ✅ Completed |
| 2 | ShareLink | Low | 10 | 5 | Phase 0.3 | ✅ Completed |
| 3 | ConfigGenerator | Medium | 20 | 7 | Phase 0.4 | ✅ Completed |
| 4 | ProxyBatchTester | High | 10 | 6 | Phase 0.5, Phase 1–3 | ✅ Completed |
| 5 | AppController | Medium | 16 | 10 | All above | ✅ Completed |

---

## Phase 0: Characterization Tests (Preparation) — ✅ COMPLETED (2026-06-22)

All 115 tests pass across 4 test executables:
- **test_config_reader_load**: 30 tests — ConfigReader::load() section defaults, type coercion, path resolution
- **test_config_reader**: 4 tests — ConfigReader::save() round-trip, overwrite, custom path
- **test_sharelink**: 21 tests — Exact URI matching for all 6 protocols, reality params, Chinese remarks
- **test_config_generator**: 13 tests (1 skipped) — Network normalization gap documented (in loadProfiles, not generateConfig)
- **test_proxy_batch_components**: 47 tests (NEW) — SqlTemplate, BlacklistThreshold, WorkerCount, CancelState, Summary, QueueManagement, EdgeCases

Key finding: `generateConfig()` does not use `db_` — nullptr works. Network normalization lives in `loadProfiles()`, not `generateConfig()`.

---

**Rule**: Write characterization tests BEFORE touching any production code. If a test fails after extraction, the extraction changed behavior — roll back immediately.

### Task 0.1 — ConfigReader load/save characterization

**Files to create/modify**:
- `tests/test_config_reader_load.cpp` — extend existing
- `tests/test_reader.cpp` — extend existing

**Test cases to add**:

1. **Section parser defaults** — For each config section (database, xray, test, log, subscription, dedup, notification, sync, auto_task, network_monitor), construct a minimal JSON with only that section and verify all fields get their default values.
2. **Type coercion** — Feed string `"true"` into bool fields, int `123` into string fields, verify the parser produces the expected coerced value.
3. **Save round-trip** — `load()` a known config file, then `save()` to a temp path, `load()` again, compare all fields. Use `test/guindb.db` references.
4. **Missing file** — Verify `load("nonexistent.json")` returns `std::nullopt` or the error is reported via `errorReporter_`.
5. **Path resolution** — Test relative paths like `./worker/guindb.db` are resolved relative to the config file's parent directory.

**Data file**: Use `test/guindb.db` and modify `bin/test_config.json` as needed.

**Acceptance**: All 5 new test groups pass with `ctest -R "ConfigReader|test_reader"`.

---

### Task 0.2 — ConfigReader save path & field coverage

**Files to modify**:
- `tests/test_config_reader_load.cpp`

**Test cases**:

1. **Save to custom path** — `save(config, "custom_path.json")` writes a valid JSON.
2. **Field completeness** — After round-trip, every field in `AppConfig` (including nested structs like `TestConfig`, `XrayConfig`) is non-empty and matches input.
3. **Overwrite behavior** — Save then save again; content is updated, not appended.

**Acceptance**: Tests pass, coverage confirms all `AppConfig` fields round-trip.

---

### Task 0.3 — ShareLink toShareUri characterization

**Files to modify**:
- `tests/test_sharelink.cpp` — extend existing

**Test cases**:

1. **VLESS URI** — Provide a `Profileitem` with `configType="vless"` and known fields; capture exact output string as expected value.
2. **VMess URI** — Same for `configType="vmess"`.
3. **Trojan URI** — Same for `configType="trojan"`.
4. **SS URI** — Same for `configType="ss"`.
5. **Hysteria2 URI** — Same for `configType="hysteria2"`.
6. **TUIC URI** — Same for `configType="tuic"`.
7. **Unsupported type** — `configType="unknown"` returns empty string.
8. **Chinese remarks** — Remark contains Chinese characters; verify URL-encoded output.

**Design choice**: Use exact string matching in tests. If the URI format ever needs to change, these tests will force a deliberate decision.

**Acceptance**: All new tests pass with `ctest -R ShareLink`.

---

### Task 0.4 — ConfigGenerator generateConfig characterization

**Files to modify**:
- `tests/test_config_generator.cpp` — extend existing; add builder-level tests without SQLite dependency

**Test cases** (builder-level, no SQLite):

1. **Generate VLESS outbound** — Call `ConfigGenerator::generateConfig()` with a real `Profileitem` (constructed in test). Verify output JSON contains `"outbounds"` with correct protocol.
2. **Network normalization** — Profile has `network=""` → output has `"network":"tcp"`. Profile has `network="splithttp"` → output has `"network":"xhttp"`.
3. **REALITY settings** — Profile has `security="reality"` → output includes `"realitySettings"` with `"serverName"`, `"publicKey"`.
4. **TLS settings** — Profile has `security="tls"` → output includes `"tlsSettings"`.

**Acceptance**: All new tests pass with `ctest -R "ConfigGenerator"`.

---

### Task 0.5 — ProxyBatchTester pure-logic characterization

**Files to create/modify**:
- `tests/test_proxy_batch_components.cpp` — new file

**Test cases** (no Xray, no SQLite — pure logic):

1. **SQL template substitution** — Simulate the SQL template with `{subid}` placeholder; verify replaced correctly.
2. **Blacklist threshold** — Simulate SQL with `{blacklist_threshold}`; verify replaced with config value.
3. **Worker count calculation** — `calculateXrayInstanceCount(proxies=10, workers=3)` returns `3`; `proxies=1, workers=3` returns `1`; `proxies=0, workers=3` returns `0`.
4. **Cancel state merging** — Verify `isCancelled()` returns `true` when internal `cancelRequested_` OR external cancel flag is set.
5. **Summary format** — Call the summary formatting (when extracted) and verify output contains "Success: X, Failed: Y, Total: Z".

**Acceptance**: All tests pass with `ctest -R "ProxyBatch"`.

---

## Phase 1: ConfigReader Decomposition (Low Risk) — ✅ COMPLETED (2026-06-22)

**Principle**: Extract components bottom-up. Each new class is independently testable. `ConfigReader::load()` and `save()` remain unchanged in signature — they internally delegate.

### Task 1.1 — Create `ConfigFileStore`

**Files**:
- `include/config/ConfigFileStore.h`
- `src/config/ConfigFileStore.cpp`

**Interface**:
```cpp
class ConfigFileStore {
public:
    std::string read(const std::string& path);  // throws on IO error
    void write(const std::string& path, const std::string& content);
};
```

**Extract from**: `ConfigReader::load()` file-reading portion.

**Tests**:
- Read existing file succeeds.
- Read nonexistent file throws.
- Write then read round-trip.
- Write creates parent directories if missing.

**Note**: The current `ConfigReader` may use `std::ifstream` with custom error handling. Preserve exact error messages.

---

### Task 1.2 — Create `ConfigPathResolver`

**Files**:
- `include/config/ConfigPathResolver.h`
- `src/config/ConfigPathResolver.cpp`

**Interface**:
```cpp
class ConfigPathResolver {
public:
    explicit ConfigPathResolver(const std::string& exeDir);
    std::string resolve(const std::string& relativePath) const;
    std::string defaultConfigPath() const;
    std::string exeDir() const;
    static std::string detectExeDir(int argc, char* argv[]);
};
```

**Extract from**: `ConfigReader` constructor path resolution + `getDefaultConfigPath()`.

**Tests**:
- Relative path `./worker/guindb.db` resolves correctly.
- Absolute path returned as-is.
- Default config path returns `<exeDir>/../bin/config.json`.

---

### Task 1.3 — Create `ConfigJsonParser`

**Files**:
- `include/config/ConfigJsonParser.h`
- `src/config/ConfigJsonParser.cpp`

**Interface**:
```cpp
class ConfigJsonParser {
public:
    boost::json::value parse(const std::string& jsonStr);
    // Returns human-readable error on failure
    std::string lastError() const;
};
```

**Extract from**: `ConfigReader` JSON parse error handling.

**Tests**:
- Valid JSON object parses.
- Invalid JSON returns empty value and sets error.
- Empty string returns error.

---

### Task 1.4 — Create `ConfigJsonSerializer`

**Files**:
- `include/config/ConfigJsonSerializer.h`
- `src/config/ConfigJsonSerializer.cpp`

**Interface**:
```cpp
class ConfigJsonSerializer {
public:
    boost::json::object serialize(const config::AppConfig& config) const;
};
```

**Extract from**: `ConfigReader::save()` JSON construction logic.

**Tests**:
- Serialize a fully populated `AppConfig`.
- Verify all sections present.
- Round-trip: serialize → parse → compare with original.

---

### Task 1.5 — Create `ConfigValidator`

**Files**:
- `include/config/ConfigValidator.h`
- `src/config/ConfigValidator.cpp`

**Interface**:
```cpp
class ConfigValidator {
public:
    struct ValidationResult {
        bool valid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ValidationResult validate(const config::AppConfig& config, const std::string& configPath);
};
```

**Extract from**: `ConfigReader` validation checks (DB existence, xray existence, SQL placeholder checks).

**Tests**:
- DB path exists → valid.
- DB path missing → invalid with error.
- Xray executable missing → invalid or warning.
- Bad SQL placeholder → warning.

---

### Task 1.6 — Create Section Parsers (10 parsers)

**Files** — create 10 section parser files in `include/config/sections/`:

| # | Parser | Fields Handled |
|---|--------|---------------|
| 1 | `DatabaseConfigParser.h` | `database.path`, `database.sql_query`, `database.sql_by_subid`, `database.reset_on_start` |
| 2 | `XrayConfigParser.h` | `xray.executable`, `xray.config_dir`, `xray.log_level`, `xray.workers`, `xray.capi_port` |
| 3 | `TestConfigParser.h` | `test.connection_timeout`, `test.request_timeout`, `test.retry_count`, `test.max_redirects` |
| 4 | `LogConfigParser.h` | `log.file_level`, `log.network_failures`, `log.sql_log_enabled` |
| 5 | `SubscriptionConfigParser.h` | `subscription.*` |
| 6 | `DedupConfigParser.h` | `dedup.enabled`, `dedup.blacklist_threshold`, `dedup.similarity_threshold` |
| 7 | `NotificationConfigParser.h` | `notification.enabled`, `notification.on_update`, `notification.on_test` |
| 8 | `SyncConfigParser.h` | `sync.target_db`, `sync.mode`, `sync.schedule_interval` |
| 9 | `AutoTaskConfigParser.h` | `auto_task.enabled`, `auto_task.schedule_cron` |
| 10 | `NetworkMonitorConfigParser.h` | `network_monitor.enabled`, `network_monitor.target_url`, `network_monitor.timeout`, `network_monitor.interval` |

**Interface pattern**:
```cpp
class DatabaseConfigParser {
public:
    void parse(const boost::json::value& root, config::AppConfig& config, const std::string& exeDir);
};
```

**Tests**: Each parser gets its own test verifying:
- Section present → correct field values.
- Section missing → default values.
- Type mismatch → warning reported + default value used.

**Location**: `tests/test_config_section_parsers.cpp` (single file, multiple TEST blocks).

---

### Task 1.7 — Refactor `ConfigReader::load()` to delegate

**Files to modify**:
- `include/ConfigReader.h` — keep public interface unchanged; update private members
- `src/ConfigReader.cpp` — rewrite `load()` and `save()` to delegate

**New `ConfigReader` private members**:
```cpp
class ConfigReader {
private:
    ConfigFileStore fileStore_;
    ConfigJsonParser jsonParser_;
    ConfigJsonSerializer jsonSerializer_;
    ConfigValidator validator_;
    ConfigPathResolver pathResolver_;
    // Section parsers as member or local variables in load()
};
```

**Changes to `load()`**:
```cpp
// Before: 200+ lines of inline parsing
// After: ~30 lines of orchestration delegating to components
```

**Tests**: Existing round-trip tests still pass (Phase 0 characterization).

**Acceptance**: All existing `test_config_reader_load` and `test_reader` tests pass. No behavioral change in `load()` output.

---

## Phase 2: ShareLink Decomposition (Low Risk) — ✅ COMPLETED (2026-06-22)

### Task 2.1 — Create `UriCodec`

**Files**:
- `include/share/UriCodec.h`
- `src/share/UriCodec.cpp`

**Interface**:
```cpp
class UriCodec {
public:
    static std::string base64Encode(const std::string& input, bool urlSafe = false);
    static std::string base64Decode(const std::string& input);
    static std::string urlEncodeStandard(const std::string& input);       // aggressive encoding
    static std::string urlEncodeRemarksOrPath(const std::string& input);  // selective encoding
    static std::string jsonEncode(const std::string& input);
};
```

**Extract from**: `ShareLink::urlEncode()` (aggressive) and anonymous `urlEncode()` (selective) — KEEP BOTH under different names.

**Tests**:
- `base64Encode("hello")` matches known output.
- `base64Decode(base64Encode(x)) == x`.
- `urlEncodeStandard("a b")` → `"a%20b"`.
- `urlEncodeRemarksOrPath("a b")` → `"a%20b"`; `urlEncodeRemarksOrPath("a=b")` preserves `=`? Check original behavior.
- `jsonEncode` produces quoted JSON-safe string.

---

### Task 2.2 — Create `Ipv6Formatter`

**Files**:
- `include/share/Ipv6Formatter.h`
- `src/share/Ipv6Formatter.cpp`

**Interface**:
```cpp
class Ipv6Formatter {
public:
    static std::string formatAddress(const std::string& address);
};
```

**Extract from**: `ShareLink::formatAddressForShareLink()` (or inline IPv6 wrapping logic — brackets around IPv6).

**Tests**:
- `192.168.1.1` → `192.168.1.1`.
- `::1` → `[::1]`.
- `2001:db8::1` → `[2001:db8::1]`.
- Empty → empty.

---

### Task 2.3 — Create Protocol URI Builders (6 builders)

**Files** — one per protocol:

| File | Builder | Extracts from ShareLink |
|------|---------|------------------------|
| `include/share/VLESSUriBuilder.h` / `src/share/VLESSUriBuilder.cpp` | `VLESSUriBuilder::build()` | VLESS URI + TUIC URI logic |
| `include/share/VMessUriBuilder.h` / `src/share/VMessUriBuilder.cpp` | `VMessUriBuilder::build()` | VMess JSON-based URI |
| `include/share/TrojanUriBuilder.h` / `src/share/TrojanUriBuilder.cpp` | `TrojanUriBuilder::build()` | Trojan `trojan://` URI |
| `include/share/SSUriBuilder.h` / `src/share/SSUriBuilder.cpp` | `SSUriBuilder::build()` | Shadowsocks `ss://` URI |
| `include/share/Hysteria2UriBuilder.h` / `src/share/Hysteria2UriBuilder.cpp` | `Hysteria2UriBuilder::build()` | Hysteria2 `hysteria2://` URI |
| Already covered in VLESSUriBuilder | — | TUIC (same format as VLESS essentially) |

**Each builder interface**:
```cpp
class VLESSUriBuilder {
public:
    std::string build(const db::models::Profileitem& profile);
};
```

**Tests**: Each builder gets exact-output tests matching the Phase 0 characterization snapshots.

---

### Task 2.4 — Create `ShareLinkFactory`

**Files**:
- `include/share/ShareLinkFactory.h`
- `src/share/ShareLinkFactory.cpp`

**Interface**:
```cpp
class ShareLinkFactory {
public:
    explicit ShareLinkFactory(std::shared_ptr<UriCodec> codec);
    std::string toShareUri(const db::models::Profileitem& profile);
    // Returns empty string for unsupported types
};
```

**Routing logic**: Switch on `profile.configType` → delegate to builder. Same as `ShareLink::toShareUri()` today.

**Tests**:
- Route VLESS → VLESSUriBuilder produces correct URI.
- Route unsupported → empty.
- Invalid builder → return empty.

---

### Task 2.5 — Refactor `ShareLink` facade

**Files to modify**:
- `include/ShareLink.h` — keep `static toShareUri(...)` as delegation
- `src/ShareLink.cpp` — rewrite to delegate to factory

```cpp
// ShareLink.cpp after decomposition
std::string ShareLink::toShareUri(const db::models::Profileitem& profile) {
    static ShareLinkFactory factory(std::make_shared<UriCodec>());
    return factory.toShareUri(profile);
}
```

**Note**: The `static` factory avoids allocation per call. If thread safety is a concern (it shouldn't be, it's stateless), use local instance.

**Acceptance**: All Phase 0 characterization tests still pass. No change in URI output.

---

## Phase 3: ConfigGenerator Decomposition (Medium Risk) — ✅ COMPLETED (2026-06-22)

### Task 3.1 — Create `ProfileConfigRepository`

**Files**:
- `include/config/ProfileConfigRepository.h`
- `src/config/ProfileConfigRepository.cpp`

**Interface**:
```cpp
class ProfileConfigRepository {
public:
    explicit ProfileConfigRepository(sqlite3* db);
    std::vector<db::models::Profileitem> loadProfiles(const std::string& subId = "");
    std::vector<db::models::ProfileExItem> loadProfileExItems();
    bool updateProfileExItem(const db::models::ProfileExItem& item);
};
```

**Extract from**: `ConfigGenerator::loadProfiles()`, `loadProfileExItems()`, `updateProfileExItem()`.

**Tests**: Use a test SQLite DB with known profiles; verify all three CRUD methods.

---

### Task 3.2 — Create `ProfileNormalizer`

**Files**:
- `include/config/ProfileNormalizer.h`
- `src/config/ProfileNormalizer.cpp`

**Interface**:
```cpp
class ProfileNormalizer {
public:
    void normalize(db::models::Profileitem& profile) const;
    static std::string normalizeNetwork(const std::string& network);
};
```

**Extract from**: `ConfigGenerator::ProfilesNormalized()` or inline normalization logic.

**Rules** (preserve existing behavior):
- Empty network → `"tcp"`.
- `"splithttp"` → `"xhttp"`.
- Invalid network → `"tcp"` (fallback, not skip).
- `"h2"` → `"grpc"` (if this exists in current code).

**Tests**:
- Input `""` → output `"tcp"`.
- Input `"splithttp"` → output `"xhttp"`.
- Input `"weird_protocol"` → output `"tcp"`.
- Input `"tcp"` → unchanged `"tcp"`.
- Input `"ws"` → unchanged `"ws"`.

---

### Task 3.3 — Create `ProfileConfigValidator`

**Files**:
- `include/config/ProfileConfigValidator.h`
- `src/config/ProfileConfigValidator.cpp`

**Interface**:
```cpp
struct ProfileValidationResult {
    bool valid;
    std::string reason;  // empty if valid
};

class ProfileConfigValidator {
public:
    ProfileValidationResult validate(const db::models::Profileitem& profile) const;
};
```

**Extract from**: Inline validation in `ConfigGenerator::generateConfig()` or `ConfigReader::validate()` that checks profile-level fields.

**Checks** (preserve current behavior):
- Address must be non-empty.
- Port must be 1–65535.
- ID must be non-empty for VLESS/VMess/Trojan.
- REALITY: publicKey and serverName must be non-empty.

**Tests**: Each check separately; valid profile passes; invalid fails with reason.

---

### Task 3.4 — Create `StreamSettingsBuilder`

**Files**:
- `include/config/StreamSettingsBuilder.h`
- `src/config/StreamSettingsBuilder.cpp`

**Interface**:
```cpp
class StreamSettingsBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile) const;
};
```

**Extract from**: `ConfigGenerator::buildStreamSettings()` or equivalent.

**Supported settings** (preserve all):
- `network` → transport type (tcp/ws/grpc/xhttp/kcp).
- `security` → `"tls"` or `"reality"`.
- TLS fields: `serverName`, `fingerprint`, `alpn`, `allowInsecure`.
- REALITY fields: `serverName`, `fingerprint`, `publicKey`, `shortId`, `spiderX`.
- WebSocket: `path`, `host`.
- gRPC: `serviceName`.
- xHTTP: `path`, `host`, `mode` (if applicable).
- kcp: `mtu`, `tti`, `uplinkCapacity`, `congestion`.

**Tests**: Each transport type produces correct `streamSettings` JSON. Use `boost::json::value` comparison.

---

### Task 3.5 — Create `OutboundBuilderFactory` + Outbound Builders (8 builders)

**Files**:

| File | Builder |
|------|---------|
| `include/config/OutboundBuilderFactory.h` / `src/config/OutboundBuilderFactory.cpp` | `OutboundBuilderFactory` — routes configType to builder |
| `include/config/outbound/VLESSOutboundBuilder.h` / `src/config/outbound/VLESSOutboundBuilder.cpp` | VLESS |
| `include/config/outbound/VMessOutboundBuilder.h` / `src/config/outbound/VMessOutboundBuilder.cpp` | VMess |
| `include/config/outbound/SSOutboundBuilder.h` / `src/config/outbound/SSOutboundBuilder.cpp` | Shadowsocks |
| `include/config/outbound/TrojanOutboundBuilder.h` / `src/config/outbound/TrojanOutboundBuilder.cpp` | Trojan |
| `include/config/outbound/SOCKSOutboundBuilder.h` / `src/config/outbound/SOCKSOutboundBuilder.cpp` | SOCKS |
| `include/config/outbound/HTTPOutboundBuilder.h` / `src/config/outbound/HTTPOutboundBuilder.cpp` | HTTP |
| `include/config/outbound/Hysteria2OutboundBuilder.h` / `src/config/outbound/Hysteria2OutboundBuilder.cpp` | Hysteria2 |
| `include/config/outbound/TUICOutboundBuilder.h` / `src/config/outbound/TUICOutboundBuilder.cpp` | TUIC |
| `include/config/outbound/WireGuardOutboundBuilder.h` / `src/config/outbound/WireGuardOutboundBuilder.cpp` | WireGuard |

**Each builder interface**:
```cpp
class VLESSOutboundBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile,
                              boost::json::object streamSettings) const;
};
```

**Factory interface**:
```cpp
class OutboundBuilderFactory {
public:
    boost::json::object create(const db::models::Profileitem& profile,
                               const std::string& tag = "proxy") const;
};
```

**Tests**: Each builder produces correct outbound JSON matching the Phase 0 characterization.

---

### Task 3.6 — Create `XrayConfigAssembler`

**Files**:
- `include/config/XrayConfigAssembler.h`
- `src/config/XrayConfigAssembler.cpp`

**Interface**:
```cpp
class XrayConfigAssembler {
public:
    boost::json::object assemble(const boost::json::object& outbound) const;
    // Wraps outbound in {"outbounds": [...], ...} with required Xray config structure
};
```

**Extract from**: `ConfigGenerator::generateConfig()` final JSON assembly.

**Tests**:
- Input outbound → output contains `"outbounds"` array.
- Array contains the input outbound object.
- Top-level has `"log"`, `"inbounds"`, `"outbounds"`, `"routing"` keys.

---

### Task 3.7 — Refactor `ConfigGenerator` facade

**Files to modify**:
- `include/ConfigGenerator.h` — keep public API; update private members
- `src/ConfigGenerator.cpp` — rewrite to delegate

**New implementation pattern**:
```cpp
bool ConfigGenerator::generateConfig(const db::models::Profileitem& profile,
                                     std::string& outJson) {
    ProfileNormalizer normalizer;
    Profileitem normalized = profile;
    normalizer.normalize(normalized);

    ProfileConfigValidator validator;
    auto result = validator.validate(normalized);
    if (!result.valid) {
        Logger::log(LogLevel::ERR, "ConfigGenerator", result.reason);
        return false;
    }

    StreamSettingsBuilder streamBuilder;
    auto streamSettings = streamBuilder.build(normalized);

    OutboundBuilderFactory factory;
    auto outbound = factory.create(normalized);

    XrayConfigAssembler assembler;
    auto config = assembler.assemble(outbound);

    outJson = boost::json::serialize(config);
    return true;
}
```

**Acceptance**: All Phase 0 characterization tests still pass. `--generator <id>` CLI output unchanged.

---

## Phase 4: ProxyBatchTester Decomposition (High Risk) — ✅ COMPLETED (2026-06-22)

### Task 4.1 — Create `ProxyBatchQuery`

**Files**:
- `include/test/ProxyBatchQuery.h`
- `src/test/ProxyBatchQuery.cpp`

**Interface**:
```cpp
class ProxyBatchQuery {
public:
    ProxyBatchQuery(sqlite3* db, const config::AppConfig& config);
    std::vector<db::models::Profileitem> loadAll();
    std::vector<db::models::Profileitem> loadBySubId(const std::string& subId);
    std::vector<db::models::Profileitem> loadByIndexId(const std::string& indexId);

private:
    std::string substituteSql(const std::string& template_sql,
                              const std::string& subId) const;
    sqlite3* db_;
    config::AppConfig config_;
};
```

**Extract from**: `ProxyBatchTester::loadProxies()` + SQL template substitution.

**Tests**: Pure logic + mock DB. Verify `{subid}` and `{blacklist_threshold}` replacement.

---

### Task 4.2 — Create `XrayWorkerPool`

**Files**:
- `include/test/XrayWorkerPool.h`
- `src/test/XrayWorkerPool.cpp`

**Interface**:
```cpp
struct XrayWorkerInfo {
    int workerId;
    int socksPort;
    int apiPort;
};

class XrayWorkerPool {
public:
    XrayWorkerPool(const config::AppConfig& config, XrayManager* manager);
    int calculateWorkerCount(int proxyCount) const;
    bool startWorkers(int count);
    void stopAll();
    std::vector<XrayWorkerInfo> getActiveWorkers() const;
};
```

**Extract from**: `ProxyBatchTester::calculateXrayInstanceCount()` + `startXrayInstances()`.

**Tests**: Worker count calculation — no real Xray startup. Mock `XrayManager`.

---

### Task 4.3 — Create `ProxyTestCounters`

**Files**:
- `include/test/ProxyTestCounters.h`
- `src/test/ProxyTestCounters.cpp`

**Interface**:
```cpp
class ProxyTestCounters {
public:
    void incrementProcessed();
    void incrementSuccess();
    void incrementFailed();
    int processed() const;
    int success() const;
    int failed() const;
    bool isComplete(int total) const;
    std::string formatSummary(int total) const;
};
```

**Extract from**: `ProxyBatchTester::testProxiesMultiThreaded()` counters + `printSummary()`.

**Tests**: Multi-threaded increment safety; summary format.

---

### Task 4.4 — Create `ProxyTestResultSink`

**Files**:
- `include/test/ProxyTestResultSink.h`
- `src/test/ProxyTestResultSink.cpp`

**Interface**:
```cpp
struct TestResultPayload {
    bool success;
    int delay;
    std::string indexId;
};

class ProxyTestResultSink {
public:
    void recordResult(const TestResultPayload& payload);
    TestResult getLastResult() const;
    bool updateDatabase(sqlite3* db, const db::models::ProfileExItem& item);
};
```

**Extract from**: LastResult tracking + DB write in workerThreadFunc.

**Tests**: Verify `getLastResult()` returns most recent recorded result.

---

### Task 4.5 — Create `ProxyTestWorker`

**Files**:
- `include/test/ProxyTestWorker.h`
- `src/test/ProxyTestWorker.cpp`

**Interface**:
```cpp
class ProxyTestWorker {
public:
    ProxyTestWorker(int workerId, int socksPort, int apiPort,
                    ProxyTestCounters* counters, ProxyTestResultSink* sink,
                    sqlite3* db, const config::AppConfig& config,
                    std::atomic<bool>* cancelFlag);

    void run(const std::vector<db::models::Profileitem>& proxies);
};
```

**Extract from**: `ProxyBatchTester::workerThreadFunc()` — the highest-risk extraction.

**Responsibilities**:
- Pop proxy from queue (or receive from caller).
- Validate proxy fields.
- Generate config via ConfigGenerator.
- Inject into Xray API.
- Call `ProxyTester::test()`.
- Write results via `ProxyTestResultSink::updateDatabase()`.
- Update counters.
- Check cancel flag.

**Tests** (injectable mode):
- Provide fake `ProxyTester` → verify counters/sink updated.
- Cancel flag set mid-test → worker stops early.
- Validate expects correct method calls in order.

---

### Task 4.6 — Refactor `ProxyBatchTester` facade

**Files to modify**:
- `include/ProxyBatchTester.h` — keep public API; minimize private members
- `src/ProxyBatchTester.cpp` — rewrite to orchestrate components

**New `ProxyBatchTester` private members**:
```cpp
class ProxyBatchTester {
private:
    ProxyBatchQuery query_;
    XrayWorkerPool workerPool_;
    ProxyTestCounters counters_;
    ProxyTestResultSink resultSink_;
    // Old members: proxies_, proxiesQueue_, queueMutex_,
    // processedCount_, workerThreads_ — mostly gone or simplified
};
```

**Acceptance**: All Phase 0 characterization tests pass. `run()`, `runWithSubId()`, `runWithIndexId()` produce same results.

---

## Phase 5: AppController Decomposition (Medium Risk) — ✅ COMPLETED (2026-06-22)

### Task 5.1 — Create `UiOperationRunner`

**Files**:
- `include/ui/UiOperationRunner.h`
- `src/ui/UiOperationRunner.cpp`

**Interface**:
```cpp
class UiOperationRunner {
public:
    UiOperationRunner();
    ~UiOperationRunner();

    bool isRunning() const;
    bool tryRun(std::function<void()> task);     // returns false if already running
    void cancel();
    bool isCancelled() const;
    void wait();

private:
    std::thread workerThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
    // Destructor: join thread if joinable, detach if not
};
```

**Extract from**: `AppController::isRunning_`, `cancelRequested_`, `workerThread_`, and the re-entry guard pattern in all `doXxx()` methods.

**Tests**:
- Two concurrent `tryRun()` → second returns false.
- Cancel → `isCancelled()` returns true.
- Destructor doesn't crash if thread never started.
- Destructor joins or detaches cleanly.

---

### Task 5.2 — Create `DatabaseConnectionService`

**Files**:
- `include/service/DatabaseConnectionService.h`
- `src/service/DatabaseConnectionService.cpp`

**Interface**:
```cpp
class DatabaseConnectionService {
public:
    explicit DatabaseConnectionService(const std::string& dbPath);
    ~DatabaseConnectionService();

    sqlite3* open();
    void close();
    sqlite3* switchDatabase(const std::string& newPath);
    sqlite3* currentDb() const;

private:
    std::string currentPath_;
    sqlite3* db_{nullptr};
    void configurePragma();  // WAL, busy timeout
};
```

**Extract from**: `AppController` constructor + `switchDatabase()`.

**Tests**:
- Open succeeds; DB path matches.
- Switch to new file succeeds; old connection closed.
- Switch to nonexistent path → returns old DB (or null — preserve current behavior).

---

### Task 5.3 — Create `ConfigService`

**Files**:
- `include/service/ConfigService.h`
- `src/service/ConfigService.cpp`

**Interface**:
```cpp
class ConfigService {
public:
    ConfigService(const config::AppConfig& cfg, ConfigReader* reader);
    config::AppConfig getConfig() const;
    bool saveConfig(const config::AppConfig& cfg);
    void maybeRestartNetworkMonitor(const config::AppConfig& oldCfg,
                                    const config::AppConfig& newCfg,
                                    NetworkMonitor* monitor);
};
```

**Extract from**: `AppController::getConfig()`, `saveConfig()`, `restartNetworkMonitor()`.

**Tests**: Config save + load round-trip; network monitor restart trigger detection.

---

### Task 5.4 — Create `SubscriptionService`

**Files**:
- `include/service/SubscriptionService.h`
- `src/service/SubscriptionService.cpp`

**Interface**:
```cpp
class SubscriptionService {
public:
    explicit SubscriptionService(sqlite3* db);
    std::vector<db::models::Subitem> loadAll();
    bool updateEnabled(const std::string& id, bool enabled);
    bool update(const db::models::Subitem& sub);
    bool remove(const std::string& subId);
    bool removeProxies(const std::string& subId);
    bool import(const db::shared::ShareLink& link);
    void doUpdate(const std::string& subId, ProgressCallback cb);
    void doUpdateAll(ProgressCallback cb);
};
```

**Extract from**: `AppController` subscription-related methods (load/update/delete/import).

**Tests**: Use test DB; verify CRUD operations.

---

### Task 5.5 — Create `ProxyListService`

**Files**:
- `include/service/ProxyListService.h`
- `src/service/ProxyListService.cpp`

**Interface**:
```cpp
class ProxyListService {
public:
    explicit ProxyListService(sqlite3* db);
    std::vector<db::models::Profileitem> load(const std::string& subId = "");
    std::unordered_map<std::string, int> countBySubId();
    std::unordered_map<std::string, int> countValidBySubId();
    std::optional<db::models::Profileitem> getByIndexId(const std::string& indexId);
    std::vector<db::models::ProfileExItem> loadResults();
};
```

**Extract from**: `AppController` proxy-list related methods.

**Tests**: Use test DB; verify counts and queries.

---

### Task 5.6 — Create `ProxyTestService`

**Files**:
- `include/service/ProxyTestService.h`
- `src/service/ProxyTestService.cpp`

**Interface**:
```cpp
class ProxyTestService {
public:
    ProxyTestService(sqlite3* db, const config::AppConfig& config,
                     const std::string& baseDir, NetworkMonitor* netMon);
    bool runBatch(const std::string& subId, std::atomic<bool>* cancel);
    bool runSingle(const std::string& indexId, std::atomic<bool>* cancel);
    bool runAll(std::atomic<bool>* cancel);
    TestResult findFirst(std::atomic<bool>* cancel);
    TestResult findBest(std::atomic<bool>* cancel);
    void cancel();
};
```

**Extract from**: `AppController` test-related methods.

**Note**: This service wraps `ProxyBatchTester`. It doesn't duplicate its logic.

**Tests**: Integration test with test DB (no real Xray — test initialization only).

---

### Task 5.7 — Create `ShareLinkExportService`

**Files**:
- `include/service/ShareLinkExportService.h`
- `src/service/ShareLinkExportService.cpp`

**Interface**:
```cpp
class ShareLinkExportService {
public:
    explicit ShareLinkExportService(sqlite3* db);
    std::tuple<bool, int, std::string> exportLinks();
    // Returns (success, count, filePath or error)
};
```

**Extract from**: `AppController::exportShareLinks()`.

**Tests**: Export from test DB; verify file created with correct number of links.

---

### Task 5.8 — Create `DatabaseMaintenanceService`

**Files**:
- `include/service/DatabaseMaintenanceService.h`
- `src/service/DatabaseMaintenanceService.cpp`

**Interface**:
```cpp
class DatabaseMaintenanceService {
public:
    explicit DatabaseMaintenanceService(sqlite3* db);
    bool deduplicate();
    bool sync(const std::string& src, const std::string& dst);
};
```

**Extract from**: `AppController::deduplicate()`, `syncDatabases()`.

**Tests**: Use test DB; verify dedup identifies duplicates; sync copies records.

---

### Task 5.9 — Create `AutoTaskService`

**Files**:
- `include/service/AutoTaskService.h`
- `src/service/AutoTaskService.cpp`

**Interface**:
```cpp
class AutoTaskService {
public:
    AutoTaskService(sqlite3* db, const config::AppConfig& config,
                    const std::string& baseDir);
    bool run(ProgressCallback cb);
    bool resume(ProgressCallback cb);
};
```

**Extract from**: `AppController::runAutoTaskAsync()`, `resumeAutoTaskAsync()`.

**Tests**: Mock AutoTaskManager; verify orchestration.

---

### Task 5.10 — Refactor `AppController` facade

**Files to modify**:
- `include/ui/AppController.h` — keep all public methods; replace private members
- `src/ui/AppController.cpp` — delegate each public method to appropriate service

**New `AppController` private members**:
```cpp
class AppController {
private:
    UiOperationRunner operationRunner_;
    DatabaseConnectionService dbService_;
    ConfigService configService_;
    SubscriptionService subscriptionService_;
    ProxyListService proxyListService_;
    ProxyTestService proxyTestService_;
    ShareLinkExportService exportService_;
    DatabaseMaintenanceService maintenanceService_;
    AutoTaskService autoTaskService_;
    NetworkMonitor netMon_;
};
```

**Example refactored method**:
```cpp
void AppController::testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
    operationRunner_.runAsync([this, subId, wxHandler]() {
        bool success = proxyTestService_.runBatch(subId, operationRunner_.cancelFlag());
        wxQueueEvent(wxHandler, new wxThreadEvent(success ? EVT_TEST_COMPLETE : EVT_TEST_FAILED));
    });
}
```

**Acceptance**:
- All existing UI behavior preserved.
- No new `auto` keyword introduced.
- Build passes: `cmake --build build --parallel 8`.
- All tests pass: `ctest -V`.

---

## CMake Changes Summary

To be applied incrementally with each phase:

| Phase | CMake Change |
|-------|-------------|
| 0 | Add `test_proxy_batch_components.cpp` to test target |
| 1 | Add `src/config/*.cpp` and `src/config/sections/*.cpp` to `CORE_SOURCES` |
| 2 | Add `src/share/*.cpp` to `CORE_SOURCES` |
| 3 | Add `src/config/outbound/*.cpp` to `CORE_SOURCES` (extend existing config glob) |
| 4 | Add `src/test/*.cpp` to `CORE_SOURCES` |
| 5 | Add `src/service/*.cpp` to `CORE_SOURCES` |

**Recommended pattern**:
```cmake
file(GLOB CONFIG_SOURCES    src/config/*.cpp src/config/sections/*.cpp src/config/outbound/*.cpp)
file(GLOB SHARE_SOURCES     src/share/*.cpp)
file(GLOB TEST_SOURCES      src/test/*.cpp)
file(GLOB SERVICE_SOURCES   src/service/*.cpp)
```

---

## Verification Criteria

After each phase, the following must hold:

1. **Build**: `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8` succeeds.
2. **Tests**: `ctest -V` shows all tests pass (add phase-specific `-R` filter during development).
3. **No `auto` leak**: `grep -Rn "\bauto\s\+" include/ src/ tests/` returns only false positives (comments, strings) or legitimate iterator patterns.
4. **CLI integration**: `.\build\validproxy.exe --help` still works.
5. **Behavioral parity**: No existing test output changes (Phase 0 characterizations act as regression guard).

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| Extracted component changes URI output | Low | Medium | Phase 0 exact-match tests catch any deviation |
| Section parser misses a field | Medium | Low | Round-trip test covers all AppConfig fields |
| Worker extraction breaks threading | Low | High | Phase 4 extracted last; keep facade as thin wrapper initially |
| AppController service extraction misses event wiring | Medium | Medium | Keep all public event methods; only change internal delegation |
| CMake glob accidentally excludes new files | Low | Medium | Verify build after each new file addition |
| `auto` keyword creeps in during extraction | Medium | Low | Run grep check after each phase; fix immediately |
| Phase 5 is too large in one go | Medium | Medium | Can split: do UiOperationRunner + 3 services first, then remaining 5 |

---

## Phase Dependency Graph

```
Phase 0 (characterization tests)
  ├── Phase 1 (ConfigReader) — no dependencies beyond Phase 0
  ├── Phase 2 (ShareLink) — no dependencies beyond Phase 0
  └── Phase 3 (ConfigGenerator) — no dependencies beyond Phase 0
        │
        └── Phase 4 (ProxyBatchTester) — ideally has Phase 1 & 3 done
              │
              └── Phase 5 (AppController) — depends on all above
```

Phases 1, 2, 3 can be done in parallel by different developers. Phases 4 and 5 are sequential.

---

## State Tracking

This plan's status is tracked in:
- `docs/INDEX.md` §7.5 (规范化设计) — spec reference
- `docs/plans/project-plans-tracker.md` — global plan index

**Status**: ✅ ALL PHASES COMPLETED (2026-06-22)

All 6 phases (0–5) have been implemented, tested, and committed. The responsibility decomposition is complete.
