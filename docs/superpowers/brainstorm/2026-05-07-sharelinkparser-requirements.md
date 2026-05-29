# ShareLinkParser (fromShareUri) - Requirements Document

**Date**: 2026-05-07  
**Source**: Brainstorm session for improvement idea #3 from `docs/ideation/2026-05-07-improvement-ideas-ideation.md`  
**Status**: Draft

---

## 1. Problem Statement

The existing `ShareLink` class (in `include/ShareLink.h`, `src/ShareLink.cpp`) only supports **generating** share links via `toShareUri()`. There is no capability to **parse** share links back into structured data. This creates a gap when:

1. Reading subscription content that contains share links (currently handled in `SubitemUpdaterV2.cpp` but with manual string parsing)
2. Importing proxies from share link format
3. Validating share link correctness

### Current Gap

- `ShareLink::toShareUri()` exists → generates `vmess://...`, `vless://...`, etc.
- **No `ShareLink::fromShareUri()`** → cannot parse share links back to `ProfileItem`

### Existing Parsing (Manual, in SubitemUpdaterV2.cpp)

In `src/SubitemUpdaterV2.cpp`, there's manual parsing:
```cpp
if (line.find("vmess://") == 0) {
    // Manual base64 decode + JSON parse
} else if (line.find("vless://") == 0) {
    // Manual URI parse
} // ... etc.
```

This is duplicated, error-prone, and not reusable.

---

## 2. Requirements

### 2.1 Functional Requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| FR-1 | Add `fromShareUri(const std::string& uri)` static method to `ShareLink` class | Parser entry point |
| FR-2 | Return `db::models::ProfileItem` struct on successful parse | Matches database schema (35 fields) |
| FR-3 | Throw `std::runtime_error` on parse failure (invalid format, unsupported protocol) | Consistent with CurlEasyHandle error handling |
| FR-4 | Support parsing **vmess://** links (Base64 JSON body) | Most common format |
| FR-5 | Support parsing **vless://** links (URI with query string) | VLESS protocol |
| FR-6 | Support parsing **trojan://** links (URI with query string) | Trojan protocol |
| FR-7 | Support parsing **ss://** links (Base64 or raw URI) | Shadowsocks protocol |
| FR-8 | Support parsing **hysteria2://** links (URI with query string) | Hysteria2 protocol |
| FR-9 | Handle IPv6 addresses in URI (wrap with `[ ]` if missing) | Use existing `formatIpv6()` helper |
| FR-10 | Parse query string parameters into ProfileItem fields | sni, alpn, flow, fingerprint, etc. |
| FR-11 | Reuse existing helpers: `base64Decode()`, `urlEncode()`, `buildQueryString()` | Don't reinvent |
| FR-12 | Output ProfileItem must pass `checkRequired()` validation | Ensure required fields are populated |

### 2.2 Non-Functional Requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| NFR-1 | Must be added to existing `ShareLink` class (extend, not new class) | User chose "Extend ShareLink (Recommended)" |
| NFR-2 | Must compile on Windows/MinGW-w64 with C++17 | Project constraint |
| NFR-3 | Must use existing `ProfileItem` struct from `include/ProfileItem.h` | Output format: `ProfileItem` (User chose "ProfileItem (Recommended)") |
| NFR-4 | No new third-party dependencies (use existing OpenSSL for Base64) | Project constraint |
| NFR-5 | Error messages should be descriptive (include protocol type, parse stage) | Debuggability |

---

## 3. Design Decisions

### 3.1 Integration Approach
**Decision**: Extend existing `ShareLink` class with `fromShareUri()` static method  
**Rationale**: User chose "Extend ShareLink (Recommended)" — keeps share link logic in one place  
**Implication**: No new class file, modify `include/ShareLink.h` and `src/ShareLink.cpp`

### 3.2 Output Format
**Decision**: Return `db::models::ProfileItem` struct  
**Rationale**: User chose "ProfileItem (Recommended)" — matches database schema (35 fields)  
**Implication**: Parser must populate all relevant fields (configtype, address, port, id, etc.)

### 3.3 Error Handling
**Decision**: Throw `std::runtime_error` on parse failure  
**Rationale**: Consistent with `CurlEasyHandle` RAII wrapper (also throws `std::runtime_error`)  
**Example**:
```cpp
try {
    auto item = share::ShareLink::fromShareUri("vmess://...");
} catch (const std::runtime_error& e) {
    // e.what() contains error details
}
```

### 3.4 Protocol Dispatch
**Decision**: Use scheme prefix to dispatch to protocol-specific private methods  
**Rationale**: Clean separation of parsing logic per protocol  
**Example**:
```cpp
static ProfileItem fromVmessUri(const std::string& body);  // vmess://body
static ProfileItem fromVlessUri(const std::string& uri);    // vless://user@host:port?query
static ProfileItem fromTrojanUri(const std::string& uri);
static ProfileItem fromSsUri(const std::string& uri);
static ProfileItem fromHysteria2Uri(const std::string& uri);
```

