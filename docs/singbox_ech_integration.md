# Sing-Box ECH/TLS Integration Notes

## Source Location
- Main TLS options: `E:\eclipse_workspace\sing-box\option\tls.go`
- Outbound TLS struct: `OutboundTLSOptions` (line 97-122)
- Outbound ECH options: `OutboundECHOptions` (line 217-227)

## Key Findings

### ECH Configuration Format
Sing-box expects ECH configs in this format:
```json
{
  "tls": {
    "enabled": true,
    "server_name": "example.com",
    "ech": {
      "enabled": true,
      "config": ["base64-encoded-ech-config"],
      "query_server_name": "example.com"
    },
    "utls": {
      "enabled": true,
      "fingerprint": "chrome"
    }
  }
}
```

### Xray ECH Format (Database)
The database stores ECH in Xray-specific format:
```
cloudflare-ech.com+https://dns.alidns.com/dns-query
```
This means: Query `https://dns.alidns.com/dns-query` for ECH config of `cloudflare-ech.com`.

### Limitation
Sing-box does NOT support dynamic ECH key fetching via DNS URL. It expects pre-fetched base64 configs.
- `ech.config` - list of base64-encoded ECH configs
- `ech.query_server_name` - server name to query (but still requires configured DNS)

### Current Issue
Sing-box VLESS outbound builder (`SingBoxVLESSOutboundBuilder.cpp`) completely ignores the `echconfiglist` field from ProfileItem, causing TLS handshake failures for servers that use ECH-aware routing.

## Reference
- https://github.com/sagernet/sing-box/blob/testing/docs/configuration/shared/tls.md
- Line 119: `ECH *OutboundECHOptions` in OutboundTLSOptions
- Line 217-227: OutboundECHOptions struct definition