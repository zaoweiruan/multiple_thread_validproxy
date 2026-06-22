#include "test/ProxyBatchQuery.h"
#include "ConfigGenerator.h"
#include "Logger.h"
#include <algorithm>

namespace test {

std::string ProxyBatchQuery::substituteTemplate(const std::string& tmpl, const std::string& placeholder, const std::string& value) const {
    std::string result = tmpl;
    size_t pos = 0;
    while ((pos = result.find(placeholder, pos)) != std::string::npos) {
        result.replace(pos, placeholder.length(), value);
        pos += value.length();
    }
    return result;
}

std::vector<db::models::Profileitem> ProxyBatchQuery::loadProxies(const std::string& subId) const {
    std::string sql;

    if (!subId.empty() && !sqlBySubId_.empty()) {
        sql = substituteTemplate(sqlBySubId_, "{subid}", subId);
    } else {
        sql = sqlQuery_;
    }

    std::string thresholdStr = std::to_string(blacklistThreshold_);
    sql = substituteTemplate(sql, "{blacklist_threshold}", thresholdStr);

    Logger::write("[ProxyBatchQuery] Executing SQL: " + sql, LogLevel::DEBUG);

    config::ConfigGenerator configGen(db_);
    return configGen.loadProfiles(sql);
}

} // namespace test
