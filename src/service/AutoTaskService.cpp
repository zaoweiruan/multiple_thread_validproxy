#include "service/AutoTaskService.h"
#include "AutoTaskManager.h"
#include "Logger.h"

namespace service {

AutoTaskService::AutoTaskService(sqlite3* db, const config::AppConfig& config,
                                std::atomic<bool>* cancel, NetworkMonitor* netMon)
    : db_(db), config_(config), cancel_(cancel), netMon_(netMon) {
}

bool AutoTaskService::run(const std::vector<std::string>& steps) {
    std::string baseDir = "";
    AutoTaskManager manager(db_, config_, baseDir, cancel_, netMon_);
    return manager.run(steps);
}

bool AutoTaskService::resume() {
    std::string baseDir = "";
    AutoTaskManager manager(db_, config_, baseDir, cancel_, netMon_);
    return manager.resume();
}

} // namespace service