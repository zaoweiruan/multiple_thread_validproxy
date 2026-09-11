# 开发流程规范 (Development Process Rules)

> 版本: v1.1  
> 创建日期: 2026-05-11  
> 最后更新: 2026-08-24（v1.1 新增「UI 自动化测试规范」章节）

---

## ⚠️ 核心规则

### **任何源代码修改前，必须先创建计划文档并经过审核**

> **"先创建计划文档、审核后再执行"**

此规则适用于所有代码变更，无论大小。

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