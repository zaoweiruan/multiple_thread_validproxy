# Current Context

> **角色**: 提供跨会话的上下文锚点。每轮会话开始时，Agent 应检查并更新 §三。
> **维护规则**: §三 在每轮会话结束时更新：移除已完成的旧条目，添加本会话的 Bug 修复引用。
> **边界**: 只记录"在哪里"和"最近做了什么"的上下文锚点，不记录深度知识（归档至 `docs/project-knowledge.md`）。

---

## 一、基于 xray、v2rayn 开源项目

- **v2rayn 源码目录**: `E:\eclipse_workspace\v2rayn`
  - 关键文件:
    - `v2rayN\ServiceLib\Handler\ConfigHandler.cs` — DedupServerList() 方法
    - `v2rayN\ServiceLib\Models\ProfileItem.cs` — 数据模型
    - `v2rayN\ServiceLib\Manager\AppManager.cs` — ProfileItems() 获取代理列表
- **xray 源码目录**: `E:\eclipse_workspace\Xray-core`

## 二、项目配置文件

- `E:\eclipse_workspace\multiple_thread_validproxy\bin\config.json`

## 三、本会话引用

> 每轮会话开始时，将前一轮的已关闭条目移至 `docs/project-knowledge.md §7` 或 `docs/bugfix/`。
> 格式: 条目简述 → `docs/bugfix/YYYY-MM-DD-xxx.md`

| 日期 | 条目 | 文档 |
|------|------|------|
| 2026-06-04 | SubitemUpdaterV2 硬编码 `"bin/config"` 路径修复 | `docs/bugfix/2026-06-04-subitemupdater-hardcoded-binconfig-path.md` |
