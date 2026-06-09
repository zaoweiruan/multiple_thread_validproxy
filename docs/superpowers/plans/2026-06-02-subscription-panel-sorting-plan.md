# Subscription Panel Column Sorting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add three-state column sorting (Name, Proxies, Update) to the subscription panel UI with visual indicators.

**Architecture:** Convert SubscriptionPanel from `wxDataViewListStore` to a custom `SubscriptionListModel` extending `wxDataViewIndexListModel`, matching the pattern used in ProxyListPanel.

**Tech Stack:** wxWidgets 3.2, C++17, CMake/Ninja

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `include/SubscriptionListModel.h` | Create | Sortable virtual model class |
| `src/ui/SubscriptionListModel.cpp` | Create | Compare() implementation |
| `include/SubscriptionPanel.h` | Modify | Replace store_ with model_ |
| `src/ui/SubscriptionPanel.cpp` | Modify | Column click handler |
| `CMakeLists.txt` | Modify | Add new source file |

---

### Task 1: Create SubscriptionListModel.h

**Files:**
- Create: `include/SubscriptionListModel.h`

- [ ] **Step 1: Write SubscriptionListModel.h**

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

    unsigned int GetCount() const override;
    wxString GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const override;
    bool SetValueByRow(const wxVariant& variant, unsigned int row, unsigned int col) override;
    int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
                unsigned int col, bool ascending) const override;

private:
    const std::vector<db::models::Subitem>* subscriptions_;
    const std::unordered_map<std::string, int>* proxyCounts_;
};
```

- [ ] **Step 2: Add include to CMakeLists.txt**

Run: `grep -n "src/ui/ProxyListModel.cpp" CMakeLists.txt`
Expected: Find existing ProxyListModel entry

- [ ] **Step 3: Add new source file to CMakeLists.txt**

Add to `add_executable(validproxy` near ProxyListModel:
```
src/ui/SubscriptionListModel.cpp
```

- [ ] **Step 4: Commit**

```bash
git add include/SubscriptionListModel.h CMakeLists.txt
git commit -m "feat: add SubscriptionListModel header for sortable subscription view"
```

---

### Task 2: Create SubscriptionListModel.cpp

**Files:**
- Create: `src/ui/SubscriptionListModel.cpp`

- [ ] **Step 1: Write SubscriptionListModel.cpp**

```cpp
#include "SubscriptionListModel.h"
#include "Logger.h"

SubscriptionListModel::SubscriptionListModel()
    : subscriptions_(nullptr), proxyCounts_(nullptr) {}

void SubscriptionListModel::setData(const std::vector<db::models::Subitem>* subs,
                                   const std::unordered_map<std::string, int>* proxyCounts) {
    subscriptions_ = subs;
    proxyCounts_ = proxyCounts;
}

void SubscriptionListModel::clear() {
    subscriptions_ = nullptr;
    proxyCounts_ = nullptr;
}

unsigned int SubscriptionListModel::GetCount() const {
    return subscriptions_ ? static_cast<unsigned int>(subscriptions_->size()) : 0;
}

wxString SubscriptionListModel::GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const {
    if (!subscriptions_ || row >= subscriptions_->size()) {
        variant = wxVariant("");
        return "";
    }
    const auto& sub = (*subscriptions_)[row];
    switch (col) {
        case COL_ROWNUM:
            variant = wxVariant(wxString::Format("%u", row + 1));
            break;
        case COL_ENABLED:
            variant = wxVariant(sub.enabled == "1");
            break;
        case COL_NAME:
            variant = wxVariant(sub.remarks);
            break;
        case COL_PROXIES: {
            int count = 0;
            if (proxyCounts_) {
                auto it = proxyCounts_->find(sub.id);
                if (it != proxyCounts_->end()) count = it->second;
            }
            variant = wxVariant(wxString::Format("%d", count));
            break;
        }
        case COL_UPDATE: {
            if (sub.updatetime.empty() || sub.updatetime == "0") {
                variant = wxVariant("Never");
            } else {
                try {
                    long ts = std::stol(sub.updatetime);
                    time_t t = static_cast<time_t>(ts);
                    struct tm tm_time;
                    localtime_s(&tm_time, &t);
                    char buf[32];
                    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_time);
                    variant = wxVariant(buf);
                } catch (...) {
                    variant = wxVariant(sub.updatetime);
                }
            }
            break;
        }
        default:
            variant = wxVariant("");
    }
    return "";
}

