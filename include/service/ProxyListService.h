#ifndef SERVICE_PROXY_LIST_SERVICE_H
#define SERVICE_PROXY_LIST_SERVICE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <sqlite3.h>
#include "Profileitem.h"
#include "ProfileExItem.h"

namespace service {

class ProxyListService {
public:
    ProxyListService(sqlite3* db);

    std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
    std::unordered_map<std::string, int> countProxiesBySubId();
    std::unordered_map<std::string, int> countValidProxiesBySubId();
    std::vector<db::models::ProfileExItem> loadProxyResults();

private:
    sqlite3* db_;
};

} // namespace service

#endif // SERVICE_PROXY_LIST_SERVICE_H