#ifndef SERVICE_AUTO_TASK_SERVICE_H
#define SERVICE_AUTO_TASK_SERVICE_H

#include <string>
#include <sqlite3.h>
#include <functional>
#include <atomic>
#include "ConfigReader.h"
#include "NetworkMonitor.h"

namespace service {

class AutoTaskService {
public:
    AutoTaskService(sqlite3* db, const config::AppConfig& config,
                   std::atomic<bool>* cancel, NetworkMonitor* netMon);

    bool run(const std::vector<std::string>& steps);
    bool resume();

private:
    sqlite3* db_;
    config::AppConfig config_;
    std::atomic<bool>* cancel_;
    NetworkMonitor* netMon_;
};

} // namespace service

#endif // SERVICE_AUTO_TASK_SERVICE_H