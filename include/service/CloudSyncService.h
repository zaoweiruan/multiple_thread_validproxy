// include/service/CloudSyncService.h - orchestrates WebDAV cloud sync.
//
// Deliberately free of wxWidgets so it can be unit tested and reused by the CLI.
#ifndef SERVICE_CLOUD_SYNC_SERVICE_H
#define SERVICE_CLOUD_SYNC_SERVICE_H

#include <string>
#include <vector>

#include "Subitem.h"
#include "net/WebDavClient.h"

namespace service {

// What to do with one remote/local pair when both sides exist.
enum class SyncDecision { Skip, Pull, Push, Conflict };

struct SubscriptionRecord {
    std::string id;
    std::string remarks;
    std::string url;
    std::string moreUrl;
    std::string userAgent;
    std::string filter;
    std::string convertTarget;
    std::string prevProfile;
    std::string nextProfile;
    std::string memo;
    int enabled = 1;
    int sort = 0;
    int autoUpdateInterval = 0;
    long long updateTime = 0;
    int preSocksPort = 0;
};

struct CloudSyncResult {
    bool ok = false;
    std::string error;
    int added = 0;
    int updated = 0;
    int skipped = 0;
    std::string detail;
};

class CloudSyncService {
public:
    CloudSyncService(net::WebDavClient& client, std::string remoteDir,
                     std::string localConfigPath, db::models::SubitemDAO& subDao);

    // R1: config.json is missing locally -> fetch from the cloud and write it.
    CloudSyncResult ensureLocalConfig();

    // Same as ensureLocalConfig(), but usable without a database connection.
    static CloudSyncResult pullConfigOnly(net::WebDavClient& client,
                                          const std::string& remoteDir,
                                          const std::string& localConfigPath);

    // R2: pull subscriptions.json and upsert into SubItem (never deletes).
    CloudSyncResult pullSubscriptions();

    // R3: upload config.json and subscriptions.json.
    CloudSyncResult pushConfig();
    CloudSyncResult pushSubscriptions();

    // ---- pure helpers (unit-tested, no network / no sqlite) ----
    static SyncDecision decide(long long localEpoch, long long remoteEpoch);
    static long long parseHttpDate(const std::string& httpDate);
    static std::string encodeSubscriptions(const std::vector<SubscriptionRecord>& items);
    static bool decodeSubscriptions(const std::string& json,
                                    std::vector<SubscriptionRecord>& out,
                                    std::string& error);
    static SubscriptionRecord fromSubitem(const db::models::Subitem& row);
    static db::models::Subitem toSubitem(const SubscriptionRecord& rec);
    static std::string remotePathFor(const std::string& remoteDir, const std::string& name);

    static constexpr long long kClockToleranceSeconds = 2;

private:
    // Returns true when the remote directory was created or already exists.
    bool ensureRemoteDir(std::string& error) const;

    net::WebDavClient& client_;
    std::string remoteDir_;
    std::string localConfigPath_;
    db::models::SubitemDAO& subDao_;
};

} // namespace service

#endif // SERVICE_CLOUD_SYNC_SERVICE_H
