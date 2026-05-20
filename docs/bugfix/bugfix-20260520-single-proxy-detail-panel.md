# Bug Fix: Single Proxy Test Failure & Proxy Detail Panel Sync

> **Date:** 2026-05-20  
> **Component:** ProxyBatchTester, ProxyListPanel, ProxyDetailPanel  
> **Severity:** High - Functionality broken

## Bug 标题

1. Testing single proxy fails with "Proxy not found: 4"  
2. Proxy detail panel shows no content and doesn't synchronize with table selection

## Bug 描述与复现步骤

### 错误信息 1 - Single Proxy Test Failure
```
[2026-05-20 12:03:24] [REPORT] Single proxy test failed
[2026-05-20 12:03:24] [WARN] Proxy not found: 4
```

### 复现步骤 1 - Single Proxy Test
1. Load subscriptions and display proxy list
2. Right-click on a proxy and select "Test" from context menu
3. Observe warning "Proxy not found: X" where X is the row number, not the actual indexId

### 复现步骤 2 - Detail Panel Sync
1. Select a proxy in the proxy list table
2. Observe the detail panel on the right shows "Host: -", "Port: -", etc.
3. No content is displayed in the detail panel

### 影响范围
- `ProxyBatchTester.cpp`: Line 290 - proxy lookup failure
- `ProxyListPanel.cpp`: Line 92 - row number displayed instead of indexId
- `ProxyDetailPanel.cpp`: Limited field display

## 根本原因分析 (Root Cause)

### Bug 1: Single Proxy Test Failure
**位置:** `ProxyListPanel.cpp` line 92

```cpp
// Current (WRONG): Displays row number 1, 2, 3... instead of actual indexId
row.push_back(wxVariant(wxString::Format("%d", static_cast<int>(store_->GetCount() + 1))));
```

**问题:** The "#" column shows row count (1, 2, 3...) but the actual `indexid` from database (like "sub_001_004") is needed for proxy lookup. When user tests proxy, `onTestProxy` retrieves this row number as indexId, which doesn't match any database record.

### Bug 2: Detail Panel Sync Issue
**位置:** `ProxyDetailPanel.cpp` - Missing fields and no visual styling

**问题:**
1. The `COL_INDEXID` stores row number, not actual `indexid` - so `onSelectionChanged` can't find the correct proxy
2. Detail panel only shows 6 basic fields, missing: type, security, alterId, uuid, flow, etc.
3. No visual styling (panel appearance)

## 修复方案与设计决策

### 方案 1: Fix indexId storage in ProxyListPanel
```cpp
// Store actual indexid in COL_INDEXID column
row.push_back(wxVariant(p.indexid));  // Actual indexId from database
```

### 方案 2: Enhance ProxyDetailPanel
- Display all fields from ProfileItem struct
- Add panel styling with group boxes (Basic Info, Advanced)

### 设计决策
1. Fix indexId storage immediately - critical for all proxy operations
2. Enhance detail panel to show all database fields in organized layout
3. Add `getProxyByIndexId()` method to AppController for fetching proxy details

## 实施计划（分步）

### 步骤 1: ProxyListPanel.cpp - Fix indexId column ✅
- Changed column header from "#" to "IndexId"
- Store `p.indexid` instead of row number

### 步骤 2: AppController.h/.cpp - Add getProxyByIndexId ✅
- Added `std::optional<db::models::Profileitem> getProxyByIndexId(const std::string& indexId)`

### 步骤 3: ProxyDetailPanel.h/.cpp - Enhance panel ✅
- Added 9 advanced fields: type, security, network, flow, sni, uuid, alterId, streamSecurity, allowInsecure
- Added Basic Info and Advanced group boxes

### 步骤 4: MainFrame.cpp - Pass proxy data to detail panel ✅
- Updated to call `controller_->getProxyByIndexId()` for advanced field display

### 步骤 5: Rebuild and test ✅
```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
# Build: Success
# Tests: No tests configured (expected for this project)
```

## 实际修改内容

### 修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/ui/ProxyListPanel.cpp` | Store actual `p.indexid` instead of row number; Column header changed to "IndexId" |
| `src/ui/ProxyDetailPanel.h` | Added 9 advanced field labels |
| `src/ui/ProxyDetailPanel.cpp` | Complete rewrite with group boxes and all fields |
| `src/ui/AppController.h` | Added `getProxyByIndexId()` method |
| `src/ui/AppController.cpp` | Implemented `getProxyByIndexId()` |
| `src/ui/MainFrame.cpp` | Updated to fetch and pass full proxy data |

### 修改前后对比

**ProxyListPanel.cpp:92**
```cpp
// Before (WRONG)
row.push_back(wxVariant(wxString::Format("%d", static_cast<int>(store_->GetCount() + 1))));

// After (FIXED)
row.push_back(wxVariant(p.indexid));  // Store actual database indexId
```

**ProxyDetailPanel.cpp - New UpdateDetail signature**
```cpp
void UpdateDetail(const std::string& indexId,
                  const std::string& host, const std::string& port,
                  const std::string& delay, const std::string& message,
                  int failures, const std::string& remarks,
                  const db::models::Profileitem* proxy = nullptr);
```

## 测试验证结果

- [x] Build successful with no errors
- [x] ProxyListPanel stores actual indexId for correct proxy lookup
- [x] ProxyDetailPanel displays all fields in organized panel layout
- [ ] Runtime verification pending (requires running application)

## 后续建议与预防措施

### 立即行动
1. Verify proxy test operation works with correct indexId
2. Verify detail panel displays all fields on proxy selection

### 长期预防
1. **编码规范**: Never use derived values (row numbers) where primary keys are expected
2. **静态检查**: Add code review checklist for display vs storage values
3. **UI验证**: Ensure displayed values in table match database primary keys

---

**文档版本:** v1.0  
**最后更新:** 2026-05-20  
**作者:** Kilo (AI Assistant)  
**状态:** 已完成 - 代码修改已提交，待运行时验证