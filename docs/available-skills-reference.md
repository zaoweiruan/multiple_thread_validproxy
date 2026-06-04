# Available Skills Reference

本文档记录项目中可用的 Agent 技能清单及安装方法。

## 代码审查相关技能

| 技能 | 用途 | 安装量 |
|------|------|--------|
| `requesting-code-review` | 完成任务、实现主要功能或合并前进行代码审查 | 内置 |
| `receiving-code-review` | 接收代码审查反馈，进行技术审查和验证 | 内置 |
| `frontend-ui-ux` | UI/UX 代码审查 | 内置 |

## 重构相关技能

| 技能 | 用途 | 安装量 | 安装命令 |
|------|------|--------|----------|
| `langgenius/dify@component-refactoring` | 组件重构 | 3.5K | `npx skills add langgenius/dify@component-refactoring` |
| `wondelai/skills@refactoring-ui` | UI 组件重构 | 3.5K | `npx skills add wondelai/skills@refactoring-ui` |
| `wondelai/skills@refactoring-patterns` | 通用重构模式 | 2.2K | `npx skills add wondelai/skills@refactoring-patterns` |
| `abhigyanpatwari/gitnexus@gitnexus-refactoring` | Git 代码重构 | 775 | `npx skills add abhigyanpatwari/gitnexus@gitnexus-refactoring` |

## 代码质量评估技能

| 技能 | 用途 | 安装量 | 安装命令 |
|------|------|--------|----------|
| `miles990/claude-software-skills@code-quality` | 代码质量分析 | 479 | `npx skills add miles990/claude-software-skills@code-quality` |
| `ruvnet/claude-flow@agent-analyze-code-quality` | 代码质量评估 | 69 | `npx skills add ruvnet/claude-flow@agent-analyze-code-quality` |
| `nickcrew/claude-ctx-plugin@code-quality-workflow` | 代码质量工作流 | 52 | `npx skills add nickcrew/claude-ctx-plugin@code-quality-workflow` |
| `onekeyhq/app-monorepo@1k-code-quality` | 代码质量检查 | 73 | `npx skills add onekeyhq/app-monorepo@1k-code-quality` |

## 通用安装方法

```bash
# 安装技能（全局）
npx skills add <owner/repo@skill> -g -y

# 搜索技能
npx skills find <query>

# 检查更新
npx skills check

# 更新所有技能
npx skills update
```

## 技能浏览器

访问 https://skills.sh/ 浏览更多技能。