bool SubscriptionListModel::SetValueByRow(const wxVariant& variant, unsigned int row, unsigned int col) {
    if (!subscriptions_ || row >= subscriptions_->size())
        return false;
    if (col == COL_NAME) {
        (*subscriptions_)[row].remarks = variant.GetString().ToStdString();
        return true;
    }
    return false;
}

int SubscriptionListModel::Compare(const wxDataViewItem& item1,
                                  const wxDataViewItem& item2,
                                  unsigned int col, bool ascending) const {
    if (!subscriptions_ || subscriptions_->size() < 2)
        return 0;
    unsigned int idx1 = static_cast<unsigned int>(reinterpret_cast<wxUIntPtr>(item1.GetID()));
    unsigned int idx2 = static_cast<unsigned int>(reinterpret_cast<wxUIntPtr>(item2.GetID()));
    if (idx1 >= subscriptions_->size() || idx2 >= subscriptions_->size())
        return 0;
    const auto& a = (*subscriptions_)[idx1];
    const auto& b = (*subscriptions_)[idx2];
    int cmp = 0;
    switch (col) {
        case COL_NAME:
            cmp = a.remarks.compare(b.remarks);
            break;
        case COL_PROXIES: {
            int countA = 0, countB = 0;
            if (proxyCounts_) {
                auto itA = proxyCounts_->find(a.id);
                auto itB = proxyCounts_->find(b.id);
                if (itA != proxyCounts_->end()) countA = itA->second;
                if (itB != proxyCounts_->end()) countB = itB->second;
            }
            cmp = (countA > countB) - (countA < countB);
            break;
        }
        case COL_UPDATE: {
            auto getTimestamp = [](const std::string& ts) -> long long {
                if (ts.empty() || ts == "0") return -1;
                try { return std::stoll(ts); } catch (...) { return -1; }
            };
            long long tA = getTimestamp(a.updatetime);
            long long tB = getTimestamp(b.updatetime);
            cmp = (tA > tB) - (tA < tB);
            break;
        }
        default:
            cmp = static_cast<int>(idx1 - idx2);
    }
    return ascending ? cmp : -cmp;
}
```

- [ ] **Step 2: Commit**

```bash
git add src/ui/SubscriptionListModel.cpp
git commit -m "feat: implement SubscriptionListModel Compare for Name/Proxies/Update sorting"
```

---

### Task 3: Modify SubscriptionPanel.h

**Files:**
- Modify: `include/ui/SubscriptionPanel.h`

- [ ] **Step 1: Update member variables**

Change:
```cpp
    AppController* controller_;
    wxDataViewCtrl* listCtrl_;
    wxDataViewListStore* store_;
    std::vector<db::models::Subitem> subs_;
```

To:
```cpp
    AppController* controller_;
    wxDataViewCtrl* listCtrl_;
    SubscriptionListModel* model_;
    std::vector<db::models::Subitem> subs_;
    std::unordered_map<std::string, int> proxyCounts_;
```

- [ ] **Step 2: Add include**

Add after other includes:
```cpp
#include "SubscriptionListModel.h"
```

- [ ] **Step 3: Commit**

```bash
git add include/ui/SubscriptionPanel.h
git commit -m "feat: update SubscriptionPanel to use SubscriptionListModel"
```

---

### Task 4: Modify SubscriptionPanel.cpp

**Files:**
- Modify: `src/ui/SubscriptionPanel.cpp`

- [ ] **Step 1: Add SortState member and header include**

Add to SubscriptionPanel.h after `proxyCounts_`:
```cpp
    SortState sortState_;
```

- [ ] **Step 2: Update constructor to use model**

Replace lines 44-51:
```cpp
    // Data view control
    listCtrl_ = new wxDataViewCtrl(this, wxID_ANY,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxDV_ROW_LINES | wxDV_SINGLE);

    // Model
    model_ = new SubscriptionListModel();
    listCtrl_->AssociateModel(model_);
    model_->DecRef(); // AssociateModel took ownership
