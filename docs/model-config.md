# Model Configuration Reference

> 版本: v4.0
> 创建日期: 2026-06-09
> 最近更新: 2026-09-03
> 配置文件: `C:\Users\dsm\.config\opencode\oh-my-openagent.json`
> 数据来源: OpenCode Zen 官方文档、OpenRouter、Kilo Gateway、`kilo_models.json`（2026-09-02 抓取，365 模型）、`update-model-config` 技能实时抓取（2026-09-03）

---

> **Kilo Gateway 模型数据源**：`E:\eclipse_workspace\gemini_start\src\kilo_models.json` 为 Kilo Gateway 权威模型目录（`fetch_time: 2026-09-02T11:10:00`，`total: 365`）。以下 Kilo 相关章节均以该目录为准。

## 模型池 (Model Pool)

### OpenCode Zen Free（来源：opencode.ai/zen）

| 模型标识符 | 来源 | 状态 | 角色定位 |
|---|---|---|---|
| `opencode/big-pickle` | OpenCode Zen Free | 限时免费 | 主力推理 — sisyphus/oracle/momus/deep/ultrabrain |
| `opencode/deepseek-v4-flash-free` | OpenCode Zen Free | 限时免费 | 通用推理 |
| `opencode/mimo-v2.5-free` | OpenCode Zen Free | **新增，限时免费** | 备降中档 |
| `opencode/nemotron-3-super-free` | OpenCode Zen Free (NVIDIA) | 限时免费 | 备降高优先级（⚠️ 实时源未返回，保留降级） |
| `opencode/nemotron-3-ultra-free` | OpenCode Zen Free (NVIDIA) | **新增，限时免费** | 备降高优先级 |
| `opencode/nemotron-3.5-lightning-free` | OpenCode Zen Free (NVIDIA) | **新增，限时免费** | 备降高优先级 |
| `opencode/laguna-s-2.1-free` | OpenCode Zen Free (Poolside) | **新增，限时免费** | 代码模型 |
| `opencode/ling-3.0-flash-fin-free` | OpenCode Zen Free (InclusionAI) | **新增，限时免费** | 金融模型 |
| `opencode/muse-spark-1.2-contributor-free` | OpenCode Zen Free | **新增，限时免费** | 备降中档 |
| `opencode/muse-spark-1.3-contributor-free` | OpenCode Zen Free | **新增，限时免费** | 备降中档 |
| `openrouter/free` | OpenRouter Free | 持续可用 | 最终备降兜底 (27+ 免费模型自动路由) |

> ⚠️ 限时免费模型数据可能被用于改进模型，请勿发送个人/敏感信息。

### Agnes 免费模型（来源：api.agnes-ai.cn，`-flash` 后缀 = 免费）

| 模型标识符 | 提供商 | 备注 |
|---|---|---|
| `agnes-2.0-flash` | Agnes AI | 通用 flash 模型 |
| `agnes-2.5-flash` | Agnes AI | 通用 flash 模型 |
| `agnes-image-2.1-flash` | Agnes AI | 图像生成 |
| `agnes-image-2.5-flash` | Agnes AI | 图像生成 |
| `agnes-video-2.5-flash` | Agnes AI | 视频生成 |

> ⚠️ 需 `AGNES_API_KEY` 环境变量；接口未发布免费标识，以 `-flash` 后缀判定为免费。

### OpenRouter Free（备选，通过 `openrouter/free` 自动路由）

| 模型标识符 | 提供商 | Context | 备注 |
|---|---|---|---|
| `openrouter/owl-alpha` | openrouter | 1.0M | 高性能 agent 模型 |
| `poolside/laguna-m.1:free` | Poolside | 262K | 旗舰代码模型 |
| `poolside/laguna-xs.2:free` | Poolside | 262K | 轻量代码模型 |
| `nvidia/nemotron-3-super-120b-a12b:free` | NVIDIA | 1M | MoE 120B |
| `nvidia/nemotron-3-ultra-550b-a55b:free` | NVIDIA | 1M | MoE 550B |
| `nvidia/nemotron-3-nano-30b-a3b:free` | NVIDIA | 256K | 高效 MoE |
| `nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free` | NVIDIA | 256K | 多模态 (text/image/audio) |
| `nvidia/nemotron-nano-12b-v2-vl:free` | NVIDIA | 128K | 视觉语言模型 |
| `nvidia/nemotron-nano-9b-v2:free` | NVIDIA | 128K | 推理模型 |
| `openai/gpt-oss-120b:free` | OpenAI | 131K | MoE 120B，开源 |
| `openai/gpt-oss-20b:free` | OpenAI | 131K | MoE 20B，开源 |
| `moonshotai/kimi-k2.6:free` | MoonshotAI | 262K | 多模态代码模型 |
| `z-ai/glm-4.5-air:free` | Z.ai | 131K | 轻量 GLM 系列 |
| `google/gemma-4-31b-it:free` | Google | 262K | 多模态 |
| `google/gemma-4-26b-a4b-it:free` | Google | 262K | MoE，多模态 |
| `nex-agi/nex-n2-pro:free` | Nex AGI | 262K | agentic MoE |
| `qwen/qwen3-coder:free` | Qwen | 1.0M | 代码专用 |
| `qwen/qwen3-next-80b-a3b-instruct:free` | Qwen | 262K | MoE |
| `meta-llama/llama-3.3-70b-instruct:free` | Meta | 131K | 通用对话 |
| `meta-llama/llama-3.2-3b-instruct:free` | Meta | 131K | 轻量快速 |
| `nousresearch/hermes-3-llama-3.1-405b:free` | NousResearch | 131K | 大参数通用 |
| `liquid/lfm-2.5-1.2b-thinking:free` | LiquidAI | 33K | 轻量推理 |
| `liquid/lfm-2.5-1.2b-instruct:free` | LiquidAI | 33K | 轻量指令 |

