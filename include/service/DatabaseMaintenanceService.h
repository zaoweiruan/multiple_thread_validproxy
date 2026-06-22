#ifndef SERVICE_DATABASE_MAINTENANCE_SERVICE_H
#define SERVICE_DATABASE_MAINTENANCE_SERVICE_H

#include <string>
#include <sqlite3.h>
#include "ConfigReader.h"

namespace service {

class DatabaseMaintenanceService {
public:
    DatabaseMaintenanceService(sqlite3* db, const config::AppConfig& config);

    bool deduplicate();
    bool syncDatabases(const std::string& src = "", const std::string& dst = "");

private:
    sqlite3* db_;
    config::AppConfig config_;
};

} // namespace service

#endif // SERVICE_DATABASE_MAINTENANCE_SERVICE_H