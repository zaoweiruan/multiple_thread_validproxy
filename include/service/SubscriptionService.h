#ifndef SERVICE_SUBSCRIPTION_SERVICE_H
#define SERVICE_SUBSCRIPTION_SERVICE_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include "Subitem.h"

namespace service {

class SubscriptionService {
public:
    SubscriptionService(sqlite3* db);

    std::vector<db::models::Subitem> loadSubscriptions() const;
    bool updateEnabled(const std::string& id, bool enabled);
    bool updateSubitem(const db::models::Subitem& sub);
    bool deleteSubscription(const std::string& subId);
    bool deleteProxiesBySubId(const std::string& subId);
    bool importSubscription(const std::string& url);

private:
    sqlite3* db_;
};

} // namespace service

#endif // SERVICE_SUBSCRIPTION_SERVICE_H