> 完整 27+ 模型列表及最新费率：https://openrouter.ai/collections/free-models

### Kilo Gateway Auto 模型（自动路由智能体）

Kilo 平台提供 5 个 **`kilo-auto/*`** 自动路由伪模型（数据源：`kilo_models.json`）。请求按 `autoRouting` 列表动态路由到最优底层模型，无需显式指定。

| 模型标识符 | 上下文 | 免费 | 训练权限 | 路由目标 |
|---|---|---|---|---|
| `kilo-auto/frontier` | 1,000,000 | 否 | 否 | 任意任务最强性能（AI SDK: anthropic/claude） |
| `kilo-auto/balanced` | 1,000,000 | 否 | 否 | 14 模型（价格/能力均衡，见下） |
| `kilo-auto/efficient` | 1,000,000 | 否 | 否 | 14 模型（按成本最优路由） |
| `kilo-auto/small` | 262,144 | 否 | 否 | 轻量小模型 |
| `kilo-auto/free` | 256,000 | **是** | **是** | 3 免费模型（见下） |

> ⚠️ `kilo-auto/free` 训练权限为 **是**（`mayTrainOnYourPrompts: true`），提示词可能被上游提供商用于改进服务，勿发送敏感数据。

**`kilo-auto/balanced` / `kilo-auto/efficient` 共享 14 模型路由列表：**

```
anthropic/claude-sonnet-5, deepseek/deepseek-v4-flash-0731, deepseek/deepseek-v4-pro-0813,
kwaipilot/kat-coder-pro-v2.5, meta/muse-spark-1.2, minimax/minimax-m3, moonshotai/kimi-k3,
openai/gpt-5.6-luna, qwen/qwen3.8-max, thinkingmachines/inkling, x-ai/grok-4.6,
xiaomi/mimo-v2.5-pro, z-ai/glm-5.3, z-ai/glm-5.3-flash
```

**`kilo-auto/free` 3 免费模型路由列表：**

```
minimax/minimax-m3:free, poolside/laguna-s-2.1:free, stepfun/step-3.7-flash:free
```

### Kilo Gateway 模型目录（kilo_models.json，2026-09-02 权威快照）

以下为 `kilo_models.json` 收录的 **付费/免费关键模型** 简表（完整 365 项以 JSON 文件为准）。

#### Kilo 免费模型（`isFree: true`，18 个）

| 模型标识符 | 上下文 | 输入模态 | 备注 |
|---|---|---|---|
| `kilo-auto/free` | 256K | text | 自动路由免费伪模型 |
| `minimax/minimax-m3:free` | 1.05M | text/image/video | MiniMax M3 多模态 |
| `minimax/minimax-m2.7:free` | 196K | text | MiniMax M2.7 |
| `poolside/laguna-s-2.1:free` | 262K | text | Poolside 代码模型（terminal-bench 70.2%） |
| `poolside/laguna-xs-2.1:free` | 262K | text | Poolside 轻量 |
| `stepfun/step-3.7-flash:free` | 262K | text/image | StepFun MoE |
| `thinkingmachines/inkling:free` | 1.05M | text/image/audio | Thinking Machines Inkling |
| `thinkingmachines/inkling-small:free` | 1.05M | text/image/audio | Inkling Small |
| `nvidia/nemotron-3-ultra-550b-a55b:free` | 1.0M | text | NVIDIA Nemotron 3 Ultra |
| `nvidia/nemotron-3-super-120b-a12b:free` | 262K | text | NVIDIA Nemotron 3 Super |
| `nvidia/nemotron-3.5-lightning:free` | 1.0M | text | NVIDIA Nemotron 3.5 Lightning |
| `nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free` | 256K | text/audio/image/video | NVIDIA 多模态 |
| `nvidia/nemotron-3.5-content-safety:free` | 128K | text/image | NVIDIA 内容安全 |
| `cohere/north-mini-code:free` | 256K | text | Cohere North Mini 代码 |
| `dots-studio/dots-3-note-preview:free` | 512K | text/image | Dots Studio（2026-09-30 到期） |
| `liquid/lfm-2.5-2.6b:free` | 64K | text | Liquid LFM 轻量 |
| `inclusionai/ling-3.0-flash-fin:free` | 262K | text | InclusionAI 金融模型 |
| `openrouter/free` | 200K | text/image | OpenRouter 免费兜底 |

