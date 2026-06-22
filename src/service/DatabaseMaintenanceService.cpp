#include "service/DatabaseMaintenanceService.h"
#include "SubitemUpdaterV2.h"
#include "Logger.h"

namespace service {

DatabaseMaintenanceService::DatabaseMaintenanceService(sqlite3* db, const config::AppConfig& config)
    : db_(db), config_(config) {
}

bool DatabaseMaintenanceService::deduplicate() {
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_);
    return updater.deduplicate();
}

bool DatabaseMaintenanceService::syncDatabases(const std::string& src, const std::string& dst) {
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_);
    return updater.syncDatabases(src.empty() ? config_.sync.source_db : src,
                                 dst.empty() ? config_.sync.target_db : dst);
}

} // namespace service