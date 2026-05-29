---
title: "feat: ShareLinkParser — extract and validate share links"
type: feat
status: cancelled
date: 2026-05-11
origin: "Required for subscription import: need to parse base64-encoded share links into proxy configs"
---

# feat: ShareLinkParser

## Problem

When importing subscriptions via `-IS`, share links (Base64-encoded proxy URIs) need to be decoded and parsed into internal proxy models. Currently there is no dedicated parser for this.

## Scope Boundaries

- Create: `src/ShareLinkParser.h`, `src/ShareLinkParser.cpp`
- Add tests: `tests/test_sharelinkparser.cpp`
- NOT modify: Existing subscription update flow

## Detailed Changes

### U1: ShareLinkParser class
- Decode Base64 share link strings
- Parse standard proxy URI schemes (vmess://, vless://, trojan://, ss://, ssr://)
- Return structured proxy config objects

### U2: Validation
- Validate decoded data is valid JSON (for vmess/vless)
- Validate required fields (address, port, type)
- Log warnings for malformed entries instead of crashing

### U3: Integration
- Wire into `SubitemUpdaterV2` subscription import flow
- Add unit tests for each protocol type

## Verification

- Compile pass (0 errors)
- Unit test: `test_sharelinkparser` with sample links
- Function test: `-IS` import with share link subscriptions works