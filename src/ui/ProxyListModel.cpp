#include "ProxyListModel.h"
#include "Logger.h"

#include <algorithm>

// -------------------------------------------------------------------
// Helper: Convert ConfigType number to protocol name
// -------------------------------------------------------------------
static wxString configTypeToName(const std::string& type) {
    int t = 0;
    try { t = std::stoi(type); } catch (...) { return "Unknown"; }
    switch (t) {
        case 1:  return "VMess";
        case 2:  return "Custom";
        case 3:  return "Shadowsocks";
        case 4:  return "SOCKS";
        case 5:  return "VLESS";
        case 6:  return "Trojan";
        case 7:  return "Hysteria2";
        case 8:  return "TUIC";
        case 9:  return "WireGuard";
        case 10: return "HTTP";
        case 11: return "Anytls";
        case 12: return "Naive";
        default: return "Unknown";
    }
}

// -------------------------------------------------------------------
ProxyListModel::ProxyListModel() = default;
ProxyListModel::~ProxyListModel() = default;

// -------------------------------------------------------------------
void ProxyListModel::setData(
    std::vector<db::models::Profileitem>* proxies,
    const std::vector<db::models::ProfileExItem>* exItems)
{
    proxies_ = proxies;
    exItems_ = exItems;
    rebuildMaps();
}

// -------------------------------------------------------------------
void ProxyListModel::rebuildMaps() {
    delayMap_.clear();
    messageMap_.clear();
    failuresMap_.clear();

    if (!exItems_) return;

    for (const auto& ex : *exItems_) {
        delayMap_[ex.indexid] = ex.delay;
        messageMap_[ex.indexid] = ex.message;
        failuresMap_[ex.indexid] = ex.consecutive_failures;
    }
}

// -------------------------------------------------------------------
void ProxyListModel::detectIdOffset() {
    idOffset_ = 0;
    if (GetCount() > 0) {
        wxDataViewItem item = GetItem(0);
        if (item.IsOk()) {
            unsigned int id = static_cast<unsigned int>(
                reinterpret_cast<wxUIntPtr>(item.GetID()));
            if (id == 1) {
                idOffset_ = 1;
            }
            Logger::write("[DIAG] ProxyListModel::detectIdOffset: idOffset_="
                          + std::to_string(static_cast<int>(idOffset_))
                          + " (GetItem(0).GetID()=" + std::to_string(id) + ")",
                          LogLevel::TRACE);
        }
    }
}

// -------------------------------------------------------------------
void ProxyListModel::clear() {
    proxies_ = nullptr;
    exItems_ = nullptr;
    delayMap_.clear();
    messageMap_.clear();
    failuresMap_.clear();
    idOffset_ = 0;
}

// -------------------------------------------------------------------
unsigned int ProxyListModel::GetCount() const {
    unsigned int c = proxies_ ? static_cast<unsigned int>(proxies_->size()) : 0;
    // Log only when returning > 0 to reduce noise
    if (c > 0) {
        Logger::write("[DIAG] ProxyListModel::GetCount() = " + std::to_string(c), LogLevel::TRACE);
    }
    return c;
}

// -------------------------------------------------------------------
wxString ProxyListModel::GetColumnType(unsigned int col) const {
    // All columns are text/string
    (void)col;
    return wxT("string");
}

