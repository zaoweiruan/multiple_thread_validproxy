# 组件包「保存」闪崩 + 保存前 JSON 合法性校验

- 日期：2026-10-10
- 模块：src/ui/ConfigDialog（组件包配置页 onBundleSave）、src/config/SchemaValidator.cpp
- 关联：docs/specs/2026-10-10-Spec-UIComponentBundleNew-v1.0.md

## 现象

用户新增组件包（`xray_ENV_geodata`，`components: []`）后在「组件包配置」页点「保存」，
应用闪崩，且配置没有保存成功。截图见 `bin/log/e378381f-*.png`。

## 根因：单参 `boost::json::parse` 会抛异常

`onBundleSave` 原代码：

```cpp
auto parseResult = boost::json::parse(bundleEditor_->GetValue().ToStdString());
if (parseResult.is_null()) {          // ← 死路径
    wxMessageBox(L"JSON 解析失败", ...);
    return;
}
auto& root = parseResult.as_object(); // ← 非对象时抛异常
```

两个缺陷：

1. **`parseResult.is_null()` 是死代码。** `boost::json::parse` 的单参重载在语法错误时
   **抛 `boost::system::system_error`**，不返回 null。所以「JSON 解析失败」的提示永远走不到；
   异常直接逃逸出 `onBundleSave`（wx 事件处理器不捕获异常）→ 进程终止 → 闪崩。
2. **`as_object()` 无类型前置检查。** 合法但非对象的 JSON（`[1,2,3]`、`"abc"`、`123`）
   解析成功却不带 `ec`，随后 `as_object()` 抛 `boost::json::system_error` → 同样闪崩。

「配置没保存」是闪崩的直接后果：落库在解析之后，异常在解析处就已终止流程。

仓库其余位置的正确写法都用 `error_code` 重载（`FullConfigTemplateDAO.cpp:728`、
`XrayApi.cpp:476`、`TemplateDecomposer.cpp:481`），此处漏改。

## 修复

### 1. 崩溃修复 + 校验闸门（`ConfigDialog.cpp::onBundleSave`）

- 改用 `error_code` 重载，语法错误 → 弹窗提示 `ec.message()` 且**不落库**
- 显式 `is_object()` 判型，非对象 → 弹窗提示且不落库
- 接 `SchemaValidator::validate(..., "componentBundle", ...)`，结构不合法 → 弹窗显示具体错误且不落库
- 追加 **ID 冲突检查**：JSON 里改 `id` 会因 DAO 以 id 为主键 upsert 而**静默覆盖**另一个包，
  现检测到与其它包重名即拒绝保存

### 2. 修正 `SchemaValidator` 的 `componentBundle` 契约（与真实数据对齐）

原校验要求 `components[i].type` 为**字符串**（`"type": "log"`），但
`ComponentBundleDAO::componentToJson` 写入的是**整数**（`compJson["type"] = static_cast<int>(comp.type)`），
生产库实际内容为 `{"type":1,...}`。即原契约与磁盘格式矛盾，且 `SchemaValidator` 无任何生产调用点，
属未启用的历史实现。

改为：`type` 必须是 `0..14` 的整数（`ComponentType` 枚举范围），另补 `id` 类型检查。

## 测试

`tests/test_schema_validator.cpp` 夹具 `componentBundleJson()` 的 `type` 由 `"log"`
改为 `std::int64_t(1)` 以匹配真实落库格式；`test_phase11_integration.cpp:369`、
`test_phase11_performance.cpp:229` 同步。新增 6 用例：

- `ComponentBundleRejectsNonObjectRoot` — 合法 JSON 但根为数组，保存前必须拦下
- `ComponentBundleRejectsStringComponentType` — 字符串 type 无法被 DAO 反序列化
- `ComponentBundleRejectsComponentTypeOutOfRange` — 99 越界
- `ComponentBundleAcceptsAllEnumBoundaryTypes` — 0 与 14 边界放行
- `ComponentBundleAcceptsEmptyComponentsArray` — **「新增」按钮的骨架包（用户截图那个）必须能存**
- `ComponentBundleRejectsNonObjectComponentEntry` / `...RejectsBadParams`

