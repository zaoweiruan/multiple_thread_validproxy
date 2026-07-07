# Sing-box Support Implementation Plan

## Overview
Add sing-box as an alternative proxy core for standalone proxy (opening proxies only). All other features (testing, subscription updates, auto tasks, etc.) remain Xray-based. sing-box uses a different JSON format and control API than Xray.

## Current Architecture Analysis

### Shared Configuration Fields (used by both Xray and sing-box)
- `socks_base_port` — SOCKS5 listen port for standalone proxy

### Xray-only Configuration Fields (unchanged)
- `xray_executable`, `xray_workers`, `xray_start_port`, `xray_api_port`
- `proxy.xray_asset_dir`, `proxy.template_config_path`

### sing-box-only Configuration Fields (to add)
- `singbox_executable` — sing-box executable path
- `proxy.singbox_asset_dir` — sing-box geoip/geosite resource directory
- `proxy.singbox_template_config_path` — sing-box config template path

### Key Differences: Xray vs Sing-box

**Xray outbound structure:**
```json
{
  "outbounds": [{
    "protocol": "vless",
    "settings": { "vnext": [{"address": "...", "port": 443, "users": [{"id": "...", "encryption": "none"]}] },
    "streamSettings": { "network": "tcp", "security": "reality", ... },
    "mux": { "enabled": true }
  }]
}
```

**Sing-box outbound structure:**
```json
{
  "outbounds": [{
    "type": "vless",
    "server": "...",
    "server_port": 443,
    "uuid": "...",
    "tls": { "enabled": true, "reality": {...} },
    "transport": { "type": "tcp", ... },
    "udp_over_tcp": {...}
  }]
}
```

### Control API Differences
- Xray: gRPC API on API port (default 0 = no API), protobuf-based
- Sing-box: CLI-based control (`sing-box run -c config.json`), no native API in older versions

## Implementation Steps

### Phase 1: Configuration (shared Xray/sing-box)
1. Add to `AppConfig` struct in `include/ConfigReader.h`:
   - `std::string singbox_executable;` (default empty, falls back to Xray)
   - `std::string proxy.singbox_asset_dir;` (default empty)
   - `std::string proxy.singbox_template_config_path;` (default empty, falls back to Xray template)

2. Update `src/ConfigJsonSerializer.cpp`:
   - Add serialization for singbox_executable, proxy.singbox_asset_dir, proxy.singbox_template_config_path

3. Update `src/ui/ConfigDialog.cpp`:
   - Add "Sing-box" category after Xray section
   - Add singbox_executable file property
   - Add singbox_asset_dir directory property
   - Add singbox_template_config_path file property
   - Update saveConfig() and validateConfig() methods

### Phase 2: sing-box Outbound Builders
4. Create `src/config/outbound/SingBox/VLESSOutboundBuilder.cpp` (sing-box format):
   - Build sing-box vless outbound with `type`, `server`, `server_port`, `uuid`, `tls`, `transport`

5. Create similar builders for:
   - VMessOutboundBuilder.cpp
   - SSOutboundBuilder.cpp
   - TrojanOutboundBuilder.cpp
   - Hysteria2OutboundBuilder.cpp
   - TUICOutboundBuilder.cpp
   - WireGuardOutboundBuilder.cpp

### Phase 3: Integration
6. Update `src/ui/AppController.cpp::startStandaloneProxy()`:
   - Add sing-box backend support
   - Use singbox_executable if configured, otherwise fall back to Xray
   - Select appropriate outbound format based on backend

## Risk Assessment
- Outbound JSON structure is fundamentally different - requires new builders
- sing-box must be installed separately (not bundled)
- Need to maintain backward compatibility with existing Xray-based configs

## Timeline
- Phase 1 (Config): 1 day
- Phase 2 (Builders): 1 day
- Phase 3 (Integration): 1 day
- Testing: 1 day