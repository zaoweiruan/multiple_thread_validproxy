#ifndef SUBITEM_UPDATER_V2_H
#define SUBITEM_UPDATER_V2_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include <fstream>
#include <iostream>
#include <optional>
#include <atomic>

#include "Subitem.h"
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "XrayManager.h"
#include "ProxyFinder.h"
#include "ConfigReader.h"

namespace update {

class SubitemUpdaterV2 {
public:
    SubitemUpdaterV2(sqlite3* db,
                    const std::string& xrayPath,
                    const config::AppConfig& config,
                    std::ofstream* logOut = nullptr,
                    const std::string& baseDir = "",
                    std::atomic<bool>* externalCancel = nullptr);

    ~SubitemUpdaterV2() {
        cleanupXray();
    }

    bool run();
    bool runSingle(const std::string& subId);
    bool runSingleWithProxy(const std::string& subId, int socksPort);
    bool deduplicate();

    // Sync databases - migrate valid proxies from source to target
    bool syncDatabases(const std::string& sourceDbPath, 
                       const std::string& targetDbPath);

    // Import subitems from file (batch import)
    bool importSubitemsFromFile(const std::string& filePath, 
                                const std::string& baseDir = "");

    // Import single URL directly
    bool importSingleUrl(const std::string& url);

private:
    enum class UpdateMethod {
        Accelerator,
        Proxy,
        Direct
    };
    
    std::vector<UpdateMethod> parseUpdateMethods(const std::vector<std::string>& methods);
    std::string fetchUrlViaAccelerator(const std::string& url);
    bool updateWithMethods(const std::string& subUrl, const std::string& subId,
                           const std::vector<UpdateMethod>& methods);
    std::string fetchUrl(const std::string& url);
    
    std::optional<db::models::Subitem> getSubscription(const std::string& subId);
    std::string fetchUrlViaProxy(const std::string& url, int socksPort);
    
    std::vector<db::models::Profileitem> parseSubscription(const std::string& content, const std::string& subid);
    bool updateProfileItems(const std::string& subid, const std::vector<db::models::Profileitem>& profiles);
    
    std::pair<int, int> getProxyPorts(const std::string& targetUrl = "");
    void releaseProxyPorts();
    
    bool startXray(const std::string& indexId, int socksPort, int apiPort);
    void cleanupXray();
    
    int deduplicatePhase0();
    int deduplicatePhase1();
    int deduplicateMergedPhase();
    int deduplicateBlacklistPhase();  // Move proxies with consecutive_failures >= threshold to blacklist subid
    void cleanupProfileExItem();
    
    bool shouldSkipUpdate(const db::models::Subitem& sub) const;
    std::string getCurrentTimestamp();
    
    // Helper methods for sync
    bool migrateSubscription(sqlite3* srcDb, sqlite3* dstDb, 
                            const std::string& subid);
    bool migrateProxy(sqlite3* srcDb, sqlite3* dstDb, 
                     const db::models::Profileitem& proxy);
    bool migrateProfileExItem(sqlite3* srcDb, sqlite3* dstDb, 
                               const std::string& indexid);
    std::string decodeBase64(const std::string& input);
    std::string urlDecode(const std::string& input);
    std::pair<std::string, std::string> parseAddressPort(const std::string& addrPart);
    
    bool execSql(const std::string& sql, const std::string& errorContext);

    bool isCancelled() const {
        return externalCancel_ && externalCancel_->load();
    }

    // Helper methods for import
    std::string extractRemarksFromUrl(const std::string& url);
    int getNextSortValue();
    bool isUrlExists(const std::string& url);
    bool hasValidPath(const std::string& url);

    sqlite3* db_;
    std::string xrayPath_;
    config::AppConfig config_;
    std::ofstream* logOut_;
    std::string baseDir_;

    XrayManager* xrayMgr_;
    ProxyFinder* proxyFinder_;

    int xrayProcessId_;
    HANDLE xrayJob_;
    std::atomic<bool>* externalCancel_{nullptr};
};

} // namespace update

#endif // SUBITEM_UPDATER_V2_H