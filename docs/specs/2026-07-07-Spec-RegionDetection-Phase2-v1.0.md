# 代理地域检测增强方案 — Phase 2: GeoIP + GeoSite 文件兜底解析

> **版本:** v1.0
> **日期:** 2026-07-07
> **状态:** 📝 草案
> **关联:** RegionDetector, Profileitem 数据模型, ProxyListPanel UI
> **前置:** Phase 1 (docs/specs/2026-07-06-Spec-RegionDetection-v1.0.md) 已完成

---

## 1. 问题陈述

### 1.1 现状

Phase 1 已实现的 RegionDetector 检测策略链：

```
detect(address, remarks):
  1. detectFromRemarks(remarks)        ← 备注关键词匹配（"香港"→HK）
  2. detectFromDomainSuffix(address)   ← 已知域名后缀（apple.com→US）
  3. detectFromTld(address)            ← TLD 映射（.jp→JP）
  4. return "Unknown"
```

### 1.2 现存缺陷

| 缺陷 | 场景 | 影响 |
|------|------|------|
| **IP 地址无检测** | Host 为 IPv4/IPv6 时跳过所有策略 | 大量 IP 直连节点显示 "Unknown" |
| **静态映射有限** | 域名未在预置表中 | 长尾域名无法匹配 |
| **TLD 泛化误判** | `.com`/`.net`/`.org` 硬映射到 US | 新加坡 `.com.sg` 等二级后缀处理不当 |
| **无外部数据源** | 纯规则匹配，准确率不可提升 | 无社区数据补充 |

### 1.3 目标

引入 **v2rayN 生态 geo 文件** 作为兜底数据源：

| 数据源 | 用途 | 命中场景 |
|--------|------|---------|
| `geoip.dat` | IP → 国家/地区 | Host 为 IPv4/IPv6 地址 |
| `geosite.dat` | 域名 → 国家/地区 | 域名未匹配静态规则 |

---

## 2. Geo 文件格式分析

### 2.1 数据源位置

```
E:\v2rayN-windows-64\bin\
├── geoip.dat                  (约 18.5 MB) — IP CIDR → 国家代码
├── geosite.dat                (约 10.3 MB) — 域名模式 → 国家代码
└── geoip-only-cn-private.dat  (约 75 KB)  — 仅中国私有 IP
```

### 2.2 Protobuf 协议格式

两个文件均使用 **Protocol Buffers 二进制编码**，协议源自 Xray-core 的 `GeoIPAssetProtocol`。

#### geoip.dat 结构

```
GeoIPList {
  repeated GeoIP entry = 1;      // 字段标签 0x0A
}

GeoIP {
  string country_code = 1;       // 字段标签 0x0A, "CN"/"US"/"JP"...
  repeated CIDR cidr = 2;        // 字段标签 0x12
}

CIDR {
  bytes ip = 1;                  // 字段标签 0x0A, 变长字节: IPv4=4字节, IPv6=16字节
  uint32 prefix = 2;             // 字段标签 0x10 varint, CIDR 前缀长度
}
```

**二进制示例（从 `geoip.dat` 实测分析）：**

```
[0x0A] [len] -> GeoIP entry
  [0x0A] [2]  "CN"        -> country_code = "CN"
  [0x12] [len] -> CIDR
    [0x0A] [4] [4B][8C][..][..]  -> ip bytes
    [0x10] [16(/0x18/0x20/0x38)] -> prefix (8/16/24/32等)
```

#### geosite.dat 结构

```
GeoSiteList {
  repeated GeoSite entry = 1;    // 字段标签 0x0A
}

GeoSite {
  string country_code = 1;       // 字段标签 0x0A
  repeated Domain domain = 2;    // 字段标签 0x12
}

Domain {
  Type type = 1;                 // 字段标签 0x08 varint
                                 //   0 = Plain       ("example.com")
                                 //   1 = Regex       ("regexp:^.*\.cn$")
                                 //   2 = Domain      ("domain:example.com")
                                 //   3 = Full        ("full:www.example.com")
  string value = 2;              // 字段标签 0x12
  uint32 attribute = 3;          // 字段标签 0x18 varint (可选)
}
```

