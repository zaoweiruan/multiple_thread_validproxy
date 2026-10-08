# Bug 修复：ConflictDetector::detectDependencies 单复数节名不匹配

- **日期**: 2026-09-24
- **类型**: bugfix
- **模块**: `src/config/ConflictDetector.cpp` / `src/config/ComponentRegistry.cpp`
- **版本**: v1.0
- **状态**: ✅ completed

## 1. 问题现象

Phase 6 全量测试（214 tests）中 3 个既有 ConflictDetectorTest 失败：

| 测试 | 期望 | 实际 |
| --- | --- | --- |
| `DetectCompleteDependencies`（test_conflict_detector.cpp:502） | conflicts.size() == 0 | 2 |
| `DetectSpecialTags`（:514） | conflicts.size() == 0 | 1 |
| `DetectMissingOutbound`（:490） | conflicts[0].component2 == "outbound" | "inbound" |

`--gtest_filter` 隔离复现仍失败 → 与测试顺序无关，是代码行为本身。

## 2. 根因分析

`ComponentRegistry::registerDefaultTypes()` 中 INBOUND/OUTBOUND 的 configSection 为**复数**（即 Xray JSON 节名）：

```cpp
ComponentType::INBOUND,  "inbounds",  "inbounds",  false, CombinationStrategy::APPEND
ComponentType::OUTBOUND, "outbounds", "outbounds", false, CombinationStrategy::APPEND
```

而 `ConflictDetector::detectDependencies` 用 `typeToString()` 返回值与**单数**比较：

```cpp
std::string typeName = registry_.typeToString(comp.type);
if (typeName == "inbound")          // typeToString(INBOUND) 实际返回 "inbounds"
else if (typeName == "outbound")    // typeToString(OUTBOUND) 实际返回 "outbounds"
```

由于 `typeToString()` 返回 configSection，INBOUND/OUTBOUND 永远不匹配单数串 → **inboundTags/outboundTags 集合永远收集为空** → routing 规则对每个 inboundTag/outboundTag 无条件报「引用不存在」的 DEPENDENCY_MISSING 冲突。仅 routing（"routing" == "routing"）命中。

这是 Phase 4 引入的残留 bug（组件注册表复数化后暴露——此前注册表单数时恰好通过）。

## 3. 修复

1. `src/config/ConflictDetector.cpp` L183：`if (typeName == "inbound")` → `if (typeName == "inbounds")`
2. `src/config/ConflictDetector.cpp` L188：`else if (typeName == "outbound")` → `else if (typeName == "outbounds")`
3. `tests/test_conflict_detector.cpp` L481：`createRoutingComponent({"socks-in"}, "nonexistent-outbound")` → `createRoutingComponent({}, "nonexistent-outbound")`（DetectMissingOutbound 测试数据修正：原 routing 同时引用不存在的 inbound "socks-in"，按新逻辑先报 inbound 缺失；改为空 inboundTag 数组，只测 outbound 缺失，符合测试意图）

## 4. 验证

- 构建：`cmake --build build --target test_config_generator --parallel 8` → 0 error 0 warning
- 测试：`tests\test_config_generator.exe --gtest_brief=1` → **214 tests（213 passed + 1 skipped 既有 generateConfig requires DB + 0 failed）**
- 3 个失败测试全部转绿（210 → 213 passed）

## 5. 经验

- 字符串比较应**与注册表 configSection 单数/复数一致**（"inbounds"/"outbounds" 是 Xray 配置节名，不是组件名字）。
- 外部改动将注册表复数化后，此前恰好通过的测试会暴露隐藏的单复数失配——**对 untracked 核心文件也不要假设其内容稳定**。