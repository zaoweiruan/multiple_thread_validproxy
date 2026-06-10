# URL验证缺口分析报告

> 日期: 2026-06-10 | 项目: Validproxy

## 概述

分析整个代码库中订阅 URL 和加速器 URL 的验证机制，识别缺失验证的路径，并提出改进方案。

## 现有验证函数

`SubitemUpdaterV2::isValidUrlFormat()` (src/SubitemUpdaterV2.cpp:2190-2208):
- 检查 http:// 或 https:// 开头
- 域名部分至少包含一个点号（.）
- 仅在导入时使用（importSingleUrl、importSubitemsFromFile）

`SubitemUpdaterV2::hasValidPath()` (src/SubitemUpdaterV2.cpp:2211-2220):
- 检查 URL 包含路径部分
- 仅作警告，不拦截

## 验证覆盖矩阵

| 操作 | 位置 | 已验证? |
|------|------|---------|
| CLI/单URL导入 | `importSingleUrl()` | 是 — isValidUrlFormat |
| 文件导入 | `importSubitemsFromFile()` | 是 — isValidUrlFormat |
| GUI添加(onImportSubscription) | `SubscriptionPanel.cpp:250` | 间接 — 最终进入importSingleUrl |
| GUI编辑(showEditDialog) | `SubscriptionPanel.cpp:353` | **否** — 直接写库，无验证 |
| 订阅更新(fetch) | `SubitemUpdaterV2.cpp:518-561` | **否** — cURL处理错误 |
| 加速器URL | `ConfigDialog` load/save | **否** — 无格式验证 |

## 缺失验证的路径

### 1. GUI编辑订阅URL (showEditDialog)

SubscriptionPanel.cpp:350-353 — 用户点击OK后直接写库：
```cpp
updated.url = urlCtrl->GetValue().ToStdString();
// ...
controller_->updateSubitem(updated);
```
传入 `isValidUrlFormat()` 可立即阻止无效 URL。

### 2. 加速器URL (ConfigDialog)

ConfigDialog 中允许用户输入任意字符串作为加速器URL，保存时无验证。应至少验证是合法的 http/https URL。

## 影响

- 编辑时保存无效URL → 后续更新静默失败（cURL错误）
- 加速器URL无效 → 加速阶段静默失败 → 用户体验差
