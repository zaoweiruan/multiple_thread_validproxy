# OpenCode Zen 免费模型一览

> 来源：[OpenCode Zen 官方文档](https://dev.opencode.ai/docs/zh-cn/zen/)
> 更新日期：2026-05-21

---

## 永久免费

| 模型 | ID | 说明 |
|------|----|------|
| **GPT 5 Nano** | `gpt-5-nano` | 永久免费，数据不用于训练，轻量任务首选，隐私安全 |
| **Big Pickle** | `big-pickle` | 完全免费，性能强劲，适合复杂编程、代码审查 |

## 限时免费

| 模型 | ID |
|------|----|
| **MiniMax M2.5 Free** | `minimax-m2.5-free` |
| **Ling 2.6 Flash Free** | `ling-2.6-flash` |
| **Hy3 Preview Free** | `hy3-preview-free` |
| **Nemotron 3 Super Free** | `nemotron-3-super-free` |
| **Qwen3.6 Plus Free** | `qwen3.6-plus-free` |

> 限时免费模型目前处于反馈收集阶段，后续可能调整。

## 选型建议

| 使用场景 | 推荐模型 | 理由 |
|---------|---------|------|
| 日常轻量任务 | GPT 5 Nano | 永久免费，响应快，数据安全 |
| 敏感/专有代码 | GPT 5 Nano | 数据不用于训练 |
| 复杂重构/架构决策 | Big Pickle | 性能接近付费模型 |
| 代码生成/日常编码 | Nemotron 3 Super Free | 性价比高 |
| 学习探索 | MiniMax M2.5 Free | 适合实验性任务 |
| 生产环境/关键项目 | 付费模型（如 Claude Sonnet 4.5, GPT 5.4）| 稳定性和隐私保障更高 |

## 使用方式

1. 在 TUI 中执行 `/connect`，选择 **OpenCode Zen**
2. 添加账单信息（免费模型无需付费）
3. 通过 `/models` 命令随时切换模型

## API 端点

所有 Zen 免费模型统一使用以下端点：

```
https://opencode.ai/zen/v1/chat/completions
```

SDK 兼容：`@ai-sdk/openai-compatible`
