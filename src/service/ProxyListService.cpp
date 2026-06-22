#include "service/ProxyListService.h"
#include "Profileitem.h"
#include "ProfileExItem.h"

namespace service {

ProxyListService::ProxyListService(sqlite3* db)
    : db_(db) {
}

std::vector<db::models::Profileitem> ProxyListService::loadProxies(const std::string& subId) {
    db::models::ProfileitemDAO dao(db_);
    if (subId.empty()) {
        return dao.getAll();
    }
    std::vector<db::models::Profileitem> all = dao.getAll();
    std::vector<db::models::Profileitem> filtered;
    for (const db::models::Profileitem& p : all) {
        if (p.subid == subId) {
            filtered.push_back(p);
        }
    }
    return filtered;
}

std::unordered_map<std::string, int> ProxyListService::countProxiesBySubId() {
    db::models::ProfileitemDAO dao(db_);
    return dao.countBySubId();
}

std::unordered_map<std::string, int> ProxyListService::countValidProxiesBySubId() {
    db::models::ProfileitemDAO dao(db_);
    return dao.countValidBySubId();
}

std::vector<db::models::ProfileExItem> ProxyListService::loadProxyResults() {
    db::models::ProfileExItemDAO dao(db_);
    return dao.getAll();
}

} // namespace service