// -------------------------------------------------------------------
void ProxyListModel::GetValueByRow(wxVariant& variant, unsigned int row,
                                   unsigned int col) const
{
    // Guard against invalid state
    if (!proxies_ || row >= proxies_->size()) {
        if (proxies_) {
            Logger::write("[DIAG] GetValueByRow: row=" + std::to_string(row)
                          + " >= proxies_->size()=" + std::to_string(proxies_->size())
                          + " col=" + std::to_string(col), LogLevel::WARN);
        } else {
            Logger::write("[DIAG] GetValueByRow: proxies_ is NULL, row="
                          + std::to_string(row) + " col=" + std::to_string(col), LogLevel::WARN);
        }
        variant = wxVariant("");
        return;
    }

    // row in wxDataViewIndexListModel is the VIEW row.
    // Convert to the data index via the model's internal mapping.
    unsigned int dataIdx = getDataIndex(row);
    if (dataIdx >= proxies_->size()) {
        Logger::write("[DIAG] GetValueByRow: dataIdx=" + std::to_string(dataIdx)
                      + " >= proxies_->size()=" + std::to_string(proxies_->size())
                      + " row=" + std::to_string(row) + " col=" + std::to_string(col), LogLevel::WARN);
        variant = wxVariant("");
        return;
    }

    const auto& p = (*proxies_)[dataIdx];
    const std::string& idx = p.indexid;

    switch (col) {
        case COL_ROWNUM:
            variant = wxVariant(wxString::Format("%u", row + 1));
            break;
        case COL_TYPE:
            variant = wxVariant(configTypeToName(p.configtype));
            break;
        case COL_ADDRESS:
            variant = wxVariant(p.address);
            break;
        case COL_PORT:
            variant = wxVariant(p.port);
            break;
        case COL_DELAY: {
            auto it = delayMap_.find(idx);
            variant = wxVariant(it != delayMap_.end() ? it->second : "-");
            break;
        }
        case COL_FAILURES: {
            auto it = failuresMap_.find(idx);
            variant = wxVariant(std::to_string(it != failuresMap_.end() ? it->second : 0));
            break;
        }
        case COL_REMARKS:
            variant = wxVariant(p.remarks);
            break;
        case COL_MESSAGE: {
            auto it = messageMap_.find(idx);
            variant = wxVariant(it != messageMap_.end() ? it->second : "");
            break;
        }
        case COL_INDEXID:
            variant = wxVariant(idx);
            break;
        default:
            variant = wxVariant("");
            break;
    }
}

// -------------------------------------------------------------------
bool ProxyListModel::SetValueByRow(const wxVariant& variant, unsigned int row,
                                   unsigned int col)
{
    // Only COL_REMARKS is editable
    if (col != COL_REMARKS || !proxies_ || row >= proxies_->size())
        return false;

    unsigned int dataIdx = getDataIndex(row);
    if (dataIdx >= proxies_->size())
        return false;

    (*proxies_)[dataIdx].remarks = variant.GetString().ToStdString();
    return true;
}

// -------------------------------------------------------------------
int ProxyListModel::Compare(const wxDataViewItem& item1,
                            const wxDataViewItem& item2,
                            unsigned int col, bool ascending) const
{
    if (!proxies_ || proxies_->size() < 2)
        return 0;

    // In wxDataViewIndexListModel, item IDs are data indices.
    // Compensate for 1-based ID bug in some wxWidgets builds.
    unsigned int idx1 = static_cast<unsigned int>(
        reinterpret_cast<wxUIntPtr>(item1.GetID()));
    unsigned int idx2 = static_cast<unsigned int>(
        reinterpret_cast<wxUIntPtr>(item2.GetID()));
    if (idx1 >= idOffset_) idx1 -= idOffset_;
    if (idx2 >= idOffset_) idx2 -= idOffset_;

    if (idx1 >= proxies_->size() || idx2 >= proxies_->size())
        return 0;

    const auto& a = (*proxies_)[idx1];
    const auto& b = (*proxies_)[idx2];

    int cmp = 0;
    switch (col) {
        case COL_ROWNUM:
            // Sort by row number (1, 2, 3...) numerically
            cmp = static_cast<int>(idx1 - idx2);
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
            int dA = 0, dB = 0;
            auto itA = delayMap_.find(a.indexid);
            auto itB = delayMap_.find(b.indexid);
            if (itA != delayMap_.end()) {
                try { dA = std::stoi(itA->second); } catch (...) { }
            }
            if (itB != delayMap_.end()) {
                try { dB = std::stoi(itB->second); } catch (...) { }
            }
            cmp = (dA > dB) - (dA < dB);
            break;
        }
        case COL_FAILURES: {
            int fA = 0, fB = 0;
            auto itA = failuresMap_.find(a.indexid);
            auto itB = failuresMap_.find(b.indexid);
            if (itA != failuresMap_.end()) fA = itA->second;
            if (itB != failuresMap_.end()) fB = itB->second;
            cmp = (fA > fB) - (fA < fB);
            break;
        }
        case COL_MESSAGE: {
            std::string mA, mB;
            auto itA = messageMap_.find(a.indexid);
            auto itB = messageMap_.find(b.indexid);
            if (itA != messageMap_.end()) mA = itA->second;
            if (itB != messageMap_.end()) mB = itB->second;
            cmp = mA.compare(mB);
            break;
        }
        case COL_REMARKS:
            cmp = a.remarks.compare(b.remarks);
            break;
        case COL_INDEXID:
            cmp = a.indexid.compare(b.indexid);
            break;
        default:
            cmp = a.indexid.compare(b.indexid);
            break;
    }

    return ascending ? cmp : -cmp;
}

