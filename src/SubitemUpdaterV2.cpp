#include "SubitemUpdaterV2.h"
#include "Subitem.h"
#include "Profileitem.h"
#include "Profileexitem.h"
#include "ConfigGenerator.h"
#include "XrayApi.h"
#include "PortManager.h"
#include "Utils.h"
#include "Logger.h"
#include "CurlEasyHandle.h"
#include "NetworkMonitor.h"

#include <curl/curl.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <array>
#include <functional>
#include <cstdint>
#include <random>
#include <windows.h>

namespace update {

namespace {

    void bindTextOrNull(sqlite3_stmt* stmt, int idx, const std::string& val) {
        if (val.empty()) {
            sqlite3_bind_null(stmt, idx);
        } else {
            sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
        }
    }


    // Pre-filter invalid proxies using checkRequired() + network validation
    bool isValidProxy(const db::models::Profileitem& p) {
        try {
            p.checkRequired();
        } catch (const std::exception& e) {
            Logger::write("SKIP: " + p.address + ":" + p.port + " - " + e.what(), LogLevel::INFO);
            return false;
        }
        if (!p.network.empty() && !utils::isValidNetwork(p.network)) {
            Logger::write("SKIP: " + p.address + ":" + p.port + " - invalid network: '" + p.network + "'", LogLevel::WARN);
            return false;
        }
        return true;
    }

