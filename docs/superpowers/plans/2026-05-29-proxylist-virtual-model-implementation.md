# ProxyList Virtual Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `wxDataViewListStore` with a custom `wxDataViewIndexListModel` in ProxyListPanel to improve performance for subscriptions with thousands of proxies.

**Architecture:** New `ProxyListModel` class (inherits `wxDataViewIndexListModel`) holds non-owning pointers to proxy data vectors. `ProxyListPanel` uses it instead of the store. Sorting is native via `Compare()`. SQL query adds `WHERE Subid=...` to avoid full table scan.

**Tech Stack:** C++17, wxWidgets, wxDataViewIndexListModel

---

### Task 1: Create ProxyListModel class (header + implementation)

**Files:**
- Create: `src/ui/ProxyListModel.h`
- Create: `src/ui/ProxyListModel.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create header file `src/ui/ProxyListModel.h`**

```cpp
#ifndef UI_PROXY_LIST_MODEL_H
#define UI_PROXY_LIST_MODEL_H

#include <wx/dataview.h>
#include <string>
#include <vector>
#include <unordered_map>

#include "Profileitem.h"

// Column indices shared between model and panel
enum ProxyListColumn {
    COL_ROWNUM   = 0,
    COL_TYPE     = 1,
    COL_ADDRESS  = 2,
    COL_PORT     = 3,
    COL_DELAY    = 4,
    COL_FAILURES = 5,
    COL_REMARKS  = 6,
    COL_MESSAGE  = 7,
    COL_INDEXID  = 8,
    COL_COUNT    = 9,
};

class ProxyListModel : public wxDataViewIndexListModel {
public:
    ProxyListModel();

    void setProxies(const std::vector<db::models::Profileitem>* proxies,
                    const std::unordered_map<std::string, std::string>* delayMap,
                    const std::unordered_map<std::string, std::string>* messageMap,
                    const std::unordered_map<std::string, int>* failuresMap);
    void clear();

    // wxDataViewIndexListModel overrides
    unsigned int GetCount() const override;
    void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override;
    bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override;
    int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
                unsigned col, bool ascending) const override;

private:
    const std::vector<db::models::Profileitem>* proxies_ = nullptr;
    const std::unordered_map<std::string, std::string>* delayMap_ = nullptr;
    const std::unordered_map<std::string, std::string>* messageMap_ = nullptr;
    const std::unordered_map<std::string, int>* failuresMap_ = nullptr;
};

#endif // UI_PROXY_LIST_MODEL_H
```

- [ ] **Step 2: Create implementation file `src/ui/ProxyListModel.cpp`**

```cpp
#include "ProxyListModel.h"
#include <wx/string.h>

ProxyListModel::ProxyListModel() = default;

void ProxyListModel::setProxies(
    const std::vector<db::models::Profileitem>* proxies,
    const std::unordered_map<std::string, std::string>* delayMap,
    const std::unordered_map<std::string, std::string>* messageMap,
    const std::unordered_map<std::string, int>* failuresMap)
{
    proxies_ = proxies;
    delayMap_ = delayMap;
    messageMap_ = messageMap;
    failuresMap_ = failuresMap;
}

void ProxyListModel::clear() {
    proxies_ = nullptr;
    delayMap_ = nullptr;
    messageMap_ = nullptr;
    failuresMap_ = nullptr;
}

unsigned int ProxyListModel::GetCount() const {
    return proxies_ ? static_cast<unsigned>(proxies_->size()) : 0;
}

void ProxyListModel::GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const {
    if (!proxies_ || row >= proxies_->size()) {
        variant = wxVariant("");
        return;
    }
    const auto& p = (*proxies_)[row];
    switch (col) {
        case COL_ROWNUM:
            variant = wxVariant(wxString::Format("%d", static_cast<int>(row + 1)));
            break;
        case COL_TYPE: {
            int t = 0;
            try { t = std::stoi(p.configtype); } catch (...) { variant = wxVariant("Unknown"); break; }
            static const char* names[] = {"", "VMess", "Custom", "Shadowsocks", "SOCKS", "VLESS",
                                          "Trojan", "Hysteria2", "TUIC", "WireGuard", "HTTP",
                                          "Anytls", "Naive"};
            if (t >= 1 && t <= 12) variant = wxVariant(names[t]);
            else variant = wxVariant("Unknown");
            break;
        }
        case COL_ADDRESS:
            variant = wxVariant(p.address);
            break;
        case COL_PORT:
            variant = wxVariant(p.port);
            break;
        case COL_DELAY: {
            std::string d = delayMap_ && delayMap_->count(p.indexid) ? delayMap_->at(p.indexid) : "-";
            variant = wxVariant(d);
            break;
        }
        case COL_FAILURES: {
            int f = failuresMap_ && failuresMap_->count(p.indexid) ? failuresMap_->at(p.indexid) : 0;
            variant = wxVariant(std::to_string(f));
            break;
        }
        case COL_REMARKS:
            variant = wxVariant(p.remarks);
            break;
        case COL_MESSAGE: {
            std::string m = messageMap_ && messageMap_->count(p.indexid) ? messageMap_->at(p.indexid) : "";
            variant = wxVariant(m);
            break;
        }
        case COL_INDEXID:
            variant = wxVariant(p.indexid);
            break;
        default:
            variant = wxVariant("");
            break;
    }
}

