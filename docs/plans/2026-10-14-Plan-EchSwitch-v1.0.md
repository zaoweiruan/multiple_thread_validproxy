---
title: "feat: ECH 开关（是否启用 ECH）—— 生成配置时清空 / 修正 echConfigList"
type: plan
status: draft
date: 2026-10-14
version: v1.0
---

# 计划：ECH 开关

## 问题描述

`ProfileItem.echconfiglist`（ECH 加密 ClientHello 配置列表）当前在生成配置时
被**无条件**写入 `tlsSettings.echConfigList`（`src/config/StreamSettingsBuilder.cpp:49-51`）：

```cpp
if (!p.echconfiglist.empty()) {
    tlsSettings["echConfigList"] = boost::json::value(p.echconfiglist);
}
```

导致两个问题：

1. **无法关闭**：某些节点/客户端不支持 ECH，用户无法在不删除该字段的前提下禁用，
   只能把订阅里的值清空——但订阅更新会再次写回。
2. **订阅源编码错误未被修正**：ECH 的 echConfigList 是 base64url 编码，部分订阅源
   会把 `+` 错误地写成空格（或 URL 解码时把 `+` 吃成空格），导致 xray 解析失败。

## 范围边界

**做**：
- 新增配置项 `proxy.ech_enabled`（bool，默认 `false`）
- 生成配置时：`false` → 不输出 `echConfigList` 字段；`true` → 输出，且值中的
  **所有空格字符替换为 `+`**
- 在配置窗口（应用配置 Tab）暴露该开关

**不做**：
- 不改数据库表结构（`echconfiglist` 列已存在，无需 schema 变更）
- 不改订阅导入/去重逻辑
- 不改 `echForceQuery` 的现有行为
- 不动 `ShareLink` 导出（分享链接保持现状）

## 详细变更

### U1: `include/ConfigReader.h` — 新增配置项字段

在 `proxy` 结构体内（`use_singbox` 附近）新增：

```cpp
// ECH (Encrypted ClientHello) 开关。false = 生成配置时不输出 echConfigList；
// true = 输出并把值中所有空格替换为 '+'（修正订阅源把 base64url 的 '+'
// 误写/被 URL 解码吃成空格的情况）。由 AppController / main_cli 启动时
// 下发到 config::setEchEnabled()，与 time_wait_blocking 同一模式。
bool ech_enabled = false;
```

### U2: `include/config/StreamSettingsBuilder.h` + `src/config/StreamSettingsBuilder.cpp` — 生成逻辑

新增全局开关（仿 `utils::setTimeWaitBlocking`），避免改动 5 处 `build()` 调用点：

```cpp
namespace config {
/// 启动时下发一次；默认 false（不输出 echConfigList）
void setEchEnabled(bool enabled);
bool echEnabled();
}
```

`build()` 内的 ECH 分支改为：

```cpp
if (echEnabled() && !p.echconfiglist.empty()) {
    std::string list = p.echconfiglist;
    // 修正订阅源把 base64url 的 '+' 写成空格的情况
    std::replace(list.begin(), list.end(), ' ', '+');
    tlsSettings["echConfigList"] = boost::json::value(list);
}
```

### U3: `include/config/sections/ProxyConfigParser.h` — 解析

仿 `use_singbox` 的写法（正确类型解析 / 错误类型告警）：

```cpp
if (proxy.contains("ech_enabled") && proxy.at("ech_enabled").is_bool()) {
    config.proxy.ech_enabled = proxy.at("ech_enabled").as_bool();
} else if (proxy.contains("ech_enabled")) {
    Logger::write("WARNING: config.proxy.ech_enabled has wrong type (expected bool), using default", LogLevel::WARN);
}
```

### U4: `src/config/ConfigJsonSerializer.cpp` — 序列化

```cpp
proxyObj["ech_enabled"] = config.proxy.ech_enabled;
```

### U5: `src/ui/ConfigDialog.cpp` — 配置窗口暴露（三处）

- 构造：`propGrid_->Append(new wxBoolProperty(L"是否启用 ECH", "proxy_ech_enabled", cfg.proxy.ech_enabled));`
- `loadConfig()`：`propGrid_->SetPropertyValue("proxy_ech_enabled", cfg.proxy.ech_enabled);`
- `saveConfig()`：`editedConfig_.proxy.ech_enabled = propGrid_->GetPropertyValueAsBool("proxy_ech_enabled");`

### U6: `src/ui/AppController.cpp` + `src/main_cli.cpp` — 启动时下发

在加载 config 后、生成开始前调 `config::setEchEnabled(appConfig->proxy.ech_enabled);`
（`main_cli.cpp` 各命令分支已有 `utils::setTimeWaitBlocking(appConfig->...)` 的同型调用点，就近加入）。

## 验证步骤

1. 构建 `validproxy` / `validproxy-cli` / `UITests`：0 error
2. `test_config_generator`：全通过 + 新增用例：
   - `EchDisabled_OmitsEchConfigList`：`ech_enabled=false` 时产物无 `echConfigList`
   - `EchEnabled_ReplacesSpacesWithPlus`：`"ab cd"` → `"ab+cd"`
   - `EchEnabled_NoSpacesUnchanged`：无空格时原样输出
   - `EchDisabled_ConfigItemDefaultsFalse`：`proxy.ech_enabled` 默认 false
3. `ctest`：`RegionBatchResolverTest`(Not Run)、`XrayManagerStartupTest`、`UI_PORTCLOSE`
   为既有失败，不应新增
4. 配置往返：改 `config.json` 的 `proxy.ech_enabled` → 重启 → 读回一致

## 文件变更列表

| 文件 | 变更 |
|---|---|
| `include/ConfigReader.h` | +1 字段 |
| `include/config/StreamSettingsBuilder.h` | +2 声明 |
| `src/config/StreamSettingsBuilder.cpp` | 开关实现 + ECH 分支改写 |
| `include/config/sections/ProxyConfigParser.h` | 解析 |
| `src/config/ConfigJsonSerializer.cpp` | 序列化 |
| `src/ui/ConfigDialog.cpp` | UI 三处 |
| `src/ui/AppController.cpp` | 启动下发 |
| `src/main_cli.cpp` | 启动下发 |
| `tests/test_config_generator.cpp` | 新增用例 |

## 风险

| 风险 | 缓解 |
|---|---|
| 全局开关在测试间泄漏 | 单测内显式 set/reset；默认 false 是安全侧 |
| 已有配置无该键 | 解析用 default false，向后兼容 |
| 5 处 `build()` 调用点行为不一致 | 开关在 `build()` 内部统一判断，调用点无需改动 |

## 状态

- [ ] 用户确认方案（B：全局开关）
- [ ] U1 配置项字段
- [ ] U2 生成逻辑
- [ ] U3/U4 解析与序列化
- [ ] U5 UI 暴露
- [ ] U6 启动下发
- [ ] 测试
- [ ] 构建 + ctest 验证