    bool insertSubItem(sqlite3* db, const db::models::Subitem& subitem) {
        std::string sql = "INSERT INTO SubItem (Id, Remarks, Url, MoreUrl, Enabled, "
                         "UserAgent, Sort, Filter, AutoUpdateInterval, UpdateTime, "
                         "ConvertTarget, PrevProfile, NextProfile, PreSocksPort, Memo) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
        
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("ERROR: insertSubItem prepare failed: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
            return false;
        }
        
        // 必须字段：不允许 NULL
        sqlite3_bind_text(stmt, 1, subitem.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, subitem.remarks.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, subitem.url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, subitem.enabled.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, subitem.sort.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, subitem.autoupdateinterval.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, subitem.updatetime.c_str(), -1, SQLITE_TRANSIENT);
        
        // 可选字段：空字符串绑定为 NULL
        bindTextOrNull(stmt, 4, subitem.moreurl);
        bindTextOrNull(stmt, 6, subitem.useragent);
        bindTextOrNull(stmt, 8, subitem.filter);
        bindTextOrNull(stmt, 11, subitem.converttarget);
        bindTextOrNull(stmt, 12, subitem.prevprofile);
        bindTextOrNull(stmt, 13, subitem.nextprofile);
        bindTextOrNull(stmt, 14, subitem.presocksport);
        bindTextOrNull(stmt, 15, subitem.memo);
        
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!success) {
            Logger::write("ERROR: insertSubItem failed for id=" + subitem.id + ": " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        }
        sqlite3_finalize(stmt);
        return success;
    }
}

SubitemUpdaterV2::SubitemUpdaterV2(sqlite3* db,
                                    const std::string& xrayPath,
                                    const config::AppConfig& config,
                                    std::ofstream* logOut,
                                    const std::string& baseDir,
                                    std::atomic<bool>* externalCancel,
                                    const NetworkMonitor* netMon)
    : db_(db), xrayPath_(xrayPath), config_(config), logOut_(logOut), baseDir_(baseDir),
      xrayMgr_(nullptr), proxyFinder_(nullptr), xrayProcessId_(0), xrayJob_(nullptr),
      externalCancel_(externalCancel), netMon_(netMon) {
}

bool SubitemUpdaterV2::run() {
    Logger::write("========================================", LogLevel::INFO);
    Logger::write("INFO: Starting SubitemUpdaterV2", LogLevel::INFO);
    Logger::write("========================================", LogLevel::INFO);

    std::vector<UpdateMethod> methods = parseUpdateMethods(config_.update_methods);
    std::string methodsLog;
    for (const UpdateMethod& m : methods) {
        if (!methodsLog.empty()) methodsLog += ", ";
        switch (m) {
            case UpdateMethod::Accelerator: methodsLog += "accelerator"; break;
            case UpdateMethod::Proxy: methodsLog += "proxy"; break;
            case UpdateMethod::Direct: methodsLog += "direct"; break;
        }
    }
    Logger::write("INFO: Update methods: " + methodsLog, LogLevel::INFO);

    db::models::SubitemDAO subDao(db_);
    std::vector<db::models::Subitem> enabledSubs = subDao.getEnabledSubscriptions();

    if (enabledSubs.empty()) {
        Logger::write("No enabled subscriptions found", LogLevel::WARN);
        return true;
    }

    Logger::write("INFO: Found " + std::to_string(enabledSubs.size()) + " enabled subscriptions", LogLevel::INFO);

    bool needProxy = false;
    for (const UpdateMethod& m : methods) {
        if (m == UpdateMethod::Proxy) {
            needProxy = true;
            break;
        }
    }

    int proxySocksPort = -1;
    int proxyApiPort = -1;
    (void)proxyApiPort;
    int totalSubs = enabledSubs.size();

    if (needProxy && !enabledSubs.empty()) {
        Logger::write("INFO: Pre-finding proxy...", LogLevel::INFO);
        std::pair<int, int> result = getProxyPorts(enabledSubs[0].url);
        proxySocksPort = result.first;
        proxyApiPort = result.second;
        if (proxySocksPort > 0) {
            Logger::write("INFO: Pre-found working proxy, socks=" + std::to_string(proxySocksPort), LogLevel::INFO);
        } else {
            Logger::write("WARN: Failed to find working proxy", LogLevel::WARN);
        }
    }

    int successCount = 0;
    int attemptedCount = 0;
    std::vector<std::tuple<std::string, std::string, std::string>> failedSubs;
    int phaseIndex = 0;
    int phaseCount = static_cast<int>(methods.size());

    for (const UpdateMethod& method : methods) {
        phaseIndex++;
        std::string methodName;
        switch (method) {
            case UpdateMethod::Accelerator: methodName = "accelerator"; break;
            case UpdateMethod::Proxy: methodName = "proxy"; break;
            case UpdateMethod::Direct: methodName = "direct"; break;
        }

        // First phase: process all enabled subs. Later phases: only failed subs.
        if (phaseIndex == 1 && enabledSubs.empty()) continue;
        if (phaseIndex > 1 && failedSubs.empty()) continue;

        size_t subsCount = (phaseIndex == 1) ? enabledSubs.size() : failedSubs.size();

        Logger::write("========================================", LogLevel::INFO);
        Logger::write("INFO: Phase " + std::to_string(phaseIndex) + "/" + std::to_string(phaseCount)
                       + " - " + methodName + " connection (" + std::to_string(subsCount) + " subs)", LogLevel::INFO);
        Logger::write("========================================", LogLevel::INFO);

        std::vector<std::tuple<std::string, std::string, std::string>> stillFailed;
        int phaseSuccess = 0;
        int phaseFail = 0;

        std::function<bool(const std::string&, const std::string&, const std::string&)> processSub = [&](const std::string& subId, const std::string& subRemarks,
                               const std::string& subUrl) {
            if (isCancelled()) return false;
            if (netMon_ && !netMon_->IsConnected()) {
                Logger::write("[SubitemUpdaterV2] network disconnected — aborting subscription update", LogLevel::WARN);
                return false;
            }

            Logger::write("INFO: " + methodName + ": " + subUrl, LogLevel::REPORT);

            std::string content;
            switch (method) {
                case UpdateMethod::Accelerator:
                    content = fetchUrlViaAccelerator(subUrl);
                    break;
                case UpdateMethod::Proxy:
                    if (proxySocksPort > 0) {
                        content = fetchUrlViaProxy(subUrl, proxySocksPort);
                    }
                    break;
                case UpdateMethod::Direct:
                    content = fetchUrl(subUrl);
                    break;
            }

            if (isCancelled()) return false;
            if (netMon_ && !netMon_->IsConnected()) {
                Logger::write("[SubitemUpdaterV2] network disconnected — aborting subscription update", LogLevel::WARN);
                return false;
            }

            if (!content.empty()) {
                Logger::write("INFO: " + methodName + " connection successful", LogLevel::INFO);
                std::vector<db::models::Profileitem> profiles = parseSubscription(content, subId);
                if (!profiles.empty()) {
                    updateProfileItems(subId, profiles);
                    successCount++;
                    phaseSuccess++;
                    Logger::write("INFO: Updated successfully: " + subId, LogLevel::INFO);
                    std::string newTime = getCurrentTimestamp();
                    std::string updateSql = "UPDATE SubItem SET UpdateTime = '" + newTime + "' WHERE Id = '" + subId + "'";
                    execSql(updateSql, "[SubitemUpdaterV2] SQL exec failed");
                } else {
                    phaseFail++;
                    stillFailed.push_back({subId, subRemarks, subUrl});
                    Logger::write("ERROR: Parse failed: " + subId, LogLevel::ERR);
                }
            } else {
                Logger::write(methodName + " connection failed", LogLevel::ERR);
                phaseFail++;
                stillFailed.push_back({subId, subRemarks, subUrl});
                Logger::write("ERROR: Failed to update: " + subId, LogLevel::ERR);
            }
            return true;
        };

        if (phaseIndex == 1) {
            for (size_t i = 0; i < enabledSubs.size(); ++i) {
                if (isCancelled()) {
                    Logger::write("INFO: Update cancelled by user during " + methodName + " phase", LogLevel::REPORT);
                    break;
                }
                if (netMon_ && !netMon_->IsConnected()) {
                    Logger::write("[SubitemUpdaterV2] network disconnected — aborting update", LogLevel::WARN);
                    break;
                }
                const db::models::Subitem& sub = enabledSubs[i];
                if (shouldSkipUpdate(sub)) {
                    Logger::write("INFO: Skipping sub " + sub.id + " (within update interval)", LogLevel::INFO);
                    continue;
                }
                Logger::write("[" + std::to_string(i + 1) + "/" + std::to_string(enabledSubs.size()) + "] " + methodName + ": " + sub.url, LogLevel::REPORT);
                attemptedCount++;
                if (!processSub(sub.id, sub.remarks, sub.url)) break;
            }
        } else {
            for (size_t i = 0; i < failedSubs.size(); ++i) {
                if (isCancelled()) {
                    Logger::write("INFO: Update cancelled by user during " + methodName + " phase", LogLevel::REPORT);
                    break;
                }
                if (netMon_ && !netMon_->IsConnected()) {
                    Logger::write("[SubitemUpdaterV2] network disconnected — aborting update", LogLevel::WARN);
                    break;
                }
                Logger::write("[" + std::to_string(i + 1) + "/" + std::to_string(failedSubs.size()) + "] " + methodName + ": " + std::get<2>(failedSubs[i]), LogLevel::REPORT);
                if (!processSub(std::get<0>(failedSubs[i]), std::get<1>(failedSubs[i]), std::get<2>(failedSubs[i]))) break;
            }
        }

        Logger::write("INFO: Phase " + std::to_string(phaseIndex) + " (" + methodName + "): "
                       + std::to_string(phaseSuccess) + " success, " + std::to_string(phaseFail) + " failed", LogLevel::INFO);

        if (phaseIndex == 1) {
            failedSubs = std::move(stillFailed);
        } else {
            failedSubs = std::move(stillFailed);
        }
    }

    releaseProxyPorts();

    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Update Summary", LogLevel::REPORT);
    Logger::write("Total subscriptions: " + std::to_string(totalSubs), LogLevel::REPORT);
    Logger::write("Update methods: " + methodsLog, LogLevel::REPORT);
    Logger::write("Total - Success: " + std::to_string(successCount) + ", Failed: " + std::to_string(totalSubs - successCount), LogLevel::REPORT);

    if (!failedSubs.empty()) {
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Failed subscriptions:", LogLevel::REPORT);
        for (const std::tuple<std::string, std::string, std::string>& sub : failedSubs) {
            Logger::write("  Id: " + std::get<0>(sub) + ", Remarks: " + std::get<1>(sub) + ", URL: " + std::get<2>(sub), LogLevel::REPORT);
        }
    }
    Logger::write("========================================", LogLevel::REPORT);

    if (config_.dedup_after_update && config_.dedup_enabled) {
        Logger::write("INFO: Running dedup after subscription update...", LogLevel::INFO);
        deduplicate();
    }

    if (successCount <= 0) {
        if (!enabledSubs.empty() && attemptedCount == 0) {
            Logger::write("All subscriptions skipped by update interval - nothing to update", LogLevel::ERR);
            return true;
        }
        if (!enabledSubs.empty()) {
            Logger::write("All subscriptions failed to update - check network connectivity", LogLevel::ERR);
        }
    }
    return successCount > 0;
}

bool SubitemUpdaterV2::runSingle(const std::string& subId) {
    Logger::write("INFO: runSingle - subId: " + subId, LogLevel::INFO);

    std::optional<db::models::Subitem> optSub = getSubscription(subId);
    if (!optSub) {
        Logger::write("ERROR: Subscription not found: " + subId, LogLevel::ERR);
        return false;
    }
    const db::models::Subitem& sub = *optSub;

    if (sub.enabled != "1") {
        Logger::write("ERROR: Subscription is disabled: " + subId, LogLevel::ERR);
        return false;
    }

    if (shouldSkipUpdate(sub)) {
        Logger::write("INFO: Skipping sub " + sub.id + " (within update interval)", LogLevel::INFO);
        return true;
    }

    std::vector<UpdateMethod> methods = parseUpdateMethods(config_.update_methods);
    if (isCancelled()) {
        Logger::write("INFO: Single update cancelled by user: " + subId, LogLevel::REPORT);
        return false;
    }
    if (netMon_ && !netMon_->IsConnected()) {
        Logger::write("[SubitemUpdaterV2] network disconnected — aborting single update: " + subId, LogLevel::WARN);
        return false;
    }
    bool result = updateWithMethods(sub.url, sub.id, methods);

    if (result) {
        std::string newTime = getCurrentTimestamp();
        std::string updateSql = "UPDATE SubItem SET UpdateTime = '" + newTime + "' WHERE Id = '" + sub.id + "'";
        execSql(updateSql, "[SubitemUpdaterV2] SQL exec failed");
    }

    releaseProxyPorts();

    if (config_.dedup_after_update && config_.dedup_enabled) {
        Logger::write("INFO: Running dedup after subscription update...", LogLevel::INFO);
        deduplicate();
    }

    return result;
}

bool SubitemUpdaterV2::runSingleWithProxy(const std::string& subId, int socksPort) {
    Logger::write("INFO: runSingleWithProxy - subId: " + subId + ", socksPort: " + std::to_string(socksPort), LogLevel::INFO);

    std::optional<db::models::Subitem> optSub = getSubscription(subId);
    if (!optSub) {
        Logger::write("ERROR: Subscription not found: " + subId, LogLevel::ERR);
        return false;
    }
    const db::models::Subitem& sub = *optSub;

    if (sub.enabled != "1") {
        Logger::write("ERROR: Subscription is disabled: " + subId, LogLevel::ERR);
        return false;
    }

    if (shouldSkipUpdate(sub)) {
        Logger::write("INFO: Skipping sub " + subId + " (within update interval)", LogLevel::INFO);
        return true;
    }

    if (isCancelled()) {
        Logger::write("INFO: Single proxy update cancelled by user: " + subId, LogLevel::REPORT);
        return false;
    }
    if (netMon_ && !netMon_->IsConnected()) {
        Logger::write("[SubitemUpdaterV2] network disconnected — aborting single proxy update: " + subId, LogLevel::WARN);
        return false;
    }
    std::string content = fetchUrlViaProxy(sub.url, socksPort);
    if (content.empty()) {
        Logger::write("Failed to fetch via proxy", LogLevel::INFO);
        return false;
    }

    std::vector<db::models::Profileitem> profiles = parseSubscription(content, sub.id);
    bool result = updateProfileItems(sub.id, profiles);
    // Only update UpdateTime when we actually fetched and parsed content successfully
    if (result) {
        std::string newTime = getCurrentTimestamp();
        std::string updateSql = "UPDATE SubItem SET UpdateTime = '" + newTime + "' WHERE Id = '" + sub.id + "'";
        execSql(updateSql, "[SubitemUpdaterV2] SQL exec failed");
    }
    return result;
}

bool SubitemUpdaterV2::execSql(const std::string& sql, const std::string& errorContext) {
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write(errorContext + ": " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

std::optional<db::models::Subitem> SubitemUpdaterV2::getSubscription(const std::string& subId) {
    std::string sql = "SELECT * FROM SubItem WHERE Id = '" + subId + "';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("ERROR: SQL prepare failed - " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return std::nullopt;
    }

    db::models::Subitem sub;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        sub = db::models::Subitem::fromStmt(stmt);
    }
    sqlite3_finalize(stmt);

    if (sub.id.empty()) {
        return std::nullopt;
    }
    return sub;
}

bool SubitemUpdaterV2::updateWithMethods(const std::string& subUrl, const std::string& subId,
                                          const std::vector<UpdateMethod>& methods) {
    for (const UpdateMethod& method : methods) {
        std::string methodName;
        switch (method) {
            case UpdateMethod::Accelerator: methodName = "accelerator"; break;
            case UpdateMethod::Proxy: methodName = "proxy"; break;
            case UpdateMethod::Direct: methodName = "direct"; break;
        }

        Logger::write("INFO: Trying " + methodName + " connection...", LogLevel::INFO);

        std::string content;
        switch (method) {
            case UpdateMethod::Accelerator:
                content = fetchUrlViaAccelerator(subUrl);
                break;
            case UpdateMethod::Proxy: {
                std::pair<int, int> proxyResult = getProxyPorts(subUrl);
                int socks = proxyResult.first;
                if (socks > 0) {
                    content = fetchUrlViaProxy(subUrl, socks);
                } else {
                    Logger::write("WARN: No proxy available for " + methodName, LogLevel::WARN);
                }
                break;
            }
            case UpdateMethod::Direct:
                content = fetchUrl(subUrl);
                break;
        }

        if (!content.empty()) {
            Logger::write("INFO: " + methodName + " connection successful", LogLevel::INFO);
            std::vector<db::models::Profileitem> profiles = parseSubscription(content, subId);
            if (!profiles.empty()) {
                return updateProfileItems(subId, profiles);
            }
        }
    }

    return false;
}

std::string SubitemUpdaterV2::fetchUrlViaAccelerator(const std::string& url) {
    if (config_.accelerator_url.empty()) {
        Logger::write("accelerator_url empty, falling back to direct fetch", LogLevel::ERR);
        return fetchUrl(url);
    }
    std::string joinedUrl = utils::joinUrl(config_.accelerator_url, url);
    Logger::write("INFO: Fetching via accelerator: " + joinedUrl, LogLevel::INFO);
    return fetchUrl(joinedUrl);
}

std::string SubitemUpdaterV2::fetchUrl(const std::string& url) {
    try {
        std::string response;
        CurlEasyHandle curl;
        curl.setUrl(url)
            .setWriteCallback(CurlEasyHandle::writeCallback, &response)
            .setFollowLocation()
            .setConnectTimeoutMs(config_.subscription_connect_timeout_ms)
            .setTimeoutMs(config_.subscription_timeout_ms)
            .setSslVerifyPeer(false)
            .setSslVerifyHost(false);

        curl.perform();
        return response;

    } catch (const std::exception& e) {
        Logger::write("fetchUrl failed - " + std::string(e.what()), LogLevel::ERR);
        return "";
    }
}

std::string SubitemUpdaterV2::fetchUrlViaProxy(const std::string& url, int socksPort) {
    try {
        std::string response;
        CurlEasyHandle curl;
        std::string proxyStr = "socks5h://127.0.0.1:" + std::to_string(socksPort);
        
        curl.setProxy(proxyStr)
            .setUrl(url)
            .setWriteCallback(CurlEasyHandle::writeCallback, &response)
            .setFollowLocation()
            .setConnectTimeoutMs(config_.subscription_connect_timeout_ms)
            .setTimeoutMs(config_.subscription_timeout_ms)
            .setSslVerifyPeer(false)
            .setSslVerifyHost(false);

        curl.perform();
        return response;

    } catch (const std::exception& e) {
        Logger::write("fetchUrlViaProxy failed - " + std::string(e.what()), LogLevel::INFO);
        return "";
    }
}

std::vector<db::models::Profileitem> SubitemUpdaterV2::parseSubscription(const std::string& content, const std::string& subid) {
     SubscriptionParser parser;
     return parser.parse(content, subid);
 }
bool SubitemUpdaterV2::updateProfileItems(const std::string& subid, const std::vector<db::models::Profileitem>& profiles) {
    (void)subid; // Kept for API compatibility, dedup now handles duplicates without delete-by-subid
    // Empty input is not a failure, just no work to do
    if (profiles.empty()) {
        Logger::write("INFO: No profiles to update (empty input)", LogLevel::INFO);
        return true;
    }

    // Phase 0: Pre-filter invalid proxies before insert
    std::vector<db::models::Profileitem> validProfiles;
    validProfiles.reserve(profiles.size());
    for (const db::models::Profileitem& p : profiles) {
        if (isValidProxy(p)) {
            validProfiles.push_back(p);
        }
    }
    int filteredCount = static_cast<int>(profiles.size() - validProfiles.size());
    if (filteredCount > 0) {
        Logger::write("FILTER: Removed " + std::to_string(filteredCount) + " invalid proxies before insert", LogLevel::REPORT);
    }
    if (validProfiles.empty()) {
        Logger::write("INFO: All profiles filtered out (no valid proxies to insert)", LogLevel::INFO);
        return true;
    }

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    int inserted = 0;

    // Dedup check: find existing record by lower(Address)+Port+ConfigType+lower(Id)+lower(Network)
    const char* dedupSql = "SELECT IndexId FROM ProfileItem WHERE lower(Address)=lower(?) "
                           "AND (Port IS NULL OR Port='' OR Port=?) "
                           "AND ConfigType=? "
                           "AND lower(Id)=lower(?) "
                           "AND (Network IS NULL OR Network='' OR lower(Network)=lower(?)) "
                           "LIMIT 1";

    // Insert new ProfileItem
    const char* insertSql = "INSERT INTO ProfileItem (IndexId, ConfigType, ConfigVersion, Address, Port, Ports, Id, "
                           "AlterId, Security, Network, Remarks, HeaderType, RequestHost, Path, StreamSecurity, "
                           "AllowInsecure, Subid, IsSub, Flow, Sni, Alpn, CoreType, PreSocksPort, Fingerprint, "
                           "DisplayLog, PublicKey, ShortId, SpiderX, Mldsa65Verify, EchConfigList, Extra, MuxEnabled, Cert, "
                            "CertSha, EchForceQuery) "
                            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    // Prepare statements once before loop for reuse
    const char* exSql = "INSERT INTO ProfileExItem (indexid, delay, speed, sort, message, consecutive_failures) "
                        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* checkStmt = nullptr;
    if (sqlite3_prepare_v2(db_, dedupSql, -1, &checkStmt, nullptr) != SQLITE_OK) {
        Logger::write("ERROR: Dedup check prepare failed - " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("ERROR: Insert prepare failed - " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        sqlite3_finalize(checkStmt);
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_stmt* exStmt = nullptr;
    if (sqlite3_prepare_v2(db_, exSql, -1, &exStmt, nullptr) != SQLITE_OK) {
        Logger::write("ERROR: ProfileExItem insert prepare failed - " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        sqlite3_finalize(checkStmt);
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        return false;
    }

    size_t count = 0;
    for (const db::models::Profileitem& p : validProfiles) {
        count++;
        if (count % 100 == 0) {
            Logger::write("Progress: " + std::to_string(count) + "/" + std::to_string(validProfiles.size()) + " profiles processed", LogLevel::REPORT);
        }

        // Step 1: Check for existing duplicate
        sqlite3_reset(checkStmt);
        sqlite3_clear_bindings(checkStmt);
        sqlite3_bind_text(checkStmt, 1, p.address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(checkStmt, 2, p.port.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(checkStmt, 3, p.configtype.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(checkStmt, 4, p.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(checkStmt, 5, p.network.c_str(), -1, SQLITE_TRANSIENT);

        bool isDuplicate = (sqlite3_step(checkStmt) == SQLITE_ROW);
        if (isDuplicate) {
            std::string existingId = reinterpret_cast<const char*>(sqlite3_column_text(checkStmt, 0));
            Logger::write("INFO: Skipping duplicate profile, keep existing IndexId: " + existingId, LogLevel::INFO);
        }

        if (isDuplicate) continue;

        // Step 2: Insert new profile (not duplicate)
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_text(stmt, 1, p.indexid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, p.configtype.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(stmt, 3, p.configversion);
        sqlite3_bind_text(stmt, 4, p.address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, p.port.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(stmt, 6, p.ports);
        sqlite3_bind_text(stmt, 7, p.id.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(stmt, 8, p.alterid);
        bindTextOrNull(stmt, 9, p.security);
        bindTextOrNull(stmt, 10, p.network);
        bindTextOrNull(stmt, 11, p.remarks);
        bindTextOrNull(stmt, 12, p.headertype);
        bindTextOrNull(stmt, 13, p.requesthost);
        bindTextOrNull(stmt, 14, p.path);
        bindTextOrNull(stmt, 15, p.streamsecurity);
        bindTextOrNull(stmt, 16, p.allowinsecure);
        bindTextOrNull(stmt, 17, p.subid);
        bindTextOrNull(stmt, 18, p.issub);
        bindTextOrNull(stmt, 19, p.flow);
        bindTextOrNull(stmt, 20, p.sni);
        bindTextOrNull(stmt, 21, p.alpn);
        bindTextOrNull(stmt, 22, p.coretype);
        bindTextOrNull(stmt, 23, p.presocksport);
        bindTextOrNull(stmt, 24, p.fingerprint);
        bindTextOrNull(stmt, 25, p.displaylog);
        bindTextOrNull(stmt, 26, p.publickey);
        bindTextOrNull(stmt, 27, p.shortid);
        bindTextOrNull(stmt, 28, p.spiderx);
        bindTextOrNull(stmt, 29, p.mldsa65verify);
        bindTextOrNull(stmt, 30, p.echconfiglist);
        bindTextOrNull(stmt, 31, p.extra);
        bindTextOrNull(stmt, 32, p.muxenabled);
        bindTextOrNull(stmt, 33, p.cert);
        bindTextOrNull(stmt, 34, p.certsha);
        bindTextOrNull(stmt, 35, p.echforcequery);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            inserted++;

            // Insert corresponding ProfileExItem with initial values
            sqlite3_reset(exStmt);
            sqlite3_clear_bindings(exStmt);
            sqlite3_bind_text(exStmt, 1, p.indexid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(exStmt, 2, "0", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(exStmt, 3, "0", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(exStmt, 4, "0", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(exStmt, 5, "NOT_TESTED", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(exStmt, 6, 0);
            sqlite3_step(exStmt);
        } else {
            Logger::write("ERROR: Insert failed for " + p.indexid + " - " + sqlite3_errmsg(db_), LogLevel::ERR);
        }
    }

    sqlite3_finalize(checkStmt);
    sqlite3_finalize(stmt);
    sqlite3_finalize(exStmt);

    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    Logger::write("Inserted " + std::to_string(inserted) + " new profiles, skipped duplicates", LogLevel::REPORT);
    // Dedup skips are normal behavior, not failures
    // As long as the transaction succeeded, return true
    return true;
}

std::pair<int, int> SubitemUpdaterV2::getProxyPorts(const std::string& targetUrl) {
    Logger::write("INFO: Getting proxy ports via ProxyFinder...", LogLevel::INFO);

    std::string configDir = baseDir_.empty() ? utils::getExecutableDir() + "/config" : baseDir_ + "/config";
    xrayMgr_ = XrayManager::getInstance(xrayPath_, configDir, config_.xray_workers);

    int started = xrayMgr_->start(1, config_.xray_start_port, config_.xray_api_port);
    if (started == 0) {
        Logger::write("ERROR: Failed to start xray instance", LogLevel::ERR);
        XrayManager::release();
        xrayMgr_ = nullptr;
        return {-1, -1};
    }

proxyFinder_ = new ProxyFinder(db_, xrayMgr_, xrayPath_,
                                     config_.test_url,
                                     targetUrl,
                                     config_.test_timeout_ms,
                                     externalCancel_ ? externalCancel_ : nullptr);

    std::pair<int, int> result = proxyFinder_->findFirstWorkingProxy(targetUrl);
    Logger::write("INFO: ProxyFinder returned socks=" + std::to_string(result.first) + ", api=" + std::to_string(result.second), LogLevel::INFO);

    return result;
}

void SubitemUpdaterV2::releaseProxyPorts() {
    if (!xrayMgr_ && !proxyFinder_) {
        return;
    }
    Logger::write("INFO: Releasing proxy ports...", LogLevel::INFO);

    if (proxyFinder_) {
        proxyFinder_->release();
        delete proxyFinder_;
        proxyFinder_ = nullptr;
    }

    if (xrayMgr_) {
        xrayMgr_->stopAll();
        XrayManager::release();
        xrayMgr_ = nullptr;
    }

    Logger::write("INFO: Proxy ports released", LogLevel::INFO);
}

bool SubitemUpdaterV2::startXray(const std::string& indexId, int socksPort, int apiPort) {
    (void)indexId;
    (void)socksPort;
    (void)apiPort;
    return true;
}

void SubitemUpdaterV2::cleanupXray() {
    releaseProxyPorts();
}

bool SubitemUpdaterV2::shouldSkipUpdate(const db::models::Subitem& sub) const
{
    if (!config_.check_auto_update_interval)
        return false;

    if (sub.autoupdateinterval.empty() || sub.updatetime.empty())
        return false;

    int intervalMinutes = std::stoi(sub.autoupdateinterval);
    if (intervalMinutes <= 0)
        return false;

    if (sub.updatetime == "0")
        return false;

    try {
        std::chrono::system_clock::time_point lastUpdate = std::chrono::system_clock::from_time_t(std::stoll(sub.updatetime));
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        long long elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastUpdate).count();
        return elapsed < intervalMinutes;
    } catch (...) {
        return false;
    }
}

std::vector<SubitemUpdaterV2::UpdateMethod> SubitemUpdaterV2::parseUpdateMethods(
    const std::vector<std::string>& methods)
{
    std::vector<UpdateMethod> result;
    for (const std::string& m : methods) {
        if (m == "accelerator" && std::find(result.begin(), result.end(), UpdateMethod::Accelerator) == result.end()) {
            result.push_back(UpdateMethod::Accelerator);
        } else if (m == "proxy" && std::find(result.begin(), result.end(), UpdateMethod::Proxy) == result.end()) {
            result.push_back(UpdateMethod::Proxy);
        } else if (m == "direct" && std::find(result.begin(), result.end(), UpdateMethod::Direct) == result.end()) {
            result.push_back(UpdateMethod::Direct);
        }
    }
    if (result.empty()) {
        result.push_back(UpdateMethod::Accelerator);
    }
    return result;
}

std::string SubitemUpdaterV2::getCurrentTimestamp() {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    return std::to_string(timestamp);
}

bool SubitemUpdaterV2::migrateSubscription(sqlite3* srcDb, sqlite3* dstDb,
                                             const std::string& subid) {
    if (subid.empty()) {
        return true; // No subscription to migrate
    }
    
    // Check if subscription exists in target DB
    std::string checkSql = "SELECT COUNT(*) FROM SubItem WHERE Id = ?";
    sqlite3_stmt* checkStmt = nullptr;
    bool exists = false;
    
    if (sqlite3_prepare_v2(dstDb, checkSql.c_str(), -1, &checkStmt, nullptr) != SQLITE_OK) {
        Logger::write("ERROR: Check prepare failed: " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);
        return false;
    }
    sqlite3_bind_text(checkStmt, 1, subid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(checkStmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(checkStmt, 0) > 0;
    }
    sqlite3_finalize(checkStmt);
    
    if (exists) {
        return true; // Already exists, skip
    }
    
    // Get subscription from source DB
    std::string srcSql = "SELECT * FROM SubItem WHERE Id = ?";
    sqlite3_stmt* srcStmt = nullptr;
    db::models::Subitem subitem;
    
    if (sqlite3_prepare_v2(srcDb, srcSql.c_str(), -1, &srcStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(srcStmt, 1, subid.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(srcStmt) == SQLITE_ROW) {
            subitem = db::models::Subitem::fromStmt(srcStmt);
        }
        sqlite3_finalize(srcStmt);
    } else {
        Logger::write("ERROR: migrateSubscription prepare failed for src: " + std::string(sqlite3_errmsg(srcDb)), LogLevel::ERR);
        return false;
    }

    if (subitem.id.empty()) {
        return false; // No valid subscription found
    }
    
    // New subscriptions synced to target should start disabled
    subitem.enabled = "0";

    return insertSubItem(dstDb, subitem);
}

bool SubitemUpdaterV2::migrateProxy(sqlite3* srcDb, sqlite3* dstDb,
                                     const db::models::Profileitem& proxy) {
    // Check if proxy exists in target DB
    std::string checkSql = "SELECT COUNT(*) FROM ProfileItem WHERE IndexId = ?";
    sqlite3_stmt* checkStmt = nullptr;
    bool exists = false;
    
    if (sqlite3_prepare_v2(dstDb, checkSql.c_str(), -1, &checkStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(checkStmt, 1, proxy.indexid.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            exists = sqlite3_column_int(checkStmt, 0) > 0;
        }
        sqlite3_finalize(checkStmt);
    }
    
    if (exists) {
        // UPDATE existing proxy
        std::string updateSql = "UPDATE ProfileItem SET ConfigType = ?, ConfigVersion = ?, Address = ?, Port = ?, Ports = ?, Id = ?, AlterId = ?, Security = ?, Network = ?, Remarks = ?, HeaderType = ?, RequestHost = ?, Path = ?, StreamSecurity = ?, AllowInsecure = ?, SubId = ?, IsSub = ?, Flow = ?, Sni = ?, Alpn = ?, CoreType = ?, PreSocksPort = ?, Fingerprint = ?, DisplayLog = ?, PublicKey = ?, ShortId = ?, SpiderX = ?, Mldsa65Verify = ?, Extra = ?, MuxEnabled = ?, Cert = ?, CertSha = ?, EchConfigList = ?, EchForceQuery = ? WHERE IndexId = ?";
        
        sqlite3_stmt* updateStmt = nullptr;
        if (sqlite3_prepare_v2(dstDb, updateSql.c_str(), -1, &updateStmt, nullptr) != SQLITE_OK) {
            Logger::write("UPDATE prepare failed for " + proxy.indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);
            return false;
        }
        
        // 必须字段：IndexId, Address, Port, Id, ConfigType
        sqlite3_bind_text(updateStmt, 1, proxy.configtype.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(updateStmt, 2, proxy.configversion);
        sqlite3_bind_text(updateStmt, 3, proxy.address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(updateStmt, 4, proxy.port.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(updateStmt, 5, proxy.ports);
        sqlite3_bind_text(updateStmt, 6, proxy.id.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(updateStmt, 7, proxy.alterid);
        bindTextOrNull(updateStmt, 8, proxy.security);
        bindTextOrNull(updateStmt, 9, proxy.network);
        bindTextOrNull(updateStmt, 10, proxy.remarks);
        bindTextOrNull(updateStmt, 11, proxy.headertype);
        bindTextOrNull(updateStmt, 12, proxy.requesthost);
        bindTextOrNull(updateStmt, 13, proxy.path);
        bindTextOrNull(updateStmt, 14, proxy.streamsecurity);
        bindTextOrNull(updateStmt, 15, proxy.allowinsecure);
        bindTextOrNull(updateStmt, 16, proxy.subid);
        bindTextOrNull(updateStmt, 17, proxy.issub);
        bindTextOrNull(updateStmt, 18, proxy.flow);
        bindTextOrNull(updateStmt, 19, proxy.sni);
        bindTextOrNull(updateStmt, 20, proxy.alpn);
        bindTextOrNull(updateStmt, 21, proxy.coretype);
        bindTextOrNull(updateStmt, 22, proxy.presocksport);
        bindTextOrNull(updateStmt, 23, proxy.fingerprint);
        bindTextOrNull(updateStmt, 24, proxy.displaylog);
        bindTextOrNull(updateStmt, 25, proxy.publickey);
        bindTextOrNull(updateStmt, 26, proxy.shortid);
        bindTextOrNull(updateStmt, 27, proxy.spiderx);
        bindTextOrNull(updateStmt, 28, proxy.mldsa65verify);
        bindTextOrNull(updateStmt, 29, proxy.extra);
        bindTextOrNull(updateStmt, 30, proxy.muxenabled);
        bindTextOrNull(updateStmt, 31, proxy.cert);
        bindTextOrNull(updateStmt, 32, proxy.certsha);
        bindTextOrNull(updateStmt, 33, proxy.echconfiglist);
        bindTextOrNull(updateStmt, 34, proxy.echforcequery);
        sqlite3_bind_text(updateStmt, 35, proxy.indexid.c_str(), -1, SQLITE_TRANSIENT);
        
        bool result = (sqlite3_step(updateStmt) == SQLITE_DONE);
        if (!result) {
            Logger::write("UPDATE step failed for " + proxy.indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);
        }
        sqlite3_finalize(updateStmt);
        
        // Update ProfileExItem
        bool exResult = migrateProfileExItem(srcDb, dstDb, proxy.indexid);
        if (!exResult) {
            return false;
        }
        
        return result;
    } else {
        // INSERT new proxy
        std::string insertSql = "INSERT INTO ProfileItem (IndexId, ConfigType, ConfigVersion, Address, Port, Ports, Id, AlterId, Security, Network, Remarks, HeaderType, RequestHost, Path, StreamSecurity, AllowInsecure, SubId, IsSub, Flow, Sni, Alpn, CoreType, PreSocksPort, Fingerprint, DisplayLog, PublicKey, ShortId, SpiderX, Mldsa65Verify, Extra, MuxEnabled, Cert, CertSha, EchConfigList, EchForceQuery) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
        
        sqlite3_stmt* insertStmt = nullptr;
        if (sqlite3_prepare_v2(dstDb, insertSql.c_str(), -1, &insertStmt, nullptr) != SQLITE_OK) {
            Logger::write("ERROR: INSERT prepare failed for proxy " + proxy.indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);
            return false;
        }
        
        // 必须字段：IndexId, ConfigType, Address, Port, Id
        sqlite3_bind_text(insertStmt, 1, proxy.indexid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insertStmt, 2, proxy.configtype.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(insertStmt, 3, proxy.configversion);
        sqlite3_bind_text(insertStmt, 4, proxy.address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insertStmt, 5, proxy.port.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(insertStmt, 6, proxy.ports);
        sqlite3_bind_text(insertStmt, 7, proxy.id.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(insertStmt, 8, proxy.alterid);
        bindTextOrNull(insertStmt, 9, proxy.security);
        bindTextOrNull(insertStmt, 10, proxy.network);
        bindTextOrNull(insertStmt, 11, proxy.remarks);
        bindTextOrNull(insertStmt, 12, proxy.headertype);
        bindTextOrNull(insertStmt, 13, proxy.requesthost);
        bindTextOrNull(insertStmt, 14, proxy.path);
        bindTextOrNull(insertStmt, 15, proxy.streamsecurity);
        bindTextOrNull(insertStmt, 16, proxy.allowinsecure);
        bindTextOrNull(insertStmt, 17, proxy.subid);
        bindTextOrNull(insertStmt, 18, proxy.issub);
        bindTextOrNull(insertStmt, 19, proxy.flow);
        bindTextOrNull(insertStmt, 20, proxy.sni);
        bindTextOrNull(insertStmt, 21, proxy.alpn);
        bindTextOrNull(insertStmt, 22, proxy.coretype);
        bindTextOrNull(insertStmt, 23, proxy.presocksport);
        bindTextOrNull(insertStmt, 24, proxy.fingerprint);
        bindTextOrNull(insertStmt, 25, proxy.displaylog);
        bindTextOrNull(insertStmt, 26, proxy.publickey);
        bindTextOrNull(insertStmt, 27, proxy.shortid);
        bindTextOrNull(insertStmt, 28, proxy.spiderx);
        bindTextOrNull(insertStmt, 29, proxy.mldsa65verify);
        bindTextOrNull(insertStmt, 30, proxy.extra);
        bindTextOrNull(insertStmt, 31, proxy.muxenabled);
        bindTextOrNull(insertStmt, 32, proxy.cert);
        bindTextOrNull(insertStmt, 33, proxy.certsha);
        bindTextOrNull(insertStmt, 34, proxy.echconfiglist);
        bindTextOrNull(insertStmt, 35, proxy.echforcequery);
        
        bool result = (sqlite3_step(insertStmt) == SQLITE_DONE);
        sqlite3_finalize(insertStmt);
        
        // Insert ProfileExItem
        bool exResult = migrateProfileExItem(srcDb, dstDb, proxy.indexid);
        if (!exResult) {
            Logger::write("ERROR: migrateProfileExItem failed for proxy " + proxy.indexid, LogLevel::ERR);
            return false;
        }
        
        return result;
    }
}

bool SubitemUpdaterV2::migrateProfileExItem(sqlite3* srcDb, sqlite3* dstDb,
                                                 const std::string& indexid) {
    // Get ProfileExItem from source
    std::string srcSql = "SELECT * FROM ProfileExItem WHERE IndexId = ?";
    sqlite3_stmt* srcStmt = nullptr;
    db::models::ProfileExItem exItem;
    bool found = false;
    
    if (sqlite3_prepare_v2(srcDb, srcSql.c_str(), -1, &srcStmt, nullptr) != SQLITE_OK) {
        Logger::write("migrateProfileExItem: Prepare source select failed for " + indexid + ": " + std::string(sqlite3_errmsg(srcDb)), LogLevel::ERR);
        return false;
    }
    sqlite3_bind_text(srcStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(srcStmt) == SQLITE_ROW) {
        exItem = db::models::ProfileExItem::fromStmt(srcStmt);
        found = true;
    }
    sqlite3_finalize(srcStmt);
    
    if (!found) {
        return true; // No extension item to migrate
    }
    
    // Check if exists in target
    std::string checkSql = "SELECT COUNT(*) FROM ProfileExItem WHERE IndexId = ?";
    sqlite3_stmt* checkStmt = nullptr;
    bool exists = false;
    
    if (sqlite3_prepare_v2(dstDb, checkSql.c_str(), -1, &checkStmt, nullptr) != SQLITE_OK) {
        Logger::write("migrateProfileExItem: Prepare target check failed for " + indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);
        return false;
    }
    sqlite3_bind_text(checkStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(checkStmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(checkStmt, 0) > 0;
    }
    sqlite3_finalize(checkStmt);
    
    if (exists) {
        // UPDATE
        std::string updateSql = "UPDATE ProfileExItem SET Delay = ?, Speed = ?, Sort = ?, Message = ?, consecutive_failures = ? WHERE IndexId = ?";
        sqlite3_stmt* updateStmt = nullptr;
        if (sqlite3_prepare_v2(dstDb, updateSql.c_str(), -1, &updateStmt, nullptr) != SQLITE_OK) {
            Logger::write("migrateProfileExItem: Prepare UPDATE failed for " + indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);
            return false;
        }
        bindTextOrNull(updateStmt, 1, exItem.delay);
        bindTextOrNull(updateStmt, 2, exItem.speed);
        bindTextOrNull(updateStmt, 3, exItem.sort);
        bindTextOrNull(updateStmt, 4, exItem.message);
        sqlite3_bind_int(updateStmt, 5, exItem.consecutive_failures);
        sqlite3_bind_text(updateStmt, 6, indexid.c_str(), -1, SQLITE_TRANSIENT);
        bool result = (sqlite3_step(updateStmt) == SQLITE_DONE);
        sqlite3_finalize(updateStmt);
        if (!result) {
            Logger::write("migrateProfileExItem: UPDATE step failed for " + indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);
            return false;
        }
    } else {
        // INSERT
        std::string insertSql = "INSERT INTO ProfileExItem (IndexId, Delay, Speed, Sort, Message, consecutive_failures) VALUES (?, ?, ?, ?, ?, ?)";
        sqlite3_stmt* insertStmt = nullptr;
        if (sqlite3_prepare_v2(dstDb, insertSql.c_str(), -1, &insertStmt, nullptr) != SQLITE_OK) {
            Logger::write("migrateProfileExItem: Prepare INSERT failed for " + indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);
            return false;
        }
        sqlite3_bind_text(insertStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
        bindTextOrNull(insertStmt, 2, exItem.delay);
        bindTextOrNull(insertStmt, 3, exItem.speed);
        bindTextOrNull(insertStmt, 4, exItem.sort);
        bindTextOrNull(insertStmt, 5, exItem.message);
        sqlite3_bind_int(insertStmt, 6, exItem.consecutive_failures);
        bool result = (sqlite3_step(insertStmt) == SQLITE_DONE);
        sqlite3_finalize(insertStmt);
        if (!result) {
            Logger::write("migrateProfileExItem: INSERT step failed for " + indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);
            return false;
        }
    }
    return true;
}

bool SubitemUpdaterV2::syncDatabases(const std::string& sourceDbPath,
                                         const std::string& targetDbPath) {
    sqlite3* srcDb = nullptr;
    sqlite3* dstDb = nullptr;
    
    // 0. Check if source and target are the same database
    Logger::write("========================================", LogLevel::INFO);
    Logger::write("INFO: Proxy Database Sync", LogLevel::INFO);
    Logger::write("========================================", LogLevel::INFO);
    Logger::write("INFO: Source: " + sourceDbPath, LogLevel::INFO);
    Logger::write("INFO: Target: " + targetDbPath, LogLevel::INFO);
    Logger::write("========================================", LogLevel::INFO);
    if (sourceDbPath == targetDbPath) {
        Logger::write("ERROR: Error: Source and target databases are the same: " + sourceDbPath, LogLevel::ERR);
        Logger::write("ERROR: Please specify different databases for sync.", LogLevel::ERR);
        return false;
    }
    
    // 1. Open source database
    if (sqlite3_open(sourceDbPath.c_str(), &srcDb) != SQLITE_OK) {
        Logger::write("Failed to open source database: " + std::string(sqlite3_errmsg(srcDb)) + " Path: " + sourceDbPath, LogLevel::ERR);
        return false;
    }
    Logger::write("Source database opened", LogLevel::DEBUG);
    
    // 2. Open target database
    if (sqlite3_open(targetDbPath.c_str(), &dstDb) != SQLITE_OK) {
        Logger::write("Failed to open target database: " + std::string(sqlite3_errmsg(dstDb)) + " Path: " + targetDbPath, LogLevel::ERR);
        sqlite3_close(srcDb);
        return false;
    }
    Logger::write("Target database opened", LogLevel::DEBUG);
    
    // 3. Query valid proxies from source (delay > 0)
    std::string sql = R"(
        SELECT p.* FROM ProfileItem p
        LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
        WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
        ORDER BY CAST(pe.Delay AS INTEGER) ASC
    )";
    
    std::vector<db::models::Profileitem> profiles = db::models::ProfileitemDAO(srcDb).getAll(sql);
    Logger::write("Found " + std::to_string(profiles.size()) + " valid proxies to migrate", LogLevel::INFO);
    
    int successCount = 0;
    int failCount = 0;
    
    // Begin transaction on dstDb for atomic proxy migration
    char* errMsg = nullptr;
    if (sqlite3_exec(dstDb, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("syncDatabases: BEGIN TRANSACTION failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        sqlite3_close(srcDb);
        sqlite3_close(dstDb);
        return false;
    }
    
    // 4. Migrate each proxy
    for (const db::models::Profileitem& profile : profiles) {
        // Skip proxies whose Subid is in dedup_subids when sync_skip_subids is enabled
        if (config_.sync.sync_skip_subids && !profile.subid.empty()) {
            bool skip = false;
            for (const std::string& sid : config_.dedup_subids) {
                if (profile.subid == sid) {
                    skip = true;
                    break;
                }
            }
            if (skip) {
                Logger::write("Skipping proxy (protected subid): " + profile.indexid + " (" + profile.remarks + ")", LogLevel::DEBUG);
                continue;
            }
        }
        
        // Migrate subscription first
        if (!profile.subid.empty()) {
            if (!migrateSubscription(srcDb, dstDb, profile.subid)) {
                Logger::write("Warning: Failed to migrate subscription " + profile.subid, LogLevel::WARN);
            }
        }
        
        // Migrate proxy
        if (migrateProxy(srcDb, dstDb, profile)) {
            successCount++;
        } else {
            Logger::write("Failed to migrate proxy " + profile.indexid, LogLevel::ERR);
            failCount++;
        }
    }
    
    // 5. Output statistics
    Logger::write("Migration Result — Total: " + std::to_string(profiles.size()) + ", Succeeded: " + std::to_string(successCount) + ", Failed: " + std::to_string(failCount), LogLevel::REPORT);

    // Commit the transaction
    if (sqlite3_exec(dstDb, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("syncDatabases: COMMIT failed, attempting ROLLBACK: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        sqlite3_exec(dstDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(srcDb);
        sqlite3_close(dstDb);
        return false;
    }

    if (failCount > 0) {
        Logger::write("Sync failed: " + std::to_string(failCount) + " proxy(es) failed to migrate", LogLevel::ERR);
    }

    sqlite3_close(srcDb);
    sqlite3_close(dstDb);

    return failCount == 0;
}

bool SubitemUpdaterV2::deduplicate() {
    Deduplicator dedup(db_, config_);
    return dedup.deduplicate();
}

// Import subitems from file
bool SubitemUpdaterV2::importSubitemsFromFile(const std::string& filePath, 
                                               const std::string& baseDir) {
    (void)baseDir; // Kept for API compatibility
    Importer importer(db_, config_);
    return importer.importFromFile(filePath);
}

// Import single URL directly
bool SubitemUpdaterV2::importSingleUrl(const std::string& url) {
    Importer importer(db_, config_);
    return importer.importSingleUrl(url);
}

} // namespace update
