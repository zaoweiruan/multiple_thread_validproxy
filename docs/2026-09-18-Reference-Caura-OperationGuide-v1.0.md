# Caura 跨会话记忆系统 — curl 操作指南 v1.0

> 文档类型：Reference  
> 适用项目：validproxy  
> 日期：2026-09-18  
> 作者：AI Agent

---

## 1. 概述

Caura 是项目级跨会话记忆系统，基于 MCP (Model Context Protocol) 协议，通过远程端点 `https://caura.ai/mcp` 提供 12 个工具接口。本指南提供完整的 PowerShell curl 命令，用于手动管理记忆数据。

## 2. 公共模板

```powershell
# 所有请求的公共部分
$URL = "https://caura.ai/mcp"
$HEADERS = @{
    "Accept"      = "application/json, text/event-stream"
    "X-API-Key"   = $env:memclaw_apikey
    "Content-Type"= "application/json"
}
# agent_id 统一使用项目名，区分不同项目的记忆空间
$AGENT_ID = "validproxy"
```

> 环境变量 `memclaw_apikey` 需提前设置。协议版本：JSON-RPC 2.0，POST 请求，`tools/call` method。

## 3. 工具接口总览

| 序号 | 工具名 | 功能 | 操作类型 |
|------|--------|------|----------|
| 1 | `caura_keystones` | 获取强制策略规则 | 读 |
| 2 | `caura_keystones_set` | 设置/删除规则 | 写 |
| 3 | `caura_list` | 列出记忆（元数据） | 读 |
| 4 | `caura_recall` | 语义搜索记忆 | 读 |
| 5 | `caura_write` | 写入记忆 | 写 |
| 6 | `caura_manage` | 管理单条记忆 | 读/写 |
| 7 | `caura_stats` | 统计记忆 | 读 |

## 4. 诊断与连通性

### 4.1 初始化会话（验证连通性）

```powershell
$body = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"curl-client","version":"1.0"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 4.2 获取工具列表

```powershell
$body = '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 4.3 统计记忆（快速概览）

```powershell
$body = '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"caura_stats","arguments":{"agent_id":"validproxy","scope":"agent"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

> 返回 `total: 0` 表示账户为空，从未写入过记忆。

## 5. 读取操作

### 5.1 获取强制策略（Keystones）

强制策略在每次会话启动时自动加载，优先级高于用户指令。

```powershell
$body = '{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"caura_keystones","arguments":{"agent_id":"validproxy"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 5.2 列出记忆（按元数据）

```powershell
# 按创建时间倒序，取最近 50 条
$body = '{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"caura_list","arguments":{"agent_id":"validproxy","limit":50,"sort":"created_at","order":"desc"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

支持参数：`limit`、`sort`、`order`、`status`、`tag`、`weight`、`page`、`per_page`

### 5.3 语义搜索记忆（recall）

```powershell
# 语义 + 关键词混合检索，返回摘要
$body = '{"jsonrpc":"2.0","id":16,"method":"tools/call","params":{"name":"caura_recall","arguments":{"agent_id":"validproxy","query":"Xray 架构决策 构建命令","top_k":10,"include_brief":true}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

支持参数：`query`、`top_k`、`include_brief`、`min_similarity`、`status`、`tag`

## 6. 写入操作

### 6.1 写入单条记忆

```powershell
$body = '{"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":"caura_write","arguments":{"agent_id":"validproxy","content":"项目采用单例模式管理 Xray 实例，通过 XrayManager 统一调度"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 6.2 批量写入记忆（≤100 条）

```powershell
$body = '{"jsonrpc":"2.0","id":21,"method":"tools/call","params":{"name":"caura_write","arguments":{"agent_id":"validproxy","items":[{"content":"禁止使用 auto 类型推导，必须显式声明变量类型"},{"content":"构建命令: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8"},{"content":"XrayApi 已修复 CreateProcessA 引起的 CMD 窗口闪烁"}]}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 6.3 设置 Keystone 规则

```powershell
$body = '{"jsonrpc":"2.0","id":30,"method":"tools/call","params":{"name":"caura_keystones_set","arguments":{"agent_id":"validproxy","doc_id":"no-auto-type","title":"禁止 auto 类型推导","content":"全栈代码中禁止使用 auto 进行类型推导，必须显式声明变量类型","scope":"agent","weight":"high"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 6.4 删除 Keystone 规则

```powershell
$body = '{"jsonrpc":"2.0","id":31,"method":"tools/call","params":{"name":"caura_keystones_set","arguments":{"agent_id":"validproxy","doc_id":"no-auto-type","action":"delete"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