> 免费模型匿名用户限速：200 请求/小时/IP；认证用户可提升至 1000 请求/天。
> 详情：https://kilo.ai/docs/gateway/models-and-providers

---

## 代理与类别配置 (Agents & Categories)

> 来源：`C:\Users\dsm\.config\opencode\oh-my-openagent.json`，2026-06-09 实际快照。

### Agents

| Agent | 主模型 | 备降链 |
|---|---|---|
| **sisyphus** (max) | `opencode/big-pickle` | deepseek-v4-flash-free → minimax-m2.5-free → nemotron-3-super-free → openrouter/free |
| **hephaestus** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **oracle** | `opencode/big-pickle` | deepseek-v4-flash-free → minimax-m2.5-free → nemotron-3-super-free → openrouter/free |
| **explore** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **multimodal-looker** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **prometheus** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **metis** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **momus** | `opencode/big-pickle` | deepseek-v4-flash-free → minimax-m2.5-free → nemotron-3-super-free → openrouter/free |
| **atlas** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **sisyphus-junior** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |

### Categories

| Category | 主模型 | 备降链 |
|---|---|---|
| **visual-engineering** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **ultrabrain** | `opencode/big-pickle` | deepseek-v4-flash-free → minimax-m2.5-free → nemotron-3-super-free → openrouter/free |
| **deep** | `opencode/big-pickle` | deepseek-v4-flash-free → minimax-m2.5-free → nemotron-3-super-free → openrouter/free |
| **artistry** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **quick** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **unspecified-low** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **unspecified-high** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |
| **writing** | `opencode/deepseek-v4-flash-free` | minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free |

---

## 备降链设计

### 层级划分

```
Tier 1 — 重型推理 (big-pickle)
  └─ sisyphus, oracle, momus, deep, ultrabrain
  └─ 备降: deepseek-v4-flash-free → minimax-m2.5-free → nemotron-3-super-free → openrouter/free
  (主模型已失败时不再重复尝试 big-pickle)

Tier 2 — 通用推理 (deepseek-v4-flash-free)
  └─ 其余所有代理和类别
  └─ 备降: minimax-m2.5-free → nemotron-3-super-free → big-pickle → openrouter/free
  (big-pickle 作为最后手段保留在备降链中)
```

### 备降顺序逻辑

```
主模型 → fallback[0] → fallback[1] → ... → fallback[N]
```

- 主模型不可用时（配额耗尽、超时、错误），按数组顺序尝试备降
- 主模型不会重复出现在备降链中（避免无效重试）
- `openrouter/free` 始终位于末尾作为最终兜底
- 备降链设计目标：**最多 4 次尝试后回退到 openrouter/free**

---

## v4.0 变更摘要（2026-09-03）

| 变更项 | 说明 |
|---|---|
| **引入 update-model-config 技能实时同步** | 抓取 3 源（Kilo/Agnes/OpenCode），以实时数据为准刷新免费模型目录 |
| OpenCode Zen Free 表新增 5 模型 | `laguna-s-2.1-free`、`ling-3.0-flash-fin-free`、`muse-spark-1.2/1.3-contributor-free`、`nemotron-3.5-lightning-free`；`nemotron-3-super-free` 实时源未返回，标注保留降级 |
| 新建 Agnes 免费模型节 | `api.agnes-ai.cn` 5 个 `-flash` 免费模型（2.0/2.5-flash、image-2.1/2.5-flash、video-2.5-flash） |
| Kilo 免费模型移除已下架项 | `meituan/longcat-2.0-free` 已下架（19 → 18 个） |

## v3.0 变更摘要（2026-09-03）

