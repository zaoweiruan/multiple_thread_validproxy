#include "SubscriptionListModel.h"
#include "Logger.h"

#include <ctime>

// -------------------------------------------------------------------
SubscriptionListModel::SubscriptionListModel() = default;

// -------------------------------------------------------------------
void SubscriptionListModel::setData(std::vector<db::models::Subitem>* subs,
                                    std::unordered_map<std::string, int>* proxyCounts,
                                    std::unordered_map<std::string, int>* validProxyCounts) {
    subscriptions_ = subs;
    proxyCounts_ = proxyCounts;
    validProxyCounts_ = validProxyCounts;
}

// -------------------------------------------------------------------
void SubscriptionListModel::clear() {
    subscriptions_ = nullptr;
    proxyCounts_ = nullptr;
    validProxyCounts_ = nullptr;
}

// -------------------------------------------------------------------
void SubscriptionListModel::detectIdOffset() {
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
unsigned int SubscriptionListModel::GetCount() const {
    return subscriptions_ ? static_cast<unsigned int>(subscriptions_->size()) : 0;
}

// -------------------------------------------------------------------
wxString SubscriptionListModel::GetColumnType(unsigned int col) const {
    // Toggle column requires "bool" type for proper editing
    if (col == SUB_COL_ENABLED) {
        return wxT("bool");
    }
    return wxT("string");
}

// -------------------------------------------------------------------
void SubscriptionListModel::GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const {
    if (!subscriptions_ || row >= subscriptions_->size()) {
        variant = wxVariant("");
        return;
    }
    const auto& sub = (*subscriptions_)[row];
    switch (col) {
        case SUB_COL_ROWNUM:
            variant = wxVariant(wxString::Format("%u", row + 1));
            break;
        case SUB_COL_ENABLED:
            variant = wxVariant(sub.enabled == "1");
            break;
        case SUB_COL_NAME:
            variant = wxVariant(sub.remarks);
            break;
        case SUB_COL_VALID: {
            int count = 0;
            if (validProxyCounts_) {
                auto it = validProxyCounts_->find(sub.id);
                if (it != validProxyCounts_->end()) count = it->second;
            }
            variant = wxVariant(wxString::Format("%d", count));
            break;
        }
        case SUB_COL_PROXIES: {
            int count = 0;
            if (proxyCounts_) {
                auto it = proxyCounts_->find(sub.id);
                if (it != proxyCounts_->end()) count = it->second;
            }
            variant = wxVariant(wxString::Format("%d", count));
            break;
        }
        case SUB_COL_UPDATE: {
            if (sub.updatetime.empty() || sub.updatetime == "0") {
                variant = wxVariant("Never");
            } else {
                try {
                    long ts = std::stol(sub.updatetime);
                    if (ts <= 0) {
                        variant = wxVariant("Never");
                    } else {
                        time_t t = static_cast<time_t>(ts);
                        struct tm tm_time;
                        localtime_s(&tm_time, &t);
                        char buf[32];
                        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_time);
                        variant = wxVariant(buf);
                    }
                } catch (...) {
                    variant = wxVariant(sub.updatetime);
                }
            }
            break;
        }
        case SUB_COL_ID:
            variant = wxVariant(sub.id);
            break;
        default:
            variant = wxVariant("");
    }
}

// -------------------------------------------------------------------
bool SubscriptionListModel::SetValueByRow(const wxVariant& variant, unsigned int row, unsigned int col) {
    if (!subscriptions_ || row >= subscriptions_->size())
        return false;
    if (col == SUB_COL_ENABLED) {
        // Handle enabled toggle - update the subscription's enabled field
        (*subscriptions_)[row].enabled = variant.GetBool() ? "1" : "0";
        return true;
    }
    if (col == SUB_COL_NAME) {
        (*subscriptions_)[row].remarks = variant.GetString().ToStdString();
        return true;
    }
    return false;
}

// -------------------------------------------------------------------
int SubscriptionListModel::Compare(const wxDataViewItem& item1,
                                  const wxDataViewItem& item2,
                                  unsigned int col, bool ascending) const {
    if (!subscriptions_ || subscriptions_->size() < 2)
        return 0;

    unsigned int idx1 = static_cast<unsigned int>(
        reinterpret_cast<wxUIntPtr>(item1.GetID()));
    unsigned int idx2 = static_cast<unsigned int>(
        reinterpret_cast<wxUIntPtr>(item2.GetID()));

    if (idx1 >= idOffset_) idx1 -= idOffset_;
    if (idx2 >= idOffset_) idx2 -= idOffset_;

    if (idx1 >= subscriptions_->size() || idx2 >= subscriptions_->size())
        return 0;

    const auto& a = (*subscriptions_)[idx1];
    const auto& b = (*subscriptions_)[idx2];

    int cmp = 0;
    switch (col) {
        case SUB_COL_NAME:
            cmp = a.remarks.compare(b.remarks);
            break;
        case SUB_COL_VALID: {
            int countA = 0, countB = 0;
            if (validProxyCounts_) {
                auto itA = validProxyCounts_->find(a.id);
                auto itB = validProxyCounts_->find(b.id);
                if (itA != validProxyCounts_->end()) countA = itA->second;
                if (itB != validProxyCounts_->end()) countB = itB->second;
            }
            cmp = (countA > countB) - (countA < countB);
            break;
        }
        case SUB_COL_PROXIES: {
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
        case SUB_COL_UPDATE: {
            auto getTimestamp = [](const std::string& ts) -> long long {
                if (ts.empty() || ts == "0") return -1;
                try { return std::stoll(ts); } catch (...) { return -1; }
            };
            long long tA = getTimestamp(a.updatetime);
            long long tB = getTimestamp(b.updatetime);
            cmp = (tA > tB) - (tA < tB);
            break;
        }
        case SUB_COL_ID: {
            auto toNum = [](const std::string& s) -> long long {
                try { return std::stoll(s); } catch (...) { return -1; }
            };
            long long nA = toNum(a.id);
            long long nB = toNum(b.id);
            cmp = (nA > nB) - (nA < nB);
            break;
        }
        default:
            cmp = static_cast<int>(idx1 - idx2);
    }

    return ascending ? cmp : -cmp;
}