# ConfigPipeline Phase 12 完成度核对报告

| 项 | 值 |
|---|---|
| 文档类型 | Plan（核对报告 / Verification Report） |
| 模块 | docs/plans · ConfigPipeline 组件化重构（Phase 12 及其 2026-10-10 增量） |
| 版本 | v1.0 |
| 日期 | 2026-10-13 |
| 状态 | Draft（待 Phase 12 作者裁决） |
| 核对方式 | 只读源码 + git 状态 + 实际编译/测试，**未修改任何代码** |
| 核对对象 | `docs/plans/2026-09-22-Plan-ConfigPipeline-Component-Implementation-v1.0.md`、`docs/design/2026-09-22-Design-ConfigPipeline-Component-v1.0.md`、`docs/specs/2026-10-10-Spec-UIComponentBundleNew-v1.0.md`、`docs/bugfix/2026-10-10-Bugfix-BundleSave-JSONParseCrash-v1.0.md` |

---

## 1. 背景与范围

`docs/plans/project-plans-tracker.md:42` 记录 **「ConfigPipeline 组件化重构实施计划 v1.6 — Phase 12 模板拆解 + 组件包配置 UI 完成 … ✅ completed」**；`:30-37` 另记录 8 条 2026-10-10 的增量条目（UI「新增」按钮、两张速查表、保存闪崩修复、链保存收口、布局调整、移除「配置管线」Tab）。

但这些条目的**对应代码交付物全部处于未提交状态**，且两份 2026-10-10 文档本身仍是 untracked。本报告逐项核对「文档声称」与「代码事实」，给出完成度判定与处置建议。

**不在本报告范围**：判断这批未提交改动的归属与提交时机（按已定方案 A，属他人在飞的 ConfigPipeline Phase 12 工作，由其作者决定）。

---

## 2. 核对方法与证据来源

| 证据 | 获取方式 | 性质 |
|---|---|---|
| tracker 状态 | `docs/plans/project-plans-tracker.md:30-42` 逐行读取 | 文档声称 |
| 交付物存在性 | 文件大小 + `ReadAllLines` 行数统计 | 事实 |
| 公开 API | `Select-String` 提取 `include/config/TemplateDecomposer.h` 全部方法声明 | 事实 |
| 测试用例数 | `Select-String '^(TEST|TEST_F|TEST_P)\('` 计数 | 事实 |
| 符号落地 | 全仓 `src/**/*.cpp` + `include/**/*.h` grep | 事实 |
| git 归属 | `git status --short`（每文件是否 ` M` / clean） | 事实 |
| 编译与运行 | `cmake --build build --target test_config_generator` + 运行 | 运行时 |

---

## 3. 核对结果

### 3.1 Phase 12 主体（tracker :42）— 全部到位

| 交付物 | tracker 声称 | 实测 | 判定 |
|---|---|---|---|
| `include/config/TemplateDecomposer.h` | 新增 | **122 行**；公开 API 齐全：`decomposeAll` / `decompose` / `decomposeAndSave` / `planMinimalBundles` / `rebuildBundlesAndChains` / `syncConfigsFromChains` / `syncConfigFromChain` / `inferTemplateType` / `sectionToComponentType` / `objectToParams` | ✅ |
| `src/config/TemplateDecomposer.cpp` | 新增 | **592 行** | ✅ |
| `tests/test_template_decomposer.cpp` | 23 用例 | **38 个 `TEST_F`**（覆盖 section 映射、TemplateType 推断、decompose 全类型、minimal bundle 规划、rebuild、sync） | ✅ 超额 |
| CLI `-DC` / `-decompose` | 注册 | `src/main_cli.cpp:98` 帮助文本、`:289` 参数分派、`:995` 命令分支、`:1024` `decomposeAndSave(true)` | ✅ |
| ConfigDialog「组件包配置」Tab | 新增 | `createBundleTab:1603`、`loadBundles:1653`、`onBundleSave:1729`、`onBundleDelete:1804`；`ConfigDialog.h:93-98` 声明齐全 | ✅ |
| CMakeLists 注册 | 4 处 | `CMakeLists.txt:718` `test_config_generator` 聚合 target 可编译可运行 | ✅ |
| 验证数据 | 316 tests（315 passed + 1 skipped） | **实测 339 tests / 338 passed / 1 skipped / 0 failed（1697 ms）** | ✅ |

唯一 SKIPPED 为 `tests/test_config_generator.cpp:60`「`generateConfig requires DB for loadProfiles - not implemented yet`」，属既有 skip，非本阶段引入。

⚠️ **但 6 个交付物文件在 `git status --short` 中全部为 ` M`（已修改未暂存）** ⇒ 交付物存在于工作区，**不在 git 历史中**。

### 3.2 2026-10-10 增量（tracker :30-37）— 文档与测试已就位，生产实现未落地

