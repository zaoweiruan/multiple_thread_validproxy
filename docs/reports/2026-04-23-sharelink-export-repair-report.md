---
title: "report(ShareLink): 分享链接导出修复完成报告"
type: report
status: completed
date: 2026-04-23
origin: ".kilo/plans/1776931315746-sunny-rocket.md"
---

# Share Link Export Repair Plan ✅ IMPLEMENTATION COMPLETE

## Implementation Status ✅ ALL FIXES VERIFIED

**Latest Verification**: 2026-04-23 16:41
- **Export File**: `proxies_20260423_164037.txt`
- **Status**: All 5 critical issues successfully resolved
- **Compatibility**: Full v2rayN share link format compliance achieved

### Verified Fixes:
1. ✅ **Path Parameter**: `path=%2F` now preserved in all applicable entries
2. ✅ **ECH Encoding**: Parameters properly URL-encoded (`%2B` not `+`)
3. ✅ **TLS Logic**: Parameters only added to TLS connections
4. ✅ **VMess Payload**: `insecure` flags correctly preserved
5. ✅ **Path Query**: Proper `?` separator encoding maintained

## Problem Analysis

After detailed comparison of `v2rayn_sharelink.txt` with the code-generated export file `proxies_20260423_154557.txt`, the following critical differences were identified:

### Key Differences

1. **Missing `path=%2F` Parameter (CRITICAL)**:
   - **11 lines affected** (1,3,4,14,16,60,61,116,121,177,184)
   - Code export completely removes `&path=%2F` from vless URLs
   - **Impact**: Breaks proxy connectivity - changes request path from `/` to empty string

2. **ECH Parameter URL-Decoded (HIGH)**:
   - **6 lines affected** (2,22,26,81,103,106)
   - Code export has decoded characters: `ech=cloudflare-ech.com+https://dns.alidns.com/dns-query`
   - Reference has encoded: `ech=cloudflare-ech.com%2Bhttps%3A%2F%2Fdns.alidns.com%2Fdns-query`
   - **Impact**: Invalid URL format (unencoded `+` in query parameters)

3. **Incorrect TLS Parameters Added (MEDIUM)**:
   - **9 lines affected** (3,4,14,16,48,61,65,119,172)
   - Code adds `insecure=0&allowInsecure=0` to `security=none` connections
   - **Impact**: Semantically incorrect - these parameters are meaningless for non-TLS connections

4. **VMess Base64 Content Altered (CRITICAL)**:
   - **2 lines affected** (134,194)
   - Code export changes `insecure: 0` to `insecure: 1` in vmess JSON payload
   - **Impact**: Alters security behavior significantly

5. **Path Parameter Malformed (HIGH)**:
   - **1 line affected** (182)
   - Code export: `path=%2Fed%3D2560` (missing `?` separator)
   - Reference: `path=%2F%3Fed%3D2560` (proper query parameter format)
   - **Impact**: Malformed URL - query parameter becomes part of path

## Root Cause

The `ShareLink::toShareUri()` and related functions in `src/ShareLink.cpp` have flawed URI construction logic:

1. **Path Parameter Filtering**: Incorrectly removes `path=%2F` assuming it's redundant, but v2ray requires explicit path specification
2. **ECH Parameter Decoding**: Incorrectly URL-decodes the ech parameter, producing invalid query strings
3. **TLS Parameter Logic**: Adds TLS-specific parameters to non-TLS connections
4. **VMess Payload Handling**: Incorrectly modifies the insecure flag in vmess configurations
5. **Path Encoding Bug**: Fails to properly encode path parameters with query strings

## Repair Implementation Plan

### Phase 1: Fix Critical Path and Parameter Issues

1. **Fix Path Parameter Removal Bug**
2. **Fix ECH Parameter Encoding**
3. **Fix TLS Parameter Logic**
4. **Fix VMess Payload Handling**
5. **Fix Path Query Parameter Encoding**

### Phase 2: Code Quality Improvements

6. **Standardize URL Encoding**
7. **Add IPv6 Support**

### Phase 3: Testing and Validation

8. **Regression Testing**
9. **Edge Case Testing**

## Expected Outcome

After fixes:
- Code export will match v2rayn_sharelink.txt format exactly
- Path parameters preserved (no incorrect removal of `path=%2F`)
- ECH parameters remain URL-encoded (`%2B` not decoded to `+`)
- TLS parameters only added to TLS connections (not to `security=none`)
- VMess payloads unchanged (insecure flag preserved)
- Path query parameters properly formatted (no missing `?` separators)
- Full compatibility with v2rayN share link format

## Files Modified
- `src/ShareLink.cpp`: Fix path parameter logic, ECH encoding, TLS parameter conditions, vmess payload handling, and path query encoding
- `src/main.cpp`: No changes needed (export format is already correct)

## Testing Command
```bash
# Test export
validproxy -TU

# Compare with reference (should be identical after fixes)
diff v2rayn_sharelink.txt proxies_TIMESTAMP.txt
```