## 7. 管理单条记忆

### 7.1 读取记忆详情

```powershell
$body = '{"jsonrpc":"2.0","id":40,"method":"tools/call","params":{"name":"caura_manage","arguments":{"agent_id":"validproxy","id":"<memory-uuid>","action":"read"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 7.2 更新记忆内容

```powershell
$body = '{"jsonrpc":"2.0","id":42,"method":"tools/call","params":{"name":"caura_manage","arguments":{"agent_id":"validproxy","id":"<memory-uuid>","action":"update","content":"更新后的内容"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 7.3 标记为过时

```powershell
$body = '{"jsonrpc":"2.0","id":43,"method":"tools/call","params":{"name":"caura_manage","arguments":{"agent_id":"validproxy","id":"<memory-uuid>","action":"transition","status":"outdated"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 7.4 删除记忆

```powershell
$body = '{"jsonrpc":"2.0","id":44,"method":"tools/call","params":{"name":"caura_manage","arguments":{"agent_id":"validproxy","id":"<memory-uuid>","action":"delete"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

## 8. 常用工作流

### 8.1 标准「检索 → 执行 → 沉淀」闭环

```
1. 任务开始前：caura_recall 检索相关记忆
2. 执行任务
3. 任务完成后：caura_write 写入结论
```

```powershell
# Step 1: 检索
$body = '{"jsonrpc":"2.0","id":16,"method":"tools/call","params":{"name":"caura_recall","arguments":{"agent_id":"validproxy","query":"<任务关键词>","top_k":5,"include_brief":true}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content

# Step 2: 执行任务...

# Step 3: 沉淀
$body = '{"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":"caura_write","arguments":{"agent_id":"validproxy","content":"<结论内容>"}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 15 | Select-Object -ExpandProperty Content
```

### 8.2 项目初始化：批量写入基础记忆

```powershell
$body = '{"jsonrpc":"2.0","id":22,"method":"tools/call","params":{"name":"caura_write","arguments":{"agent_id":"validproxy","items":[{"content":"项目: validproxy — C++17 代理验证工具，wxWidgets GUI + CLI"},{"content":"构建: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8"},{"content":"测试: ctest -V（全量），ctest -R <TestName> -V（单项）"},{"content":"代码规范: 禁止 auto 类型推导，C++17 标准"},{"content":"目标平台: Windows MinGW/GCC，默认终端 PowerShell"},{"content":"核心模块: XrayManager, XrayInstance, XrayApi, ProxyFinder, ProxyBatchTester, ProxyTester, ConfigGenerator, ConfigReader, SubitemUpdaterV2, DatabaseHelper, Logger, ShareLink, PortManager, UrlFetcher, CurlEasyHandle"},{"content":"UI框架: wxWidgets 3.2+ (wxMSW)，入口 src/main_gui.cpp"},{"content":"数据存储: SQLite，生产数据库 bin/worker/guindb.db"},{"content":"配置文件: bin/config.json（生产）, bin/test_config.json（测试）"},{"content":"日志级别: TRACE < DEBUG < INFO < REPORT < WARN < ERR"}]}}}'
Invoke-WebRequest -Uri $URL -Method POST -Body $body -Headers $HEADERS -TimeoutSec 30 | Select-Object -ExpandProperty Content
```

## 9. 安全红线

严禁通过 Caura 保存以下内容：

- API Token、密码、密钥、证书、凭据
- 用户的私密原文（个人身份、敏感数据）
- 任何形式的会话令牌或认证字符串

如需提及，使用占位符 `<REDACTED>` 替代。

## 10. 故障排查

| 现象 | 原因 | 解决方法 |
|------|------|----------|
| `total: 0` | 账户为空，从未写入 | 执行批量写入命令 |
| MCP 工具不可见 | 会话启动时连接失败 | 重启 opencode 会话 |
| API 认证失败 | 环境变量未设置 | `echo $env:memclaw_apikey` 检查 |
| 连接超时 | 网络问题 | 检查 `https://caura.ai/mcp` 可达性 |
| JSON 解析错误 | body 格式不正确 | 检查引号和转义字符 |

## 11. 服务端信息

| 项目 | 值 |
|------|-----|
| 端点 | `https://caura.ai/mcp` |
| 版本 | Caura v3.15.0 |
| 工具数 | 12 |
| 协议 | MCP JSON-RPC 2.0 |
| 认证 | `X-API-Key` Header |

---

## 修订历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-09-18 | 初始版本，包含完整 curl 命令集 |