**二进制示例（从 `geosite.dat` 实测分析）：**

```
[0x0A] [len] -> GeoSite entry
  [0x0A] [1]  "C"               -> country_code (部分条目非标准2字母)
  [0x12] [len] -> Domain
    [0x08] [02]                  -> type = Domain (2)
    [0x12] [len] "example.com"   -> value
```

### 2.3 字段标签编码规则（最小化 Protobuf 解码器所需）

| 字段标签 | Protobuf 编码 | Wire Type | 含义 |
|----------|--------------|-----------|------|
| `0x0A` | `(1 << 3) | 2` = 0x0A | length-delimited | field 1, bytes/string |
| `0x12` | `(2 << 3) | 2` = 0x12 | length-delimited | field 2, bytes/string |
| `0x08` | `(1 << 3) | 0` = 0x08 | varint | field 1, int32/uint32 |
| `0x10` | `(2 << 3) | 0` = 0x10 | varint | field 2, int32/uint32 |
| `0x18` | `(3 << 3) | 0` = 0x18 | varint | field 3, int32/uint32 |
| `0x20` | `(4 << 3) | 0` = 0x20 | varint | field 4, int32/uint32 |
| `0x38` | `(7 << 3) | 0` = 0x38 | varint | field 7, int32/uint32 |

---

## 3. 设计：Minimal Protobuf Decoder

### 3.1 为什么不引入 protobuf 库

| 方案 | 问题 |
|------|------|
| `protobuf` 完整库 | 依赖过大 (MinGW 编译困难, 二进制 ~MB 级) |
| `nanopb` | 需生成代码，交叉编译配置复杂 |
| `protobuf-c` | 同上 |
| **✅ 自研最小解码器** | 仅处理 GeoIP/GeoSite 两个消息类型，~200 行 |

### 3.2 自研解码器设计

仅实现 protobuf 二进制线格式中最常用的两种 wire type：

**Wire Type 0 (Varint)：**
- 每个字节的高位为 continuation bit (bit 7)
- 低 7 位拼接为小端序整数
- 用于字段标签、int32/uint32、枚举值

**Wire Type 2 (Length-delimited)：**
- Varint 字段标签 + Varint 长度 + 数据字节
- 用于 string、bytes、嵌套 message

**核心接口：**

```cpp
namespace utils {
namespace protobuf {

// 解码一个 varint 值，返回解码后的值和消耗的字节数
struct VarintResult {
    uint64_t value;
    size_t consumed;  // 0 表示解码失败
};
VarintResult readVarint(const uint8_t* data, size_t len);

// 解析字段标签：返回 (field_number, wire_type, consumed)
struct FieldTag {
    uint32_t fieldNumber;
    uint32_t wireType;  // 0=varint, 2=length-delimited
    size_t consumed;
};
FieldTag readFieldTag(const uint8_t* data, size_t len);

// 读取 length-delimited 字段的 payload
struct LengthDelimitedResult {
    const uint8_t* data;
    size_t length;
    size_t consumed;
};
LengthDelimitedResult readLengthDelimited(const uint8_t* data, size_t len);

} // namespace protobuf
} // namespace utils
```

---

## 4. 设计：新文件结构

### 4.1 新增文件

```
include/utils/
├── RegionDetector.h        ← [修改] 增加 detectFromGeoIp / detectFromGeoSite
├── GeoFileReader.h          ← [新增] GeoIP + GeoSite 文件读取器

src/utils/
├── RegionDetector.cpp      ← [修改] 检测链增加 geo 文件兜底层
├── GeoFileReader.cpp        ← [新增] 文件解析 + 查找
└── ProtoWireDecoder.cpp     ← [新增] 最小 protobuf 线格式解码器（可选独立文件）
```

