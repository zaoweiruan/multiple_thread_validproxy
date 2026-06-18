#ifndef SUBSCRIPTION_UPDATER_H
#define SUBSCRIPTION_UPDATER_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include <optional>
#include <atomic>
#include "Subitem.h"
#include "ConfigReader.h"

class NetworkMonitor;

namespace update {

class SubscriptionUpdater {
public:
    SubscriptionUpdater(sqlite3* db,
                       const std::string& xrayPath,
                       const config::AppConfig& config,
                       std::atomic<bool>* externalCancel = nullptr,
                       const NetworkMonitor* netMon = nullptr);

    enum class UpdateMethod {
        Accelerator,
        Proxy,
        Direct
    };

    std::vector<UpdateMethod> parseUpdateMethods(const std::vector<std::string>& methods);
    
    std::string fetchUrlViaAccelerator(const std::string& url);
    std::string fetchUrl(const std::string& url);
    std::string fetchUrlViaProxy(const std::string& url, int socksPort);

    bool updateWithMethods(const std::string& subUrl, const std::string& subId,
                           const std::vector<UpdateMethod>& methods);

private:
    bool isCancelled() const {
        return externalCancel_ && externalCancel_->load();
    }

    sqlite3* db_;
    std::string xrayPath_;
    config::AppConfig config_;
    std::atomic<bool>* externalCancel_{nullptr};
    const NetworkMonitor* netMon_{nullptr};
};

} // namespace update

#endif // SUBSCRIPTION_UPDATER_H