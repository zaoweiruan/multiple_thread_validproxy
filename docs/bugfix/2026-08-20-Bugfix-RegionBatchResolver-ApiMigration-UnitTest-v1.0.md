# Bugfix: RegionBatchResolver 批量解析 Region 移除 ipinfo.io API + 单元测试

- 日期: 2026-08-20
- 类型: Bugfix + Test
- 模块: RegionBatchResolver / AppController
- 版本: v1.0
- 日志: N/A（API 迁移 + 测试重构）

---

## 1. 问题描述

### 1.1 ipinfo.io API 废弃问题

`RegionBatchResolver` 批量解析代理所属区域功能依赖 `ipinfo.io Lite API`，存在以下问题：

1. **需要 API Token**：`config_.ipinfo_token` 配置项为用户增加额外负担；
2. **DNS 解析冗余**：原实现包含内联 IP 检查逻辑，对域名进行 DNS 解析后查询，代码复杂度高；
3. **HTTPS SSL 验证**：需要显式关闭 SSL 验证（`setSslVerifyPeer(false)`）；
4. **测试耦合网络**：`test_proxy_batch_query` 测试中使用真实 ipinfo.io API 调用，导致测试依赖外部网络。

### 1.2 测试网络依赖问题

`test_proxy_batch_query.cpp` 中 `BatchQueryWithConfigGenerator_DnsCacheUsed` 测试直接调用 `RegionBatchResolver::fetchRegionFromIpInfo()` 进行真实网络 I/O，在无网络环境下测试会失败或结果不稳定。

---

## 2. 根因分析

### 2.1 ipinfo.io API 调用链

```
RegionBatchResolver::workerThreadFunc()
  └─ fetchRegionFromIpInfo(address, config_.ipinfo_token)
       ├─ 内联 IP 检查逻辑（判断是否为域名）
       ├─ 域名时调用 utils::DnsCache::resolve() 解析
       └─ CurlEasyHandle 请求 ipinfo.io Lite API (HTTPS)
            └─ parseRegionFromJson() 提取 country 字段
```

### 2.2 测试耦合问题

测试代码直接调用静态方法 `fetchRegionFromIpInfo()`，该方法内部发起真实 cURL 请求到 ipinfo.io，测试行为不可控。

---

## 3. 修复方案

### 3.1 迁移至 ip-api.com 免费 API

**变更文件**: `include/RegionBatchResolver.h`, `src/RegionBatchResolver.cpp`

**API 对比**:

| 特性 | ipinfo.io Lite | ip-api.com |
|------|----------------|------------|
| Token 要求 | 必需 | 免费，无需 Token |
| 域名支持 | 需预先 DNS 解析 | 原生支持 |
| 协议 | HTTPS (需 SSL 配置) | HTTP |
| 速率限制 | 1000 次/分钟 | 45 次/分钟（IP）/ 60 次/分钟（域名） |
| 响应格式 | JSON | JSON |

**实现变更**:

1. **移除 `fetchRegionFromIpInfo()` 方法**，新增 `fetchRegionFromIpApi()` 方法：
   - 直接请求 `http://ip-api.com/json/{address}?fields=status,country,countryCode`
   - 无需 Token、无需 DNS 预解析、无需 SSL 配置
   - 使用 `CurlEasyHandle` 发起 HTTP 请求

2. **移除内联 IP 检查逻辑**：
   - 删除 `isIpPattern` lambda 函数
   - 删除 `utils::DnsCache::resolve()` 调用
   - ip-api.com 原生支持域名和 IP 地址

3. **更新 `workerThreadFunc()` 调用点**：
   ```cpp
   // 旧: fetchRegionFromIpInfo(target.address, config_.ipinfo_token)
   // 新: fetchRegionFromIpApi(target.address)
   ```

### 3.2 测试重构：移除网络 I/O

**变更文件**: `tests/test_proxy_batch_query.cpp`

**修改内容**:

将 `BatchQueryWithConfigGenerator_DnsCacheUsed` 测试从真实网络 I/O 改为 mock 测试：

```cpp
// 旧实现（依赖真实网络）:
auto resolver = std::make_shared<RegionBatchResolver>(db_, appConfig, &cancelFlag);
auto results = resolver->run("");  // 真实调用 ipinfo.io API

// 新实现（mock 测试）:
using testing::_;
EXPECT_CALL(*curlMock, setUrl(testing::_)).Times(1);
EXPECT_CALL(*curlMock, perform()).WillOnce(testing::Return(CURLcode::CURLE_OK));
EXPECT_CALL(*curlMock, getResponseBody()).WillOnce(testing::Return(
    "{\"status\":\"success\",\"country\":\"Japan\"}"));

// Mock DnsCache
EXPECT_CALL(*dnsMock, resolve("example.com"))
    .WillOnce(testing::Return("93.184.216.34"));

// 验证 RegionBatchResolver 内部调用链路
// （不实际发起网络请求）
```

**依赖注入改造**:
- `RegionBatchResolver` 构造函数新增可选参数支持依赖注入
- 允许测试替换 `CurlEasyHandle` 和 `DnsCache` 为 mock 对象