// -------------------------------------------------------------------
int ProxyListModel::findRowByIndexId(const std::string& indexId) const {
    if (!proxies_) return -1;

    unsigned int count = GetCount();
    for (unsigned int viewRow = 0; viewRow < count; ++viewRow) {
        unsigned int dataIdx = getDataIndex(viewRow);
        if (dataIdx < proxies_->size() &&
            (*proxies_)[dataIdx].indexid == indexId) {
            return static_cast<int>(viewRow);
        }
    }
    return -1;
}

// -------------------------------------------------------------------
unsigned int ProxyListModel::getDataIndex(unsigned int viewRow) const {
    wxDataViewItem item = GetItem(viewRow);
    unsigned int id = static_cast<unsigned int>(
        reinterpret_cast<wxUIntPtr>(item.GetID()));
    if (!item.IsOk()) {
        Logger::write("[DIAG] getDataIndex(" + std::to_string(viewRow) + "): GetItem returned INVALID item!",
                      LogLevel::WARN);
        return id;
    }
    // Compensate for 1-based ID bug in some wxWidgets builds
    if (id >= idOffset_) {
        id -= idOffset_;
    }
    return id;
}

// -------------------------------------------------------------------
std::string ProxyListModel::getIndexIdAtRow(unsigned int viewRow) const {
    if (!proxies_) return "";
    unsigned int dataIdx = getDataIndex(viewRow);
    if (dataIdx >= proxies_->size()) return "";
    return (*proxies_)[dataIdx].indexid;
}

// -------------------------------------------------------------------
std::string ProxyListModel::getDelay(const std::string& indexId) const {
    auto it = delayMap_.find(indexId);
    return it != delayMap_.end() ? it->second : "";
}

// -------------------------------------------------------------------
std::string ProxyListModel::getMessage(const std::string& indexId) const {
    auto it = messageMap_.find(indexId);
    return it != messageMap_.end() ? it->second : "";
}

// -------------------------------------------------------------------
int ProxyListModel::getFailures(const std::string& indexId) const {
    auto it = failuresMap_.find(indexId);
    return it != failuresMap_.end() ? it->second : 0;
}

// -------------------------------------------------------------------
void ProxyListModel::notifyTestResultChanged() {
    unsigned int count = GetCount();
    for (unsigned int i = 0; i < count; ++i) {
        wxDataViewItem item = GetItem(i);
        ValueChanged(item, COL_DELAY);
        ValueChanged(item, COL_MESSAGE);
        ValueChanged(item, COL_FAILURES);
    }
}

// -------------------------------------------------------------------
const db::models::Profileitem* ProxyListModel::getProfileAtRow(
    unsigned int viewRow) const
{
    if (!proxies_) return nullptr;
    unsigned int dataIdx = getDataIndex(viewRow);
    if (dataIdx >= proxies_->size()) return nullptr;
    return &(*proxies_)[dataIdx];
}