bool ProxyListModel::SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) {
    if (!proxies_ || row >= proxies_->size()) return false;
    if (col == COL_REMARKS) {
        (*proxies_)[row].remarks = variant.GetString().ToStdString();
        return true;
    }
    return false;
}

int ProxyListModel::Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
                             unsigned col, bool ascending) const {
    if (!proxies_) return 0;
    unsigned row1 = static_cast<unsigned>(wxPtrToUInt(item1.GetID()));
    unsigned row2 = static_cast<unsigned>(wxPtrToUInt(item2.GetID()));
    if (row1 >= proxies_->size() || row2 >= proxies_->size()) return 0;

    const auto& a = (*proxies_)[row1];
    const auto& b = (*proxies_)[row2];

    int cmp = 0;
    switch (col) {
        case COL_ROWNUM:
            cmp = (row1 > row2) - (row1 < row2);
            break;
        case COL_TYPE:
            cmp = a.configtype.compare(b.configtype);
            break;
        case COL_ADDRESS:
            cmp = a.address.compare(b.address);
            break;
        case COL_PORT:
            cmp = a.port.compare(b.port);
            break;
        case COL_DELAY: {
            int delayA = delayMap_ && delayMap_->count(a.indexid) ? std::stoi(delayMap_->at(a.indexid)) : -1;
            int delayB = delayMap_ && delayMap_->count(b.indexid) ? std::stoi(delayMap_->at(b.indexid)) : -1;
            cmp = (delayA > delayB) - (delayA < delayB);
            break;
        }
        case COL_FAILURES: {
            int failA = failuresMap_ && failuresMap_->count(a.indexid) ? failuresMap_->at(a.indexid) : 0;
            int failB = failuresMap_ && failuresMap_->count(b.indexid) ? failuresMap_->at(b.indexid) : 0;
            cmp = (failA > failB) - (failA < failB);
            break;
        }
        case COL_REMARKS:
            cmp = a.remarks.compare(b.remarks);
            break;
        case COL_MESSAGE: {
            std::string msgA = messageMap_ && messageMap_->count(a.indexid) ? messageMap_->at(a.indexid) : "";
            std::string msgB = messageMap_ && messageMap_->count(b.indexid) ? messageMap_->at(b.indexid) : "";
            cmp = msgA.compare(msgB);
            break;
        }
        case COL_INDEXID:
            cmp = a.indexid.compare(b.indexid);
            break;
        default:
            cmp = 0;
            break;
    }
    return ascending ? cmp : -cmp;
}
```

- [ ] **Step 3: Add `src/ui/ProxyListModel.cpp` to CMakeLists.txt**

Read the current CMakeLists.txt to find the UI source files section, then add:
```
${CMAKE_SOURCE_DIR}/src/ui/ProxyListModel.cpp
```

- [ ] **Step 4: Verify build succeeds**

Run:
```
cd /d E:\eclipse_workspace\multiple_thread_validproxy && cmake --build build --parallel 8 2>&1
```
Expected: Compilation succeeds (a `.cpp.obj` entry for ProxyListModel appears)

---

### Task 2: Refactor ProxyListPanel to use ProxyListModel

**Files:**
- Modify: `src/ui/ProxyListPanel.h`
- Modify: `src/ui/ProxyListPanel.cpp`

- [ ] **Step 1: Update `ProxyListPanel.h`**

Remove `wxDataViewListStore* store_` member, add `class ProxyListModel* model_`, add `#include "ProxyListModel.h"`:

```cpp
#include "ProxyListModel.h"
// ... in class body, replace store_ with:
ProxyListModel* model_;
```