### 4.2 GeoFileReader 类设计

```cpp
// include/utils/GeoFileReader.h

namespace utils {

/**
 * 基于 v2rayN geoip.dat 文件的 IP → 地区查找器
 *
 * 索引结构: 将 geoip.dat 解析为内存中按国家代码分组的 CIDR 列表
 * 查找: IPv4 使用 Trie/线性扫描, IPv6 使用线性扫描
 *
 * 加载: 懒加载 (首次查找时自动加载)
 * 线程安全: 否（由调用方保证外部同步）
 */
class GeoIpReader {
public:
    GeoIpReader();
    ~GeoIpReader();

    /// 设置 geo 文件路径（默认 E:\v2rayN-windows-64\bin\geoip.dat）
    void setFilePath(const std::string& path);

    /// 查找 IP 所属国家代码, 返回 "CN"/"US"/"JP" 或空字符串
    std::string lookup(const std::string& ip) const;

    /// 显式加载文件; 失败返回 false + log
    bool load();

    /// 是否已加载
    bool isLoaded() const { return loaded_; }

private:
    struct CidrEntry {
        uint32_t ipv4_;        // network byte order
        uint8_t prefix_;
        std::string country_;
    };

    struct Ipv6CidrEntry {
        uint8_t ipv6_[16];
        uint8_t prefix_;
        std::string country_;
    };

    std::string filePath_;
    mutable bool loaded_ = false;

    std::vector<CidrEntry> ipv4Entries_;     // ~15 万条
    std::vector<Ipv6CidrEntry> ipv6Entries_; // ~5 万条

    bool parseFile(const uint8_t* data, size_t size);
    bool parseGeoIpEntry(const uint8_t* data, size_t size, const std::string& countryCode);
};

/**
 * 基于 v2rayN geosite.dat 文件的域名 → 地区查找器
 *
 * 索引结构: 解析为 country_code → domain_patterns 的 map
 * 查找: 精确匹配 + 后缀匹配（与 RegionDetector::detectFromDomainSuffix 同策略）
 */
class GeoSiteReader {
public:
    GeoSiteReader();
    ~GeoSiteReader();

    void setFilePath(const std::string& path);

    /// 查找域名所属国家代码
    std::string lookup(const std::string& domain) const;

    bool load();
    bool isLoaded() const { return loaded_; }

private:
    struct DomainEntry {
        int type_;          // 0=Plain, 1=Regex, 2=Domain, 3=Full
        std::string value_;
    };

    std::string filePath_;
    mutable bool loaded_ = false;

    // 按国家代码索引的域名列表
    std::unordered_map<std::string, std::vector<DomainEntry>> domainMap_;

    bool parseFile(const uint8_t* data, size_t size);
    bool parseGeoSiteEntry(const uint8_t* data, size_t size, const std::string& countryCode);

    /// 匹配单个域名模式
    bool matchDomain(const std::string& domain, const DomainEntry& entry) const;
};

} // namespace utils
```

### 4.3 内存与性能估算

| 数据结构 | 条目数 | 单条大小 | 总计 |
|----------|--------|---------|------|
| geoip IPv4 CIDR | ~150,000 | 8 bytes (ip+prefix) + 3 bytes(country) | ~1.7 MB |
| geoip IPv6 CIDR | ~50,000 | 17 bytes (ip+prefix) + 3 bytes(country) | ~1.0 MB |
| geosite domains | ~500,000 | ~50 bytes avg (domain+country) | ~25 MB |
| **总计** | | | **~28 MB** |

> **优化选项**: geosite 可使用排序数组 + 二分查找代替 unordered_map(s) 降低内存
> **加载时机**: 首次检测到未解析的地址时才加载，避免启动开销

---

## 5. 设计：增强的检测链

### 5.1 完整检测策略