## 验证

- 构建 validproxy / validproxy-cli / UITests / test_config_generator：0 error
- `test_config_generator` 335 → 335 passed + 1 skipped + **0 failed**（修复前 2 例失败）
- ctest 47/49：`RegionBatchResolverTest`(Not Run，链接缺源)、`UI_PORTCLOSE`(需真实 xray) 均为既有失败
- 生产库 `xray_ENV_geodata`（components 为空数组）经同规则校验：通过，可正常保存

## 追加收口：`onChainSave` + `syncConfigsFromChains`（同批）

用户要求「一并收口」。审阅后发现**先前判断有误**：`onChainSave` 并不解析用户可编辑的 JSON，
它从 `chainEditor_->getChain()` 取结构化对象再自行序列化，故无需 JSON 校验。真正的缺口是另外两个：

### 缺口 A：坏链被静默放行（`onChainSave`）

原代码只检查 `ComponentListPanel::hasErrors(result.conflicts)`，从不检查 `result.success`。
而 `BundleChainResolver::resolveBundleChain`（`src/config/BundleChainResolver.cpp:90-96`）
遇到不存在的 Bundle 会**直接 return**，此时 `result.conflicts` 仍是空 vector：

```cpp
const std::optional<ComponentBundle> bundle = loadBundle(bundleId);
if (!bundle.has_value()) {
    result.success = false;
    result.errorMessage = "Bundle 不存在: " + bundleId;
    return result;          // ← conflicts 从未被填充
}
```

于是「引用了已删除组件包」的链，`hasErrors(空)` 为 false → 判定无冲突 → **照常保存成功**。
此后物化拿不到任何组件，Config 与链持续分歧。

修复：**先判 `success` 再判 conflicts**，失败时弹 `errorMessage` 并拒绝保存。

### 缺口 B：`syncConfigsFromChains` 用子串匹配组件包 ID

原过滤条件 `it->bundleChain.find(changedBundleId) == npos` 是对整段 JSON 做子串搜索。
ID 互为前缀时（如 `xray_INBOU` vs `xray_INBOUND`）会互相误命中，导致无关模板被白跑物化；
反之若 ID 恰好出现在链 JSON 的其它字段中也会误判。

修复：新增文件内静态辅助 `chainReferencesBundle()`，**解析 `bundleIds` 后逐项精确比较**。

### 缺口 C：物化失败仍报「成功」

`syncConfigFromChain` 返回值被忽略，链已落库但 Config 未更新时也弹「并已同步该模板的 Config」，
与实际不符。修复：按返回值区分「成功」与「部分成功」（附日志提示）。

## 追加测试（3 例）

- `SyncConfigsFromChains_MatchesBundleIdExactly` — 精确 ID 命中；前缀 ID 不得命中
- `SyncConfigFromChain_FailsWhenBundleMissing` — 引用缺失返回 false 且 Config 不被写坏（按语义比对 JSON，占位符仍在）
- `SyncConfigFromChain_FailsOnMalformedChain` — 缺 `bundleIds` / 非 JSON / 非对象三种坏链均失败而非崩溃

> 首轮这三个用例曾失败，原因是**测试夹具写错**（`makePlaceholderConfig` 实拆 2 个包而非 1 个；
> 断言直接比字符串而重建阶段已把多行文本规范化为压缩单行），非被测代码问题，已按实际语义改正。

## 追加验证

- 构建 validproxy / validproxy-cli / UITests / test_config_generator：0 error
- `test_config_generator` **338 passed + 1 skipped + 0 failed**（收口前 335）
- ctest 47/49：`RegionBatchResolverTest`(Not Run)、`UI_PORTCLOSE`(Timeout) 均为既有失败

## 最终已知范围

- `ConfigDialog.cpp:1480`（链路预览）已有 try/catch 保护，无需处理
- 链 JSON 由代码序列化产生，不经用户编辑，故不存在与 bundle 保存同类的解析崩溃