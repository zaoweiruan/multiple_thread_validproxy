# Free AI Models List (2026)
Aggregated from OpenRouter, OpenCode Zen, Kilo, and NVIDIA platforms

---

## Table of Contents
- [OpenRouter Free Models](#openrouter-free-models)
- [OpenCode Zen Free Models](#opencode-zen-free-models)
- [Kilo Code Free Models](#kilo-code-free-models)
- [NVIDIA Free Access](#nvidia-free-access)

---

## OpenRouter Free Models

**Rate Limits:** 20 requests/minute, 50-1000 requests/day (50 req/day if account credit < $10, 1000 req/day if ≥ $10)

### Free Models by Capability

| Model | Context | Strength/Skill |
|-------|---------|----------------|
| **qwen/qwen3-coder:free** | 262K | Strongest free coding model, 480B parameters |
| **deepseek/deepseek-r1:free** | 128K | Reasoning, math, complex problem solving |
| **deepseek/deepseek-v4-flash:free** | 1.05M | General-purpose, large context |
| **google/gemma-4-31b-it:free** | 262K | Google's Gemma 4, safety-tuned |
| **google/gemma-4-26b-a4b-it:free** | 262K | MoE architecture, efficient inference |
| **nvidia/nemotron-3-super-120b-a12b:free** | 1M | NVIDIA 120B hybrid MoE, long-context reasoning |
| **openai/gpt-oss-120b:free** | 131K | OpenAI's open-source model |
| **openai/gpt-oss-20b:free** | 131K | Smaller OpenAI open-source variant |
| **meta-llama/llama-3.3-70b-instruct:free** | 128K | Solid all-purpose model |
| **meta-llama/llama-3.2-3b-instruct:free** | 128K | Fast, low-VRAM-friendly |
| **z-ai/glm-4.5-air:free** | 131K | Z.ai GLM model |
| **arcee-ai/trinity-large-thinking:free** | 262K | Arcee's reasoning model |
| **poolside/laguna-m.1:free** | 131K | Poolside flagship coding model |
| **poolside/laguna-xs.2:free** | 131K | Smaller Poolside model |
| **minimax/minimax-m2.5:free** | 205K | MiniMax's M2.5 model |
| **baidu/cobuddy:free** | 131K | Baidu's CoBuddy |

### Specialized OpenRouter Free Models

| Model | Specialty |
|-------|-----------|
| **nvidia/nemotron-3-nano-30b-a3b:free** | 30B nano model, efficient reasoning |
| **nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free** | Multimodal (video/audio/image/text) |
| **nvidia/nemotron-nano-12b-v2-vl:free** | Vision-language model |
| **nvidia/nemotron-nano-9b-v2:free** | 9B vision model |
| **liquid/lfm-2.5-1.2b-thinking:free** | Liquid's lightweight reasoning model |
| **liquid/lfm-2.5-1.2b-instruct:free** | Lightweight instruction model |
| **nousresearch/hermes-3-llama-3.1-405b:free** | Large parameter count |
| **cognitivecomputations/dolphin-mistral-24b:free** | Fine-tuned variant |
| **openrouter/free** (router) | Auto-selects best available free model |

---

## OpenCode Zen Free Models

**Note:** Limited-time free models, availability may change

### Current Free Models

| Model | Context | Type | Best For |
|-------|---------|------|----------|
| **Big Pickle** | Variable | Stealth model | Complex programming, code review |
| **DeepSeek V4 Flash Free** | Variable | MoE (284B total, 13B active) | Fast inference, strong coding |
| **Nemotron 3 Super Free** | Variable | NVIDIA 120B hybrid MoE | Code generation, reasoning |
| **MiniMax M2.5 Free** | Variable | MiniMax model | Learning, exploration |
| **MiniMax M2.1 Free** | Variable | MiniMax model | General coding |
| **GLM 4.7 Free** | Variable | Z.ai GLM | Reasoning, multilingual |
| **GLM 5 Free** | Variable | Z.ai GLM | General tasks |
| **Kimi K2.5 Free** | Variable | Moonshot model | Long-context tasks |
| **Qwen3.6 Plus Free** | Variable | Qwen model | Complex tasks |

### OpenCode Zen Pricing Variants

| Model | Input | Output | Cached Read | Cached Write |
|-------|-------|--------|-------------|--------------|
| Big Pickle | Free | Free | Free | - |
| DeepSeek V4 Flash Free | Free | Free | Free | - |
| Nemotron 3 Super Free | Free | Free | Free | - |
| MiniMax M2.5 Free | Free | Free | Free | - |

---

## Kilo Code Free Models

### Auto-Free Tier (kilo-auto/free)
- Routes to best available free models dynamically
- No credits required
- Subject to rate limits
- May log prompts for model improvement

### Individual Free Models

| Model | Context | Provider | Specialty |
|-------|---------|----------|-----------|
| **poolside/laguna-m.1:free** | 131K | Poolside | Flagship coding agent model |
| **poolside/laguna-xs.2:free** | 131K | Poolside | Smaller, efficient model |
| **openai/gpt-oss-120b:free** | 131K | OpenAI | Open-source 120B model |
| **openai/gpt-oss-20b:free** | 131K | OpenAI | Smaller 20B variant |
| **z-ai/glm-4.5-air:free** | 131K | Z.ai | Efficient GLM model |
| **arcee-ai/trinity-large-thinking:free** | 262K | Arcee | Reasoning model |
| **nvidia/nemotron-3-super-120b-a12b:free** | 1M | NVIDIA | Long-context reasoning |
| **nvidia/nemotron-3-nano-30b-a3b:free** | 256K | NVIDIA | Efficient nano model |
| **nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free** | 256K | NVIDIA | Multimodal reasoning |
| **deepseek/deepseek-v4-flash:free** | 1.05M | DeepSeek | Flash variant |
| **minimax/minimax-m2.5:free** | 205K | MiniMax | M2.5 model |
| **google/gemma-4-31b-it:free** | 262K | Google | Gemma 4 instruction |
| **inclusionai/ring-2.6-1t:free** | 262K | inclusionAI | Reasoning, agent workflows |
| **tencent/hy3-preview:free** | 262K | Tencent | Hybrid MoE for agentic workflows |
| **openrouter/pony-alpha** | 200K | OpenRouter | Coding, roleplay |
| **google/gemma-4-26b-a4b-it:free** | 262K | Google | MoE Gemma 4 |

---

## NVIDIA Access

### Free Options

| Access Method | Details |
|---------------|---------|
| **build.nvidia.com** | Free trial: 1000 credits on signup, additional 4000 with business email |
| **Hugging Face** | Download and run open models free (Apache 2.0 license) |
| **OpenRouter** | Free variants via `:free` suffix (nvidia/nemotron-3-super-120b-a12b:free) |
| **Self-host** | Download from Hugging Face, run locally at no cost |
| **Kilo Code** | Free access to Nemotron models via kilo.ai |

### Nemotron Model Family

| Model | Parameters | Context | Specialty |
|-------|------------|---------|-----------|
| **Nemotron-3-Super-120B-A12B** | 120B (12B active) | 1M | Long-context reasoning, agentic workflows |
| **Nemotron-3-Nano-30B-A3B** | 30B (3B active) | 128K | Efficient reasoning |
| **Nemotron-3-Nano-Omni-30B-A3B** | 30B | 128K | Multimodal (video/audio/image/text) |
| **Nemotron-3-Nano-4B** | 4B | 262K | Edge deployment, small agents |
| **Nemotron-Nano-9B-V2** | 9B | 128K | Reasoning and chat |
| **Nemotron-Nano-12B-2-VL** | 12B | 128K | Vision-language |

---

## Model Recommendations by Task

### Best Free Models for Coding (2026)

| Rank | Model | Platform | Why |
|------|-------|----------|-----|
| 1 | **Qwen3-Coder-480B** | OpenRouter | 262K context, strongest free coding model |
| 2 | **DeepSeek-V4-Flash** | OpenRouter/Zen | Fast inference, efficient MoE |
| 3 | **MiniMax-M2.5** | OpenCode Zen/Kilo | Good balance of capability/cost |
| 4 | **Big Pickle** | OpenCode Zen | Stealth model, good performance |
| 5 | **Nemotron-3-Super** | OpenRouter/Kilo | 1M context, long reasoning |

### Best Free Models for Reasoning

| Rank | Model | Platform | Why |
|------|-------|----------|-----|
| 1 | **DeepSeek-R1** | OpenRouter | Dedicated reasoning model |
| 2 | **Nemotron-3-Super** | OpenRouter/Kilo | Hybrid MoE, 1M context |
| 3 | **Trinity-Large-Thinking** | OpenRouter/Kilo | Arcee reasoning model |
| 4 | **Nemotron-3-Nano-Omni** | OpenRouter/Kilo | Multimodal reasoning |
| 5 | **Qwen3-Next** | OpenRouter | Reasoning capabilities |

### Best Free Models for Large Context

| Rank | Model | Context | Platform |
|------|-------|---------|----------|
| 1 | **DeepSeek-V4-Flash** | 1.05M | OpenRouter |
| 2 | **Nemotron-3-Super** | 1M | OpenRouter/Kilo |
| 3 | **Gemma-4-31B** | 262K | OpenRouter/Kilo |
| 4 | **Gemma-4-26B-A4B** | 262K | OpenRouter/Kilo |
| 5 | **Llama-3.1-405B** | Variable | OpenRouter |

---

## Rate Limits Summary

| Platform | Per-Minute Limit | Daily Limit | Notes |
|----------|------------------|-------------|-------|
| OpenRouter (Free) | 20 RPM | 50-1000/day | 50 if < $10 credit, 1000 if ≥ $10 |
| OpenCode Zen | Provider-dependent | Variable | Limited-time free models |
| Kilo (Auto-Free) | Provider-dependent | Variable | May log prompts |
| NVIDIA API | 1000 initial | Trial credits | 1000 + 4000 with business email |

---

## Data Privacy Notes

| Platform | Data Usage Policy |
|----------|-------------------|
| OpenRouter Free | Provider-dependent, may log prompts |
| OpenCode Zen | Limited-time models may log for improvement |
| Kilo Auto-Free | May route through NVIDIA endpoints (trial terms) |
| NVIDIA API | Trial use only, prompts logged (non-production) |
| Self-hosted (HF) | Full control, no data sharing |

---

*Last Updated: May 2026*
*Sources: OpenRouter, OpenCode, Kilo.ai, NVIDIA Developer*