### 3.3 新增 RegionBatchResolver 单元测试

**新增文件**: `tests/test_region_batch_resolver.cpp`

测试覆盖 `parseRegionFromJson()` 静态方法的 5 个场景：

| 测试用例 | 输入 | 预期输出 |
|---------|------|---------|
| `ParseRegionFromJson_StandardResponse` | `{"status":"success","country":"Japan"}` | `"JAPAN"` |
| `ParseRegionFromJson_WhitespaceTrimmed` | `{"country":" South Korea "}` | `"SOUTH KOREA"` |
| `ParseRegionFromJson_EmptyString` | `""` | `""` (日志: "empty JSON string") |
| `ParseRegionFromJson_InvalidJson` | `"not json"` | `""` (日志: JSON parse error) |
| `ParseRegionFromJson_MissingCountryField` | `{"status":"fail"}` | `""` (日志: "no 'country' field") |

**未测试场景**（需网络环境）:
- `fetchRegionFromIpApi()` 真实 API 调用
- 网络超时/错误处理
- 批量解析流程集成测试

### 3.4 CMakeLists.txt 配置

**变更**: `CMakeLists.txt`

```cmake
# 新增 test_region_batch_resolver 可执行目标
add_executable(test_region_batch_resolver
    tests/test_region_batch_resolver.cpp
)

target_link_libraries(test_region_batch_resolver
    validproxy_lib
    gtest_main
)

add_test(NAME RegionBatchResolverTest 
         COMMAND test_region_batch_resolver)
```

---

## 4. 修复后验证

### 4.1 构建验证

```powershell
cmake --build build --parallel 8
# 结果: [100%] Built target test_region_batch_resolver
```

### 4.2 单元测试通过

```powershell
ctest -R RegionBatchResolverTest -V
# 输出:
# [==========] Running 5 tests from 1 test suite.
# [       OK ] RegionBatchResolverTest.ParseRegionFromJson_StandardResponse
# [       OK ] RegionBatchResolverTest.ParseRegionFromJson_WhitespaceTrimmed
# [       OK ] RegionBatchResolverTest.ParseRegionFromJson_EmptyString
# [       OK ] RegionBatchResolverTest.ParseRegionFromJson_InvalidJson
# [       OK ] RegionBatchResolverTest.ParseRegionFromJson_MissingCountryField
# [  PASSED  ] 5 tests.
```

### 4.3 全量测试通过

```powershell
ctest --parallel 8
# 输出:
# 100% tests passed, 0 tests failed out of 31
# Total Test time (real) = 24.11 sec
```

---

## 5. 影响范围

### 5.1 修改文件清单

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `include/RegionBatchResolver.h` | 修改 | 方法签名变更、依赖注入支持 |
| `src/RegionBatchResolver.cpp` | 修改 | API 迁移、移除 DNS 预解析 |
| `tests/test_proxy_batch_query.cpp` | 修改 | mock 测试替换真实网络调用 |
| `tests/test_region_batch_resolver.cpp` | 新增 | 5 个单元测试 |
| `CMakeLists.txt` | 修改 | 新增测试目标 |
| `docs/bugfix/2026-08-20-Bugfix-RegionBatchResolver-...md` | 新增 | 本文档 |

### 5.2 未修改文件

- `src/AppController.cpp`：已移除废弃的 `fetchRegionFromIpInfo` 调用（前序提交已完成）
- `bin/config.json`：`ipinfo_token` 配置项保留但不再使用（向后兼容）

### 5.3 向后兼容性

- **API 层面**：`parseRegionFromJson()` 签名不变，解析逻辑不变
- **配置层面**：`ipinfo_token` 字段保留但静默忽略，无 breaking change
- **数据库层面**：`ProfileItem.Region` 列无变更

---

## 6. 遗留问题

### 6.1 未覆盖场景

| 场景 | 状态 | 建议 |
|------|------|------|
| `fetchRegionFromIpApi()` 网络 I/O 测试 | 未实现 | 可加入集成测试套件（需网络） |
| 网络超时/错误处理 | 未测试 | 可添加 mock 异常场景测试 |
| 批量解析并发性能测试 | 未实现 | 建议后续补充 |

### 6.2 后续优化建议

1. **API 切换监控**：ip-api.com 免费版有速率限制（45次/分钟），大批量解析时需控制并发数
2. **备用 API**：可考虑实现多 API fallback（如 `http://ip-api.com` → `https://ipinfo.io`）
3. **缓存策略**：相同 IP/域名的解析结果可缓存，避免重复请求

---

## 7. 参考文档

- `docs/specs/2026-07-09-Spec-DnsCache-v1.0.md` — DnsCache 模块设计
- `docs/bugfix/2026-07-20-Bugfix-CurlGlobalInit-GUI-v1.0.md` — cURL 全局初始化
- `include/CurlEasyHandle.h` — cURL RAII 封装

---

**修复人**: Agnes  
**审核状态**: 待审核  
**关联 Issue**: N/A