```

- [ ] **Step 3: Update column labels for sort indicators**

Change lines 54-58 to add ↕ indicator to sortable columns:
```cpp
    listCtrl_->AppendTextColumn("#", 0, wxDATAVIEW_CELL_INERT, 30);
    listCtrl_->AppendToggleColumn("启用", 1, wxDATAVIEW_CELL_ACTIVATABLE, 30);
    listCtrl_->AppendTextColumn("Name ↕", 2, wxDATAVIEW_CELL_EDITABLE, 200);
    listCtrl_->AppendTextColumn("Proxies ↕", 3, wxDATAVIEW_CELL_INERT, 70, wxALIGN_RIGHT);
    listCtrl_->AppendTextColumn("Update ↕", 4, wxDATAVIEW_CELL_INERT, 130);
```

- [ ] **Step 4: Add column header click binding**

After the toggle event binding (around line 79), add:
```cpp
    // Bind column header click for sorting
    listCtrl_->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &SubscriptionPanel::onColumnHeaderClick, this);
```

- [ ] **Step 5: Replace updateSubscriptionList to use model**

Replace the entire method with:
```cpp
void SubscriptionPanel::updateSubscriptionList(const std::vector<db::models::Subitem>& subs,
                                              const std::unordered_map<std::string, int>& proxyCounts) {
    subs_ = subs;
    proxyCounts_ = proxyCounts;
    model_->setData(&subs_, &proxyCounts_);
    model_->Reset(0);
    model_->Reset(static_cast<unsigned int>(subs_.size()));
}
```

- [ ] **Step 6: Add onColumnHeaderClick method declaration to header**

Add to SubscriptionPanel.h private section:
```cpp
    void onColumnHeaderClick(wxDataViewEvent& event);
```

- [ ] **Step 7: Add onColumnHeaderClick method implementation**

Add before the event table (after includes):
```cpp
// -------------------------------------------------------------------
void SubscriptionPanel::onColumnHeaderClick(wxDataViewEvent& event) {
    int col = event.GetColumn();
    Logger::write("[SubscriptionPanel] Column header click: column=" + std::to_string(col), LogLevel::DEBUG);

    // Only Name (2), Proxies (3), and Update (4) columns are sortable
    if (col == COL_NAME || col == COL_PROXIES || col == COL_UPDATE) {
        // Cycle direction: None -> Asc -> Desc -> None
        if (sortState_.column == col) {
            switch (sortState_.direction) {
                case SortDirection::Asc: sortState_.direction = SortDirection::Desc; break;
                case SortDirection::Desc: sortState_.direction = SortDirection::None; sortState_.column = -1; break;
                default: sortState_.direction = SortDirection::Asc; break;
            }
        } else {
            sortState_.column = col;
            sortState_.direction = SortDirection::Asc;
        }

        if (sortState_.direction != SortDirection::None) {
            wxDataViewColumn* dvCol = listCtrl_->GetColumn(col);
            if (dvCol) {
                dvCol->SetSortOrder(sortState_.direction == SortDirection::Asc);
            }
            listCtrl_->GetModel()->Resort();
        } else {
            wxDataViewColumn* currentSort = listCtrl_->GetSortingColumn();
            if (currentSort) {
                currentSort->UnsetAsSortKey();
            }
            model_->Reset(0);
            model_->Reset(static_cast<unsigned int>(subs_.size()));
        }
    }
    event.Skip();
}
```

- [ ] **Step 8: Commit**

```bash
git add include/ui/SubscriptionPanel.h src/ui/SubscriptionPanel.cpp
git commit -m "feat: implement subscription panel column sorting for Name/Proxies/Update"
```

---

### Task 5: Build and Verify

- [ ] **Step 1: Build the project**

```bash
cmake --build build --parallel 8
```

Expected: No compilation errors

- [ ] **Step 2: Run tests**

```bash
bin/test_*.exe
```

Expected: All 6 test suites pass

- [ ] **Step 3: Commit**

```bash
git commit -m "chore: verify subscription panel sorting build and tests pass"
```