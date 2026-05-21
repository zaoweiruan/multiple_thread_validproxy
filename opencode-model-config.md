# OpenCode 模型配置文档

> 最后更新: 2026-05-21
> 依据: `docs/opencode-zen-free-models.md`

## 模型分级

| 层级 | 模型 | 说明 |
|------|------|------|
| **永久免费** | `opencode/big-pickle` | 复杂编码、架构设计主力 |
| **永久免费** | `opencode/gpt-5-nano` | 轻量任务首选，日常使用 |
| **限时免费** | `opencode/ling-2.6-flash` | 快速响应 |
| **限时免费** | `opencode/nemotron-3-super-free` | 代码生成推荐 |
| **限时免费** | `opencode/minimax-m2.5-free` | 学习型任务 |
| **非Zen/备降** | `opencode/deepseek-v4-flash-free` | 快速搜索/轻量分类 |
| **非Zen/备降** | `kilo/free` | 最后备降（需 Kilo API key） |
| **未使用** | `openrouter/free` | 已移除（不推荐） |

## 备降链策略

所有 Agent 按以下优先级组织 fallback：

```
永久免费 → 限时免费 → 非Zen备降
```

即: `gpt-5-nano` → `ling-2.6-flash` → `nemotron-3-super-free` → `minimax-m2.5-free` → `deepseek-v4-flash-free` → `kilo/free`

## Agents 配置

### Primary: `big-pickle`（复杂任务 — 6 个）

| Agent | Fallbacks |
|-------|-----------|
| **sisyphus** (max) | gpt-5-nano, ling-2.6-flash, nemotron-3-super-free, minimax-m2.5-free, deepseek-v4-flash-free, kilo/free |
| **hephaestus** | 同上 |
| **oracle** | 同上 |
| **momus** | 同上 |
| **ultrabrain** (category) | 同上 |
| **deep** (category) | 同上 |

### Primary: `ling-2.6-flash`（中等任务 — 6 个）

| Agent | Fallbacks |
|-------|-----------|
| **multimodal-looker** | gpt-5-nano, nemotron-3-super-free, minimax-m2.5-free, deepseek-v4-flash-free, kilo/free |
| **prometheus** | gpt-5-nano, **big-pickle**, nemotron-3-super-free, minimax-m2.5-free, deepseek-v4-flash-free, kilo/free |
| **metis** | 同上 |
| **atlas** | 同上 |
| **sisyphus-junior** | 同上 |
| **visual-engineering** (category) | 同上 |
| **artistry** (category) | 同上 |
| **unspecified-high** (category) | 同上 |

### Primary: `deepseek-v4-flash-free`（轻量任务 — 4 个）

| Agent | Fallbacks |
|-------|-----------|
| **explore** | gpt-5-nano, ling-2.6-flash, nemotron-3-super-free, minimax-m2.5-free, kilo/free |
| **quick** (category) | 同上 |
| **unspecified-low** (category) | 同上 |
| **writing** (category) | 同上 |

## 模型频次统计

| 模型 | Primary 计数 | Fallback 出现次数 | 总计 |
|------|-------------|-------------------|------|
| `big-pickle` | 6 | 10 | 16 |
| `ling-2.6-flash` | 8 | 12 | 20 |
| `deepseek-v4-flash-free` | 4 | 13 | 17 |
| `gpt-5-nano` | 0 | 18 | 18 |
| `nemotron-3-super-free` | 0 | 18 | 18 |
| `minimax-m2.5-free` | 0 | 18 | 18 |
| `kilo/free` | 0 | 18 | 18 |

## 与 Zen 文档的差异

| Zen 文档模型 | 当前状态 | 说明 |
|-------------|---------|------|
| `gpt-5-nano` | ✅ 已使用 | 永久免费，作为所有 fallback 第一顺位 |
| `big-pickle` | ✅ 已使用 | 永久免费，复杂任务 primary |
| `ling-2.6-flash` | ✅ 已使用 | 限时免费，中等任务 primary |
| `minimax-m2.5-free` | ✅ 已使用 | 限时免费，fallback 中 |
| `nemotron-3-super-free` | ✅ 新增 | 限时免费，fallback 中 |
| `hy3-preview-free` | ❌ 未添加 | 限时免费，但在非中文环境质量不稳定 |
| `qwen3.6-plus-free` | ❌ 未添加 | 限时免费，但备降链已够深不做膨胀 |

## 排除的模型

| 模型 | 原因 |
|------|------|
| `openrouter/free` | 不稳定，移除 |
| `hy3-preview-free` | 非中文环境质量不稳定，暂不添加 |
| `qwen3.6-plus-free` | 备降链已有 6 级足够，避免冗余 |
| `openrouter/free` (已移除) | 之前曾使用，因不稳定移除 |

## API Keys

- `kilo`: `auth.json` 中为占位符 `"api"`，需替换为真实 key 才可使用 `kilo/free`
- 其他 Zen 免费模型无需 API key