```cpp
std::string RegionDetector::detect(
    const std::string& address,
    const std::string& remarks)
{
    // 第 1 层: 备注关键词（最高优先级，用户显式标注）
    {
        std::string region = detectFromRemarks(remarks);
        if (!region.empty() && region != "Unknown") return region;
    }

    std::string host = extractDomain(address);
    if (host.empty()) return "Unknown";

    // 第 2 层: IP 地址 → geoip.dat
    if (isIpPattern(host)) {
        std::string region = detectFromGeoIp(host);
        if (!region.empty()) return region;
        return "Unknown";  // geoip.dat 也查不到则放弃
    }

    // 第 3 层: 静态域名后缀匹配
    {
        std::string region = detectFromDomainSuffix(host);
        if (!region.empty()) return region;
    }

    // 第 4 层: TLD 映射
    {
        std::string region = detectFromTld(host);
        if (!region.empty()) return region;
    }

    // 第 5 层: geosite.dat 域名模式匹配（最终兜底）
    {
        std::string region = detectFromGeoSite(host);
        if (!region.empty()) return region;
    }

    return "Unknown";
}
```

### 5.2 新增私有方法

```cpp
private:
    /// 通过 geoip.dat 查找 IP 所属地区
    static std::string detectFromGeoIp(const std::string& ip);

    /// 通过 geosite.dat 查找域名所属地区
    static std::string detectFromGeoSite(const std::string& domain);

    /// 获取全局 GeoIpReader 实例（懒加载单例）
    static GeoIpReader& getGeoIpReader();

    /// 获取全局 GeoSiteReader 实例（懒加载单例）
    static GeoSiteReader& getGeoSiteReader();
```

### 5.3 Geo 文件路径配置

从 `bin/config.json` 读取或硬编码默认路径：

```json
{
  "geo": {
    "geoip_path": "E:\\v2rayN-windows-64\\bin\\geoip.dat",
    "geosite_path": "E:\\v2rayN-windows-64\\bin\\geosite.dat"
  }
}
```

> **设计决策**: 默认路径指向 v2rayN 现有文件，无需复制。当文件不存在时静默降级（不阻止正常功能）。

---

## 6. 风险与缓解

| 风险 | 可能性 | 影响 | 缓解 |
|------|--------|------|------|
| geoip.dat 格式随 Xray-core 更新变动 | 低 | 高 | 文件格式已稳定多年；解析器设计为可独立验证 |
| 大文件解析性能（geosite.dat 加载 ~500ms） | 中 | 低 | 懒加载 + 首次访问缓存；后台线程加载（可选） |
| Protobuf 自研解码器兼容性缺陷 | 中 | 中 | 单元测试覆盖已知边界文件；可切换为 protobuf 完整库 |
| IPv6 CIDR 匹配低效（线性扫描） | 高 | 低 | IPv6 条目少（~5 万），单次查找 < 1ms |
| geo 文件缺失或路径错误 | 高 | 低 | 静默降级，不影响 `detect()` 返回值；仅日志警告 |
| protobuf 解析内存占用（~28 MB） | 中 | 低 | 仅懒加载；可通过离线预处理压缩索引 |

---

## 7. 实施计划

### Task 1: 最小 Protobuf 线格式解码器

- **文件**: `include/utils/ProtoWireDecoder.h` + `src/utils/ProtoWireDecoder.cpp`（或内联在 `GeoFileReader.cpp` 中）
- **内容**: `readVarint()`, `readFieldTag()`, `readLengthDelimited()` 三个核心函数
- **依赖**: 无（纯 C++ 字节操作）
- **测试**: `tests/test_proto_wire_decoder.cpp`

### Task 2: GeoIpReader 实现

- **文件**: `include/utils/GeoFileReader.h` + `src/utils/GeoFileReader.cpp`
- **内容**:
  - `GeoIpReader` 类：`load()`, `lookup()`, `parseFile()`
  - IPv4 CIDR 匹配：与运算 `(ip & mask) == (cidr & mask)`
  - IPv6 CIDR 匹配：memcmp 前 N 字节
