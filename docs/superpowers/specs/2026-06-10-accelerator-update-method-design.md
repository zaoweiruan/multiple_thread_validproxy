# 加速器更新方式 — 设计文档

- **文档类型**: Spec / 技术方案
- **日期**: 2026-06-10
- **项目**: Validproxy
- **模块**: 订阅更新 / SubitemUpdaterV2 / Config

---

## 1. 背景与目标

### 1.1 需求概述

新增第三种订阅更新方式"通过加速器"，将原有的 `priority_mode` 单选配置替换为 `update_methods` 多选列表，支持三方法级联重试。

### 1.2 术语

| 术语 | 说明 |
|------|------|
| 加速器 (accelerator) | 一个中转 URL 前缀，拼接在订阅 URL 前形成完整抓取地址 |
| 更新方式 (update method) | 获取订阅内容的方法：accelerator / proxy / direct |

---

## 2. 配置结构

### 2.1 新增 & 变更字段

```json
{
  "subscription": {
    "accelerator_url": "https://cdn.加速器.com/",
    "update_methods": ["accelerator", "proxy", "direct"]
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `accelerator_url` | string | 新增。加速器服务的 URL |
| `update_methods` | array[string] | 新增。启用的更新方式列表，顺序即重试顺序。可选值: `accelerator`, `proxy`, `direct` |
| ~~`priority_mode`~~ | ~~string~~ | **移除**。原有 `direct_first` / `proxy_first` / `direct_only` |

### 2.2 默认值与验证规则

- `update_methods` 为空或不存在 → 默认 `["accelerator"]`（列表第一项）
- 合法元素: `accelerator`, `proxy`, `direct`
- ~~约束: 若 `accelerator` 在列表中，`proxy` 也必须在列表中~~（已移除，无此约束）
- 重复元素: 静默去重
- 无效元素: 静默忽略
- `update_methods` 中元素按 `accelerator → proxy → direct` 固定顺序排列（即使 JSON 中顺序不同，也按此逻辑顺序处理）

### 2.3 C++ 结构体变更

**`include/ConfigReader.h` — `config::AppConfig`**

```cpp
// 移除
std::string priority_mode;

// 新增
std::string accelerator_url;
std::vector<std::string> update_methods;
```

**`include/SubitemUpdaterV2.h`**

```cpp
// 新增枚举
enum class UpdateMethod {
    Accelerator,
    Proxy,
    Direct
};
```

---

## 3. 加速器 URL 拼接规则

加速器 URL 与订阅 URL 拼接时确保有且仅有一个 `/`：

```
标准形式:
  accelerator_url  = "https://加速器.example.com/path"
  sub_url          = "https://sub.example.com/v2?token=abc"
  结果: "https://加速器.example.com/path/https://sub.example.com/v2?token=abc"

尾部/头部斜杠处理:
  "https://加速器.example.com/" + "/https://sub.example.com" → 
    去尾/去头: "https://加速器.example.com" + "/" + "https://sub.example.com"
