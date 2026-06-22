#ifndef TEST_PROXY_BATCH_QUERY_H
#define TEST_PROXY_BATCH_QUERY_H

#include <string>
#include <vector>
#include "Profileitem.h"
#include "ConfigGenerator.h"

namespace test {

class ProxyBatchQuery {
public:
    ProxyBatchQuery(sqlite3* db, const std::string& sqlQuery = "", const std::string& sqlBySubId = "", int blacklistThreshold = 0)
        : db_(db), sqlQuery_(sqlQuery), sqlBySubId_(sqlBySubId), blacklistThreshold_(blacklistThreshold) {}

    std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "") const;

private:
    sqlite3* db_;
    std::string sqlQuery_;
    std::string sqlBySubId_;
    int blacklistThreshold_;

    std::string substituteTemplate(const std::string& tmpl, const std::string& placeholder, const std::string& value) const;
};

} // namespace test

#endif // TEST_PROXY_BATCH_QUERY_H
