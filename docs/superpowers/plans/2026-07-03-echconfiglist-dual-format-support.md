# echconfiglist Dual Format Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse echconfiglist to distinguish between Base64-encoded ECH config and DNS URL format, generating correct sing-box `ech.config` (for Base64) or `ech.enabled: true` (for DNS URL).

**Architecture:** Add a helper function `parseEchConfigList` in SingBoxVLESSOutboundBuilder that inspects the echconfiglist string. If it contains a space, treat it as DNS URL format (keep `ech.enabled: true`). Otherwise, treat it as Base64 ECH config (decode and populate `ech.config` array).

**Tech Stack:** C++17, Boost.JSON, base64 decoding

---

## Task 1: Add Base64 decode helper function

**Files:**
- Create: `include/utils/Base64Utils.h`
- Create: `src/utils/Base64Utils.cpp`

- [ ] **Step 1: Create Base64Utils header**

```cpp
// include/utils/Base64Utils.h
#ifndef BASE64_UTILS_H
#define BASE64_UTILS_H

#include <string>
#include <vector>

namespace utils {

// Decode a Base64-encoded string to raw bytes.
// Returns empty vector on invalid input.
std::vector<uint8_t> base64Decode(const std::string& input);

// Check if a string appears to be valid Base64 (only A-Za-z0-9+/= chars, length multiple of 4)
bool looksLikeBase64(const std::string& input);

} // namespace utils

#endif // BASE64_UTILS_H
```

- [ ] **Step 2: Create Base64Utils implementation**

```cpp
// src/utils/Base64Utils.cpp
#include "utils/Base64Utils.h"
#include <sstream>
#include <iomanip>

namespace utils {

static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::vector<uint8_t> base64Decode(const std::string& input) {
    std::vector<uint8_t> decoded;
    if (input.empty()) {
        return decoded;
    }

    // Basic validation: length must be multiple of 4
    if (input.length() % 4 != 0) {
        return decoded;
    }

    // Check if all characters are valid Base64
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c != '=' && base64_chars.find(c) == std::string::npos) {
            return decoded;
        }
    }

    // Decode
    unsigned int pos = 0;
    std::vector<unsigned char> buffer(4);
    std::vector<unsigned int> index(256, 0);

    for (unsigned int i = 0; i < 64; ++i) {
        index[static_cast<unsigned char>(base64_chars[i])] = i;
    }
    index[64] = 0; // '='
    index[65] = 0; // '='

    while (pos < input.length()) {
        unsigned int len = 0;
        for (unsigned int i = 0; i < 4; ++i) {
            char c = input[pos + i];
            if (c == '=') {
                buffer[i] = 0;
                if (i == 0) { len = 0; break; }
                if (i == 1) { len = 1; break; }
                if (i == 2) { len = 2; break; }
            } else {
                buffer[i] = static_cast<unsigned char>(index[static_cast<unsigned char>(c)]);
            }
            ++len;
        }
        pos += 4;

        decoded.push_back(static_cast<uint8_t>((buffer[0] << 2) | (buffer[1] >> 4)));
        if (len > 1) {
            decoded.push_back(static_cast<uint8_t>((buffer[1] << 4) | (buffer[2] >> 2)));
        }
        if (len > 2) {
            decoded.push_back(static_cast<uint8_t>((buffer[2] << 6) | buffer[3]));
        }
    }

    return decoded;
}

bool looksLikeBase64(const std::string& input) {
    if (input.empty() || input.length() % 4 != 0) {
        return false;
    }
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c != '=' && base64_chars.find(c) == std::string::npos) {
            return false;
        }
    }
    return true;
}

} // namespace utils
```

- [ ] **Step 3: Add CMakeLists.txt entries**

Add `src/utils/Base64Utils.cpp` to the source file list in `CMakeLists.txt`.

- [ ] **Step 4: Verify compilation**

Run: `cmake --build build --parallel 8`
Expected: Clean compilation with no errors.