| tracker 条目 | 声称交付物 | 实测 | 判定 |
|---|---|---|---|
| :31 「新增」按钮 | `ConfigDialog::onBundleNew`（含 ID 校验 + 核心类型选择 + 骨架落库） | **全仓 0 命中** | ❌ |
| :32 组件类型/占位符速查表 | `populateBundleTypeTable` / `populateBundleVarTable` | **全仓 0 命中** | ❌ |
| :33 保存闪崩修复（核心） | `onBundleSave` 改用 `error_code` 重载 + `is_object()` 判型 + `SchemaValidator` 闸门 + ID 冲突检查 | **`ConfigDialog.cpp:1739` 仍是单参 `boost::json::parse`** | ❌ |
| :34 缺口 B：精确 ID 匹配 | 静态辅助 `chainReferencesBundle` | ✅ 存在于 `TemplateDecomposer.cpp:552`（定义）与 `:584`（调用） | ✅ |
| :34 缺口 A/C：`onChainSave` 判 success、物化失败不报成功 | `onChainSave` 逻辑 | `onChainSave:1978` 存在；具体判据需结合 `:1951` 的 `error_code` 用法一并复核 | ⚠️ |
| :37 移除「配置管线」Tab | 删除 `createPipelineTab` | **`ConfigDialog.h:63` 声明仍在，`.cpp` 2 处仍在** | ❌ |
| :33 的 6 个测试 | `ComponentBundleRejectsNonObjectRoot` / `…RejectsStringComponentType` / `…RejectsComponentTypeOutOfRange` / `…AcceptsAllEnumBoundaryTypes` / `…AcceptsEmptyComponentsArray` / `…RejectsNonObjectComponentEntry` | `tests/test_schema_validator.cpp` **全部存在** | ✅ |
| :34 的 3 个测试 | `SyncConfigsFromChains_MatchesBundleIdExactly` / `SyncConfigFromChain_FailsWhenBundleMissing` / `SyncConfigFromChain_FailsOnMalformedChain` | `tests/test_template_decomposer.cpp` **全部存在** | ✅ |
| :32 的 design 校正 | 占位符表 5 → 8（补 `enableObservatory` / `enableCnDirect` / `blockQuic`），`observatoryInterval` 单位改毫秒 | design 文档中 `enableObservatory` 2 处、`blockQuic` 2 处、`observatoryInterval` 3 处 | ✅ |

### 3.3 🔴 最严重分歧：闪崩修复从未生效

`src/ui/ConfigDialog.cpp` 在 git 中 **clean**（既无工作区改动，也不在任何本阶段提交内），其 `:1739` 仍为 bugfix 文档明确指认的死路径写法：

```cpp
auto parseResult = boost::json::parse(bundleEditor_->GetValue().ToStdString());
if (parseResult.is_null()) {   // ← 单参重载语法错误时抛异常，此分支不可达
    wxMessageBox(L"JSON 解析失败", ...);
    return;
}
auto& root = parseResult.as_object();   // ← 合法但非对象的 JSON 在此抛异常
```

⇒ **用户点「保存」仍会闪崩**，而 `docs/bugfix/2026-10-10-Bugfix-BundleSave-JSONParseCrash-v1.0.md` 的「## 修复」章节声称已改为 `error_code` 重载并接上校验闸门。

同时，bugfix 文档新增的 6 个测试全部落在 `SchemaValidator` 层，**不经过 `onBundleSave`** ⇒ 即使全部通过，也无法证明闪崩已修，属**假覆盖**。

### 3.4 文档一致性缺陷（5 处）

| # | 缺陷 | 位置 | 影响 |
|---|---|---|---|
| D1 | **plan 文档只有阶段 1–11，从无阶段 12** | `2026-09-22-Plan-ConfigPipeline-Component-Implementation-v1.0.md` §阶段计划共 11 个标题 | tracker 记录的「Phase 12 ✅ completed」在 plan 中无对应条目，后续无法回溯阶段 12 的原始范围与验收标准 |
| D2 | **plan 变更记录只有 v1.0**，而标题写 v1.6、正文列 11 阶段 | 同上「## 变更记录」仅 1 行 | 迭代历史全部丢失 |
| D3 | **design §13 标题写「52h」，表格合计写「57h」** | `Design-…v1.0.md:1762` vs 同节表格末行 | 自相矛盾；其下「关键变更（v1.6）」明写「40h 调整为 57h（+17h）」⇒ **57h 为准，标题 52h 有误** |
| D4 | **design 标题写「v1.5」**，变更记录最新却是 v1.6 | `Design-…v1.0.md:10` | 标题未同步，易误读为旧版 |
| D5 | **两份 2026-10-10 文档仍 untracked**，但 tracker `:31/:32/:33/:34/:35/:36` 已引用其路径 | `docs/specs/2026-10-10-Spec-UIComponentBundleNew-v1.0.md`、`docs/bugfix/2026-10-10-Bugfix-BundleSave-JSONParseCrash-v1.0.md`（均 `??`） | tracker 指向 git 中不存在的文档路径；他人无法从仓库读到这两份文档 |

---

## 4. 结论：完成度矩阵

