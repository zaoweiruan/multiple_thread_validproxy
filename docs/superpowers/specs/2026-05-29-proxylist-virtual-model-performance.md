# ProxyList Virtual Model Performance Optimization

## Problem

When a subscription contains many proxies (e.g., 2390 proxies in "barry-far-All_Configs_Sub"), `ProxyListPanel::loadProxies()` is slow due to:

1. **Full table SQL scan**: `AppController::loadProxies(subId)` calls `dao.getAll()` which executes `SELECT * FROM ProfileItem` returning all ~3703 rows, then filters in C++ by subId. This loads 35% more data than needed.
2. **Row-by-row store rebuild**: `store_->AppendItem()` is called once per proxy, each triggering internal wxWidgets UI layout/render calculations.
3. **No SQL WHERE clause**: The DAO method accepts a custom SQL string, but `loadProxies()` passes the default `SELECT * FROM ProfileItem` with no filter.

## Solution: Virtual Model (wxDataViewIndexListModel)

Replace `wxDataViewListStore` with a custom `wxDataViewIndexListModel` subclass. The model holds non-owning pointers to the proxy data vectors in `ProxyListPanel`. wxWidgets calls `GetValueByRow()` only for visible rows (viewport-based lazy evaluation). Sorting is handled natively via `Compare()`.

### Files

- **Create**: `src/ui/ProxyListModel.h`, `src/ui/ProxyListModel.cpp`
- **Modify**: `src/ui/ProxyListPanel.h`, `src/ui/ProxyListPanel.cpp`
- **Modify**: `CMakeLists.txt` (add new source files)

### ProxyListModel API

```cpp
class ProxyListModel : public wxDataViewIndexListModel {
public:
    ProxyListModel();
    
    // Called by ProxyListPanel when data changes
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
    // Non-owning pointers — data lives in ProxyListPanel vectors
    const std::vector<db::models::Profileitem>* proxies_ = nullptr;
    const std::unordered_map<std::string, std::string>* delayMap_ = nullptr;
    const std::unordered_map<std::string, std::string>* messageMap_ = nullptr;
    const std::unordered_map<std::string, int>* failuresMap_ = nullptr;
};
```

- `GetCount()`: returns `proxices_ ? proxies_->size() : 0`
- `GetValueByRow()`: switch on `col`, return `proxies_->at(row).address` etc.
- `SetValueByRow()`: only COL_REMARKS is editable, modify `proxies_->at(row).remarks`
- `Compare()`: switch on `col`, compare `proxies_->at(row1).field` vs `proxies_->at(row2).field`

### ProxyListPanel Changes

**Members**: replace `wxDataViewListStore* store_` with `ProxyListModel* model_`.

**Constructor**: create model, `listCtrl_->AssociateModel(model_)`, remove column AppendTextColumn calls (they stay the same, just use different model association). Keep `wxDV_SINGLE` style.

**`loadProxies(subId)`**:
1. Load exItems from DB (unchanged)
2. Build delay/message/failures maps (unchanged)
3. Add `Freeze()` before model operations
4. `model_->setProxies(&proxies_, &delayMap, &messageMap, &failuresMap)`
5. `model_->Reset()`
6. Select first row if none selected (unchanged)
7. Add `Thaw()` after model operations

**`refreshResults()`**:
1. Reload exItems, rebuild maps
2. For each row, call `model_->RowChanged(row)` instead of `store_->SetValue()`

**`sortProxiesByColumn()`**: removed entirely. Sort is now handled by:
```cpp
void ProxyListPanel::onColumnHeaderClick(wxDataViewEvent& event) {
    int col = event.GetColumn();
    // Cycle direction (same as before: None→Asc→Desc→None)
    if (sortState_.column == col)
        sortState_.direction = cycle(sortState_.direction);
    else { sortState_.column = col; sortState_.direction = Asc; }
    
    if (sortState_.direction != SortDirection::None && model_)
        model_->Resort();
    else
        loadProxies(currentSubId_);  // resets to original order
}
```

The sorting itself uses `Compare()` in the model, called internally by wxWidgets.

**`selectProxyByIndexId(indexId)`**: iterate model's view rows via `GetValueByRow()` to correctly handle sorted order:
```cpp
for (unsigned row = 0; row < model_->GetCount(); ++row) {
    wxVariant idxVar;
    model_->GetValueByRow(idxVar, row, COL_INDEXID);
    if (idxVar.GetString().ToStdString() == indexId) {
        listCtrl_->Select(model_->GetItem(row));
        listCtrl_->EnsureVisible(model_->GetItem(row));
        break;
    }
}
```

**`filterBySearch()`**: unchanged (still filters proxies_ vector, then calls loadProxies which calls model_->Reset()).

**`onSelectionChanged()`**: access data via model's `GetValueByRow()` to correctly handle sorted view order:
```cpp
wxDataViewItem item = listCtrl_->GetSelection();
unsigned viewRow = wxPtrToUInt(item.GetID());
wxVariant idxVar;
model_->GetValueByRow(idxVar, viewRow, COL_INDEXID);
std::string indexId = idxVar.GetString().ToStdString();

// Find proxy in data vector by indexId
db::models::Profileitem* proxy = nullptr;
for (auto& p : proxies_) {
    if (p.indexid == indexId) { proxy = &p; break; }
}
// ... build delay/message/failures maps, fire ProxySelectionEvent
```

### AppController Changes

**`loadProxies(subId)`**: Change DAO call to use WHERE clause:
```cpp
if (subId.empty())
    return dao.getAll();
std::string sql = "SELECT * FROM ProfileItem WHERE Subid = '" + subId + "';";
return dao.getAll(sql);
```
This eliminates loading all rows when the subId is known.

### CMakeLists.txt

Add `src/ui/ProxyListModel.cpp` to the source file list.

### Column Constants

Move column enum from ProxyListPanel.cpp to a shared header or keep it accessible from both ProxyListPanel and ProxyListModel. Since ProxyListModel needs the column enum for `GetValueByRow()` and `Compare()`, define it in `ProxyListModel.h`:

```cpp
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
```

Include `ProxyListModel.h` in `ProxyListPanel.cpp` and remove the local enum definition.

### Edge Cases

- **Empty proxy list**: `GetCount()` returns 0, `GetValueByRow()` is never called. `clear()` sets all pointers to nullptr.
- **Sort when no data**: `Compare()` checks `proxies_` before accessing.
- **SubId changes mid-load**: `currentSubId_` tracks active subscription; model Reset() ensures view is consistent.
- **exItems updates during test**: `refreshResults()` calls `RowChanged()` for each visible row. For large datasets, call `Reset()` instead to avoid N individual notifications.

### Testing

- Build: confirm no compilation errors with new model class
- Test suites: 3 unit test suites pass
- Manual: click column headers to verify sort Asc/Desc/None cycle
- Manual: switch subscriptions with 10/100/2390 proxies — verify load time improvement
- Manual: search filter + sort combined
