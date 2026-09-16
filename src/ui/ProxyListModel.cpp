#include "ProxyListModel.h"
#include "Logger.h"
#include "Utils.h"

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

void ProxyListModel::setDataWithoutRebuild(
    std::vector<db::models::Profileitem>* proxies,
    const std::vector<db::models::ProfileExItem>* exItems)
{
    proxies_ = proxies;
    exItems_ = exItems;
}

void ProxyListModel::setMaps(const utils::ProxyListMaps& maps) {
    delayMap_ = maps.delayMap;
    messageMap_ = maps.messageMap;
    failuresMap_ = maps.failuresMap;
    startCountMap_ = maps.startCountMap;
    runtimeMap_ = maps.runtimeMap;
    healthMap_ = maps.healthMap;
}

// -------------------------------------------------------------------
// Rebuild lookup maps from exItems_.
// NOTE: total_runtime_ms is only back-filled into ProfileExItem when a
// session ends (finalizeStop).  During an in-progress session the DB row
// may still carry 0, so we MUST NOT overwrite a non-zero runtimeMap_
// entry with 0 — otherwise the Runtime column loses its historical base
// on every refreshResults().
// -------------------------------------------------------------------
void ProxyListModel::rebuildMaps() {
    delayMap_.clear();
    messageMap_.clear();
    failuresMap_.clear();
    startCountMap_.clear();
    healthMap_.clear();

    if (!exItems_) {
        runtimeMap_.clear();
        return;
    }

    for (const db::models::ProfileExItem& ex : *exItems_) {
        delayMap_[ex.indexid] = ex.delay;
        messageMap_[ex.indexid] = ex.message;
        failuresMap_[ex.indexid] = ex.consecutive_failures;

        startCountMap_[ex.indexid] = ex.start_count;

        // Preserve existing historical runtime when the running session
        // hasn't been committed back to ProfileExItem yet (still 0).
        if (ex.total_runtime_ms > 0) {
            runtimeMap_[ex.indexid] = ex.total_runtime_ms;
        } else if (runtimeMap_.find(ex.indexid) == runtimeMap_.end()) {
            runtimeMap_[ex.indexid] = 0;
        }
        // else: keep the previous non-zero runtimeMap_ entry.

        int stable = ex.start_count - ex.crash_count;
        if (stable < 0) stable = 0;
        if (ex.start_count == 0) {
            healthMap_[ex.indexid] = 0.0;
        } else {
            healthMap_[ex.indexid] = static_cast<double>(stable + 1) /
                                     static_cast<double>(ex.start_count + 2);
        }
    }
}

// -------------------------------------------------------------------
bool ProxyListModel::setRunningDurations(
    const std::unordered_map<std::string, long long>& runningMs) {
    // Whole-map replacement: runningMs is the current snapshot of
    // in-progress sessions (absolute heartbeat durations), so assigning
    // instead of accumulating keeps the Runtime column idempotent across
    // repeated periodic refreshes.
    bool changed = (runningDurations_ != runningMs);
    runningDurations_ = runningMs;

    // Recompute health with a running-time bonus so the score varies while
    // the proxy is running: base (Bayesian smoothing) + min(elapsed/30min,1)*0.3,
    // capped at 1.0.
    // NOTE: health must be refreshed even when the running snapshot is
    // unchanged, because rebuildMaps() may have replaced exItems_ in the
    // meantime (e.g. refreshResults reloads DB rows while a session is
    // still in progress).
    if (exItems_) {
        const long long RAMP_MS = 1800000LL;   // 30 minutes to reach max bonus
        const double WEIGHT = 0.3;
        for (const db::models::ProfileExItem& ex : *exItems_) {
            long long running = 0;
            std::unordered_map<std::string, long long>::const_iterator rit =
                runningDurations_.find(ex.indexid);
            if (rit != runningDurations_.end()) {
                running = rit->second;
            }
            int stable = ex.start_count - ex.crash_count;
            if (stable < 0) stable = 0;
            double base = 0.0;
            double bonus = 0.0;
            if (ex.start_count > 0) {
                base = static_cast<double>(stable + 1) /
                       static_cast<double>(ex.start_count + 2);
                if (running > 0) {
                    double ramp = static_cast<double>(running) /
                                  static_cast<double>(RAMP_MS);
                    if (ramp > 1.0) ramp = 1.0;
                    bonus = ramp * WEIGHT;
                }
            }
            double h = base + bonus;
            if (h > 1.0) h = 1.0;
            healthMap_[ex.indexid] = h;
        }
    }

    if (!changed) {
        // No data change (e.g. no in-progress session): the caller can
        // skip the view redraw entirely.
        return false;
    }
    return true;
}

