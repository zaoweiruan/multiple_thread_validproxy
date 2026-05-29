---
title: "feat: Add ConfigType column and move IndexId to last in ProxyListPanel"
type: feat
status: completed
date: 2026-05-20
---

# ProxyListPanel: Add ConfigType Column and Move IndexId

## Changes

### 1. Add ConfigType Protocol Type Column
- New "Type" column displaying protocol name
- ConfigType mapping (from v2rayn EConfigType.cs):

| Value | Protocol |
|-------|----------|
| 1 | VMess |
| 2 | Custom |
| 3 | Shadowsocks |
| 4 | SOCKS |
| 5 | VLESS |
| 6 | Trojan |
| 7 | Hysteria2 |
| 8 | TUIC |
| 9 | WireGuard |
| 10 | HTTP |
| 11 | Anytls |
| 12 | Naive |

### 2. Move IndexId to Last Column
- Old position: Column 0 (hidden)
- New position: Column 8 (last)
- Reason: IndexId used internally, doesn't need early visibility

## Files Changed
| File | Changes |
|------|---------|
| `src/ui/ProxyListPanel.cpp` | Updated column enum, added Type column, fixed configTypeToName() |

## Bug Fix Notes
- **2026-05-21**: Fixed ConfigType mapping to match v2rayn EConfigType
- **2026-05-21**: Fixed duplicate event binding and std::sort indentation for Host column sorting