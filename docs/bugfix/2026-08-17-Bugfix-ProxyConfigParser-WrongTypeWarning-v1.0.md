# Bugfix: ProxyConfigParser 合法 proxy 误报 "has wrong type" 警告

- 日期: 2026-08-17
- 类型: Bugfix
- 模块: ConfigReader / ProxyConfigParser
- 版本: v1.0
- 状态: ✅ completed

---

## 1. 现象

`bin/log/ui_20260817_094751.log` 启动时出现伪警告：

```
[2026-08-17 09:47:51] [WARN] WARNING: config.proxy has wrong type
```

但 `bin/config.json` 中 `proxy` 是**合法对象**（`socks_base_port: 10808`、
`xray_executable`、`use_singbox: false` 等字段齐全），不应触发 wrong type 警告。

## 2. 根因

`include/config/sections/ProxyConfigParser.h` 第 95 行控制流逻辑错误：
`Logger::write("WARNING: config.proxy has wrong type", ...)` 被误放在
`if (obj.contains("proxy") && obj.at("proxy").is_object())` 分支**内部**
（正常解析完所有字段后无条件执行），而不是放在 `else if` 分支。导致：
- 合法 proxy 对象 → 假警告（本 bug）；
- proxy 为错误类型（如字符串）→ 警告消息仍缺 `(expected object), using default`
  后缀，与其余 9 个解析器（SubscriptionConfigParser.h:83、
  XrayConfigParser.h:44、LogConfigParser.h:47 等）风格不一致。

## 3. 修复

`include/config/sections/ProxyConfigParser.h`：警告从 if 分支内部移到
`else if (obj.contains("proxy"))` 分支，并补全消息后缀：

```cpp
    // ... 正常解析 proxy 对象所有字段（保持原样）...
    } else if (obj.contains("proxy")) {
        Logger::write("WARNING: config.proxy has wrong type (expected object), using default", LogLevel::WARN);
    }
```

`AppConfig::proxy` 匿名 struct 所有字段均有结构体内默认值
（`socks_base_port = 10808`、`use_singbox = false`、`scoring_delay_weight = 0.2`、
`scoring_stability_weight = 0.3`、`scoring_history_weight = 0.5`），
proxy 非 object 时无需额外 else 分支设置默认值。

## 4. 验证（TDD）

- 新增 2 个回归测试（`tests/test_config_reader_load.cpp`，ConfigReaderLoadTest）：
  - `ProxySectionValidObject_NoWrongTypeWarning`：合法 proxy 对象加载后，
    经 `Logger::pushCallback` 捕获的日志中**不得出现**
    `config.proxy has wrong type`（修复前失败：复现假警告）；
  - `ProxySectionWrongType_UsesDefaultAndWarns`：`"proxy": "not-an-object"` 时
    保留默认值 `socks_base_port=10808`、`use_singbox=false`，且捕获到
    `config.proxy has wrong type (expected object), using default` 警告。
- 修复前：2 测试均 FAILED（RED 确认）；修复后：2 测试均 PASSED（GREEN）。
- 全量回归：`test_config_reader.exe` 36/36 通过；
  `ctest --test-dir build -R ConfigReaderTest -V` → 100% passed。

## 5. 范围与影响

- 仅 `ProxyConfigParser.h` 一行警告分支调整 + 测试文件追加 2 用例；
- 正常配置加载路径行为不变（合法 proxy 对象解析逻辑零改动）；
- 错误类型 proxy 的警告消息与全项目解析器风格统一。