### 3.5 JSON Parsing for vmess
**Decision**: Use existing Boost.JSON (already linked) to parse vmess Base64 JSON body  
**Rationale**: Project already uses Boost 1.88 for JSON; no new dependency  
**Implication**: `#include <boost/json.hpp>` in `ShareLink.cpp`

---

## 4. URI Format References

### 4.1 vmess:// (Base64 JSON)
```
vmess://base64encoded_json
```
Decoded JSON:
```json
{
  "v": "2",
  "ps": "remarks",
  "add": "address",
  "port": "port",
  "id": "uuid",
  "aid": "alterId",
  "scy": "security",
  "net": "network",
  "type": "headerType",
  "host": "requestHost",
  "path": "path",
  "tls": "streamSecurity",
  "sni": "sni",
  "alpn": "alpn",
  "fp": "fingerprint",
  "insecure": "allowInsecure"
}
```

### 4.2 vless:// (URI with query)
```
vless://uuid@address:port?encryption=none&security=tls&sni=...&fp=...&type=ws&path=...
```

### 4.3 trojan:// (URI with query)
```
trojan://password@address:port?security=tls&sni=...&fp=...&type=ws&path=...
```

### 4.4 ss:// (Base64 userinfo or raw)
```
ss://base64(method:password)@address:port
OR
ss://method:password@address:port
```

### 4.5 hysteria2:// (URI with query)
```
hy2://password@address:port?sni=...&alpn=...&insecure=...
```

---

## 5. Usage Examples (After Implementation)

### 5.1 Parse vmess link
```cpp
#include "ShareLink.h"
#include "ProfileItem.h"

try {
    std::string uri = "vmess://ewogICJ2IjogIjIiLAogICJwcyI6ICJyZW1hcmsg...";
    db::models::ProfileItem item = share::ShareLink::fromShareUri(uri);
    
    std::cout << "ConfigType: " << item.configtype << std::endl;
    std::cout << "Address: " << item.address << std::endl;
    std::cout << "Port: " << item.port << std::endl;
    // ... use item
} catch (const std::runtime_error& e) {
    std::cerr << "Parse failed: " << e.what() << std::endl;
}
```

### 5.2 Batch parse (replace manual parsing in SubitemUpdaterV2.cpp)
**Before** (current manual parsing):
```cpp
if (line.find("vmess://") == 0) {
    std::string b64 = line.substr(8);
    std::string json = ShareLink::base64Decode(b64);
    // ... manual JSON parse ...
}
```

**After** (using fromShareUri):
```cpp
try {
    db::models::ProfileItem item = share::ShareLink::fromShareUri(line);
    // item is ready to use
} catch (const std::runtime_error& e) {
    Logger::write("ERROR: Parse share link failed - " + std::string(e.what()), LogLevel::LOG_ERROR);
}
```

---

## 6. Acceptance Criteria

- [ ] `ShareLink::fromShareUri()` parses all 5 protocols (vmess, vless, trojan, ss, hysteria2)
- [ ] Returns `ProfileItem` with all required fields populated
- [ ] Throws `std::runtime_error` on invalid URI format
- [ ] Throws `std::runtime_error` on unsupported protocol
- [ ] Handles IPv6 addresses correctly (uses `formatIpv6()`)
- [ ] Reuses existing helpers (`base64Decode()`, `urlEncode()`, etc.)
- [ ] Uses Boost.JSON for JSON parsing (vmess)
- [ ] Compiles without errors on MinGW-w64 with C++17
- [ ] Parse output passes `ProfileItem::checkRequired()` validation
- [ ] Error messages are descriptive (include protocol, parse stage)

---

## 7. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| vmess JSON format variations | Medium | Follow v2rayN's JSON schema exactly; test with real-world links |
| Base64 decoding edge cases (padding, URL-safe) | Medium | Use existing `base64Decode()`; add URL-safe Base64 support if needed |
| Query string parsing complexity (vless/trojan/hysteria2) | Medium | Reuse `buildQueryString()` logic; parse query string into map |
| Boost.JSON not fully linked | Low | Already used in project; verify in CMakeLists.txt |
| IPv6 address parsing edge cases | Low | Use existing `isValidIpv6()` and `formatIpv6()` helpers |

---

## 8. Next Steps

1. Run `ce-plan` to create implementation plan
2. Implement `fromShareUri()` in `ShareLink` class
3. Add protocol-specific private methods (`fromVmessUri`, `fromVlessUri`, etc.)
4. Update `SubitemUpdaterV2.cpp` to use new parser (replace manual parsing)
5. Add tests for parser (create `tests/test_share_link_parser.cpp`)
6. Verify with real-world share links

---

## 9. References

- **Existing ShareLink class**: `include/ShareLink.h`, `src/ShareLink.cpp` (499 lines)
- **ProfileItem struct**: `include/ProfileItem.h` (35 fields)
- **v2rayN reference**: `E:\eclipse_workspace\v2rayn\ServiceLib\Handler\ConfigHandler.cs` (DedupServerList method)
- **Boost.JSON docs**: https://www.boost.org/doc/libs/1_88_0/libs/json/doc/html/index.html