- [ ] **Step 5: Commit**

```bash
git add include/utils/Base64Utils.h src/utils/Base64Utils.cpp CMakeLists.txt
git commit -m "feat: add Base64Utils helper for ECH config decoding"
```

---

## Task 2: Modify SingBoxVLESSOutboundBuilder to parse echconfiglist dual formats

**Files:**
- Modify: `src/config/outbound/SingBoxVLESSOutboundBuilder.cpp`

- [ ] **Step 1: Add include for Base64Utils and replace ECH handling logic**

Replace lines 67-70 in `SingBoxVLESSOutboundBuilder.cpp`:

```cpp
// OLD CODE (lines 67-70):
if (!p.echconfiglist.empty()) {
    tlsObj["ech"] = boost::json::object{ {"enabled", true} };
}

// NEW CODE:
if (!p.echconfiglist.empty()) {
    const std::string& configList = p.echconfiglist;
    
    // Detect format: DNS URL format contains a space (domain + URL)
    // Base64 format contains no spaces
    if (configList.find(' ') != std::string::npos) {
        // DNS URL format: keep ech.enabled: true for DNS-based ECH discovery
        tlsObj["ech"] = boost::json::object{ {"enabled", true} };
    } else {
        // Base64-encoded ECH config: decode and populate ech.config array
        auto decodedBytes = utils::base64Decode(configList);
        if (!decodedBytes.empty()) {
            // Convert to hex string for sing-box ech.config
            std::ostringstream hexStream;
            for (size_t i = 0; i < decodedBytes.size(); ++i) {
                hexStream << std::hex << std::setw(2) << std::setfill('0') 
                          << static_cast<int>(decodedBytes[i]);
            }
            boost::json::array configArr;
            configArr.push_back(hexStream.str());
            tlsObj["ech"] = boost::json::object{
                {"enabled", true},
                {"config", configArr}
            };
        } else {
            // Decode failed, fall back to enabled: true
            tlsObj["ech"] = boost::json::object{ {"enabled", true} };
        }
    }
}
```

- [ ] **Step 2: Add #include for Base64Utils**

Add after line 3 in `SingBoxVLESSOutboundBuilder.cpp`:
```cpp
#include "utils/Base64Utils.h"
```

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build --parallel 8`
Expected: Clean compilation with no errors.

- [ ] **Step 4: Commit**

```bash
git add src/config/outbound/SingBoxVLESSOutboundBuilder.cpp
git commit -m "feat: support dual echconfiglist formats (Base64 config + DNS URL)"
```

---

## Task 3: Update tests for dual echconfiglist format support

**Files:**
- Modify: `tests/test_singbox_vless_builder.cpp`

- [ ] **Step 1: Rename existing ECH test to DNS URL format test**

Change Test 7 (line 171) from `EchFieldsEnabled` to `EchDnsUrlFormat`:

```cpp
// ============================================================
// Test 7: DNS URL format echconfiglist → ech.enabled: true
// ============================================================
TEST(SingBoxVLESSBuilderTest, EchDnsUrlFormat) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "tcp", "tls", "sni.example.com");
    // DNS URL format: contains space → ech.enabled: true
    p.echconfiglist = "cloudflare-ech.com https://dns.alidns.com/dns-query";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("tls"));
    const auto& tls = result.at("tls").as_object();
    ASSERT_TRUE(tls.contains("ech")) << "ECH must be emitted when echconfiglist is non-empty";
    EXPECT_TRUE(tls.at("ech").as_object().at("enabled").as_bool());
    // DNS URL format: no config array, only enabled flag
    EXPECT_FALSE(tls.at("ech").as_object().contains("config"));
    EXPECT_STREQ(tls.at("server_name").as_string().c_str(), "sni.example.com");
}
```

- [ ] **Step 2: Add new test for Base64 format echconfiglist**

Add after Test 7:

```cpp
// ============================================================
// Test 8: Base64 format echconfiglist → ech.config array
// ============================================================
TEST(SingBoxVLESSBuilderTest, EchBase64Format) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "tcp", "tls", "sni.example.com");
    // Base64 format: no space → decode and populate ech.config
    p.echconfiglist = "AEX+DQBB3QAgACDKVB2V7yDpI20qncxRiGsIDR1ruSTtoup5ksC/+InUAwAEAAEAAAY2xvdWRmbGFyZS1lY2guY29tAAA=";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("tls"));
    const auto& tls = result.at("tls").as_object();
    ASSERT_TRUE(tls.contains("ech")) << "ECH must be emitted when echconfiglist is non-empty";
    EXPECT_TRUE(tls.at("ech").as_object().at("enabled").as_bool());
    // Base64 format: must have config array
    ASSERT_TRUE(tls.at("ech").as_object().contains("config"));
    const auto& configArr = tls.at("ech").as_object().at("config").as_array();
    EXPECT_EQ(configArr.size(), 1);
    // config should be a hex-encoded string of the decoded base64
    std::string configHex = configArr[0].as_string().c_str();
    EXPECT_GT(configHex.length(), 0) << "ECH config hex should not be empty";
    EXPECT_STREQ(tls.at("server_name").as_string().c_str(), "sni.example.com");
}