// -------------------------------------------------------------------
bool ProxyListModel::updateResultFor(const std::string& indexId,
                                     const std::string& delay,
                                     const std::string& message,
                                     int failures) {
    bool changed = false;
    std::unordered_map<std::string, std::string>::iterator it = delayMap_.find(indexId);
    const bool known = it != delayMap_.end();
    if (!known || it->second != delay) {
        delayMap_[indexId] = delay;
        changed = true;
    }
    std::unordered_map<std::string, std::string>::iterator mIt = messageMap_.find(indexId);
    if (mIt == messageMap_.end() || mIt->second != message) {
        messageMap_[indexId] = message;
        changed = true;
    }
    std::unordered_map<std::string, int>::iterator fIt = failuresMap_.find(indexId);
    if (fIt == failuresMap_.end() || fIt->second != failures) {
        failuresMap_[indexId] = failures;
        changed = true;
    }
    return changed;
}

// -------------------------------------------------------------------
// ItemChanged() is a public member of wxDataViewModel (dataview.h), so the
// model can notify the view about a single item directly.  The view then
// refreshes exactly that row (and re-sorts it if a sort order is active).
void ProxyListModel::notifyTestResultChangedFor(const std::string& indexId) {
    const int row = findRowByIndexId(indexId);
    if (row < 0) {
        return;
    }
    // Bit-cast the row back to the same item ID the model handed to the
    // view (m_hash entries are row + idOffset_, see detectIdOffset()).
    const wxDataViewItem item = wxDataViewItem(
        reinterpret_cast<void*>(static_cast<wxUIntPtr>(
            static_cast<unsigned int>(row) + idOffset_)));
    ItemChanged(item);
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
    startCountMap_.clear();
    runtimeMap_.clear();
    healthMap_.clear();
    runningDurations_.clear();
    idOffset_ = 0;
}

// -------------------------------------------------------------------
unsigned int ProxyListModel::GetCount() const {
    unsigned int c = proxies_ ? static_cast<unsigned int>(proxies_->size()) : 0;
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

    const db::models::Profileitem& p = (*proxies_)[dataIdx];
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
            std::unordered_map<std::string, std::string>::const_iterator it = delayMap_.find(idx);
            variant = wxVariant(it != delayMap_.end() ? it->second : "-");
            break;
        }
        case COL_FAILURES: {
            std::unordered_map<std::string, int>::const_iterator it = failuresMap_.find(idx);
            variant = wxVariant(std::to_string(it != failuresMap_.end() ? it->second : 0));
            break;
        }
        case COL_REMARKS:
            variant = wxVariant(p.remarks);
            break;
        case COL_MESSAGE: {
            std::unordered_map<std::string, std::string>::const_iterator it = messageMap_.find(idx);
            variant = wxVariant(it != messageMap_.end() ? it->second : "");
            break;
        }
        case COL_REGION:
            variant = wxVariant(p.region);
            break;
        case COL_INDEXID:
            variant = wxVariant(idx);
            break;
        case COL_START_COUNT: {
            std::unordered_map<std::string, int>::const_iterator it = startCountMap_.find(idx);
            variant = wxVariant(std::to_string(it != startCountMap_.end() ? it->second : 0));
            break;
        }
        case COL_TOTAL_RUNTIME_MS: {
            long long base = 0;
            std::unordered_map<std::string, long long>::const_iterator it =
                runtimeMap_.find(idx);
            if (it != runtimeMap_.end()) {
                base = it->second;
            }
            long long running = 0;
            std::unordered_map<std::string, long long>::const_iterator rit =
                runningDurations_.find(idx);
            if (rit != runningDurations_.end()) {
                running = rit->second;
            }
            long long totalMs = base + running;
            long long totalSec = totalMs / 1000;
            long long minutes = totalSec / 60;
            long long secs = totalSec % 60;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld:%02lld",
                          static_cast<long long>(minutes),
                          static_cast<long long>(secs));
            variant = wxVariant(buf);
            break;
        }
        case COL_HEALTH: {
            std::unordered_map<std::string, double>::const_iterator it = healthMap_.find(idx);
            double h = it != healthMap_.end() ? it->second : 0.0;
            variant = wxVariant(wxString::Format("%.3f", h));
            break;
        }
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

    const db::models::Profileitem& a = (*proxies_)[idx1];
    const db::models::Profileitem& b = (*proxies_)[idx2];

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
            std::unordered_map<std::string, std::string>::const_iterator itA = delayMap_.find(a.indexid);
            std::unordered_map<std::string, std::string>::const_iterator itB = delayMap_.find(b.indexid);
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
            std::unordered_map<std::string, int>::const_iterator itA = failuresMap_.find(a.indexid);
            std::unordered_map<std::string, int>::const_iterator itB = failuresMap_.find(b.indexid);
            if (itA != failuresMap_.end()) fA = itA->second;
            if (itB != failuresMap_.end()) fB = itB->second;
            cmp = (fA > fB) - (fA < fB);
            break;
        }
        case COL_MESSAGE: {
            std::string mA, mB;
            std::unordered_map<std::string, std::string>::const_iterator itA = messageMap_.find(a.indexid);
            std::unordered_map<std::string, std::string>::const_iterator itB = messageMap_.find(b.indexid);
            if (itA != messageMap_.end()) mA = itA->second;
            if (itB != messageMap_.end()) mB = itB->second;
            cmp = db::models::ProfileExItemDAO::compareMessage(mA, mB);
            break;
        }
        case COL_REMARKS:
            cmp = a.remarks.compare(b.remarks);
            break;
        case COL_REGION:
            cmp = a.region.compare(b.region);
            break;
        case COL_INDEXID:
            cmp = a.indexid.compare(b.indexid);
            break;
        case COL_START_COUNT: {
            int sA = 0, sB = 0;
            auto itA = startCountMap_.find(a.indexid);
            auto itB = startCountMap_.find(b.indexid);
            if (itA != startCountMap_.end()) sA = itA->second;
            if (itB != startCountMap_.end()) sB = itB->second;
            cmp = (sA > sB) - (sA < sB);
            break;
        }
        case COL_TOTAL_RUNTIME_MS: {
            long long rA = getRuntime(a.indexid);
            long long rB = getRuntime(b.indexid);
            cmp = (rA > rB) - (rA < rB);
            break;
        }
        case COL_HEALTH: {
            double hA = 0.0, hB = 0.0;
            auto itA = healthMap_.find(a.indexid);
            auto itB = healthMap_.find(b.indexid);
            if (itA != healthMap_.end()) hA = itA->second;
            if (itB != healthMap_.end()) hB = itB->second;
            cmp = (hA > hB) - (hA < hB);
            break;
        }
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
    std::unordered_map<std::string, std::string>::const_iterator it = delayMap_.find(indexId);
    return it != delayMap_.end() ? it->second : "";
}

