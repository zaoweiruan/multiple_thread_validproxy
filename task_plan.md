# Task Plan: Subscription URL Proxy Fetch & Fallback

## Goal
实现从 Subitem 表获取 enabled 订阅 URL，自动拉取代理列表更新 Profileitem，并在 URL 不可达时 fallback 到最佳代理。

## Phases

| Phase | Description | Status | Notes |
|-------|-------------|--------|-------|
| 1 | 创建规划文件 | complete | task_plan.md, findings.md, progress.md |
| 2 | 在 SubitemDAO 添加 getEnabledSubscriptions() 方法 | complete | 查询 enabled='true' 的 url |
| 3 | 实现 HTTP 订阅 URL 获取功能 | complete | 使用 curl 获取 base64 编码的订阅内容 |
| 4 | 实现订阅内容解析（base64 解码 → share link → Profileitem） | complete | 支持 vmess://, vless://, ss://, trojan:// |
| 5 | 实现 Profileitem 更新/插入逻辑 | complete | 根据 subid 关联，先删后增 |
| 6 | 实现 fallback 逻辑：URL 不可达时从 subid='5544178410297751350' 选最小 delay 代理 | complete | 查询 ProfileExItem 按 delay 排序 |
| 7 | 通过 fallback 代理获取订阅文件 | complete | 使用 socks 代理 curl 请求 |
| 8 | 完善日志记录 | complete | 所有操作写入日志文件 |
| 9 | 集成到 main.cpp 或独立入口 | complete | 作为新功能模块调用 |
| 10 | 编译测试 | complete | Release build 成功 |

## Architecture

```
SubitemUpdater (new class)
├── getEnabledSubscriptions() → vector<Subitem>
├── fetchSubscription(url) → string (raw content)
├── fetchViaFallback(url, fallbackProxy) → string
├── parseSubscription(content, subid) → vector<Profileitem>
├── updateProfileItems(subid, profiles) → bool
└── run() → execute full pipeline with logging
```

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
| - | - | - |
