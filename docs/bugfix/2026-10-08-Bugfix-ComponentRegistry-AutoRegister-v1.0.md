---
title: "docs: bugfix — 组件链编辑「组件类型」全 unknown / 数组组件策略退化为 OVERRIDE"
type: bugfix
status: completed
date: 2026-10-08
updated: 2026-10-08
version: v1.0
---

# Bugfix：组件链编辑「组件类型」全 unknown

## 1. 现象

配置对话框 →「模板配置」→「组件链编辑」，选中带组件链的模板并点「解析」后：

- 右侧组件列表「组件类型」列全部显示 `unknown`
- 「配置节」列为空、「组合策略」列为 `-`
- 「来源 Bundle」列文字被截断（如 `xray_base_00…`）
- 底部状态显示「解析成功：4 个组件，0 个冲突」——解析本身没报错

证据截图：`bin/log/8aacf227-1458-4929-8085-3580455928b7.png`

## 2. 根因

`ComponentRegistry::registerDefaultTypes()` **只在单元测试里被调用**（6 个测试文件的 `SetUp()`），生产代码路径（`ConfigDialog` / `BundleChainEditor` / `BundleChainResolver` / `ConflictDetector`）从未调用过。

于是单例 `ComponentRegistry::instance()` 的 `metadata_` 在生产中始终为空：

- `getMetadata(type)` 恒返回 `nullptr` → `ComponentListPanel::rebuild()` 走 else 分支 → 类型列 `unknown`、配置节空、策略 `-`
- `getStrategy(type)` 走默认返回 → **更严重的功能缺陷**：`BundleChainResolver::mergeComponents` 拿不到 `APPEND`/`MERGE`，把 `inbounds`/`outbounds`/`routing` 当作 `OVERRIDE` 处理——base 链的入站会被业务链的入站整体**覆盖**而不是追加，等于 base 打底失效
- `ConflictDetector` 用 `typeToString()`（同样依赖 `metadata_`）填 `conflict.component1` → 恒为 `unknown`，与 UI 的高亮匹配全部落空

## 3. 修复

### 3.1 `ComponentRegistry::instance()` 首次访问自动注册（`include/config/ComponentRegistry.h`、`src/config/ComponentRegistry.cpp`）

新增私有 `ensureDefaults()` + `defaultsRegistered_` 标志，`instance()` 内幂等调用一次 `registerDefaultTypes()`。测试显式调用 `registerDefaultTypes()` 仍然有效（其内部先 `clear()` 再全量注册）。

`clear()` 同时把 `defaultsRegistered_` 置回 `false`：清空即视为「未注册默认类型」，下次 `instance()` 自动恢复，使单例在任何调用序列下都可用。既有直接持有引用调 `clear()` 的两个用例（`GetMetadataReturnsNullForUnregistered` / `ClearRemovesAllRegistrations`）不经 `instance()`，仍能观察到空表，行为不变。

### 3.2 组件列表列语义修正（`src/ui/ComponentListPanel.cpp`）

原代码把「组件类型」列和「配置节」列都填 `meta->configSection`，两列重复且类型列丢失语义。改为：

- 「组件类型」列：`meta->displayName`（如 `Inbounds`），未注册时退化为 `typeToString()`（即 configSection），再退化才是 `unknown`
- 「配置节」列：`meta->configSection`（如 `inbounds`）

### 3.3 tag 冲突高亮前缀匹配（同文件）

`ConflictDetector` 对数组组件的 tag 冲突写作 `component1 = "<typeName>:<tag>"`，原 UI 用 `c.component1 != typeName` 精确比较，tag 冲突永远匹配不上。改为同时匹配精确名与 `"<typeName>:"` 前缀。

### 3.4 列宽调整（同文件）

「来源 Bundle」列 160 → 210，「组件类型」140 → 130，「配置节」120 → 110，「状态」90 → 70，避免文字被截断。

## 4. 验证

- 新增回归用例 `ComponentRegistryAutoRegister.InstanceRegistersDefaultsWithoutExplicitCall`（`tests/test_component_registry.cpp`）：不显式调 `registerDefaultTypes()` 直接取 `instance()`，断言 `count()==15`、`INBOUND`/`OUTBOUND` 策略为 `APPEND`、`ROUTING` 为 `MERGE`、`displayName`/`configSection` 非空
- `test_config_generator`：320 tests → **319 PASS + 1 SKIP（既有）+ 0 FAIL**（原 318 + 新增 1）
- `validproxy` 构建 0 error
- UI 冒烟 `UI_MAINWINDOW` + `UI_POOL`：2/2 PASS

## 5. 影响面

修复后除 UI 显示外，还纠正了一条**配置生成的语义缺陷**：base 链与业务链合并时，`inbounds[]`/`outbounds[]` 现在按 `APPEND`（同 tag 替换、无 tag 追加）、`routing` 按 `MERGE` 处理，与设计文档 §4.3 一致。已生成的 8 条链（`xray_base_001` + `xray_test_001` 等）在下次解析/生成时会自动按正确策略合并，无需重建链。