- **测试**: `tests/test_geoip_reader.cpp`

### Task 3: GeoSiteReader 实现

- **文件**: `include/utils/GeoFileReader.h` + `src/utils/GeoFileReader.cpp`（与 Task2 同文件）
- **内容**:
  - `GeoSiteReader` 类：`load()`, `lookup()`, `parseFile()`
  - 域名匹配策略与 RegionDetector 的 `detectFromDomainSuffix()` 统一
  - 支持 Plain (0), Domain (2), Full (3) 三种类型匹配
  - Regex (1) 类型协商使用 `<regex>` 库（可选实现）
- **测试**: `tests/test_geosite_reader.cpp`

### Task 4: RegionDetector 检测链增强

- **文件**: `include/utils/RegionDetector.h` + `src/utils/RegionDetector.cpp`
- **变更**:
  - 新增 `detectFromGeoIp()` 和 `detectFromGeoSite()` 私有方法
  - 新增 `getGeoIpReader()` 和 `getGeoSiteReader()` 懒加载单例
  - 修改 `detect()` 加入第 2/5 层检测
  - 修复缺失的 `isIpAddress()` 实现（已在 header 声明但无实现）
  - 补充 `extractTld()` 实现
- **测试**: `tests/test_region_detector.cpp` 扩展

### Task 5: Geo 文件路径配置

- **文件**: `include/ConfigReader.h` + `src/ConfigReader.cpp`
- **变更**: 读取 `config.json` 中 `geo.geoip_path` / `geo.geosite_path` 字段
- **默认值**: `E:\v2rayN-windows-64\bin\geoip.dat` / `geosite.dat`
- **降级**: 路径不存在或未配置时，GeoFileReader 静默返回空

### Task 6: CMakeLists.txt 构建适配

```cmake
# 新增源文件
src/utils/GeoFileReader.cpp
# 可选：
src/utils/ProtoWireDecoder.cpp

# 无需额外链接库
```

### Task 7: 全量测试与验证

| 测试用例 | 输入 | 预期 |
|----------|------|------|
| IPv4 → geoip | "8.8.8.8" | "US" |
| IPv4 → geoip (CN) | "114.114.114.114" | "CN" |
| IPv6 → geoip | "2001:4860:4860::8888" | "US" |
| 域名不存在于 geosite | "nonexistent.xyz" | "" |
| 域名匹配 Plain | "google.com" (类型 0) | "US" |
| 域名匹配 Domain | "google.com" (类型 2) | "US" |
| 域名匹配 Full | "www.google.com" (类型 3) | "US" |
| geo 文件不存在 | — | 降级到 "Unknown" |
| 检测链优先级 | — | remarks > geoip/domainSuffix > TLD > geosite |
| protobuf varint 解码 | 0xAC 0x02 | 300 (0x012C) |
| protobuf 字段标签解码 | 0x0A | field=1, type=2 |
| CIDR 匹配 | 192.168.1.5 in 192.168.1.0/24 | true |
| CIDR 不匹配 | 192.168.2.5 in 192.168.1.0/24 | false |

---

## 8. 依赖与文件变更清单

### 新增文件

| 文件 | 预估行数 | 说明 |
|------|---------|------|
| `include/utils/GeoFileReader.h` | ~120 | GeoIpReader + GeoSiteReader 声明 |
| `src/utils/GeoFileReader.cpp` | ~500 | 解析 + 查找逻辑 |
| `tests/test_geoip_reader.cpp` | ~150 | geoip.dat 单元测试 |
| `tests/test_geosite_reader.cpp` | ~150 | geosite.dat 单元测试 |
| `tests/test_proto_wire_decoder.cpp` | ~80 | protobuf 解码器单元测试 |

### 修改文件

