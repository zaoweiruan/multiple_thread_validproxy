# 开发流程规范 (Development Process Rules)

> 版本: v1.2  
> 创建日期: 2026-05-11  
> 最后更新: 2026-10-14（v1.2 新增「非本会话源码变更不得修改」「库表 Schema 变更基准」"
"「高危数据重建命令」三条核心规则）

---

## ⚠️ 核心规则

### **规则 1：任何源代码修改前，必须先创建计划文档并经过审核**

> **"先创建计划文档、审核后再执行"**

此规则适用于所有代码变更，无论大小。

---

### **规则 2：非本会话产生的源码变更，一律不得修改**

**判定**：若某个文件的工作区改动**不是当前会话产生的**（例如来自他人在并行工作线上的
未提交改动、来自 `git checkout`/`git stash`/合并产生的他方改动），则：

- **不得**修改该文件，也**不得**将其纳入自己的提交
- 必须**暂停并提示用户**：该变更由谁产生、属于哪条工作线、以及「未经修改方确认，
  不得代为提交」
- 需要基于该变更继续工作时，先请用户确认，或请修改方先自行提交

**原因**：2026-10-13 实际发生——提交 `2ff9089`（另一工作线「修正 Task 1-6 拆分缺陷」）
以一份过期快照盖写了 `src/ui/ConfigDialog.cpp` / `.h`，把本会话在该两文件的**全部**改动
一次性抹掉。因未提交，工作量全部丢失，需逐行重做。

**适用范围**：`src/`、`include/`、`tests/`、`CMakeLists.txt` 等一切源码与构建文件；
数据文件（`bin/config.json` 等）同理。

---

### **规则 3：库表 Schema 变更后，必须同步 `scripts/sync_db_schema.sql`**

**触发条件**：本次变更新增/修改/删除任何库表、列、索引或约束时
（例如 `FullConfigTemplateItem` 增列、`ComponentBundle` 建表、既有表 ALTER）。

**基准库**（唯一事实来源）：

```
E:\v2rayN-windows-64\guiConfigs\guiNDB_Original.db
```

**动作**：以该基准库的结构为准，更新

```
E:\eclipse_workspace\multiple_thread_validproxy\scripts\sync_db_schema.sql
```

使「v2rayn 原生库 → 本项目库」的同步脚本覆盖全部本次 schema 差异。

**要求**：

- 更新前先核对基准库结构（如 `PRAGMA table_info`），确认差异清单
- 脚本头部的「差异」说明须同步更新，列出本次新增的差异项
- 脚本中的 `ALTER TABLE ... ADD COLUMN` 语句在列已存在时会报错，
  故须通过 `scripts/run_sync_db_schema.ps1` 执行（自动处理「列已存在」等错误），
  不要裸跑 `.sql`
- 若本次变更**未**触及 v2rayn 原生库的表，则明确记录「无需改动脚本」及理由，
  不要留下未说明的空档

---

### **规则 4：执行会重建既有数据的命令前，必须先确认**

**高危命令**（会**重建/覆盖**既有数据，不可回退）：

| 命令 | 危害 |
|---|---|
| `validproxy-cli.exe -RB` | 按「core × component-type」**重建**最小组件包并**改写**各模板组件链 |
| `validproxy-cli.exe -DC` | 基于 `decomposeAll()` **重新拆包** |
| 任何直接写 `FullConfigTemplateItem.Config` 的操作 | 可能覆盖已物化的正确配置 |

**实测教训（2026-10-14）**：为「去除父模板」而重新物化 Config 时误用 `-RB`，
它把链引用的 `xray_DNS` 拆成了 `xray_DNS_base` / `xray_DNS_pool` / `xray_DNS_standalone`，
导致链引用失效、3 个模板丢失 `log`/`dns` 节。已从 DB 备份恢复，并新增
`-SM/-sync-materialize`（只按现有链物化、**不碰组件包表**）作为安全替代。

**要求**：

- 执行前先备份数据库（`bin/worker/guindb.backup-*.db`）并关闭占用它的进程
  （含 `bin/worker/validproxy-*.exe` 携带副本——它会以内存映射方式持有 DB，
  导致文件无法被覆盖，复制静默失败）
- 只想「按现有链重新物化 Config」时，用 `-SM`，**不要**用 `-RB` / `-DC`
- 执行后必须**逐节比对**变更前后的配置，确认没有节丢失

---

## 流程

```
1. 识别需要修改的代码或功能
       ↓
2. 在 docs/plans/ 下创建或更新计划文档
   - 文件命名: YYYY-MM-DD-XXX-descriptive-name.md
   - 必须包含 YAML frontmatter (title, type, status, date)
   - 详细描述: 问题描述、范围边界、具体变更、验证步骤
       ↓
3. 计划文档审核 (自审或 peer review)
   - 确认变更范围合理
   - 确认不会引入回归
   - 确认验证方法可行
       ↓
4. 执行代码修改
   - 严格按计划文档执行
   - 如需偏离计划，先更新文档再修改
       ↓
5. 验证
   - 编译通过 (0 errors)
   - 单元测试通过
   - 功能测试通过 (如适用)
       ↓
6. 更新计划文档状态
   - status: completed
   - 记录验证结果
       ↓
7. 更新 docs/plans/project-plans-tracker.md 中的进度
```

---

## 计划文档规范

