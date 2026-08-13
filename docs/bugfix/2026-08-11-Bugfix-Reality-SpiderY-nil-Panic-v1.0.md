# Bugfix: Reality SpiderY nil 越界 panic 导致 Xray 实例崩溃 v1.0

- **日期**：2026-08-11
- **模块**：`XrayApi`（gRPC Direct 注入路径） / `Xray-core`（reality 传输，Go 二进制）
- **关联问题**：批量测试中 REALITY 节点经 gRPC API 注入后，xray 进程在握手验证失败时于 `reality.go:273` 越界 panic 崩溃
- **关联文档**：`docs/plans/2026-08-10-Plan-ImportProxyValidation-v1.0.md`（U4 REALITY+gRPC 降级 skip 的前置调查）

## 1. 背景与动机

### 1.1 事实链（RCA）

1. 批量测试引擎通过 `XrayApi::addOutboundDirect`（`src/XrayApi.cpp` line 2149）将代理 outbound 以**原生 HTTP/2 gRPC 协议**注入运行中的 xray 进程（`/xray.app.proxyman.command.HandlerService/AddOutbound`），**不走 JSON 配置文件解析**。
2. REALITY 传输的 outbound 由 `XrayApi::encodeRealitySettings`（line 810-874）手工编码 `reality.Config` protobuf。该函数编码了 field 1/2/4/5/6/21/22/23/24/26，但**遗漏了 field 27（`spider_y`）**。
3. `reality.Config.SpiderY` 是 `[]int64`（`config.pb.go` line 45，`protobuf:"varint,27,rep,packed"`），未编码时 gRPC 接收端得到 **nil 切片**。
4. Xray-core JSON 配置路径（`infra/conf/transport_internet.go` line 967）则**无条件** `config.SpiderY = make([]int64, 10)`——两条路径对 `spider_y` 的初始状态不一致，gRPC 路径是唯一 nil 来源。
5. `reality.go` `UClient`（line 183 `if !uConn.Verified {`）在 REALITY 握手证书验证失败（死节点/被重定向/MITM 场景）时启动 spider goroutine，其中**无边界检查**地访问 `config.SpiderY[0..9]` 共 10 个索引：
   - line 232/233：`crypto.RandBetween(config.SpiderY[4], config.SpiderY[5])`（times）
   - line 238：`strings.Repeat("0", int(crypto.RandBetween(config.SpiderY[0], config.SpiderY[1])))`（padding cookie）
   - line 262：`crypto.RandBetween(config.SpiderY[6], config.SpiderY[7])`（interval）
   - line 267：`crypto.RandBetween(config.SpiderY[2], config.SpiderY[3])`（concurrency）
   - **line 273**：`time.Sleep(time.Duration(crypto.RandBetween(config.SpiderY[8], config.SpiderY[9])) * time.Millisecond)` ← **首个 panic 点**
6. goroutine 内 panic 未被 recover → 整个 xray 进程崩溃（Go 运行时终止程序）。
7. 触发场景与 `2026-08-10-Analysis-InjectionError-ConfigRootCause-v1.0.md` 中 C1 观测吻合：`11:49:15 REALITY+gRPC panic（reality.go:273 越界，socks=10000/api=10080）`——批量测试中大量死节点/错误配置触发验证失败分支，从而进入 spider goroutine。
8. `crypto.RandBetween(from, to)`（`common/crypto/crypto.go`）在 from==to 时直接返回 from：**全 0 的 `SpiderY` 数组完全安全**（padding 空、times=1、interval/return sleep 0、concurrency=0 无额外 goroutine），即 JSON 路径的 10 元素全 0 默认值语义即为安全默认。

### 1.2 修复策略选择

| 方案 | 描述 | 结论 |
| --- | --- | --- |
| **A（上游补丁）** | `reality.go` `UClient` 入口对 `SpiderY` 长度 < 10 时补 `make([]int64, 10)` 全 0 默认值 | 根治但**本机无法部署**：xray.exe 为 v2rayN 外部二进制（`E:\v2rayN-windows-64\bin\xray\xray.exe`），且本机无 Go 工具链（`go` 不在 PATH） |
| **B（应用侧修复）** | `encodeRealitySettings` 补编码 field 27（`spider_y`，10 元素全 0 默认），使 gRPC 路径与 JSON 路径初始状态一致 | **可立即实施**，从源头消除 nil 切片 |

选择 **方案 B**：应用侧与 Xray-core JSON 适配器行为对齐（无条件发送 10 元素数组），不依赖外部二进制更新；方案 A 作为上游补丁文本附于 §5 供日后部署。

## 2. 变更范围

| 文件 | 变更 |
| --- | --- |
| `include/XrayApi.h` | 新增 `encodePackedInt64Field`、`parseSpiderYParams` 静态方法声明 |
| `src/XrayApi.cpp` | USE_GRPC_API 块新增 `<cstdlib>`/`<cerrno>`；实现 `encodePackedInt64Field`（packed varint wire format）；实现 `parseSpiderYParams`（镜像 transport_internet.go 的 p/c/t/i/r → 槽 0/2/4/6/8 解析）；`encodeRealitySettings` **无条件编码 field 27**（10 元素全 0 数组） |
| `tests/test_xray_api_direct.cpp` | 新增 `ParseSpiderYParams`、`EncodeRealitySettingsSpiderY`、`EncodeRealitySettingsSpiderYAlwaysPresent` 3 个回归测试；修复既有 `EncodeStreamConfigRealitySecurity` 期望字节 |
| `docs/bugfix/2026-08-11-Bugfix-Reality-SpiderY-nil-Panic-v1.0.md` | 本文档 |
| `src/ProxyBatchTester.cpp` | 移除 `preGenerateConfigs` 中 REALITY+gRPC 降级 skip（原 line 471-478）及未使用的引用 `p`——field 27 修复后该 skip 不再必要 |