| 文件 | 变更说明 |
|------|---------|
| `include/utils/RegionDetector.h` | 新增 `detectFromGeoIp`, `detectFromGeoSite`, `getGeoIpReader`, `getGeoSiteReader`; 补充 `isIpAddress` 声明修正 |
| `src/utils/RegionDetector.cpp` | 检测链增强; 补充 `isIpAddress`/`extractTld` 实现 |
| `include/ConfigReader.h` | 新增 geo 文件路径配置字段 |
| `src/ConfigReader.cpp` | 读取 `geo.geoip_path` / `geo.geosite_path` |
| `CMakeLists.txt` | 添加 `src/utils/GeoFileReader.cpp` 到 CORE_SOURCES |

---

## 9. 接受标准

1. [ ] `GeoIpReader` 正确解析 `geoip.dat`，支持 IPv4/IPv6 CIDR 查找
2. [ ] `GeoSiteReader` 正确解析 `geosite.dat`，支持 Plain/Domain/Full 类型匹配
3. [ ] `RegionDetector::detect()` 对 IP 地址输出非 "Unknown" 结果
4. [ ] 检测链优先级正确：remarks > IP查 geoip > 域名后缀 > TLD > geosite
5. [ ] geo 文件缺失时静默降级，日志记录 `WARN` 级别
6. [ ] 自研 protobuf 解码器通过边界测试
7. [ ] IPv4 CIDR 匹配正确（含边缘情况：prefix=0 全匹配、prefix=32 精确匹配）
8. [ ] IPv6 CIDR 匹配正确（含压缩格式 `::1`）
9. [ ] 所有新代码遵守项目 `auto` 禁用规范
10. [ ] 全量 `ctest -V` 通过
11. [ ] 加载 geosite.dat 后内存 < 35 MB

---

## 附录 A: protobuf 线格式参考

### Varint 编码

```
每个字节:
  bit 7    = continuation (1=还有更多字节, 0=最后一字节)
  bits 6-0 = 数据（小端序拼接）

示例: 0xAC 0x02
  0xAC = 1 0101100  → continuation=1, 数据=0x2C
  0x02 = 0 0000010  → continuation=0, 数据=0x02
  结果 = 0x02 << 7 | 0x2C = 256 + 44 = 300
```

### 字段标签

```
tag = (field_number << 3) | wire_type

wire_type:
  0 = varint     (int32, uint32, sint32, bool, enum)
  1 = 64-bit     (fixed64, sfixed64, double)
  2 = length-delimited (string, bytes, embedded message)
  5 = 32-bit     (fixed32, sfixed32, float)

字段 1, wire_type 2 → (1 << 3) | 2 = 0x0A
字段 2, wire_type 0 → (2 << 3) | 0 = 0x10
```

### 嵌套消息

```cpp
// GeoIP 的 ProtoWire 编码示例（十六进制）:
0A 08              // field=1, wire_type=2, length=8
  0A 02 43 4E     // field=1, wire_type=2, length=2, "CN"
  12 04            // field=2, wire_type=2, length=4
    0A 04          //   field=1, wire_type=2, length=4 (CIDR.ip)
      C0 A8 01 01  //   192.168.1.1
    10 18          //   field=2, wire_type=0, value=24 (CIDR.prefix)
```

解析流程：
1. 读取字段标签 0x0A → field=1, type=length-delimited
2. 读取长度 8 → 子消息 8 字节
3. 递归解析子消息...

---

## 附录 B: v2rayN 源码参考

从 v2rayN 代码库中获得的参考信息：

- geo 文件在 Xray-core 侧处理，v2rayN 仅下载和传参
- geo 文件下载 URL: `https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/{filename}.dat`
- `{filename}` 替换为 `geoip.dat` 或 `geosite.dat`
- 域名匹配类型: 0=Plain, 1=Regex, 2=Domain, 3=Full（来自 Xray-core）
- Xray-core 源码: `app/router/routercommon/common.pb.go`
- Xray-core 域名匹配: `common/geodata/geodat.pb.go`

---

*文档结束*