// ============================================================
// Test 9: ECH fields are omitted when echconfiglist is empty
// ============================================================
TEST(SingBoxVLESSBuilderTest, EchFieldsNotSet) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "tcp", "tls", "sni.example.com");
    // echconfiglist is empty — no ECH in output
    p.echconfiglist = "";
    p.echforcequery = "";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("tls"));
    const auto& tls = result.at("tls").as_object();
    EXPECT_FALSE(tls.contains("ech")) << "ECH must NOT be emitted when echconfiglist is empty";
    EXPECT_STREQ(tls.at("server_name").as_string().c_str(), "sni.example.com");
}
```

- [ ] **Step 3: Renumber subsequent tests**

Update Test 8 → Test 9, Test 9 → Test 10, Test 10 → Test 11, Test 11 → Test 12.

- [ ] **Step 4: Run tests to verify**

Run: `ctest -R SingBoxVLESSBuilderTest -V`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add tests/test_singbox_vless_builder.cpp
git commit -m "test: add dual echconfiglist format tests (Base64 + DNS URL)"
```

---

## Task 4: Integration verification

**Files:**
- None (manual verification)

- [ ] **Step 1: Build full project**

Run: `cmake --build build --parallel 8`
Expected: Clean compilation.

- [ ] **Step 2: Run all tests**

Run: `ctest -V`
Expected: All tests pass, no regressions.

- [ ] **Step 3: Verify sing-box config generation**

Generate a sing-box config for profile with Base64 echconfiglist and verify:
- `ech.config` contains hex-encoded decoded ECH config
- No `ech.enabled: true` without `config` array

Generate a sing-box config for profile with DNS URL echconfiglist and verify:
- `ech.enabled: true` only (no config array)

- [ ] **Step 4: Commit**

```bash
git add -u
git commit -m "verify: integration test for dual echconfiglist format support"
```

---

## Notes for Implementer

1. **sing-box ech.config format**: sing-box expects `ech.config` to be an array of hex-encoded ECH config bytes. The Base64 input from Xray needs to be decoded to bytes, then hex-encoded for sing-box.

2. **Multiple Base64 configs**: If echconfiglist contains multiple Base64 configs separated by commas, split them and add each to the config array. For now, single config is assumed.

3. **Fallback behavior**: If Base64 decoding fails, fall back to `ech.enabled: true` to maintain backward compatibility.

4. **DNS URL format edge case**: The DNS URL format `encryptedsni.com+https://dns.alidns.com/dns-query` contains a `+` but also a space, so space-based detection correctly identifies it as DNS URL format.

5. **Do NOT use `auto`** - project constraint from AGENTS.md §1.
