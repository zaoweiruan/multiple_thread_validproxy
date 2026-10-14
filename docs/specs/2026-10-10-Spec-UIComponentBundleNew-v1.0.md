# 新增「组件包配置」页的「新增」按钮

- 日期：2026-10-10
- 模块：src/ui/ConfigDialog（组件包配置子页）
- 关联计划：docs/plans/2026-09-22-Plan-ConfigPipeline-Component-Implementation-v1.0.md

## 背景

「组件包配置」页当前只有 刷新 / 删除 / 保存 三个按钮，没有创建入口。
组件包只能由 `-RB`（rebuild-bundles）从模板 Config 拆分产生；用户若想手工新增一个
自定义组件包（例如新增一段 routing 规则包），在 UI 上无路可走。

## 目标

在「组件包配置」页新增「新增」按钮，允许用户创建一个新的组件包。

## 设计

「新增」创建一条**立即落库的骨架组件包**，并选中它、把它填进右侧编辑器；
用户在此基础上继续编辑，点「保存」完成定制。

流程：

1. 弹出输入框让用户填写新组件包的 ID（默认 `xray_CUSTOM`）。
2. 校验 ID 非空、且 DB 中不存在（`dao.getById`）。
3. 弹出单选框选择核心类型（Xray / SingBox）。
4. 以骨架内容落库：id / name(=id) / description / coreType / version=1 /
   enabled=true / components=[]，走既有 `ComponentBundleDAO::save`。
5. `loadBundles()` 刷新并选中新建行，编辑器显示骨架 JSON（多行美化显示）。
6. 同步 `syncConfigsFromChains(新ID)`（新建包尚未被任何链引用，预期同步 0 个模板）。

用户在编辑器里改完点「保存」，复用现有 `onBundleSave` 落库，无需改动它。

### 为什么立即落库而不是「草稿态」

`onBundleSave` 要求先选中列表行（`ConfigDialog.cpp:1754-1758`，`sel < 0` 直接提示
「请先选择要保存的组件包」）。若「新增」只填编辑器不落库，用户接着点「保存」会被
这个前置条件挡住，无法完成创建。因此让「新增」直接把骨架写进 DB 并选中该行，
使既有保存路径立即可用——避免为「草稿」引入额外的状态字段。

骨架的 `components` 为空数组，是合法且可解析的包（不贡献任何节段），
即使用户放弃编辑也只会留下一条空包，不会破坏任何模板的物化。

## 改动范围

- `src/ui/ConfigDialog.h`：声明 `onBundleNew`。
- `src/ui/ConfigDialog.cpp`：
  - `createBundleTab()`：新增「新增」按钮并绑定 `onBundleNew`。
  - 新增 `ConfigDialog::onBundleNew`：输入 ID + 核心、唯一性校验、填入骨架 JSON。

## 不改动

- `ComponentBundleDAO`（新增走既有 `save`）。
- `onBundleSave` / `onBundleDelete` / `ComponentResolver` 等其它逻辑。

## 追加：组件类型 / 占位符速查表（2026-10-10 第二批需求）

需求：①「组件包配置」页增加组件类型表与占位符表，**上下排列为一组**，置于 JSON 编辑器**水平右侧**；
②把 `docs/design/2026-09-22-Design-ConfigPipeline-Component-v1.0.md` 里的这两张表按当前代码校正。

### 布局

原 `mainSizer`（横向）为 `leftSizer`(组件包列表) | `rightSizer`(JSON 编辑器)。现改为：

```
mainSizer(横向)
├── leftSizer        组件包列表 + 按钮栏
└── editorRowSizer   (新增，横向)
    ├── rightSizer   JSON 编辑器（proportion=1，保持原有可伸缩宽度）
    └── tableSizer   (新增，纵向) ← 两表上下排列
        ├── 组件类型表 wxListCtrl (360x380, proportion=1)
        └── 占位符表   wxListCtrl (360x220, proportion=1)
```

### 组件类型表：运行时生成，不硬编码

`populateBundleTypeTable()` 通过 `ComponentRegistry::instance().getAllTypes()` +
`getMetadata(type)` 填充，列=枚举/值/Xray 节/SingBox 节/策略。
**刻意不写死 15 行**——若硬编码，代码新增类型时这张表会静默过期（这正是原文档漂移的原因）。
注册表现有测试 `tests/test_component_registry.cpp:24` 已断言 `getAllTypes().size() == 15`。

### 占位符表：静态 8 行

`populateBundleVarTable()` 用静态表列出 `variablesToMap()`（`ConfigBuilder.cpp:35-46`）
的 8 个占位符。此处**必须**静态：布尔/字符串占位符无法从注册表推导。

## 文档校正内容

`docs/design/2026-09-22-Design-ConfigPipeline-Component-v1.0.md` 两处：

1. **§2.3.4 占位符表**：5 个 → 8 个（补 `${enableObservatory}` / `${enableCnDirect}` / `${blockQuic}`）；
   修正 `observatoryInterval` 单位为**毫秒**（原写「秒」，默认 30000）；
   补类型列与「布尔替换后是字符串」的注意事项。
2. **§6.2 组件类型映射表**：改用枚举 `INBOUND`/`OUTBOUND`（原文档误用单数 `inbound`/`outbound`）；
   补枚举值、组合策略、单例标记、SingBox 节名（`ROUTING`→`route`）；
   原 `geodata`/`version` 两行在改写时一度孤立残留，已清理。

两处均附「校正说明」记录原表错在哪、依据哪个源文件，避免后人重复踩。

## 验证

- 构建 validproxy / validproxy-cli / UITests 0 error
- test_config_generator 329（328 passed + 1 skipped），含 `getAllTypes()==15` 断言
- UI_MAINWINDOW / UI_CHAINPREVIEW PASS——后者构造完整 ConfigDialog 并遍历全部 4 个子 Tab，
  即本批新增的两个 wxListCtrl 在真实 GUI 中被构造且无 wx assert
- ctest 47/49（`RegionBatchResolverTest` 链接缺源、`UI_PORTCLOSE` 需真实 xray，均为既有失败）

**已知未验证**：截图确认。沙箱内 SendKeys/WM_COMMAND 无法将焦点送入 GUI 进程
（尝试 Ctrl+, 与测试里用的 WM_COMMAND 均未打开对话框），故未取得渲染后的界面截图。
UI 表内容正确性依赖「构造成功 + 数据源已被单测断言」，非目视确认。