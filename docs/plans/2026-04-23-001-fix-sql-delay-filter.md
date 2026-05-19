---
title: "fix(main.cpp): 修正 SQL Delay > 0 过滤条件 + ShareLink 导出修复"
type: fix
status: completed
date: 2026-04-23
origin: ".kilo/plans/1776415141516-jolly-mountain.md"
---

# 修复 SQL 条件与 ShareLink 导出逻辑

## 代码位置与修改

**文件**: `src/main.cpp`  
**位置**: `commandMode == "tourl"` 分支，lines ~487-493

**目标**: 确保 SQL 过滤条件为 `Delay > 0`

```cpp
// 正确的 SQL（应与 v2rayN 导出逻辑一致）
std::string sql = R"(
    SELECT p.*, COALESCE(pe.Delay, 0) as ExDelay
    FROM ProfileItem p
    LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
    WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
    ORDER BY CAST(pe.Delay AS INTEGER) ASC
)";
```

**常见错误情况**:
- `> 0` 被改为 `>= 0` → 会包含 Delay=0 的未测试代理（数量剧增）
- 去掉 `AND ... > 0` 条件 → 导出所有代理（数量巨大）
- 使用 `p.IsSub = 'true'` 替代 → 条件不同，结果不同
- 遗漏 `p.SubId != 'custom'` → 可能包含自定义配置

**修复**: 将条件明确写为 `> 0`，并确保该分支是 `-TU` 实际使用的代码路径。

---

### 已完成的修复

1. **main.cpp** (lines 491, 607):
   - 修正 SQL 条件为 `Delay > 0`（移除 `IsSub='true'` 限制）
   - 删除重复死代码（lines 591-661，72 行）

2. **ShareLink.cpp**:
   - VLESS/Trojan: `insecure/allowInsecure` 动态计算（基于 `AllowInsecure` 字段）
   - VLESS/Trojan: 路径改为完整 `urlEncode`
   - Shadowsocks: 插件路径 `=` 转义 + 完整 URL 编码
   - Shadowsocks: 移除 base64 尾部 `=` 填充

3. **构建配置**:
   - `CMakeLists.txt`: 添加 `src/ShareLink.cpp` 到可执行文件源列表

---

### 当前状态（2026-04-23 更新）

**文件对比**：
- 旧参考 `v2ray_sharelink.txt`: 148 行
- 新参考 `v2rayn_sharelink.txt`: 200 行 ← **v2rayN 已更新，此为新的正确基准**
- 我们最新导出 `proxies_20260423_154557.txt`: 200 行（数量匹配 ✅）

**待验证**: 内容是否 100% 一致？需逐行比对。

**配置状态**：
- `bin/config.json` 数据库路径已手动更新为 `E:/v2rayN-windows-64/guiConfigs/guiNDB.db`
- 订阅 ID 为 `5544178410297751350`（v2rayN 当前订阅，200 个代理）
- SQL 过滤 `Delay > 0` 返回 200 条记录（与导出行数一致）

---

### 待办任务

| 任务 | 状态 | 说明 |
|------|------|------|
| 1. 比对 `proxies_20260423_154557.txt` 与 `v2rayn_sharelink.txt` 内容 | ⏳ 待执行 | 逐行比较，验证导出格式 |
| 2. 如发现差异，定位具体代理及字段 | ⏳ 待执行 | 重点关注 AllowInsecure、路径编码、base64 格式 |
| 3. 如需，调整 ShareLink.cpp 并重新编译 | ⏳ 待执行 | 修复残余差异 |
| 4. 提交 `CMakeLists.txt` 修复 | ⏳ 待提交 | `git add CMakeLists.txt` |

---

### 为什么之前是 148，现在是 200？

- v2rayN 订阅已更新（新增代理或重新测试）
- 数据库 `Delay > 0` 的代理从 148 变为 200
- **我们的 SQL 条件正确，应导出全部 200 个**