// -------------------------------------------------------------------
std::string ProxyListModel::getMessage(const std::string& indexId) const {
    std::unordered_map<std::string, std::string>::const_iterator it = messageMap_.find(indexId);
    return it != messageMap_.end() ? it->second : "";
}

// -------------------------------------------------------------------
int ProxyListModel::getFailures(const std::string& indexId) const {
    std::unordered_map<std::string, int>::const_iterator it = failuresMap_.find(indexId);
    return it != failuresMap_.end() ? it->second : 0;
}

// -------------------------------------------------------------------
std::string ProxyListModel::getProxyValidityReason(const std::string& indexId) const {
    std::string delay = getDelay(indexId);
    if (utils::isDelayValid(delay)) {
        return "";
    }
    if (delay.empty() || delay == "-1") {
        return "untested";
    }
    return "invalid";
}

// -------------------------------------------------------------------
long long ProxyListModel::getRuntime(const std::string& indexId) const {
    long long base = 0;
    std::unordered_map<std::string, long long>::const_iterator it =
        runtimeMap_.find(indexId);
    if (it != runtimeMap_.end()) {
        base = it->second;
    }
    long long running = 0;
    std::unordered_map<std::string, long long>::const_iterator rit =
        runningDurations_.find(indexId);
    if (rit != runningDurations_.end()) {
        running = rit->second;
    }
    return base + running;
}

// -------------------------------------------------------------------
double ProxyListModel::getHealth(const std::string& indexId) const {
    std::unordered_map<std::string, double>::const_iterator it =
        healthMap_.find(indexId);
    return it != healthMap_.end() ? it->second : 0.0;
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
void ProxyListModel::notifyHistoryChanged() {
    unsigned int count = GetCount();
    for (unsigned int i = 0; i < count; ++i) {
        std::string indexId = getIndexIdAtRow(i);
        if (indexId.empty()) {
            continue;
        }
        // 仅通知有 standalone 运行历史（start_count > 0）的行，
        // 避免对全库（数万行）逐一 ValueChanged 造成 UI 线程卡死。
        std::unordered_map<std::string, int>::const_iterator it = startCountMap_.find(indexId);
        if (it == startCountMap_.end() || it->second <= 0) {
            continue;
        }
        wxDataViewItem item = GetItem(i);
        ValueChanged(item, COL_START_COUNT);
        ValueChanged(item, COL_TOTAL_RUNTIME_MS);
        ValueChanged(item, COL_HEALTH);
    }
}

// -------------------------------------------------------------------
// Notify only the rows that have an in-progress (running) session and
// actually changed.  Runtime and Health vary while a proxy is running;
// Starts remains historical/cumulated.
// -------------------------------------------------------------------
void ProxyListModel::notifyRunningChanged() {
    unsigned int count = GetCount();
    for (unsigned int i = 0; i < count; ++i) {
        std::string indexId = getIndexIdAtRow(i);
        if (indexId.empty()) {
            continue;
        }
        if (runningDurations_.find(indexId) == runningDurations_.end()) {
            continue;  // 无 running 会话 → 跳过
        }
        wxDataViewItem item = GetItem(i);
        ValueChanged(item, COL_TOTAL_RUNTIME_MS);
        ValueChanged(item, COL_HEALTH);
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