```

实现逻辑 (`utils::joinUrl`):

```cpp
std::string joinUrl(const std::string& base, const std::string& suffix) {
    if (base.empty()) return suffix;
    // base 去尾部 '/'
    // suffix 去头部 '/'
    // 返回 base + "/" + suffix
}
```

---

## 4. 更新流程

### 4.1 整体流程

```
SubitemUpdaterV2::run()
│
├─ [旧路径] update_methods 为空 → 默认 ["accelerator"]
│
├─ 解析 update_methods 为 vector<UpdateMethod>
│
├─ 加载所有启用的订阅，按 sort 升序排列
│
├─ 初始化失败列表 failedSubs = 所有启用的订阅
│
├─ Phase: Accelerator (若 Accelerator ∈ update_methods)
│   ├─ 拼接加速器URL
│   ├─ 对 failedSubs 中每个 sub:
│   │     fetchUrlViaAccelerator(sub) → parse → updateProfileItems
│   │     成功 → 从 failedSubs 移除
│   └─ failedSubs 为空 → 提前结束
│
├─ Phase: Proxy (若 Proxy ∈ update_methods)
│   ├─ 启动 Xray + ProxyFinder 获取代理端口
│   ├─ 对 failedSubs 中每个 sub:
│   │     fetchUrlViaProxy(sub, socksPort) → parse → updateProfileItems
│   │     成功 → 从 failedSubs 移除
│   └─ failedSubs 为空 → 提前结束
│
├─ Phase: Direct (若 Direct ∈ update_methods)
│   ├─ 对 failedSubs 中每个 sub:
│   │     fetchUrlDirect(sub) → parse → updateProfileItems
│   └─ failedSubs 为空 → 提前结束
│
├─ 释放代理端口
├─ 可选去重
└─ 输出汇总
```

### 4.2 加速器抓取方法

```cpp
std::string SubitemUpdaterV2::fetchUrlViaAccelerator(const std::string& url) {
    std::string fetchUrl = utils::joinUrl(config_.accelerator_url, url);
    // 使用 CurlEasyHandle 执行 GET 请求（同 fetchUrl 逻辑）
    CurlEasyHandle curl;
    curl.setUrl(fetchUrl)
        .setWriteCallback(...)
        .setFollowLocation()
        .setConnectTimeoutMs(config_.subscription_connect_timeout_ms)
        .setTimeoutMs(config_.subscription_timeout_ms)
        .setSslVerifyPeer(false)
        .setSslVerifyHost(false);
    curl.perform();
    return curl.getResponseBody();
}
```

### 4.3 runSingle 方法变更

`runSingle(subId)` 同样适配多方法逻辑：

```
runSingle(subId):
  update_methods 中的每个方法依次尝试
  成功即停止
  全部失败则返回失败
```

---

## 5. UI 改动 (ConfigDialog)

### 5.1 配置对话框

- **移除**: 订阅 → 更新方式 (priority_mode 下拉框)
- **新增**: 订阅 → 加速器地址 (文本输入)
- **新增**: 订阅 → 更新方式 (多选复选框)
  - 选项: `accelerator` / `proxy` / `direct`，固定排列顺序
  - ~~验证: 若勾选 accelerator，必须同时勾选 proxy~~（已移除，无此约束）
  - 默认: 全不勾选 → 视为 ["accelerator"]

### 5.2 配置保存

- ConfigDialog OK 时序列化 `update_methods` 为 JSON 数组
- 校验逻辑在 `validateConfig()` 中实现
- `priority_mode` 不再写入 config.json

---

## 6. 向后兼容

- 读取旧版 config.json（含 `priority_mode`，无 `update_methods`）
  - 若 `priority_mode` 存在且 `update_methods` 不存在 → 自动转换：
    - `"direct_first"` → `["direct", "proxy"]`
    - `"proxy_first"` → `["proxy"]`
    - `"direct_only"` → `["direct"]`
  - 转换后保存回 config.json（`priority_mode` 移除，`update_methods` 写入）
  - 注：这改变了原有 proxy_first 和 direct_first 的行为（之前有 2 阶段 fallback，现在只有单一方法或固定顺序）
  - 实际：`proxy_first` = 只用代理，`["proxy"]` 等价；`direct_first` = 先直连再代理，`["direct", "proxy"]` 等价；`direct_only` = 只用直连，`["direct"]` 等价

---

## 7. 边界情况

| 场景 | 行为 |
|------|------|
| `update_methods` 为空 | 默认 `["accelerator"]` |
| 只勾选 `direct` | 仅使用直连，等价旧 `direct_only` |
| 只勾选 `proxy` | 仅使用代理，等价旧 `proxy_first` |
| 勾选 `proxy` + `accelerator` | accelerator → proxy 顺序重试（注意：未勾选 direct） |
| 勾选 `accelerator` 但未勾选 `proxy` | 按 accelerator → direct 顺序（跳过 proxy） |
| `accelerator_url` 为空但勾选了 accelerator | 加速器退化为直连，直接使用订阅 URL 抓取（无需拼接） |
| 网络全部失败 | failedSubs 最终非空 → 汇总报告失败数量 |
| 单订阅更新 (runSingle) | 按 update_methods 顺序依次尝试，成功即停 |