| 范围 | 完成度 | 说明 |
|---|---|---|
| **Phase 12 主体**（模板拆解 + 组件包配置 Tab + CLI + CMake + 测试） | **✅ 100%** | 测试数超额（38 vs 23），运行时 338 passed / 0 failed；**但 6 个交付物文件全部未提交** |
| **10-10 UI 增量**（`onBundleNew` 新增按钮 + 两张速查表） | **❌ 0%** | 生产代码全无；纯 UI 增强，缺失不影响功能正确性 |
| **10-10 闪崩修复**（`onBundleSave` error_code + 判型 + 校验闸门 + ID 冲突检查） | **❌ 未生效** | 崩溃根因代码原样存在且已提交；**唯一影响用户可用性的缺陷** |
| **10-10 缺口 B**（`syncConfigsFromChains` 精确 ID 匹配） | **✅ 已落地** | `TemplateDecomposer.cpp:552/584` |
| **10-10 缺口 A/C**（`onChainSave` 判 success、物化失败不报成功） | **⚠️ 需复核** | 函数存在，具体判据待核 |
| **10-10 测试与 design 校正** | **✅ 已就位** | 6 + 3 个用例存在；占位符表已补 8 项 |
| **10-10 移除「配置管线」Tab** | **❌ 未做** | 声明与实现均仍在 |
| **文档一致性** | **⚠️ 5 处缺陷** | D1–D5，见 §3.4 |

**总体判定：Phase 12 主体完成；2026-10-10 增量处于「文档与测试已写、生产实现未落地」的分裂状态，不应视为完成。**

---

## 5. 建议处置顺序

按「先修正确性、再补功能、最后整理文档」排序：

1. **补闪崩修复**（唯一影响用户的缺陷）
   在 `ConfigDialog::onBundleSave`（`ConfigDialog.cpp:1729-1803`）按 bugfix 文档所述改为 `boost::json::parse(text, ec)` + `ec` 判错 + 显式 `is_object()` 判型 + `SchemaValidator::validate(..., "componentBundle", ...)` 闸门 + 与其它包的 ID 冲突检查；失败路径一律弹窗且**不落库**。
   同时补一个**穿过 `onBundleSave` 的**回归测试（现有 6 个测试都在 `SchemaValidator` 层，无法覆盖该路径）。
   参照仓库内既有的正确写法：`src/config/FullConfigTemplateDAO.cpp:728`、`src/XrayApi.cpp:476`、`src/config/TemplateDecomposer.cpp:481`。

2. **决定是否实现 UI 增量**
   `onBundleNew`（`Spec-UIComponentBundleNew` 的「新增」按钮）与 `populateBundleTypeTable` / `populateBundleVarTable`（两张速查表）为纯 UI 增强。
   注意 `onBundleNew` 依赖「立即落库」设计（因 `onBundleSave` 要求先选中列表行），实现前需确认与步骤 1 的保存闸门不冲突。

3. **决定「配置管线」Tab 的去留**
   tracker :37 记录用户选项 1 为「评估彻底移除」，但代码未动。需确认是「已决定保留」还是「待办未做」。

4. **修正文档一致性（D1–D5）**
   在 plan 中补写「阶段 12」条目与其验收标准、补齐变更记录；修正 design §13 的 52h → 57h、标题版本号 v1.5 → v1.6。

5. **最后再提交**
   上述 1–4 完成后，把 Phase 12 交付物与 10-10 增量作为一个或多个提交纳入 git。当前工作区的 18 个已跟踪未暂存文件 + 2 个 untracked 文档与另一条工作线（StandalonePortImmediateRebind，15 个 commit 已落地）**零耦合**，可独立提交。

---

## 6. 附录：复现命令

```powershell
# 1. tracker 状态
Select-String -Path docs/plans/project-plans-tracker.md -Pattern 'Phase 12|2026-10-10'

# 2. plan 文档阶段数（预期只有阶段 1-11）
Select-String -Path docs/plans/2026-09-22-Plan-ConfigPipeline-Component-Implementation-v1.0.md `
  -Pattern '^### 阶段'

# 3. Phase 12 交付物规模
#    include/config/TemplateDecomposer.h  122 行
#    src/config/TemplateDecomposer.cpp    592 行
#    tests/test_template_decomposer.cpp  951 行 / 38 个 TEST_F
[System.IO.File]::ReadAllLines('include/config/TemplateDecomposer.h').Length

# 4. 10-10 UI 增量是否落地（预期全部 0）
Get-ChildItem -Path src,include -Recurse -Include *.cpp,*.h |
  Select-String -Pattern 'onBundleNew|populateBundleTypeTable|populateBundleVarTable'

# 5. 闪崩根因是否仍在（预期命中 :1739 的单参 parse）
Select-String -Path src/ui/ConfigDialog.cpp -Pattern 'boost::json::parse'

# 6. 编译 + 运行时验证
cmake --build build --target test_config_generator    # 预期 0 error
#    运行后预期：339 tests / 338 passed / 1 skipped / 0 failed
```

---

## 7. 变更记录

| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-10-13 | v1.0 | 初始核对报告：Phase 12 主体判定完成（未提交）；2026-10-10 增量判定为「文档+测试已就位、生产实现未落地」；记录 5 处文档一致性缺陷 |