# Bugfix: 即时订阅切换后其它订阅显示为空（缓存误存过滤子集）

- **日期**: 2026-08-24
- **模块**: AppController / ProxyListPanel
- **关联**: `docs/specs/2026-08-24-Spec-ProxyListPanel-InstantSubSwitch-v1.0.md`（本 bug 为该 spec 实现引入的回归）

## 1. 现象

启动后显示首个订阅内容；切换到任何其它订阅，代理列表为空。

## 2. 根因（RCA）

即时切换实现的前提假设是"面板缓存 `allProxies_` 持有全表"。但该假设错误：

`AppController::loadProxiesAsync` 后台线程（AppController.cpp 原 311-319 行）：

1. `dao.getAll()` 读**全表**进局部变量 `allProxies`
2. `copy_if` 按 subId 过滤进 `proxies`
3. `ProxyListLoadedEvent` **只携带过滤后的 `proxies` 子集**，全表随即丢弃

而面板 4 参 `loadProxies` 中 `allProxies_ = proxies;` 把这个**子集**当成了全集缓存：

- 启动：缓存 = 首个订阅的行 → 显示正常；
- 切换订阅 B：`applySubscriptionFilter(B)` 在只含订阅 A 行的缓存里找
  `subid == B` → 0 命中 → 空列表——与症状完全吻合。

根因归属：上一轮 RCA 时误判"事件携带全表"（实际全表在后台线程即被丢弃），
属即时切换实现自身引入的回归。

## 3. 修复

后台线程手中的全表本来就是读完即弃——让事件改携全表，零额外 IO、
零额外内存峰值：

| 文件 | 变更 |
| :--- | :--- |
| `AppController.cpp` | 删除 copy_if 分支：`ProxyListLoadedEvent` 直接携带 `dao.getAll()` 全表 |
| `ProxyListPanel.cpp` | 4 参 `loadProxies` 缓存存全表后，尾部视图逻辑委托 `applySubscriptionFilter(subId)` 复用（原重复的 sortState 重置/setDataWithoutRebuild/双 Reset/detectIdOffset/selectFirst 全部由后者承担，净减代码） |

数据流（修复后）：

```text
后台线程: getAll() 全表 ──事件──▶ allProxies_ = 全表 (cacheReady_=true)
                                    │
切换订阅: applySubscriptionFilter(B) ─ 内存过滤 ─▶ proxies_ = B 的行 ─▶ Reset 视图
```

## 4. 验证

- 构建 0 error（`cmake --build build --parallel 8`）
- `ctest` 32/32 全部通过
- 手动验证清单：
  1. 启动 GUI → 首个订阅内容正常显示；
  2. 切换到任意其它订阅 → 内容立即正确显示（含空订阅显示空列表）;
  3. 来回快速切换 → 均瞬时且内容正确；
  4. 右键「刷新」→ 当前订阅从 DB 重载正常。

## 5. 范围控制

失败分支（sqlite3_open 失败）仍发空 profiles+空 maps——此时 cacheReady_ 置
true 且缓存为空，后续切换得空表。此为既有语义（DB 打不开本就无数据可示），
不在本次修复范围。
