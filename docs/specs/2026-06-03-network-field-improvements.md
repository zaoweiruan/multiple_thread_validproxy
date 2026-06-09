---
title: "feat: network splithttp 映射 xhttp + 无效值默认tcp"
type: spec
status: completed

## 验证结果

- 编译通过
- 6 个测试套件全部通过

### 修改文件列表
- `src/SubitemUpdaterV2.cpp` - vmess/vless 解析中 splithttp → xhttp 映射，无效值默认 tcp；修复 `updateWithStrategy` fallback 逻辑
- `src/ShareLink.cpp` - xhttp 输出支持
date: 2026-06-03
origin: "代理配置 network 字段处理"
---

# Network 字段处理改进

## 需求

1. vmess/vless 配置中 network 字段值为 "splithttp" 时，映射为 "xhttp"
2. network 字段值无效时，默认回退为 "tcp"

## 设计

### D1: splithttp → xhttp 映射

修改 SubitemUpdaterV2.cpp 在设置 network 后添加映射逻辑：

```cpp
profile.network = getJsonValueString(obj, "net", "tcp");
// Map splithttp to xhttp
if (profile.network == "splithttp") {
    profile.network = "xhttp";
}
// Validate network value, fallback to tcp if invalid
if (profile.network != "tcp" && profile.network != "ws" && profile.network != "h2" && profile.network != "xhttp" && profile.network != "grpc") {
    profile.network = "tcp";
}
```

### D2: ShareLink 生成支持 xhttp

修改 ShareLink.cpp 添加 xhttp 支持：

```cpp
} else if (!network.empty() && network != "tcp") {
    // Support splithttp -> xhttp mapping
    std::string net = network;
    if (net == "splithttp") net = "xhttp";
    // xhttp uses obfs=xhttp style for share links
    query = "obfs=" + net;
    ...
}
```

## 实施计划

| 步骤 | 文件 | 变更 |
|------|------|------|
| 1 | src/SubitemUpdaterV2.cpp | 添加 network 值验证和映射 |
| 2 | src/ShareLink.cpp | 添加 xhttp 支持 |

## 验证步骤

1. 订阅 vmess:// 链接中 network=splithttp → 解析后 network=xhttp
2. 订阅 network=invalid → 解析后 network=tcp
3. ShareLink 生成 xhttp 类型链接正确