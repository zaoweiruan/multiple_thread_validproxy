#include "service/SubscriptionService.h"
#include "Profileitem.h"
#include "Subitem.h"
#include "Logger.h"

namespace service {

SubscriptionService::SubscriptionService(sqlite3* db)
    : db_(db) {
}

std::vector<db::models::Subitem> SubscriptionService::loadSubscriptions() const {
    db::models::SubitemDAO dao(db_);
    return dao.getAll();
}

bool SubscriptionService::updateEnabled(const std::string& id, bool enabled) {
    db::models::SubitemDAO dao(db_);
    return dao.updateEnabled(id, enabled);
}

bool SubscriptionService::updateSubitem(const db::models::Subitem& sub) {
    db::models::SubitemDAO dao(db_);
    return dao.updateSubitem(sub);
}

bool SubscriptionService::deleteSubscription(const std::string& subId) {
    // Delete associated proxies first
    db::models::ProfileitemDAO proxyDao(db_);
    proxyDao.deleteBySubId(subId);

    db::models::SubitemDAO subDao(db_);
    return subDao.deleteById(subId);
}

bool SubscriptionService::deleteProxiesBySubId(const std::string& subId) {
    db::models::ProfileitemDAO proxyDao(db_);
    return proxyDao.deleteBySubId(subId);
}

bool SubscriptionService::importSubscription(const std::string& url) {
    // TODO: Requires SubitemUpdaterV2 integration with full config
    Logger::write("Subscription import requires SubitemUpdaterV2 context", LogLevel::DEBUG);
    return false;
}

} // namespace service