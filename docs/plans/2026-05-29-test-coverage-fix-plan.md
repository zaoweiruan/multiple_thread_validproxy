# 测试覆盖修复计划 (Test Coverage Fix Plan)

## 当前状态（2026-05-29）
- 现有测试: **40 个** （LoggerTest 14 + DedupTest 11 + CurlEasyHandleTest 1 + ProfileitemTest 3 + ShareLinkTest 11 + ConfigGeneratorTest 2 (1 skipped)）
- 覆盖模块: 6 / 20+
- 高风险模块: ShareLink、ConfigReader、ConfigGenerator（已标记 P0）

## 已完成

### ShareLink 测试 (tests/test_sharelink.cpp)
| 测试 | 协议 | 状态 |
|------|------|------|
| VlessUriBasic | VLESS | ✅ |
| VlessUriWithRemarks | VLESS | ✅ |
| VmessUriBasic | VMess | ✅ |
| VmessUriContainsBase64 | VMess | ✅ |
| TrojanUriBasic | Trojan | ✅ |
| SsUriBasic | Shadowsocks | ✅ |
| Hysteria2UriBasic | Hysteria2 | ✅ |
| UnsupportedProtocolSocks | Socks | ✅ |
| UnsupportedProtocolWireGuard | WireGuard | ✅ |
| UnsupportedProtocolHttp | HTTP | ✅ |
| GetConfigTypeName | 工具函数 | ✅ |

### ConfigGenerator 测试 (tests/test_config_generator.cpp)
| 测试 | 验证点 | 状态 |
|------|--------|------|
| ProfileitemRequiredFieldsCheck | 字段校验 | ✅ |
| ProfileitemInvalidPort | 异常抛出 | ✅ |
| GenerateVlessOutboundBasic | 跳过（需 DB） | ⏭ |

### ConfigReader 测试
**问题**: Utils.cpp 包含 Windows Unicode API，静态分析编译失败。
**状态**: 延后（待 Windows 构建环境或 Mock 函数）

## 总计
- 6 个测试套件全部通过
- 40 个测试全部通过（38 通过 + 1 SKIP）

## 未来工作
- ConfigGenerator 的完整 generateConfig 测试（需 Mock DB）
- ConfigReader 的完整 load/save 测试（需 Mock Utils 或修复 Windows API）