**不修改**：`Xray-core` 源码（方案 A 仅提供补丁文本，见 §5）；`XrayInstance.cpp` JSON 配置路径（本就安全）。原 REALITY+gRPC 降级 skip（U4）已随本修复移除（见上）。

## 3. 设计

### 3.1 `encodePackedInt64Field(int fieldNumber, const int64_t* values, int count)`

packed 重复字段的 wire format（protobuf field 27 声明含 `packed`）：

```cpp
static std::string encodePackedInt64Field(int fieldNumber,
                                          const int64_t* values, int count);
```

行为要点：
- 生成 tag = `(fieldNumber << 3) | 2`（wire type 2，length-delimited）
- payload 为各值 `encodeVarint(static_cast<uint64_t>(values[i]))` 的顺序拼接（int64 转 uint64 保留补码位模式，负值编码为 10 字节 varint）
- 输出 = tag varint + payload 长度 varint + payload

### 3.2 `parseSpiderYParams(const std::string& spiderX, int64_t* out)`

镜像 Xray-core JSON 适配器 `infra/conf/transport_internet.go` line 961-989 的 spiderX→SpiderY 解析：

```cpp
static void parseSpiderYParams(const std::string& spiderX, int64_t* out);
```

行为要点：
- 先清空 10 个槽位；spiderX 不含 `?` 直接返回（全 0）
- 按 `&` 拆分 `key=value` 对；仅首次出现的每个已知参数生效（`seenP/C/T/I/R` 标志）
- 参数映射：`p`→槽 `[0,1]`（padding）、`c`→`[2,3]`（concurrency）、`t`→`[4,5]`（times）、`i`→`[6,7]`（interval）、`r`→`[8,9]`（return）
- 值按 `-` 拆分：无 `-` → 两槽同值；有 `-` → lo=第一段、hi=第二段（与 Go `strings.Split` 取 `[0]`/`[1]` 一致）；空值跳过
- 非法数值（`std::strtoll` 失败/ERANGE/尾部残留）→ 槽保持 0，镜像 Go `ParseInt` 错误忽略语义

### 3.3 `encodeRealitySettings` 接入点

spiderX 块（原 line 869-872）改造为：

```cpp
const boost::json::value* spiderX = reality.if_contains("spiderX");
std::string spiderXText;
if (spiderX != nullptr && spiderX->is_string()) {
    spiderXText = spiderX->as_string().c_str();
    result += encodeString(26, spiderXText);
}
// field 27 (spider_y): 无条件编码 10 元素数组（与 xray-core JSON 适配器一致）。
// gRPC 路径若不发送该字段，接收端 SpiderY 为 nil 切片，
// reality.go:273 越界访问将 panic 整个 xray 进程。
int64_t spiderY[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
parseSpiderYParams(spiderXText, spiderY);
result += encodePackedInt64Field(27, spiderY, 10);
```

## 4. 验证方案

### 4.1 单元测试

| 用例 | 断言 |
| --- | --- |
| `ParseSpiderYParams` | `"/?p=3-5"`→`[0]=3 [1]=5` 其余 0；`"/"`→全 0；`"/?t=2"`→`[4]=2 [5]=2`；`"/?c=1-3-9"`→`[2]=1 [3]=3`（第二段后忽略）；五参数全设→各槽对应；空串→全 0；`"/?p=abc"`→0（非法值忽略） |
| `EncodeRealitySettingsSpiderY` | reality 含 `spiderX:"/?p=3-5"` → 编码结果含 field 27（tag 0xDA 0x01 = `(27<<3)|2`，len 0x0A = 10），payload 字节 `[3,5,0,0,0,0,0,0,0,0]` |
| `EncodeRealitySettingsSpiderYAlwaysPresent` | reality 无 `spiderX` key → field 27 仍存在且 payload 10 字节全 0 |
| `EncodeStreamConfigRealitySecurity`（修复既有） | 期望字节追加 field 27 全 0 后与实编码一致 |

### 4.2 构建与回归

```powershell
cmake --build build --parallel 8   # 327/327 targets 成功
ctest --test-dir build             # 22/22 全绿（55.58s，含 XrayApiDirectTest 10.24s）
```

U4 skip 移除后重验：`cmake --build build --parallel 2`（内存受限下降低并行度）327/327 targets 成功；`ctest --test-dir build` 22/22 全绿（首轮 2 个网络探测类用例因瞬态网络波动失败，立即重跑通过，与本次改动无关）。

## 5. 预期收益与后续

- **收益**：gRPC 路径注入的 REALITY outbound 与 JSON 路径初始状态一致，`SpiderY` 恒为 10 元素数组，`reality.go:273` 等 10 处索引访问不再越界——批量测试中死节点触发验证失败分支时 xray 进程不再崩溃。
- **上游补丁（方案 A，供日后部署）**：在 `E:\eclipse_workspace\Xray-core\transport\internet\reality\reality.go` `UClient`（line 118 `localAddr` 之后）添加：

```go
if len(config.SpiderY) < 10 {
    config.SpiderY = make([]int64, 10)
}
```

  部署前提：本机安装 Go 工具链并重新编译 xray.exe（当前为 v2rayN 外部二进制 `E:\v2rayN-windows-64\bin\xray\xray.exe`）。此补丁同时保护其他不发送 `spider_y` 的 gRPC 客户端。
- **长期建议**：U4（REALITY+gRPC 降级 skip）已随本修复移除，REALITY 节点现可正常参与批量测试；后续可补充 REALITY 节点端到端连通性回归测试，并在方案 A 部署后移除应用侧 field 27 的依赖说明。
