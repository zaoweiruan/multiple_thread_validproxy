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
#include "update/SubscriptionParser.h"
#include "update/SubscriptionUpdater.h"
#include "update/Deduplicator.h"
#include "update/Importer.h"

class NetworkMonitor;

namespace update {

class SubitemUpdaterV2 {
public:
    SubitemUpdaterV2(sqlite3* db,
                    const std::string& xrayPath,
                    const config::AppConfig& config,
                    std::ofstream* logOut = nullptr,
                    const std::string& baseDir = "",
                    std::atomic<bool>* externalCancel = nullptr,
                    const NetworkMonitor* netMon = nullptr);

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
    // httpStatus (optional): receives the HTTP response code of the transfer.
    // 0 means "no response was obtained" (exception, timeout, DNS failure) and is
    // distinct from a real 4xx/5xx, so the caller can tell an upstream subscription
    // that is GONE (404) apart from a body that was fetched but could not be parsed.
    std::string fetchUrlViaAccelerator(const std::string& url, long* httpStatus = nullptr);
    bool updateWithMethods(const std::string& subUrl, const std::string& subId,
                           const std::vector<UpdateMethod>& methods);
    std::string fetchUrl(const std::string& url, long* httpStatus = nullptr);
    
    std::optional<db::models::Subitem> getSubscription(const std::string& subId);
    std::string fetchUrlViaProxy(const std::string& url, int socksPort, long* httpStatus = nullptr);

    // ── 上游订阅失效（HTTP 404）标记机制 ─────────────────────────────
    // 判定源站已删除/改名后：Remarks 加 "404-" 前缀 + Enabled 置 0。
    // 这样失效订阅在 UI 列表里一眼可见（备注前缀）且不再参与自动更新
    // （Enabled=0 会让它退出 getEnabledSubscriptions()）。
    // 但「上游可能恢复」，所以 run() 仍会定期探测这批订阅：一旦某次
    // 更新解析出节点，就自动摘掉前缀并把 Enabled 恢复原值。
    // 标记是幂等的：重复标记不会叠成 "404-404-"，探测成功后的去前缀
    // 也只针对当前确实带前缀的行。
    static constexpr const char* kSourceGonePrefix = "404-";
    static bool hasSourceGoneMarker(const std::string& remarks);
    void markSourceGone(const std::string& subId, const std::string& remarks);
    void clearSourceGoneMarker(const std::string& subId, const std::string& remarks);
    std::vector<db::models::Subitem> getSourceGoneSubscriptions();

    // Prepared-statement helper for the two text parameters of the marker
    // updates. Remarks is user-supplied free text, so it must never be
    // interpolated into SQL text (an apostrophe in a remark would either
    // break the statement or inject into it).
    bool execSqlBind(const std::string& sql, const std::string& first, const std::string& second);
    
    std::vector<db::models::Profileitem> parseSubscription(const std::string& content, const std::string& subid);
    bool updateProfileItems(const std::string& subid, const std::vector<db::models::Profileitem>& profiles);
    
    std::pair<int, int> getProxyPorts(const std::string& targetUrl = "");
    void releaseProxyPorts();
    
    bool startXray(const std::string& indexId, int socksPort, int apiPort);
    void cleanupXray();
    
    bool shouldSkipUpdate(const db::models::Subitem& sub) const;
    std::string getCurrentTimestamp();
    
    bool migrateSubscription(sqlite3* srcDb, sqlite3* dstDb, 
                             const std::string& subid);
    bool migrateProxy(sqlite3* srcDb, sqlite3* dstDb, 
                     const db::models::Profileitem& proxy);
    bool migrateProfileExItem(sqlite3* srcDb, sqlite3* dstDb, 
                               const std::string& indexid);
    
    bool execSql(const std::string& sql, const std::string& errorContext);

    bool isCancelled() const {
        return externalCancel_ && externalCancel_->load();
    }

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
    const NetworkMonitor* netMon_{nullptr};
};

} // namespace update

#endif // SUBITEM_UPDATER_V2_H