### Frontmatter (必须)

```yaml
---
title: "fix/feat/docs: 简要描述"
type: fix | feat | refactor | docs
status: draft | in_progress | completed | cancelled | superseded
date: YYYY-MM-DD
origin: "(可选) 来源说明"
supersedes: (可选) ["被替代的文档名"]
---
```

### 内容结构 (推荐)

```markdown
# 标题

## 问题描述

## 范围边界
- 修改: xxx
- NOT 修改: xxx

## 详细变更
### U1: 文件1 — 变更描述
### U2: 文件2 — 变更描述

## 验证步骤

## 文件变更列表

## 风险
```

---

## 状态定义

| 状态 | 含义 |
|------|------|
| `draft` | 计划已创建，待审核 |
| `in_progress` | 已开始执行 |
| `completed` | 已执行完毕并通过验证 |
| `cancelled` | 计划不再执行，说明原因 |
| `superseded` | 被其他计划取代，保留参考 |

---

## 日志等级规范 (补充)

| 等级 | 用途 | 示例 |
|------|------|------|
| `INFO` | 常规流程、生命周期事件 | "Started successfully", "SQL returned N profiles" |
| `WARN` | 边界条件、可恢复异常 | "No proxies to test", "Using default network" |
| `ERR` | 错误路径、失败操作 | "Failed to create process", "Sync failed: N proxy(es)" |
| `REPORT` | 统计汇总、性能指标 | "Migration Result — Total: 703, Succeeded: 703" |
| `DEBUG` | 调试信息、详细追踪 | "Source database opened" |

**所有 `Logger::write` 调用必须包含显式的 `LogLevel` 参数。**

---

## UI 自动化测试规范

> 生效基础: [`docs/plans/2026-08-24-Plan-UITestFramework-v1.0.md`](./plans/2026-08-24-Plan-UITestFramework-v1.0.md)（已交付）
> 交付报告: [`docs/reports/2026-08-24-Report-UITestFramework-v1.0.md`](./reports/2026-08-24-Report-UITestFramework-v1.0.md)

### 适用范围（强制）

以下变更**必须**随附 UI 自动化用例（新增或扩展既有用例）：

| 变更类型 | 要求 |
|------|------|
| 新增 / 修改窗口、对话框、面板及其中的控件行为 | 随附对应用例 |
| 修改 MainFrame / AppController 中影响 UI 可见状态的调度逻辑 | 随附或回归相关用例 |
| 修改 Events 自定义事件中影响 UI 刷新的链路 | 回归相关用例 |

纯算法 / DAO / 网络层变更由 GTest 覆盖，不强制 UI 用例。

### 框架组成与命令

| 组件 | 位置 |
|------|------|
| 测试框架（UIA COM RAII / 元素封装 / 进程生命周期 / 失败截图+树dump+JUnit 监听器） | `tests/ui/framework/` |
| 测试用例（Catch2 3.x，`catch2:x64-mingw-static`） | `tests/ui/Test*.cpp` |
| 控件定位常量（**唯一定位标识源，禁止在用例内硬编码选择器**） | `tests/ui/UIIds.h` |
| 一键验收脚本（工具链→依赖→构建→沙箱→测试 五步流水线） | `scripts\build-and-test.bat` |
| 分步脚本（构建 / 沙箱重置 / 仅跑测试） | `scripts\build.bat`、`scripts\prepare-ui-sandbox.bat`、`scripts\test-ui.bat` |
| 测试沙箱（隔离 DB 与配置，每次运行自动重建） | `test/ui-sandbox/` |

### 开发流程（TDD + 定位先行）

1. **定位先行**: 新控件的 UIA 标识必须先用发现工具实测获取（运行 TestDiscovery 或 `UITests.exe --dumptree`），再固化进 `UIIds.h`；**禁止凭猜测写死 Name/ClassName**
2. 先写用例（RED）→ 实现功能 → 用例转绿（GREEN）
3. 交付前 `.\scripts\build-and-test.bat` 必须 **ALL TESTS PASSED**（exit 0）
4. 失败按 **A业务 / B框架 / C用例 / D环境** 四分类处置；B 类（框架级）累计 ≥3 次停止修复并升级评审；**严禁削弱或删除断言使测试通过**

### 数据隔离红线

- 测试只允许触碰 `test/ui-sandbox/`；**严禁读写 `bin/worker/guindb.db` 与 `test/guindb.db`**
- **调试/修复验证一律使用 `bin/` 下的最新构建产物**；`bin/worker/validproxy.exe` 为陈旧副本（非构建输出），禁止作为调试、验证或复现对象（2026-09-11 AppHang 排查教训：曾误将 bin/worker 陈旧副本纳入排查范围）
- GUI 以真实 `bin\validproxy.exe -c <沙箱配置>` 启动；测试进程只依赖 Win32/UIA/COM，不链接 wxWidgets
- 失败产物（PNG 截图 + UTF-8 控件树 dump）自动输出至 `test-results/ui-artifacts/`

### 已知限制（二期）

- wxDataViewCtrl 自绘单元格内容断言未支持（决策 D5），列表数据暂以间接信号断言
- 沙箱 config 未显式写 `xray.executable`（D8 弹窗由 prepare 脚本存在性守卫兜底）

---

## 引用

- [计划文档目录](./plans/)
- [长期记忆](../project-knowledge.md)