Remove the `sortProxiesByColumn` and `resetSort` method declarations (sorting is now handled by model's `Compare()`).

- [ ] **Step 2: Rewrite constructor in `ProxyListPanel.cpp`**

In the constructor, replace:
```cpp
store_ = new wxDataViewListStore();
listCtrl_->AssociateModel(store_);
store_->DecRef();  // AssociateModel took ownership
```
With:
```cpp
model_ = new ProxyListModel();
listCtrl_->AssociateModel(model_);
model_->DecRef();  // AssociateModel took ownership
```

Remove the local column enum definition (now in ProxyListModel.h).

- [ ] **Step 3: Rewrite `loadProxies()`**

Replace the old implementation with:
```cpp
void ProxyListPanel::loadProxies(const std::string& subId) {
    currentSubId_ = subId;
    allProxies_ = controller_->loadProxies(subId);

    if (subId.empty() || subId != currentSubId_) {
        // New subscription - reset sort state
        sortState_.column = -1;
        sortState_.direction = SortDirection::None;
    }
    // Use allProxies_ as the current view (filterBySearch will subset if needed)
    proxies_ = allProxies_;

    exItems_ = controller_->loadProxyResults();

    // Build lookup maps
    std::unordered_map<std::string, std::string> delayMap;
    std::unordered_map<std::string, std::string> messageMap;
    std::unordered_map<std::string, int> failuresMap;
    for (const auto& ex : exItems_) {
        delayMap[ex.indexid] = ex.delay;
        messageMap[ex.indexid] = ex.message;
        failuresMap[ex.indexid] = ex.consecutive_failures;
    }

    // Update model and reset view
    listCtrl_->Freeze();
    model_->setProxies(&proxies_, &delayMap, &messageMap, &failuresMap);
    model_->Reset();

    // Select first row if none selected
    if (!proxies_.empty()) {
        if (!listCtrl_->GetSelection().IsOk()) {
            listCtrl_->Select(model_->GetItem(0));
            // Fire ProxySelectionEvent for first proxy
            auto& p = proxies_[0];
            std::string delay = delayMap.count(p.indexid) ? delayMap[p.indexid] : "";
            std::string msg = messageMap.count(p.indexid) ? messageMap[p.indexid] : "";
            int failures = failuresMap.count(p.indexid) ? failuresMap[p.indexid] : 0;
            wxWindow* topLevel = wxGetTopLevelParent(this);
            if (topLevel && topLevel != this) {
                ProxySelectionEvent selEvt(p.indexid, p.address, p.port,
                                           delay, msg, failures, p.remarks);
                wxQueueEvent(topLevel, selEvt.Clone());
            }
        }
    }
    listCtrl_->Thaw();
}
```

Note: `proxies_` is preserved as a vector so `filterBySearch()` and `onSelectionChanged()` can iterate it. The model holds a pointer to it.

- [ ] **Step 4: Rewrite `refreshResults()`**

Replace with:
```cpp
void ProxyListPanel::refreshResults() {
    exItems_ = controller_->loadProxyResults();

    // Build lookup maps
    std::unordered_map<std::string, std::string> delayMap;
    std::unordered_map<std::string, std::string> messageMap;
    std::unordered_map<std::string, int> failuresMap;
    for (const auto& ex : exItems_) {
        delayMap[ex.indexid] = ex.delay;
        messageMap[ex.indexid] = ex.message;
        failuresMap[ex.indexid] = ex.consecutive_failures;
    }

    model_->setProxies(&proxies_, &delayMap, &messageMap, &failuresMap);
    model_->Reset();
}
```

- [ ] **Step 5: Rewrite `onColumnHeaderClick()`, remove `sortProxiesByColumn()` and `resetSort()`**

Replace `onColumnHeaderClick`:
```cpp
void ProxyListPanel::onColumnHeaderClick(wxDataViewEvent& event) {
    int col = event.GetColumn();
    Logger::write("[ProxyListPanel] Column header click: column=" + std::to_string(col), LogLevel::DEBUG);

    // Cycle direction: None -> Asc -> Desc -> None
    if (sortState_.column == col) {
        sortState_.direction = (sortState_.direction == SortDirection::Asc)
            ? SortDirection::Desc
            : (sortState_.direction == SortDirection::Desc
                ? SortDirection::None
                : SortDirection::Asc);
    } else {
        sortState_.column = col;
        sortState_.direction = SortDirection::Asc;
    }

    if (sortState_.direction != SortDirection::None && model_) {
        // Enable sort indicator on the column header
        listCtrl_->GetColumn(col)->SetSortOrder(sortState_.direction == SortDirection::Asc);
        model_->Resort();
    } else {
        // Reset to original order
        loadProxies(currentSubId_);
    }

    Logger::write("[ProxyListPanel] Column header click done: col=" + std::to_string(sortState_.column)
                  + ", dir=" + std::to_string(static_cast<int>(sortState_.direction)), LogLevel::DEBUG);
}
```

Remove the entire `sortProxiesByColumn()` and `resetSort()` methods.

- [ ] **Step 6: Rewrite `onSelectionChanged()`**

Replace the proxy-finding logic to work with the model:
```cpp
void ProxyListPanel::onSelectionChanged(wxDataViewEvent& event) {
    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;

    unsigned viewRow = static_cast<unsigned>(wxPtrToUInt(item.GetID()));
    wxVariant idxVar;
    model_->GetValueByRow(idxVar, viewRow, COL_INDEXID);
    std::string indexId = idxVar.GetString().ToStdString();

    // Find proxy by indexId
    db::models::Profileitem* proxy = nullptr;
    for (auto& p : proxies_) {
        if (p.indexid == indexId) { proxy = &p; break; }
    }

    // Build lookup from exItems_
    std::unordered_map<std::string, std::string> delayMap;
    std::unordered_map<std::string, std::string> messageMap;
    std::unordered_map<std::string, int> failuresMap;
    for (const auto& ex : exItems_) {
        delayMap[ex.indexid] = ex.delay;
        messageMap[ex.indexid] = ex.message;
        failuresMap[ex.indexid] = ex.consecutive_failures;
    }

    // Get proxy info
    std::string host = "", port = "", delay = "", message = "", remarks = "";
    int failures = 0;
    if (proxy) {
        host = proxy->address;
        port = proxy->port;
        delay = delayMap.count(indexId) ? delayMap[indexId] : "";
        message = messageMap.count(indexId) ? messageMap[indexId] : "";
        failures = failuresMap.count(indexId) ? failuresMap[indexId] : 0;
        remarks = proxy->remarks;
    }

    wxWindow* topLevel = wxGetTopLevelParent(this);
    if (topLevel && topLevel != this && proxy) {
        ProxySelectionEvent selEvt(indexId, host, port, delay, message, failures, remarks);
        wxQueueEvent(topLevel, selEvt.Clone());
    }
    (void)event;
}
```

- [ ] **Step 7: Remove `configTypeToName()` and unused includes**

Remove the `configTypeToName()` static function from ProxyListPanel.cpp (now handled in ProxyListModel::GetValueByRow). Also remove `#include <unordered_map>` if it was only used in ProxyListPanel.cpp (the maps are now local in each method, but they still use unordered_map in ProxyListPanel.cpp).

Actually, `configTypeToName()` is no longer needed since the model handles type-to-name conversion. Remove the function.

- [ ] **Step 8: Build and fix any compilation errors**

Run:
```
cd /d E:\eclipse_workspace\multiple_thread_validproxy && cmake --build build --parallel 8 2>&1
```
Fix any compilation errors and rebuild.

---

### Task 3: Optimize SQL query in AppController

**Files:**
- Modify: `src/ui/AppController.cpp`

- [ ] **Step 1: Add SQL WHERE clause to `loadProxies()`**

Change `AppController::loadProxies()`:
```cpp
std::vector<db::models::Profileitem> AppController::loadProxies(const std::string& subId) {
    db::models::ProfileitemDAO dao(db_);
    if (subId.empty()) {
        return dao.getAll();
    }
    std::string sql = "SELECT * FROM ProfileItem WHERE Subid = '" + subId + "';";
    return dao.getAll(sql);
}
```

This eliminates the full table scan when loading proxies for a specific subscription. Note: the old implementation loaded ALL proxies first, then filtered in C++. The new approach returns only matching rows from SQLite.

---

### Task 4: Build and run tests

**Files:**
- Test: `bin\validproxy.exe -show-sub` (CLI smoke test)

- [ ] **Step 1: Rebuild project**

Run:
```
cd /d E:\eclipse_workspace\multiple_thread_validproxy && cmake --build build --parallel 8 2>&1
```
Expected: Build succeeds with no errors

- [ ] **Step 2: Run unit tests**

Run:
```
cd /d E:\eclipse_workspace\multiple_thread_validproxy && ctest --test-dir build -V 2>&1
```
Expected: All 3 tests pass

- [ ] **Step 3: CLI smoke test**

Run:
```
cd /d E:\eclipse_workspace\multiple_thread_validproxy && bin\validproxy.exe -show-sub 2>&1 | findstr "Total:"
```
Expected: Shows "Total: 58 subscriptions" (no crash, no model errors)
