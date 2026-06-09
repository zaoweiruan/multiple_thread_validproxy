---
title: "feat: subscription panel column sorting (Name, Proxies, Update)"
type: Spec
status: draft
date: 2026-06-02
---

# Subscription Panel Column Sorting - Technical Specification

## Problem Description
The subscription panel UI currently displays subscriptions in a fixed order. Users need to click column headers (Name, Proxies, Update) to sort the subscription list. The Proxies and Update columns may be important for identifying large subscriptions or those needing updates.

## Scope
- **Modify**: SubscriptionPanel to support column header click sorting
- **Create**: SubscriptionListModel for sortable virtual list model
- **NOT Modify**: ProxyListPanel (already has sorting), database schema, other panels

## Architecture
Convert SubscriptionPanel from `wxDataViewListStore` to a custom `SubscriptionListModel` extending `wxDataViewIndexListModel`. This enables the same three-state sorting pattern (None→Asc→Desc→None) already used in ProxyListPanel.

## Detailed Changes

### U1: Create SubscriptionListModel.h
**File**: `include/SubscriptionListModel.h`

```cpp
#pragma once
#include <wx/dataview.h>
#include <vector>
#include <string>
#include <unordered_map>
#include "Subitem.h"

enum {
    COL_ROWNUM = 0,
    COL_ENABLED = 1,
    COL_NAME = 2,
    COL_PROXIES = 3,
    COL_UPDATE = 4
};

enum class SortDirection { None, Asc, Desc };

struct SortState {
    int column = -1;
    SortDirection direction = SortDirection::None;
};

class SubscriptionListModel : public wxDataViewIndexListModel {
public:
    SubscriptionListModel();
    ~SubscriptionListModel() override = default;

    void setData(const std::vector<db::models::Subitem>* subs,
                 const std::unordered_map<std::string, int>* proxyCounts);
    void clear();

    // Override to provide cell values
    unsigned int GetCount() const override;
    wxString GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const override;

    // Override for sorting
    int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
                unsigned int col, bool ascending) const override;

    bool GetAttrByRow(unsigned int row, unsigned int col, wxDataViewCellMode mode,
                      wxDataViewCellAttr& attr) const override;

private:
    const std::vector<db::models::Subitem>* subscriptions_;
    const std::unordered_map<std::string, int>* proxyCounts_;
    SortState sortState_;
};
```

### U2: Create SubscriptionListModel.cpp
**File**: `src/ui/SubscriptionListModel.cpp`

Implement:
- `setData()` - Store pointers to subscription data and proxy count map
- `Compare()` - String compare for Name, numeric compare for Proxies/Update
- Handle "Never" for empty/invalid updatetime values

### U3: Modify SubscriptionPanel.h
**File**: `include/ui/SubscriptionPanel.h`

Changes:
- Replace `wxDataViewListStore* store_` with `SubscriptionListModel* model_`
- Add `sortState_` member
- Add `onColumnHeaderClick()` method declaration

### U4: Modify SubscriptionPanel.cpp
**File**: `src/ui/SubscriptionPanel.cpp`

Changes:
- Replace store_ with model_ in constructor
- Implement `onColumnHeaderClick()` with three-state cycle
- Update column setup to show sort indicators (↕)
- Add `EVT_DATAVIEW_COLUMN_HEADER_CLICK` binding

## Verification Steps
1. Build compiles without errors
2. Clicking Name column cycles: no indicator → ↕ (ascending) → ↕ (descending) → no indicator
3. Clicking Proxies column sorts numeric values correctly
4. Clicking Update column sorts by timestamp (Never appears last)
5. All existing subscription functionality preserved

## Risks
- ProxyListPanel uses numeric IDs for rows; SubscriptionPanel uses 0-based display row numbers. Must handle the idOffset_ logic correctly.