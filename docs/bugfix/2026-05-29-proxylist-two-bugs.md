---
title: "fix: ProxyListPanel 两个数据加载 Bug 修复"
type: fix
status: completed
date: 2026-05-29
---

# ProxyListPanel 数据加载 Bug 修复

## Bug 1：相同订阅重新加载时显示空白

### Bug 描述

订阅面板显示 "1" 个代理（`countBySubId()` 正确计数），但点击该订阅时 ProxyList 窗口显示为空。

### Root Cause 分析

`ProxyListPanel::loadProxies()`（第 70-102 行）使用 `subIdChanged` 标志决定是否更新 `proxies_`：

```cpp
// 旧代码 (有 bug)
bool subIdChanged = (subId != currentSubId_);
currentSubId_ = subId;
allProxies_ = controller_->loadProxies(subId);
if (subIdChanged) {
    proxies_ = allProxies_;  // 仅 subId 变更时才更新
    sortState_.column = -1;
    sortState_.direction = SortDirection::None;
}
// subId 没变 → proxies_ 保留旧数据（可能为空）
```

触发序列：
1. 应用启动 → 加载第一个订阅（DB 中尚无代理）→ `proxies_` 为空，显示空白 ✓
2. 订阅更新 → 代理数据写入 DB
3. 订阅面板计数刷新显示 "1" ✓
4. 用户点击同一订阅 → `loadProxies(sameSubId)` 被调用
5. `subIdChanged = false` → `proxies_` **未被刷新** → 依然是空向量
6. `model_->Reset(0)` → 显示 0 行 → 代理列表空白！

影响代码路径：
- `MainFrame::onDedupCompleted()`（第 487 行）：去重后重新加载同一订阅
- `MainFrame::onMenuConfig()` DB 切换后（第 550 行）：重新加载
- `MainFrame::onSubscriptionSelected()`（第 177 行）：用户点击同一订阅

### 修复

移除 `subIdChanged` 守卫，始终刷新 `proxies_` 并重置排序状态：

```cpp
// 新代码
void ProxyListPanel::loadProxies(const std::string& subId) {
    currentSubId_ = subId;
    allProxies_ = controller_->loadProxies(subId);

    // 无条件刷新：重置排序状态并拷贝新数据
    sortState_.column = -1;
    sortState_.direction = SortDirection::None;
    proxies_ = allProxies_;

    exItems_ = controller_->loadProxyResults();
    model_->setData(&proxies_, &exItems_);
    model_->Reset(static_cast<unsigned int>(proxies_.size()));
    // ...
}
```

**文件**: `src/ui/ProxyListPanel.cpp:70-81`

---

## Bug 2：排序激活时搜索导致最后记录空白

### Bug 描述

ProxyList 显示若干条记录，但最后一条（或多条）记录显示为空白。在列排序激活状态下搜索时触发。

### Root Cause 分析

`ProxyListPanel::filterBySearch()` 在排序激活时调用 `model_->Resort()` 而未先调用 `model_->Reset()`：

```cpp
// 旧代码 (有 bug)
if (sortState_.direction != SortDirection::None) {
    dvCol->SetSortOrder(...);
    model_->Resort();  // ← 缺少 Reset()！
} else {
    model_->Reset(proxies_.size());
}
```

`wxDataViewIndexListModel` 内部维护 `m_list` 作为视图行到数据索引的映射。`Reset(N)` 设置 `m_list` 大小为 N，每个条目 ID = 数据索引。`Resort()` 仅重排现有 `m_list` 条目，不改变大小。

当搜索将 `proxies_` 从 N 项缩减为 M 项（M < N）时：
1. `m_list` 仍然有 N 个条目（ID = 0..N-1）
2. `GetCount()` 返回 M（`proxies_->size()`）
3. 视图查询行 0..M-1
4. `getDataIndex(row)` = `m_list[row].GetID()` = row（如果 `m_list` 尚未被 `Resort()` 重排）
5. 但如果 `Resort()` 已经重排过 `m_list`（之前一次排序），则 `m_list[0]` 的 ID 可能 >= M
6. `GetValueByRow` 守卫检查 `dataIdx >= proxies_->size()` → 返回空字符串
7. 某些行显示为空白

更精确地说，在以下序列中：
1. 加载 5 条代理 → `proxies_` 有 5 项
2. 用户按 Latency 列排序 → `Resort()` 重排 `m_list`（例如 `m_list[0].GetID() = 2` 等）
3. 用户搜索过滤 → `proxies_` 缩小到 2 项
4. 无 `Reset()` → `m_list` 仍为 5 个条目（ID 基于旧排序）
5. `GetCount() = 2`，行 0-1
6. `getDataIndex(0) = m_list[0].GetID()` 可能为 3，`proxies_[3]` 越界 → 空白

### 修复

在 `filterBySearch()` 的排序路径中添加 `model_->Reset()`：

```cpp
// 新代码
if (sortState_.direction != SortDirection::None) {
    model_->Reset(static_cast<unsigned int>(proxies_.size()));  // ← 新增
    wxDataViewColumn* dvCol = listCtrl_->GetColumn(sortState_.column);
    if (dvCol) {
        dvCol->SetSortOrder(sortState_.direction == SortDirection::Asc);
    }
    model_->Resort();
} else {
    model_->Reset(static_cast<unsigned int>(proxies_.size()));
}
```

**文件**: `src/ui/ProxyListPanel.cpp:280-293`

---

## 补充优化

### 优化 1：`filterBySearch` 不再调用 `loadProxies`

旧版 `filterBySearch()` 末尾有 `loadProxies(currentSubId_)`，这无意中重新加载了服务器数据，若不靠 `subIdChanged` 守卫机制，会**覆盖**过滤结果。新版直接更新模型，不调用 `loadProxies`：

```cpp
// 旧代码
void ProxyListPanel::filterBySearch(const wxString& query) {
    // ... 过滤 proxies_ ...
    loadProxies(currentSubId_);  // 覆盖过滤结果！（靠子 ID 守卫幸存）
}

// 新代码
void ProxyListPanel::filterBySearch(const wxString& query) {
    // ... 过滤 proxies_ ...
    model_->setData(&proxies_, &exItems_);
    // 直接重置/排序模型，不调用 loadProxies
}
```

## 验证

- [x] `loadProxies` 无条件刷新 `proxies_`：支持更新/去重/切换 DB 后重新加载
- [x] `filterBySearch` + 排序：Reset + Resort 顺序正确
- [x] 构建成功（Ninja, Debug）
- [x] 3/3 测试通过（CurlEasyHandleTest、DedupTest、ProfileitemTest）

## 相关代码路径总览

| 调用者 | 文件:行号 | 影响 |
|--------|-----------|------|
| `onSubscriptionSelected` | MainFrame.cpp:177 | 用户点击订阅 → 加载新数据 |
| `initPanels` | MainFrame.cpp:387 | 启动时加载第一个订阅 |
| `onDedupCompleted` | MainFrame.cpp:487 | 去重后重新加载 → Bug 1 触发 |
| DB 切换 | MainFrame.cpp:550-552 | 切换数据库后重新加载 → Bug 1 触发 |
| `filterBySearch` | ProxyListPanel.cpp:267-293 | 搜索过滤 → Bug 2 触发 |
| `onColumnHeaderClick` | ProxyListPanel.cpp:131-175 | 排序（不受 Bug 2 影响，因为 `proxies_` 大小不变） |
