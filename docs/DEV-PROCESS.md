# 开发流程规范 (Development Process Rules)

> 版本: v1.0  
> 创建日期: 2026-05-11  
> 最后更新: 2026-05-11

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
7. 更新 docs/plans/2026-05-11-todo-tracker.md 中的进度
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

## 引用

- [计划文档目录](./plans/)
- [长期记忆](../project-knowledge.md)