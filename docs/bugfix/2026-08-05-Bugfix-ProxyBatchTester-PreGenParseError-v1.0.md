# Bugfix: 批量测试 PreGen 失败节点触发 boost.json 解析错误刷屏与卡顿 (2026-08-05)

## 症状

- 12:31 启动的批量测试（全批 224,605 个代理）运行日志
  `bin/worker/log/ui_20260805_123051.log` 中，**13:25:02 起连续刷屏**：
  `[WARN] [XrayApi] parseOutboundJson exception: syntax error [boost.json:1 at ./boost/json/basic_parser_impl.hpp:1052 in function 'parse_string']`
- 报错间隔约 5 秒（13:25:02→07→08→08→13→13→13→18→18→18→23→23→23→24→28→28→29→29→33→34→34→34→39…），
  与 curl 5 秒超时重试节奏吻合 —— **每个垃圾节点在 3 次重试中反复触发解析失败**，
  数十个垃圾节点即造成长时间卡顿。

## 根因（RCA）

### 注入链路

1. **预生成失败无标记**：`ProxyBatchTester::preGenerateConfigs()`（`src/ProxyBatchTester.cpp`）
   对垃圾节点调用 `ConfigGenerator::generateConfig()` 抛异常后，仅 push 一个
   **空 `outbound_json` 的 `failCfg`**（`configFailed=true`，但 outbound_json 为空字符串），
   未记录该 index 失败（老版本无 `pregenFailedFlags_`）。
2. **worker 无防护**：worker 线程对空字符串调用
   `XrayApi::addOutboundDirect(outboundJson, ...)` →
   `parseOutboundJson()`（`src/XrayApi.cpp` L629-702）用 boost.json
   `bj::parse(outboundJson)` 解析**空串** → 抛 `syntax error ... parse_string` →
   catch 后 `Logger::write(... WARN)` 返回 false。
3. **重试放大**：每次失败走 3 次重试（每次 5 秒超时），
   单个垃圾节点浪费约 15 秒，数十个垃圾节点造成长期卡顿。

### 数据源

垃圾节点来自订阅解析乱码（见 8-04 GarbageSS 修复）遗留的
`Telegram🇨🇳 @WangCai2` 等 type=5 不可解码节点，以及
`Argh94-ShadowSocks` 订阅的 malformed 数据。

## 修复

### 1. `src/ProxyBatchTester.cpp` — preGenerateConfigs 记录失败标记

```cpp
catch (const std::exception& e) {
    Logger::write("[ProxyBatchTester] Pre-gen config failed for "
                  + proxies_[i].indexid + ": " + e.what(), LogLevel::WARN);
    config::XrayConfig failCfg;
    failCfg.configFailed = true;
    preGenConfigs_.push_back(failCfg);
    pregenFailedFlags_[i] = true;   // ← 新增：标记该 index 预生成失败
}
```

### 2. `src/ProxyBatchTester.cpp` — worker 循环 E7 skip

```cpp
// E7: Skip proxies whose pre-generation failed (outbound_json is empty).
if (pregenFailedFlags_[profileIdx]) {
    failedCount_.fetch_add(1, std::memory_order_relaxed);
    processedCount_.fetch_add(1, std::memory_order_relaxed);
    resultQueue_.enqueue(proxies_[profileIdx].indexid, -1, false, "PREGEN_FAILED");
    teardownWorkerResult(workerId, "PREGEN_FAILED");
    continue;   // ← 不再调用 addOutboundDirect，空 outbound_json 不会进入 boost.json
}
```

**关键效果**：预生成失败的节点在 worker 循环入口即被跳过，
空 outbound_json 不再进入 `addOutboundDirect` → `parseOutboundJson`，
彻底杜绝 boost.json 对空串的解析异常与 3 次重试放大。

### 3. `src/ProxyBatchTester.cpp` — DIAG 防御输出

worker 取出 preGenConfigs_ 后校验 `outbound_json` 内容
（`size() < 2 || outbound_json[0] != '{'`），异常时以 ERR 级别输出
hex 转义的前 80 字节，便于后续定位异常 config 来源。

### 4. `include/ProxyBatchTester.h` — 新增成员

```cpp
std::vector<bool> pregenFailedFlags_;   // E7: per-index pre-gen failure flags
```

## 验证

| 项目 | 结果 |
|------|------|
| Debug 构建（Ninja, -j8） | ✅ `bin/validproxy.exe` + `bin/validproxy-cli.exe` 生成成功 |
| 修复代码入产物 | ✅ 二进制含 `PREGEN_FAILED` / `Pre-gen config failed` / `BAD outbound_json` 字符串 |
| 全量 ctest | ✅ 21/21 通过（38.57s） |
| E7 单元测试（新增 7 用例） | ✅ `PreGenFailedSkipTest` 7/7 通过 |
| 老二进制对比 | ✅ 12:30 日志无 `PREGEN`/`DIAG` 记录 → 证实老版本无防护（HEAD f094669 不含 E7） |

## 新增单元测试

`tests/test_proxy_batch_components.cpp` 按项目既有 "reproduce internal logic" 模式
新增 `PreGenFailedSkipTest`（7 用例），覆盖：
- 正常节点继续 / 失败节点跳过 / 全部失败全部跳过
- 越界 index 走 guard 分支、flags 与 configs 长度漂移防御、空 flags 全继续
- 混合批次决策计数（2 跳过 + 3 继续）

## 后续建议

- 生产库 `bin/worker/guindb.db` 中仍残留 49+ 个 `Telegram🇨🇳 @WangCai2`
  type=5 垃圾节点及更多不可解码行，建议运行 `-D, -dedup` 清理。
