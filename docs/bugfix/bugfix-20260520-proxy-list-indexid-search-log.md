# Bug Fix: Proxy List IndexId Column, Search Box, and Log Filtering

> **Date:** 2026-05-20  
> **Component:** ProxyListPanel, MainFrame, LogPanel, SubscriptionPanel  
> **Severity:** Medium - UI functionality issues

## Bug 标题

1. Proxy list displays row numbers but needs actual indexId for proxy lookups
2. Search box in toolbar doesn't filter the proxy list
3. Log output only shows TRACE level messages on startup
4. Subscription update time shows Unix timestamp instead of readable datetime

## Bug 描述与复现步骤

### Bug 1: IndexId Column Confusion
1. Launch application and load proxies
2. Select a proxy and observe the "# " column shows "1, 2, 3..." 
3. The hidden column was being used incorrectly for lookups

### Bug 2: Search Box Non-functional
1. Type in the search box and press Enter
2. Observe: No filtering occurs, all proxies remain visible

### Bug 3: Log Filtering Incorrect on Startup
1. Launch application
2. Observe log panel - only TRACE messages appear initially
3. Expected: INFO level (default selection) should be active

### Bug 4: Subscription Update Time Format
1. View subscription list
2. Observe "Update" column shows Unix timestamp (e.g., "1700000000")
3. Expected: Readable datetime (e.g., "2024-05-20 12:00")

## 根本原因分析 (Root Cause)

### Bug 1: IndexId Column
**位置:** `ProxyListPanel.cpp` 
- Row number was being displayed in COL_INDEXID, but handlers expected actual indexId
- Selection lookup by address was unreliable (duplicates possible)

### Bug 2: Search Box
**位置:** `MainFrame.cpp`
- `onSearchBoxEnter()` only updated status bar, didn't call filter method

### Bug 3: Log Filtering
**位置:** `LogPanel.cpp` constructor
- `minLevel_` initialized in-class to `LogLevel::TRACE` (line 34 of header)
- But filter dropdown defaults to INFO (selection index 2)
- Mismatch between UI selection and actual filter level

### Bug 4: DateTime Format
**位置:** `SubscriptionPanel.cpp`
- Raw `sub.updatetime` (Unix timestamp string) displayed directly

## 修复方案与设计决策

### 方案 1: Split IndexId and Row Number Columns
```cpp
enum {
    COL_INDEXID  = 0,  // Hidden column - actual indexId for lookups
    COL_ROWNUM   = 1,  // Display row number (1,2,3...)
    COL_ADDRESS  = 2,
    // ...
};
```

### 方案 2: Implement filterBySearch() in ProxyListPanel
- Store full proxy list in `allProxies_`
- Filter to `proxies_` when search query provided
- Restore full list when query empty

### 方案 3: Initialize minLevel_ in Constructor
```cpp
levelFilter_->SetSelection(2); // default: INFO
minLevel_ = LogLevel::INFO; // Initialize to match the default selection
```

### 方案 4: Format DateTime in SubscriptionPanel
```cpp
static std::string formatUpdateTime(const std::string& updatetime) {
    long timestamp = std::stol(updatetime);
    time_t t = static_cast<time_t>(timestamp);
    struct tm tm_time;
    localtime_s(&tm_time, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_time);
    return std::string(buf);
}
```

## 实施计划（分步）

### 步骤 1: ProxyListPanel - Add column indices ✅
- Updated enum to separate COL_INDEXID and COL_ROWNUM
- Modified loadProxies() to store actual indexId in hidden column

### 步骤 2: ProxyListPanel - Add filterBySearch() ✅
- Added `allProxies_` member for unfiltered storage
- Implemented filterBySearch() method

### 步骤 3: MainFrame - Wire up search box ✅
- Updated onSearchBoxEnter() to call proxyPanel_->filterBySearch()

### 步骤 4: LogPanel - Fix initialization ✅
- Added explicit minLevel_ = LogLevel::INFO in constructor

### 步骤 5: SubscriptionPanel - Format datetime ✅
- Added formatUpdateTime() static method
- Updated loadSubscriptions() to use formatted datetime

## 实际修改内容

### 修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/ui/ProxyListPanel.h` | Added `allProxies_` and `currentSubId_` members, `filterBySearch()` method |
| `src/ui/ProxyListPanel.cpp` | Split indexId/rownum columns, implemented filterBySearch() |
| `src/ui/MainFrame.cpp` | Wired search box to filterBySearch() |
| `src/ui/LogPanel.cpp` | Initialize minLevel_ to match default filter selection |
| `src/ui/SubscriptionPanel.h` | Added `formatUpdateTime()` declaration |
| `src/ui/SubscriptionPanel.cpp` | Implemented datetime formatting |

## 测试验证结果

- [x] Build successful (no compilation errors)
- [x] 3/3 unit tests pass (test_model)
- [x] 11/11 unit tests pass (test_dedup)
- [ ] Manual runtime verification pending

## 后续建议与预防措施

### 立即行动
1. Test column sorting with new indexId storage
2. Test search box filtering functionality
3. Verify log filtering shows INFO level by default

### 长期预防
1. **状态同步**: Always sync internal state with UI default selections in constructors
2. **列设计**: Separate display values from lookup keys in data views
3. **时间格式**: Always format timestamps for user display

---