| 变更项 | 说明 |
|---|---|
| **数据源升级** | 以 `kilo_models.json`（2026-09-02 权威抓取，365 模型）为准更新 Kilo 章节 |
| 新增 Kilo Gateway Auto 模型节 | 5 个 `kilo-auto/*` 自动路由模型（frontier/balanced/efficient/small/free）+ 完整 14/4 路由列表 |
| 重写 Kilo Gateway 免费模型节 | 移除已下架 `poolside/laguna-m.1:free`，以目录实际 19 个免费模型为准（新增 meituan/longcat、minimax-m2.7:free、inkling、laguna-s-2.1、dots-studio、cohere/north-mini-code 等） |
| 补充关键模型上下文与模态 | 免费模型上下文长度、输入模态、训练权限标注 |

## v2.1 变更摘要（2026-06-09）

| 变更项 | 说明 |
|---|---|
| 新增 `opencode/mimo-v2.5-free` | OpenCode Zen 2026-05-27 新增，当前配置未使用 |
| 新增 `opencode/nemotron-3-ultra-free` | OpenCode Zen NVIDIA 高优先级免费模型 |
| 新增 OpenRouter Free 模型详表 | 27+ 模型完整列表，含上下文长度与能力标注 |
| 新增 Kilo Gateway Free 模型节 | `stepfun/step-3.7-flash:free` 等平台原生免费模型 |
| 配置来源快照 | 附加 `oh-my-openagent.json` 完整内容作为可验证快照 |

---

## v2.0 变更历史

| 日期 | 版本 | 变更 | 说明 |
|---|---|---|---|
| 2026-09-03 | v4.0 | update-model-config 技能实时同步 | 3 源实时抓取；OpenCode 加 5、Agnes 新建、Kilo 移除 longcat-2.0 |
| 2026-09-03 | v3.0 | Kilo 模型目录重写 | 以 `kilo_models.json`（2026-09-02，365 模型）权威数据更新 Kilo Auto/免费模型章节 |
| 2026-06-09 | v2.1 | 免费模型更新 | 补充 mimo-v2.5-free、nemotron-3-ultra-free；新增 OpenRouter/Kilo Gateway 详表 |
| 2026-05-26 | v2.0 | 全面替换 | 移除 `gpt-5-nano`(付费)、`ling-2.6-flash`(已移除)；统一为 `deepseek-v4-flash-free`；重构备降链消除重复 |
| 2026-05-25 | v1.0 | 初始版本 | 移除 `qwen3.6-plus-free`，统一免费模型池 |

---

## 原生多模态免费模型 (Multimodal Free Models)

以下免费模型原生支持图像、视频、音频等多模态输入：

| 模型标识符 | 提供商 | 支持模态 | 上下文 | 备注 |
|---|---|---|---|---|
| `nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free` | NVIDIA | text/image/video/audio | 256K | 企业级多模态感知子代理 |
| `nvidia/nemotron-nano-12b-v2-vl:free` | NVIDIA | text/multi-image | 128K | 视觉语言模型，文档理解强 |
| `moonshotai/kimi-k2.6:free` | MoonshotAI | text/image | 262K | 多模态代码模型，支持 agent 编排 |
| `google/gemma-4-31b-it:free` | Google | text/image | 262K | 多模态，140+ 语言 |
| `google/gemma-4-26b-a4b-it:free` | Google | text/image/video(60s) | 262K | MoE，视频理解 |
| `nex-agi/nex-n2-pro:free` | Nex AGI | text/image | 262K | agentic MoE，Qwen3.5 架构 |
| `minimax/minimax-m3` | MiniMax | text/image | 1.0M | 高吞吐多模态 |
| `nvidia/nemotron-3.5-content-safety:free` | NVIDIA | text/image | 128K | 内容安全多模态 |
| `google/lyria-3-pro-preview` | Google | text/image | 1.0M | 音频/音乐生成 |
| `google/lyria-3-clip-preview` | Google | text/image | 1.0M | 音频/音乐生成 |

> 数据来源：OpenRouter 免费模型页面、freellm.net 模型列表、Kilo Gateway 文档，抓取时间 2026-06-09。

### 多模态模型选用建议

| 场景 | 推荐模型 |
|---|---|
| wxgui-analyzer（GUI 截图分析） | 主模型：`nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free`；**第一备降：`stepfun/step-3.7-flash:free`**（Kilo 免费，text/image）→ `minimax/minimax-m3:free` → `openrouter/free` |
| 通用图像理解 | `google/gemma-4-31b-it:free` 或 `nvidia/nemotron-nano-12b-v2-vl:free` |
| 多图像/图表/文档 | `nvidia/nemotron-nano-12b-v2-vl:free`（OCRBench v2 领先） |
| 视频理解 | `google/gemma-4-26b-a4b-it:free` 或 `nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free` |
| 音频/音乐 | `google/lyria-3-pro-preview` 或 `google/lyria-3-clip-preview` |
| 代码+视觉 | `moonshotai/kimi-k2.6:free` 或 `nex-agi/nex-n2-pro:free` |
| 企业 agent | `nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free` |
