# Bugfix: RegionBatchResolver 数据源切换 ip-api.com → ipwho.is（Cloudflare anycast 判定差异修复）

- 日期: 2026-08-21
- 类型: Bugfix
- 模块: RegionBatchResolver / AppController
- 版本: v1.0
- 日志: N/A（数据源准确性问题）

---

## 1. 问题描述

用户报告批量解析 region 结果与 ipinfo.io 基准差异大（`parse region error.txt`）。
对 3 个 Cloudflare IP 实测对比：

| IP | ASN | ipinfo.io（基准） | ip-api.com（迁移后） | 差异 |
|----|-----|------------------|---------------------|------|
| 104.26.14.137 | AS13335 Cloudflare | United States | Canada | ✗ |
| 86.38.214.205 | AS209242 Cloudflare London | Lithuania | Turkey | ✗ |
| 172.67.74.2 | AS13335 Cloudflare | United States | Canada | ✗ |

3/3 全部不一致，且跨洲级偏差（北美↔欧洲）。

## 2. 根因分析

### 2.1 排除代码缺陷

- `parseRegionFromJson()` 单元测试通过，`country` 字段提取逻辑正确；
- ip-api.com 响应格式 `{"status":"success","country":"Canada","countryCode":"CA"}` 解析无误。

### 2.2 确认根因：GeoIP 数据库分歧

三个 IP 均为 **Cloudflare anycast IP**（全球多点广播、无唯一物理位置），是各 GeoIP
数据库判定分歧最大的类别。实测多数据源横向对比：

| 数据源 | 104.26.14.137 | 86.38.214.205 | 172.67.74.2 | 与基准一致 |
|--------|---------------|---------------|-------------|-----------|
| ipinfo.io（基准） | US | LT | US | — |
| **ip-api.com free** | CA | TR | CA | **0/3** |
| **ipwho.is** | US | LT | US | **3/3** |
| freeipapi.com | 返回 HTML，不可用 | — | — | — |

结论：ip-api.com 免费库对 Cloudflare 网段准确度差；ipwho.is（免费、无 Token、HTTPS）
与 ipinfo.io 判定完全一致。

### 2.3 约束条件

- ipwho.is **不支持域名直查**（返回 `{"success":false,"message":"404 not found"}`），
  需恢复旧版（ipinfo.io 时代）的 DnsCache 域名预解析逻辑；
- MinGW curl 无 CA bundle，HTTPS 需按项目惯例 `setSslVerifyPeer(false)`。

## 3. 修复方案

### 3.1 fetch 函数重写

**变更文件**: `src/RegionBatchResolver.cpp`, `include/RegionBatchResolver.h`

- `fetchRegionFromIpApi()` → `fetchRegionFromIpWhoIs()`：
  - 恢复 `isIpPattern` lambda（IPv4：全数字+点且 3 个点；IPv6：含 `:`）判断域名；
  - 域名先经 `utils::DnsCache::resolve()` 解析为 IP（失败跳过返回空）；
  - 请求 `https://ipwho.is/{ip}`（15s 超时 / 10s 连接超时 / 跟随重定向 /
    `setSslVerifyPeer(false)` 对齐 UrlFetcher/SubitemUpdaterV2 惯例）；
- 新增 `#include "utils/DnsCache.h"`、`#include <cctype>`。

### 3.2 调用点同步

- `workerThreadFunc()`（批量路径）与 `AppController::resolveSingleRegion`
  （单代理右键路径）均改调 `fetchRegionFromIpWhoIs()`；
- 修正遗留 stale 日志文案 `fetchRegionFromIpInfo returned empty` → `fetchRegionFromIpWhoIs`。

### 3.3 解析器兼容性

`parseRegionFromJson()` 无需修改：ipwho.is 成功响应同样携带完整国名 `country` 字段
（如 `"country":"United States"`），失败响应 `{"success":false,"message":...}` 无
`country` 字段 → 自然落入既有 MissingCountryField 分支返回空。

### 3.4 测试更新

**变更文件**: `tests/test_region_batch_resolver.cpp`

- 测试 JSON 样例更新为 ipwho.is 响应格式（`success`/`country_code` 字段名）；
- 新增 `ParseRegionFromJson_IpWhoIsFailureResponse` 用例覆盖
  `{"success":false,"message":"404 not found"}` → `""`。

## 4. 验证结果

```powershell
cmake --build build --parallel 8   # 构建成功，仅预存 warning
ctest -R RegionBatchResolverTest -V # 6/6 通过（新增 1 用例）
ctest --parallel 8                  # 31/31 全部通过
```

端到端 URL 核验：`https://ipwho.is/104.26.14.137` → `"country":"United States"`
与 ipinfo.io 基准一致；域名链路复用旧版已验证的 DnsCache 实现。

## 5. 影响范围与已知限制

| 项 | 说明 |
|----|------|
| 已有 Region 数据 | 不自动刷新；如需重测需清空 ProfileItem.Region 后重新批量解析 |
| ipwho.is 限额 | 官方免费档 10k 次/月；批量解析仅处理 delay>0 且 Region 为空的增量代理，常规使用可控 |
| anycast 固有歧义 | Cloudflare 等 anycast IP 无唯一物理位置，任何数据库判定均为近似值 |

## 6. 参考文档

- `docs/bugfix/2026-08-20-Bugfix-RegionBatchResolver-ApiMigration-UnitTest-v1.0.md` — 前序 ipinfo.io→ip-api.com 迁移
- `docs/specs/2026-07-09-Spec-DnsCache-v1.0.md` — DnsCache 模块设计
- `include/CurlEasyHandle.h` — cURL RAII 封装

---

**修复人**: ox-alpha
**审核状态**: 待审核
**关联 Issue**: N/A
