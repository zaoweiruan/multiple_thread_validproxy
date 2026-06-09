# multiple_thread_validproxy — 长期记忆

> **角色**: 项目深度知识参考手册。记录 AGENTS.md（概览）、`docs/context.md`（会话上下文）和 `docs/bugfix/`（事件日志）不覆盖的知识。
>
> **边界说明**:
> - `docs/context.md` → 轻量上下文锚点（外部项目路径、配置位置、当前会话引用）
> - `docs/project-knowledge.md` → 深度参考（测试规范、错误标准、架构决策、工具模式）
> - AGENTS.md → 概览与入口（CLI 参数、模块映射、核心规则）
> - `docs/bugfix/` → 单次 Bug 修复记录（移出 context.md §三 后归档至此）
>
> **更新机制**: 每次会话结束时，检查是否有新的架构决策/跨模块知识需要追加至 §7。单模块修复不进此文件。确保不引入 AGENTS.md 和 `context.md` 已有内容。

---

## 1. 测试规范与数据库配置（简要）

> 完整测试规范（数据库、回归命令、日志级别）见 **AGENTS.md §构建与测试** 和 **AGENTS.md §CLI 参数速查**。

**核心要点**:
- 回归测试库: `test/guiNDB_empty.db`（711 profiles, 8 SubIDs）
- 大规模验证: `test/guindb.db`（53,837 profiles, 44 SubIDs）
- 测试配置: `bin/test_config_empty.json` → `test/guiNDB_empty.db`
- 测试代码始终使用 `test/` 下数据库，**不得操作** `bin/worker/guindb.db`

---

## 2. 代理测试错误级别分类标准

| 分类 | LogLevel | 说明 | 示例 |
|------|----------|------|------|
| **网络错误** | `INFO` | 代理连通性测试失败（预期内） | curl 超时、DNS 解析失败 |
| **配置生成错误** | `ERR` | 代理配置不完整 | `checkRequired` 失败、无效协议 |
| **注入 outbound 错误** | `ERR` | xray 注入失败 | `addOutbound` 非零退出码 |

**原则**: 网络错误是正常行为（代理无效），不视为异常。配置/注入错误表示系统自身问题，使用 `ERR`。

---

## 3. Google Test 规范

### 源码位置
- 本地路径: `E:\eclipse_workspace\googletest`
- 集成方式: CMake `add_subdirectory(E:/eclipse_workspace/googletest)`

### 断言对照
| 非致命 | 致命 | 说明 |
|--------|------|------|
| `EXPECT_EQ` | `ASSERT_EQ` | 相等 |
| `EXPECT_NE` | `ASSERT_NE` | 不等 |
| `EXPECT_TRUE` | `ASSERT_TRUE` | 条件真 |
| `EXPECT_GT` | `ASSERT_GT` | 大于 |
| `EXPECT_LT` | `ASSERT_LT` | 小于 |

- 优先使用非致命 `EXPECT_*` 收集所有失败
- 致命 `ASSERT_*` 用于前置条件检查（nullptr、DB 连接）
- `tests/` 下非 GTest 文件（如 `test_curl_easy_handle.cpp`）使用 `cassert` + 自包含 `main()`

### 测试数据库约定
- 测试代码始终使用 `test/guindb.db` 或 `test/guiNDB_empty.db`
- 不得操作 `bin/worker/guindb.db`（生产数据库）

---

## 4. 错误分析记录（2026-05-14）

> 完整分析（4 类错误模式、根因、核心结论）见 **`docs/reports/error-report_20260514.md`**。

**摘要**: 日志扫描发现 4 类错误 — REALITY 字段缺失、Network 数据污染、校验策略不一致。根因在导入阶段校验不足。

---

## 5. 计划跟踪 — 三文档协同模型

| 文档 | 用途 | 生命周期 |
|------|------|---------|
| `docs/plans/YYYY-...-plan.md` | 单个计划详细描述 | draft → completed/cancelled |
| `docs/plans/project-plans-tracker.md` | 全量索引 + 进度总览 | 长期 in_progress |
| `docs/project-knowledge.md` | 长期记忆、关键决策、架构知识 | 持久存在 |

### 生命周期流程
```
创建计划文档 (draft)
  → 更新跟踪文档（添加索引行）
  → 审核计划
  → 执行代码变更
  → 更新计划文档 (completed/cancelled)
  → 更新跟踪文档（同步索引状态）
  → 更新本文件（记录关键决策）
```

---

## 6. 常见工具模式

### ID 生成 (`Utils.cpp:21-32`)
```cpp
std::string utils::generateUniqueId();  // 19 位数字, 4|5 开头
```
用于 `Profileitem::indexid`、`Subitem::id`（批量导入）。

### URL 工具 (SubitemUpdaterV2)
- `isValidUrlFormat(url)` — http/https + 有效域名
- `hasValidPath(url)` — 检查路径部分
- `extractRemarksFromUrl(url)` — 从 URL 提取 remarks

### 弃用提示
- `SubitemUpdaterV2::log()` 已弃用，新代码使用 `Logger::write()`

---

## 7. 最近关键决策记录

> 仅记录 **影响架构或跨模块** 的决策。单模块 Bug 修复见 `docs/bugfix/`。

| 日期 | 决策 | 影响 |
|------|------|------|
| 2026-05-05 | 移除 `blacklisted` 冗余字段，改用 `consecutive_failures < threshold` 实时计算 | ProfileExItem 简化 |
| 2026-05-05 | 去重统一为 5 字段键：`Address+Port+ConfigType+Id+Network` | 避免 VMess/VLESS 误判 |
| 2026-04-16 | XrayManager 改为单例模式 | ProxyFinder/AppController 复用同一实例 |
| 2026-04-28 | CoreType NULL 处理：空值时 `sqlite3_bind_null()` | v2rayN 兼容性 |
| 2026-05-06 | 废弃 `update_subscription` 和 `check_auto_update_interval` 字段 | ConfigReader 清理 |
