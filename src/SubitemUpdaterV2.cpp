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

#include <curl/curl.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <array>
#include <set>
#include <cstdint>
#include <random>
#include <windows.h>

namespace update {

namespace {
    size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    std::string getJsonValueString(const boost::json::object& obj, const char* key, const char* defaultVal = "") {
        if (!obj.contains(key)) return defaultVal;
        try {
            auto& val = obj.at(key);
            if (val.is_string()) return val.as_string().c_str();
            if (val.is_int64()) return std::to_string(val.as_int64());
            if (val.is_double()) return std::to_string(static_cast<int>(val.as_double()));
} catch (...) {
            Logger::write("getJsonValueString: exception for key " + std::string(key ? key : "(null)"), LogLevel::WARN);
        }
        return defaultVal;
    }

    void bindTextOrNull(sqlite3_stmt* stmt, int idx, const std::string& val) {
        if (val.empty()) {
            sqlite3_bind_null(stmt, idx);
        } else {
            sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
        }
    }

    // Network whitelist check (mirrors ConfigGenerator::isValidNetwork)
    bool isValidNetwork(const std::string& network) {
        if (network.empty()) return false;
        std::string lower = network;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        static const std::set<std::string> valid = {
            "tcp","ws","grpc","h2","httpupgrade","kcp","xhttp","http","quic"
        };
        if (valid.count(lower) == 0) {
            if (lower == "raw" || lower == "tcp,udp") {
                return true;
            }
        }
        return valid.count(lower) > 0;
    }

    // Pre-filter invalid proxies before insert/dedup
    // R1: REALITY requires PublicKey
    // R2: REALITY requires Sni
    // R3: Network must be valid if non-empty (empty is allowed, ConfigGenerator falls back to "tcp")
    // R4: Address must not be empty
    // R5: Port must be valid (1-65535)
    bool isValidProxy(const db::models::Profileitem& p) {
        // R1: REALITY requires PublicKey
        if (p.streamsecurity == "reality" && p.publickey.empty()) {
            Logger::write("SKIP: " + p.address + ":" + p.port + " - REALITY missing PublicKey", LogLevel::WARN);
            return false;
        }
        // R2: REALITY requires Sni
        if (p.streamsecurity == "reality" && p.sni.empty()) {
            Logger::write("SKIP: " + p.address + ":" + p.port + " - REALITY missing Sni", LogLevel::WARN);
            return false;
        }
        // R3: Invalid network (non-empty only)
        if (!p.network.empty() && !isValidNetwork(p.network)) {
            Logger::write("SKIP: " + p.address + ":" + p.port + " - invalid network: '" + p.network + "'", LogLevel::WARN);
            return false;
        }
        // R4: Empty address
        if (p.address.empty()) {
            Logger::write("SKIP: empty address (IndexId: " + p.indexid + ")", LogLevel::WARN);
            return false;
        }
        // R5: Invalid port
        if (p.port.empty()) {
            Logger::write("SKIP: " + p.address + " - empty port", LogLevel::WARN);
            return false;
        }
        try {
            int portVal = std::stoi(p.port);
            if (portVal <= 0 || portVal > 65535) {
                Logger::write("SKIP: " + p.address + ":" + p.port + " - port out of range", LogLevel::WARN);
                return false;
            }
        } catch (...) {
            Logger::write("SKIP: " + p.address + ":" + p.port + " - non-numeric port", LogLevel::WARN);
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
                                    std::atomic<bool>* externalCancel)
    : db_(db), xrayPath_(xrayPath), config_(config), logOut_(logOut), baseDir_(baseDir),
      xrayMgr_(nullptr), proxyFinder_(nullptr), xrayProcessId_(0), xrayJob_(nullptr),
      externalCancel_(externalCancel) {
}

bool SubitemUpdaterV2::run() {
    Logger::write("========================================", LogLevel::INFO);
    Logger::write("INFO: Starting SubitemUpdaterV2", LogLevel::INFO);
    Logger::write("========================================", LogLevel::INFO);

    std::vector<UpdateMethod> methods = parseUpdateMethods(config_.update_methods);
    std::string methodsLog;
    for (const auto& m : methods) {
        if (!methodsLog.empty()) methodsLog += ", ";
        switch (m) {
            case UpdateMethod::Accelerator: methodsLog += "accelerator"; break;
            case UpdateMethod::Proxy: methodsLog += "proxy"; break;
            case UpdateMethod::Direct: methodsLog += "direct"; break;
        }
    }
    Logger::write("INFO: Update methods: " + methodsLog, LogLevel::INFO);

    db::models::SubitemDAO subDao(db_);
    auto enabledSubs = subDao.getEnabledSubscriptions();

    if (enabledSubs.empty()) {
        Logger::write("INFO: No enabled subscriptions found", LogLevel::INFO);
        return false;
    }

    Logger::write("INFO: Found " + std::to_string(enabledSubs.size()) + " enabled subscriptions", LogLevel::INFO);

    bool needProxy = false;
    for (const auto& m : methods) {
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
        auto result = getProxyPorts(enabledSubs[0].url);
        proxySocksPort = result.first;
        proxyApiPort = result.second;
        if (proxySocksPort > 0) {
            Logger::write("INFO: Pre-found working proxy, socks=" + std::to_string(proxySocksPort), LogLevel::INFO);
        } else {
            Logger::write("WARN: Failed to find working proxy", LogLevel::WARN);
        }
    }

    int successCount = 0;
    std::vector<std::tuple<std::string, std::string, std::string>> failedSubs;
    int phaseIndex = 0;
    int phaseCount = static_cast<int>(methods.size());

    for (const auto& method : methods) {
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

        auto processSub = [&](const std::string& subId, const std::string& subRemarks,
                               const std::string& subUrl) {
            if (isCancelled()) return false;

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

            if (!content.empty()) {
                Logger::write("INFO: " + methodName + " connection successful", LogLevel::INFO);
                auto profiles = parseSubscription(content, subId);
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
                Logger::write("WARN: " + methodName + " connection failed", LogLevel::WARN);
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
                const auto& sub = enabledSubs[i];
                if (shouldSkipUpdate(sub)) {
                    Logger::write("INFO: Skipping sub " + sub.id + " (within update interval)", LogLevel::INFO);
                    continue;
                }
                Logger::write("[" + std::to_string(i + 1) + "/" + std::to_string(enabledSubs.size()) + "] " + methodName + ": " + sub.url, LogLevel::REPORT);
                if (!processSub(sub.id, sub.remarks, sub.url)) break;
            }
        } else {
            for (size_t i = 0; i < failedSubs.size(); ++i) {
                if (isCancelled()) {
                    Logger::write("INFO: Update cancelled by user during " + methodName + " phase", LogLevel::REPORT);
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
        for (const auto& sub : failedSubs) {
            Logger::write("  Id: " + std::get<0>(sub) + ", Remarks: " + std::get<1>(sub) + ", URL: " + std::get<2>(sub), LogLevel::REPORT);
        }
    }
    Logger::write("========================================", LogLevel::REPORT);

    if (config_.dedup_after_update && config_.dedup_enabled) {
        Logger::write("INFO: Running dedup after subscription update...", LogLevel::INFO);
        deduplicate();
    }

    return successCount > 0;
}

bool SubitemUpdaterV2::runSingle(const std::string& subId) {
    Logger::write("INFO: runSingle - subId: " + subId, LogLevel::INFO);

    auto optSub = getSubscription(subId);
    if (!optSub) {
        Logger::write("ERROR: Subscription not found: " + subId, LogLevel::ERR);
        return false;
    }
    const auto& sub = *optSub;

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

    auto optSub = getSubscription(subId);
    if (!optSub) {
        Logger::write("ERROR: Subscription not found: " + subId, LogLevel::ERR);
        return false;
    }
    const auto& sub = *optSub;

    if (sub.enabled != "1") {
        Logger::write("ERROR: Subscription is disabled: " + subId, LogLevel::ERR);
        return false;
    }

    if (shouldSkipUpdate(sub)) {
        Logger::write("INFO: Skipping sub " + subId + " (within update interval)", LogLevel::INFO);
        return true;
    }

    std::string content = fetchUrlViaProxy(sub.url, socksPort);
    if (content.empty()) {
        Logger::write("Failed to fetch via proxy", LogLevel::INFO);
        return false;
    }

    auto profiles = parseSubscription(content, sub.id);
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
    for (const auto& method : methods) {
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
                auto [socks, api] = getProxyPorts(subUrl);
                (void)api;
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
            auto profiles = parseSubscription(content, subId);
            if (!profiles.empty()) {
                return updateProfileItems(subId, profiles);
            }
        }
    }

    return false;
}

std::string SubitemUpdaterV2::fetchUrlViaAccelerator(const std::string& url) {
    if (config_.accelerator_url.empty()) {
        Logger::write("INFO: accelerator_url empty, falling back to direct fetch", LogLevel::INFO);
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
        Logger::write("fetchUrl failed - " + std::string(e.what()), LogLevel::INFO);
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
    std::vector<db::models::Profileitem> profiles;
    
    bool hasProtocol = (content.find("://") != std::string::npos);
    std::string decoded;
    
    if (hasProtocol) {
        Logger::write("INFO: Content appears to be plain text share links", LogLevel::INFO);
        decoded = content;
    } else {
        Logger::write("INFO: Attempting base64 decode...", LogLevel::INFO);
        decoded = decodeBase64(content);
        
        if (decoded.length() < 10 || decoded.find("://") == std::string::npos) {
            Logger::write("WARN: Base64 decode produced invalid content, using original", LogLevel::WARN);
            decoded = content;
        }
    }
    
    Logger::write("INFO: Final content length: " + std::to_string(decoded.length()), LogLevel::INFO);
    
    std::istringstream stream(decoded);
    std::string line;
    
    int lineCount = 0;
    int validCount = 0;
    while (std::getline(stream, line)) {
        lineCount++;
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();
        
        db::models::Profileitem profile;
        profile.subid = subid;
        profile.issub = "1";
        
        if (line.find("vmess://") == 0) {
            std::string encoded = line.substr(8);
            
            std::string jsonStr = decodeBase64(encoded);
            if (jsonStr.empty()) {
                jsonStr = urlDecode(encoded);
            }
            
            if (jsonStr.empty()) continue;
            
            bool isJson = (jsonStr.find('{') != std::string::npos && jsonStr.find('}') != std::string::npos);
            
            bool parseSuccess = false;
            try {
                if (!isJson) {
                    throw std::runtime_error("not JSON");
                }
                boost::json::value jv = boost::json::parse(jsonStr);
                boost::json::object obj = jv.as_object();
                
                profile.configtype = "1";
                profile.configversion = "2";
                profile.alterid = "0";
                profile.network = "tcp";
                profile.coretype = "";
                profile.muxenabled = "0";
                profile.address = getJsonValueString(obj, "add", "");
                
                if (obj.contains("port")) {
                    auto& portVal = obj.at("port");
                    if (portVal.is_int64()) {
                        profile.port = std::to_string(portVal.as_int64());
                    } else if (portVal.is_string()) {
                        profile.port = portVal.as_string().c_str();
                    } else if (portVal.is_double()) {
                        profile.port = std::to_string(static_cast<int>(portVal.as_double()));
                    }
                }
                
                profile.id = getJsonValueString(obj, "id", "");
                
                if (obj.contains("aid")) {
                    auto& aidVal = obj.at("aid");
                    if (aidVal.is_int64()) {
                        profile.alterid = std::to_string(aidVal.as_int64());
                    } else if (aidVal.is_string()) {
                        profile.alterid = aidVal.as_string().c_str();
                    } else {
                        profile.alterid = "0";
                    }
                } else {
                    profile.alterid = "0";
                }
                
profile.security = getJsonValueString(obj, "scy", "auto");
                 profile.network = getJsonValueString(obj, "net", "tcp");
                 // Map splithttp to xhttp and validate network value
                 if (profile.network == "splithttp") {
                     profile.network = "xhttp";
                 }
                 if (profile.network != "tcp" && profile.network != "ws" && profile.network != "h2" &&
                     profile.network != "xhttp" && profile.network != "grpc") {
                     profile.network = "tcp";
                 }
                 profile.remarks = getJsonValueString(obj, "ps", "");
                profile.path = getJsonValueString(obj, "path", "");
                profile.requesthost = getJsonValueString(obj, "host", "");
                profile.streamsecurity = getJsonValueString(obj, "tls", "");
                profile.sni = getJsonValueString(obj, "sni", "");
                if (profile.sni.empty() && !profile.requesthost.empty() && profile.streamsecurity == "tls") {
                    profile.sni = profile.requesthost;
                }
                profile.flow = getJsonValueString(obj, "flow", "");
                profile.fingerprint = getJsonValueString(obj, "fp", "chrome");
                profile.alpn = getJsonValueString(obj, "alpn", "");
                profile.headertype = getJsonValueString(obj, "type", "");
                std::string insecureVal = getJsonValueString(obj, "insecure", "");
                profile.allowinsecure = (insecureVal == "1" || insecureVal == "true") ? "true" : "false";
                parseSuccess = true;
            } catch (const std::exception& e) {
                std::string errMsg = e.what();
                if (errMsg.find("incomplete") != std::string::npos || 
                    errMsg.find("extra data") != std::string::npos) {
                    size_t braceStart = jsonStr.find('{');
                    if (braceStart != std::string::npos) {
                        size_t braceEnd = jsonStr.rfind('}');
                        if (braceEnd != std::string::npos && braceEnd > braceStart) {
                            std::string singleJson = jsonStr.substr(braceStart, braceEnd - braceStart + 1);
                            try {
                                boost::json::value jv = boost::json::parse(singleJson);
                                boost::json::object obj = jv.as_object();
                                profile.configtype = "1";
                                profile.configversion = "2";
                                profile.alterid = "0";
                                profile.network = "tcp";
                                profile.coretype = "";
                                profile.muxenabled = "0";
                                profile.address = getJsonValueString(obj, "add", "");
                                profile.id = getJsonValueString(obj, "id", "");
                                profile.security = getJsonValueString(obj, "scy", "auto");
                                profile.remarks = getJsonValueString(obj, "ps", "");
parseSuccess = true;
} catch (...) {}
                        }
                    }
                }
                if (!parseSuccess) {
                    Logger::write("WARN: Failed to parse vmess: " + errMsg, LogLevel::WARN);
                }
            }
            if (!parseSuccess) continue;
        } else if (line.find("vless://") == 0) {
            std::string uri = line.substr(8);
            size_t atPos = uri.find('@');
            if (atPos == std::string::npos) continue;
            
            profile.configtype = "5";
            profile.configversion = "2";
            profile.alterid = "0";
            profile.network = "tcp";
            profile.coretype = "";
            profile.muxenabled = "0";
            profile.security = "none";
            profile.id = uri.substr(0, atPos);
            if (!profile.id.empty() && profile.id.front() == '/') {
                profile.id.erase(profile.id.begin());
            }
            
            std::string hostPart = uri.substr(atPos + 1);
            size_t qPos = hostPart.find('?');
            std::string addrPart = (qPos != std::string::npos) ? hostPart.substr(0, qPos) : hostPart;
            
            auto [addr, port] = parseAddressPort(addrPart);
            if (addr.empty()) continue;
            profile.address = addr;
            if (!profile.address.empty() && profile.address.back() == '/') {
                profile.address.pop_back();
            }
            profile.port = port;
            
            if (qPos != std::string::npos) {
                std::string params = hostPart.substr(qPos + 1);
                size_t hashPos = params.find('#');
                std::string query = (hashPos != std::string::npos) ? params.substr(0, hashPos) : params;
                
                std::istringstream paramStream(query);
                std::string param;
                while (std::getline(paramStream, param, '&')) {
                    size_t eqPos = param.find('=');
                    if (eqPos == std::string::npos) continue;
                    std::string key = param.substr(0, eqPos);
                    std::string val = urlDecode(param.substr(eqPos + 1));
                    
                    if (key == "type") profile.network = val;
                    else if (key == "security") profile.streamsecurity = val;
                    else if (key == "sni") profile.sni = val;
                    else if (key == "path") profile.path = val;
                    else if (key == "host") profile.requesthost = val;
                    else if (key == "fp") profile.fingerprint = val;
                    else if (key == "flow") profile.flow = val;
                    else if (key == "pbk") profile.publickey = val;
                    else if (key == "sid") profile.shortid = val;
                    else if (key == "spx") profile.spiderx = val;
                    else if (key == "headerType") profile.headertype = val;
                    else if (key == "allowInsecure") profile.allowinsecure = (val == "1" || val == "true") ? "true" : "false";
                    else if (key == "alpn") profile.alpn = val;
else if (key == "e" || key == "ech") profile.echconfiglist = val;
                 }
                 
                 // Validate and map network value for vless
                 if (profile.network == "splithttp") {
                     profile.network = "xhttp";
                 }
                 if (profile.network != "tcp" && profile.network != "ws" && profile.network != "h2" &&
                     profile.network != "xhttp" && profile.network != "grpc") {
                     profile.network = "tcp";
                 }
                 
                 if (hashPos != std::string::npos) {
                     profile.remarks = urlDecode(params.substr(hashPos + 1));
                 }
             }
        } else if (line.find("ss://") == 0) {
            std::string uri = line.substr(5);
            size_t hashPos = uri.find('#');
            if (hashPos != std::string::npos) {
                profile.remarks = urlDecode(uri.substr(hashPos + 1));
                uri = uri.substr(0, hashPos);
            }
            
            size_t atPos = uri.find('@');
            if (atPos == std::string::npos) continue;
            
            profile.configtype = "3";
            profile.configversion = "2";
            profile.alterid = "0";
            profile.network = "";
            profile.coretype = "";
            profile.muxenabled = "0";
            
            std::string userInfo = uri.substr(0, atPos);
            std::string hostInfo = uri.substr(atPos + 1);
            
            auto [addr, portWithParams] = parseAddressPort(hostInfo);
            if (addr.empty()) continue;
            profile.address = addr;
            if (!profile.address.empty() && profile.address.back() == '/') {
                profile.address.pop_back();
            }
            profile.port = portWithParams;
            
            size_t slashPos = profile.port.find('/');
            if (slashPos != std::string::npos) {
                profile.port = profile.port.substr(0, slashPos);
            }
            
            size_t qPos = profile.port.find('?');
            if (qPos != std::string::npos) {
                std::string params = profile.port.substr(qPos + 1);
                profile.port = profile.port.substr(0, qPos);
                
                std::istringstream paramStream(params);
                std::string param;
                while (std::getline(paramStream, param, '&')) {
                    size_t eqPos = param.find('=');
                    if (eqPos == std::string::npos) continue;
                    std::string key = param.substr(0, eqPos);
                    std::string val = urlDecode(param.substr(eqPos + 1));
                    
                    if (key == "plugin") {
                        profile.extra = val;
                        std::string hostFromPlugin;
                        std::istringstream pluginStream(val);
                        std::string pluginPart;
                        while (std::getline(pluginStream, pluginPart, ';')) {
                            size_t partEqPos = pluginPart.find('=');
                            if (partEqPos == std::string::npos) continue;
                            std::string pKey = pluginPart.substr(0, partEqPos);
                            std::string pVal = pluginPart.substr(partEqPos + 1);
                            if (pKey == "mode" && pVal == "websocket") {
                                profile.network = "ws";
                            } else if (pKey == "path") {
                                profile.path = pVal;
                            } else if (pKey == "host") {
                                hostFromPlugin = pVal;
                                profile.requesthost = pVal;
                            } else if (pKey == "sni") {
                                profile.sni = pVal;
                            } else if (pKey == "tls") {
                                profile.streamsecurity = "tls";
                            } else if (pKey == "mux") {
                                profile.muxenabled = (pVal == "0") ? "0" : "1";
                            }
                        }
                        if (!profile.sni.empty() && !hostFromPlugin.empty()) {
                            if (profile.sni.find('@') != std::string::npos || 
                                profile.sni.find('\xE2\x80') != std::string::npos ||
                                profile.sni.find(' ') != std::string::npos) {
                                profile.sni = hostFromPlugin;
                            }
                        }
                        if (profile.streamsecurity == "tls" && profile.sni.empty() && !hostFromPlugin.empty()) {
                            profile.sni = hostFromPlugin;
                        }
                    }
                    else if (key == "obfs") profile.extra = profile.extra + ",obfs=" + val;
                    else if (key == "obfs-host") profile.requesthost = val;
                    else if (key == "obfs-uri") profile.path = val;
                    else if (key == "sni") profile.sni = val;
                    else if (key == "tls") profile.streamsecurity = val;
                    else if (key == "security") profile.streamsecurity = val;
                    else if (key == "path") profile.path = val;
                    else if (key == "host") profile.requesthost = val;
                    else if (key == "mode") profile.headertype = val;
                }
                
                if (profile.extra.empty() && !profile.path.empty()) {
                    profile.network = "ws";
                }
                
                if (!profile.sni.empty()) {
                    profile.streamsecurity = "tls";
                    if (profile.sni.find('@') != std::string::npos || 
                        profile.sni.find('\xE2\x80') != std::string::npos ||
                        profile.sni.find(' ') != std::string::npos) {
                        if (!profile.requesthost.empty()) {
                            profile.sni = profile.requesthost;
                        }
                    }
                }
                
                if (profile.streamsecurity == "tls" && profile.sni.empty() && !profile.requesthost.empty()) {
                    profile.sni = profile.requesthost;
                }
            }
            
            std::string decodedInfo = decodeBase64(userInfo);
            size_t methodPos = decodedInfo.find(':');
            if (methodPos != std::string::npos) {
                profile.security = decodedInfo.substr(0, methodPos);
                profile.id = decodedInfo.substr(methodPos + 1);
            }
} else if (line.find("trojan://") == 0) {
            std::string uri = line.substr(9);
            size_t atPos = uri.find('@');
            if (atPos == std::string::npos) continue;
            
            profile.configtype = "6";
            profile.configversion = "2";
            profile.alterid = "0";
            profile.network = "";
            profile.coretype = "";
            profile.muxenabled = "0";
            profile.id = uri.substr(0, atPos);
            if (!profile.id.empty() && profile.id.front() == '/') {
                profile.id.erase(profile.id.begin());
            }
            
            std::string hostPart = uri.substr(atPos + 1);
            size_t qPos = hostPart.find('?');
            std::string addrPart = (qPos != std::string::npos) ? hostPart.substr(0, qPos) : hostPart;
            
auto [addr, port] = parseAddressPort(addrPart);
            if (addr.empty()) continue;
            profile.address = addr;
            if (!profile.address.empty() && profile.address.back() == '/') {
                profile.address.pop_back();
            }
            profile.port = port;
            
            if (qPos != std::string::npos) {
                std::string params = hostPart.substr(qPos + 1);
                size_t hashPos = params.find('#');
                std::string query = (hashPos != std::string::npos) ? params.substr(0, hashPos) : params;
                
                std::istringstream paramStream(query);
                std::string param;
                while (std::getline(paramStream, param, '&')) {
                    size_t eqPos = param.find('=');
                    if (eqPos == std::string::npos) continue;
                    std::string key = param.substr(0, eqPos);
                    std::string val = urlDecode(param.substr(eqPos + 1));
                    
                    if (key == "sni") profile.sni = val;
                    else if (key == "allowInsecure") profile.allowinsecure = (val == "1" || val == "true") ? "true" : "false";
                    else if (key == "alpn") profile.alpn = val;
                    else if (key == "fp") profile.fingerprint = val;
                    else if (key == "security") profile.streamsecurity = val;
                    else if (key == "path") profile.path = val;
                    else if (key == "host") profile.requesthost = val;
else if (key == "type") profile.network = val;
                     else if (key == "network") profile.network = val;
                 }
                 
                 // Validate and map network value
                 if (profile.network == "splithttp") {
                     profile.network = "xhttp";
                 }
                 if (profile.network != "tcp" && profile.network != "ws" && profile.network != "h2" &&
                     profile.network != "xhttp" && profile.network != "grpc") {
                     profile.network = "tcp";
                 }
                
                if (hashPos != std::string::npos) {
                    profile.remarks = urlDecode(params.substr(hashPos + 1));
                }
            }
            
            if (!profile.sni.empty()) {
                profile.streamsecurity = "tls";
            }
            
if (!profile.extra.empty() && profile.extra.front() == ',') {
                profile.extra.erase(profile.extra.begin());
            }
        } else if (line.find("hysteria2://") == 0 || line.find("hy2://") == 0) {
            std::string uri = line.find("hysteria2://") == 0 ? line.substr(12) : line.substr(5);
            size_t atPos = uri.find('@');
            if (atPos == std::string::npos) continue;
            
            profile.configtype = "7";
            profile.configversion = "2";
            profile.alterid = "0";
            profile.network = "";
            profile.coretype = "";
            profile.muxenabled = "0";
            profile.id = uri.substr(0, atPos);
            if (!profile.id.empty() && profile.id.front() == '/') {
                profile.id.erase(profile.id.begin());
            }
            
            std::string hostPart = uri.substr(atPos + 1);
            size_t qPos = hostPart.find('?');
            std::string addrPart = (qPos != std::string::npos) ? hostPart.substr(0, qPos) : hostPart;
            
            size_t colonPos = addrPart.find(':');
            if (colonPos == std::string::npos) continue;
            profile.address = addrPart.substr(0, colonPos);
            if (!profile.address.empty() && profile.address.back() == '/') {
                profile.address.pop_back();
            }
            profile.port = addrPart.substr(colonPos + 1);
            if (!profile.port.empty() && profile.port.back() == '/') {
                profile.port.pop_back();
            }
            
            if (qPos != std::string::npos) {
                std::string params = hostPart.substr(qPos + 1);
                size_t hashPos = params.find('#');
                std::string query = (hashPos != std::string::npos) ? params.substr(0, hashPos) : params;
                
                std::istringstream paramStream(query);
                std::string param;
                while (std::getline(paramStream, param, '&')) {
                    size_t eqPos = param.find('=');
                    if (eqPos == std::string::npos) continue;
                    std::string key = param.substr(0, eqPos);
                    std::string val = urlDecode(param.substr(eqPos + 1));
                    
                    if (key == "sni") profile.sni = val;
                    else if (key == "security") profile.streamsecurity = val;
                    else if (key == "up") profile.extra = "up=" + val;
                    else if (key == "down") profile.extra = profile.extra + ",down=" + val;
                    else if (key == "obfs") {
                        if (val == "salamander") profile.headertype = "none";
                    }
                    else if (key == "obfs-password") {
                        profile.path = val;
                    }
                    else if (key == "mport") profile.ports = val;
                    else if (key == "pinSHA256") profile.certsha = val;
                    else if (key == "insecure" || key == "allowInsecure") profile.allowinsecure = (val == "1" || val == "true") ? "true" : "false";
                }
                
                if (hashPos != std::string::npos) {
                    profile.remarks = urlDecode(params.substr(hashPos + 1));
                }
            }
            
            if (!profile.sni.empty()) {
                profile.streamsecurity = "tls";
            }
            
            if (!profile.extra.empty() && profile.extra.front() == ',') {
                profile.extra.erase(profile.extra.begin());
            }
        } else {
            continue;
        }
        
        profile.indexid = utils::generateUniqueId();
        profiles.push_back(profile);
        validCount++;
    }
    
    Logger::write("INFO: Parsed " + std::to_string(validCount) + " valid profiles from " + std::to_string(lineCount) + " lines", LogLevel::INFO);
    return profiles;
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
    for (const auto& p : profiles) {
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
    for (const auto& p : validProfiles) {
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

    auto result = proxyFinder_->findFirstWorkingProxy(targetUrl);
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
        auto lastUpdate = std::chrono::system_clock::from_time_t(std::stoll(sub.updatetime));
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastUpdate).count();
        return elapsed < intervalMinutes;
    } catch (...) {
        return false;
    }
}

std::vector<SubitemUpdaterV2::UpdateMethod> SubitemUpdaterV2::parseUpdateMethods(
    const std::vector<std::string>& methods)
{
    std::vector<UpdateMethod> result;
    for (const auto& m : methods) {
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
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    return std::to_string(timestamp);
}

std::pair<std::string, std::string> SubitemUpdaterV2::parseAddressPort(const std::string& addrPart) {
    auto trimTrailingSlash = [](std::string s) -> std::string {
        if (!s.empty() && s.back() == '/') {
            s.pop_back();
        }
        return s;
    };

    if (addrPart.find("[|:") == 0) {
        size_t ipStart = addrPart.find("ffff:") + 5;
        size_t ipEnd = addrPart.find("]:", ipStart);
        if (ipStart != std::string::npos && ipEnd != std::string::npos && ipEnd > ipStart) {
            std::string ip = addrPart.substr(ipStart, ipEnd - ipStart);
            std::string port = trimTrailingSlash(addrPart.substr(ipEnd + 2));
            return {ip, port};
        }
    }

    if (addrPart.find('[') == 0) {
        size_t closeBracket = addrPart.find(']');
        if (closeBracket != std::string::npos && closeBracket + 1 < addrPart.length()) {
            std::string ip = addrPart.substr(1, closeBracket - 1);
            std::string port = trimTrailingSlash(addrPart.substr(closeBracket + 2));
            return {ip, port};
        }
    }

    size_t colonPos = addrPart.find(':');
    if (colonPos == std::string::npos) {
        return {addrPart, ""};
    }
    std::string addr = addrPart.substr(0, colonPos);
    std::string port = trimTrailingSlash(addrPart.substr(colonPos + 1));
    return {addr, port};
}

std::string SubitemUpdaterV2::decodeBase64(const std::string& input) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    int in = 0;
    int padding = 0;

    for (size_t idx = 0; idx < input.size(); idx++) {
        char c = input[idx];
        if (c == '=') {
            padding++;
            in = 0;
        } else {
            in = c;
        }

        if (in < 0 || in > 127) continue;
        if (c == '=') continue;

        size_t pos = 0;
        for (size_t k = 0; k < sizeof(base64_chars); k++) {
            if (base64_chars[k] == c) {
                pos = k;
                break;
            }
        }

        char_array_4[i] = pos;
        i++;

        if (i == 4) {
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0x0F) << 4) + ((char_array_4[2] & 0x3C) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];

            for (int k = 0; k < 3; k++) {
                ret += char_array_3[k];
            }

            i = 0;
        }
    }

    if (i > 0) {
        for (int k = i; k < 4; k++) {
            char_array_4[k] = 0;
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0x0F) << 4) + ((char_array_4[2] & 0x3C) >> 2);

        for (int k = 0; k < i - 1; k++) {
            ret += char_array_3[k];
        }
    }

    return ret;
}

std::string SubitemUpdaterV2::urlDecode(const std::string& input) {
    std::string result = input;
    bool changed = true;
    int maxIterations = 2;
    while (changed && maxIterations-- > 0) {
        changed = false;
        std::string decoded;
        for (size_t i = 0; i < result.size(); i++) {
            if (result[i] == '%' && i + 2 < result.size()) {
                std::string hexStr = result.substr(i + 1, 2);
                for (char& c : hexStr) c = std::tolower(c);
                int value;
                std::istringstream iss(hexStr);
                if (iss >> std::hex >> value) {
                    decoded += static_cast<char>(value);
                    i += 2;
                    changed = true;
                } else {
                    decoded += result[i];
                }
            } else if (result[i] == '+' && maxIterations == 1) {
                decoded += ' ';
                changed = true;
            } else {
                decoded += result[i];
            }
        }
        result = decoded;
    }
    return result;
}

int SubitemUpdaterV2::deduplicatePhase0() {
    if (config_.dedup_subids.empty()) {
        Logger::write("INFO: Phase 0 skipped - no dedup_subids configured", LogLevel::INFO);
        return 0;
    }
    
    int updated = 0;
    
    std::string protectedSubId = config_.dedup_subids[0];
    
    std::string sql = "UPDATE ProfileItem SET SubId = '" + protectedSubId + "' WHERE IndexId IN (SELECT pi.IndexId FROM ProfileItem pi JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId WHERE pe.Delay > 0 AND pe.Delay != '-1')";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Phase0 update failed - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return 0;
    }
    
    updated += sqlite3_changes(db_);
    
    if (config_.dedup_subids.size() > 1) {
        std::string fallbackSubId = config_.dedup_subids[1];
        
        sql = "UPDATE ProfileItem SET SubId = '" + fallbackSubId + "' WHERE SubId = '" + protectedSubId + "' AND IndexId IN (SELECT pi.IndexId FROM ProfileItem pi JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId WHERE pe.Delay <= 0 OR pe.Delay = '-1')";
        
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            Logger::write("ERROR: Phase0 fallback update failed - " + std::string(errMsg), LogLevel::ERR);
            sqlite3_free(errMsg);
            return updated;
        }
        
        updated += sqlite3_changes(db_);
        Logger::write("INFO: Phase 0 updated: " + std::to_string(updated) + " proxies (protected: " + protectedSubId + ", fallback: " + fallbackSubId + ")", LogLevel::INFO);
    } else {
        Logger::write("INFO: Phase 0 updated: " + std::to_string(updated) + " proxies to subid: " + protectedSubId, LogLevel::INFO);
    }
    
    return updated;
}

int SubitemUpdaterV2::deduplicatePhase1() {
    std::string subidsList;
    for (size_t i = 0; i < config_.dedup_subids.size(); ++i) {
        if (i > 0) subidsList += ", ";
        subidsList += "'" + config_.dedup_subids[i] + "'";
    }
    
    std::string sql = "DELETE FROM ProfileItem WHERE (";
    sql += "Address LIKE '10.%' OR ";
    sql += "Address LIKE '172.16.%' OR Address LIKE '172.17.%' OR Address LIKE '172.18.%' OR ";
    sql += "Address LIKE '172.19.%' OR Address LIKE '172.20.%' OR Address LIKE '172.21.%' OR ";
    sql += "Address LIKE '172.22.%' OR Address LIKE '172.23.%' OR Address LIKE '172.24.%' OR ";
    sql += "Address LIKE '172.25.%' OR Address LIKE '172.26.%' OR Address LIKE '172.27.%' OR ";
    sql += "Address LIKE '172.28.%' OR Address LIKE '172.29.%' OR Address LIKE '172.30.%' OR ";
    sql += "Address LIKE '172.31.%' OR Address LIKE '192.168.%' OR ";
    sql += "LENGTH(Address) < 5 OR Address NOT LIKE '%.%' OR Address LIKE '127.%' OR ";
    sql += "Address = '0.0.0.0' OR Address LIKE '% %' OR Address LIKE '[%' OR ";
    sql += "Address LIKE '%:%' OR Address LIKE '%[%]%' OR Address LIKE '%@%' OR ";
    sql += "Address LIKE 'http://%' OR Address LIKE 'https://%' OR Address LIKE '%.'";
    sql += " OR SubId = '' OR SubId IS NULL";
    sql += " OR (SubId NOT IN (SELECT Id FROM SubItem))";
    
    if (!config_.dedup_subids.empty()) {
        sql += " OR (StreamSecurity = '' AND SubId NOT IN (" + subidsList + "))";
    }
    sql += ")";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Phase1 dedup failed - " + std::string(errMsg), LogLevel::ERR);
        Logger::write("ERROR: SQL: " + sql, LogLevel::ERR);
        sqlite3_free(errMsg);
        return 0;
    }
    
    int deleted = sqlite3_changes(db_);
    Logger::write("INFO: Phase 1 deleted: " + std::to_string(deleted) + " (invalid addresses)", LogLevel::INFO);
    
    if (!config_.dedup_subids.empty()) {
        std::string sql2 = "DELETE FROM ProfileItem WHERE (";
        sql2 += "Port <= 0 OR Port > 65535 OR Port IS NULL";
        sql2 += ")";
        
        char* errMsg2 = nullptr;
        if (sqlite3_exec(db_, sql2.c_str(), nullptr, nullptr, &errMsg2) != SQLITE_OK) {
            Logger::write("ERROR: Phase1b dedup failed - " + std::string(errMsg2), LogLevel::ERR);
            sqlite3_free(errMsg2);
        } else {
            int deleted2 = sqlite3_changes(db_);
            deleted += deleted2;
            Logger::write("INFO: Phase 1b deleted: " + std::to_string(deleted2) + " (invalid ports)", LogLevel::INFO);
        }

        // Phase 1c: Remove invalid REALITY proxies (missing PublicKey or Sni)
        std::string sql3 = "DELETE FROM ProfileItem WHERE StreamSecurity = 'reality' AND (PublicKey IS NULL OR PublicKey = '')";
        char* errMsg3 = nullptr;
        if (sqlite3_exec(db_, sql3.c_str(), nullptr, nullptr, &errMsg3) != SQLITE_OK) {
            Logger::write("ERROR: Phase1c dedup failed - " + std::string(errMsg3), LogLevel::ERR);
            sqlite3_free(errMsg3);
        } else {
            int deleted3 = sqlite3_changes(db_);
            deleted += deleted3;
            Logger::write("INFO: Phase 1c deleted: " + std::to_string(deleted3) + " (REALITY missing PublicKey)", LogLevel::INFO);
        }

        std::string sql4 = "DELETE FROM ProfileItem WHERE StreamSecurity = 'reality' AND (Sni IS NULL OR Sni = '')";
        char* errMsg4 = nullptr;
        if (sqlite3_exec(db_, sql4.c_str(), nullptr, nullptr, &errMsg4) != SQLITE_OK) {
            Logger::write("ERROR: Phase1c dedup failed (Sni) - " + std::string(errMsg4), LogLevel::ERR);
            sqlite3_free(errMsg4);
        } else {
            int deleted4 = sqlite3_changes(db_);
            deleted += deleted4;
            Logger::write("INFO: Phase 1c deleted: " + std::to_string(deleted4) + " (REALITY missing Sni)", LogLevel::INFO);
        }
    }
    
    return deleted;
}

int SubitemUpdaterV2::deduplicateMergedPhase() {
    if (config_.dedup_subids.empty()) {
        Logger::write("INFO: Merged dedup skipped: no dedup_subids configured", LogLevel::INFO);
        return 0;
    }
    
    std::string subidsList;
    for (size_t i = 0; i < config_.dedup_subids.size(); ++i) {
        if (i > 0) subidsList += ", ";
        subidsList += "'" + config_.dedup_subids[i] + "'";
    }
    
    std::string sql =
        "DELETE FROM ProfileItem WHERE IndexId IN ("
        "SELECT IndexId FROM ("
        "SELECT pi.IndexId, ROW_NUMBER() OVER ("
        "PARTITION BY lower(pi.Address), pi.Port, pi.ConfigType, lower(pi.Id), lower(pi.Network) "
        "ORDER BY CASE WHEN pi.SubId IN (" + subidsList + ") THEN 0 ELSE 1 END, "
        "CAST(COALESCE(pe.Delay, 0) AS INTEGER) DESC"
        ") as rn FROM ProfileItem pi "
        "LEFT JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId"
        ") WHERE rn > 1"
        ")";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Merged dedup failed - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return 0;
    }
    
    int deleted = sqlite3_changes(db_);
    Logger::write("INFO: Merged dedup deleted: " + std::to_string(deleted) + " (CTE, keep best per combo)", LogLevel::INFO);
    return deleted;
}

int SubitemUpdaterV2::deduplicateBlacklistPhase() {
    if (!config_.blacklist_enabled) {
        Logger::write("INFO: Blacklist phase skipped - blacklist_enabled=false", LogLevel::INFO);
        return 0;
    }
    
    if (config_.blacklist_subid.empty()) {
        Logger::write("INFO: Blacklist phase skipped - blacklist_subid not configured", LogLevel::INFO);
        return 0;
    }
    
    int threshold = config_.blacklist_threshold;
    
    std::string sql = "UPDATE ProfileItem SET SubId = '" + config_.blacklist_subid + "' "
                      "WHERE IndexId IN (SELECT IndexId FROM ProfileExItem "
                      "WHERE consecutive_failures >= " + std::to_string(threshold) + ")";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Blacklist phase failed - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return 0;
    }
    
    int updated = sqlite3_changes(db_);
    Logger::write("INFO: Blacklist phase moved " + std::to_string(updated) + " proxies to blacklist subid: " + config_.blacklist_subid, LogLevel::INFO);
    return updated;
}

void SubitemUpdaterV2::cleanupProfileExItem() {
    std::string sql = "DELETE FROM ProfileExItem WHERE IndexId NOT IN (SELECT IndexId FROM ProfileItem)";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: ProfileExItem cleanup failed - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return;
    }
    
    int deleted = sqlite3_changes(db_);
    Logger::write("INFO: ProfileExItem cleaned: " + std::to_string(deleted) + " orphaned records", LogLevel::INFO);
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
    
    auto profiles = db::models::ProfileitemDAO(srcDb).getAll(sql);
    Logger::write("Found " + std::to_string(profiles.size()) + " valid proxies to migrate", LogLevel::INFO);
    
    int successCount = 0;
    int failCount = 0;
    
    // 4. Migrate each proxy
    for (const auto& profile : profiles) {
        // Skip proxies whose Subid is in dedup_subids when sync_skip_subids is enabled
        if (config_.sync.sync_skip_subids && !profile.subid.empty()) {
            bool skip = false;
            for (const auto& sid : config_.dedup_subids) {
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

    if (failCount > 0) {
        Logger::write("Sync failed: " + std::to_string(failCount) + " proxy(es) failed to migrate", LogLevel::ERR);
    }

    sqlite3_close(srcDb);
    sqlite3_close(dstDb);

    return failCount == 0;
}

bool SubitemUpdaterV2::deduplicate() {
    Logger::write("========================================", LogLevel::INFO);
    Logger::write("INFO: Starting Deduplication", LogLevel::INFO);
    Logger::write("========================================", LogLevel::INFO);
    
    std::string countSql = "SELECT COUNT(*) FROM ProfileItem";
    sqlite3_stmt* stmt = nullptr;
    int totalBefore = 0;
    if (sqlite3_prepare_v2(db_, countSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            totalBefore = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    Logger::write("Total proxies before: " + std::to_string(totalBefore), LogLevel::REPORT);
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Failed to begin transaction - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    
    Logger::write("Phase 1/4 - Marking working proxies with protected subid", LogLevel::REPORT);
    int p0 = deduplicatePhase0();
    Logger::write("Phase 1 completed: " + std::to_string(p0) + " proxies marked", LogLevel::REPORT);
    
    Logger::write("Phase 2/4 - Moving blacklisted proxies to blacklist subid", LogLevel::REPORT);
    int pBlacklist = deduplicateBlacklistPhase();
    Logger::write("Phase 2 completed: moved " + std::to_string(pBlacklist) + " proxies to blacklist", LogLevel::REPORT);
    
    Logger::write("Phase 3/4 - Removing invalid addresses (private IPs)", LogLevel::REPORT);
    int p1 = deduplicatePhase1();
    Logger::write("Phase 3 completed: removed " + std::to_string(p1) + " proxies", LogLevel::REPORT);
    
    Logger::write("Phase 4/4 - Removing duplicates (merged CTE)", LogLevel::REPORT);
    int pMerged = deduplicateMergedPhase();
    Logger::write("Phase 4 completed: removed " + std::to_string(pMerged) + " proxies", LogLevel::REPORT);
    
    Logger::write("Cleaning up ProfileExItem...", LogLevel::REPORT);
    cleanupProfileExItem();
    
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Failed to commit transaction - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    int totalAfter = 0;
    if (sqlite3_prepare_v2(db_, countSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            totalAfter = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    int totalDeleted = totalBefore - totalAfter;
    
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Deduplication Summary", LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Total deleted: " + std::to_string(totalDeleted), LogLevel::REPORT);
    Logger::write("Total remaining: " + std::to_string(totalAfter), LogLevel::REPORT);
    Logger::write("Dedup completed successfully", LogLevel::REPORT);
    
    return true;
}

// Helper function: extract remarks from URL
std::string SubitemUpdaterV2::extractRemarksFromUrl(const std::string& url) {
    // Format: domain first path - filename (without extension)
    // Example: https://github.com/a/b.txt -> a-b
    
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return "imported";
    
    std::string pathPart = url.substr(schemeEnd + 3);
    size_t pathStart = pathPart.find('/');
    if (pathStart == std::string::npos || pathStart == 0) {
        // Only domain, no path
        return "imported";
    }
    
    std::string path = pathPart.substr(pathStart + 1);
    std::string domain = pathPart.substr(0, pathStart);
    
    // Get first path segment
    size_t segEnd = path.find('/');
    std::string firstSeg = (segEnd != std::string::npos) ? path.substr(0, segEnd) : path;
    
    // Get filename without extension
    std::string filename = firstSeg;
    size_t lastSlash = firstSeg.find_last_of('/');
    if (lastSlash != std::string::npos) {
        filename = firstSeg.substr(lastSlash + 1);
    }
    
    // Remove extension
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos) {
        filename = filename.substr(0, dotPos);
    }
    
    // Also process firstSeg to remove extension
    dotPos = firstSeg.find_last_of('.');
    if (dotPos != std::string::npos) {
        firstSeg = firstSeg.substr(0, dotPos);
    }
    
    // Extract last path segment (for remarks)
    std::string lastSeg = path;
    size_t lastSlash2 = path.find_last_of('/');
    if (lastSlash2 != std::string::npos) {
        lastSeg = path.substr(lastSlash2 + 1);
    }
    dotPos = lastSeg.find_last_of('.');
    if (dotPos != std::string::npos) {
        lastSeg = lastSeg.substr(0, dotPos);
    }
    
    // Combine: first path segment - last filename
    std::string remarks = firstSeg;
    if (!lastSeg.empty() && lastSeg != firstSeg) {
        remarks = firstSeg + "-" + lastSeg;
    }
    
    // If still empty, use domain
    if (remarks.empty()) {
        remarks = domain;
    }
    
    return remarks;
}

// Helper function: get next sort value (max + 10)
int SubitemUpdaterV2::getNextSortValue() {
    std::string sql = "SELECT MAX(CAST(Sort AS INTEGER)) FROM SubItem";
    sqlite3_stmt* stmt = nullptr;
    int maxSort = 0;
    
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            maxSort = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    return maxSort + 10;
}

// Helper function: check if URL already exists
bool SubitemUpdaterV2::isUrlExists(const std::string& url) {
    std::string sql = "SELECT COUNT(*) FROM SubItem WHERE Url = ?";
    sqlite3_stmt* stmt = nullptr;
    bool exists = false;
    
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            exists = sqlite3_column_int(stmt, 0) > 0;
        }
        sqlite3_finalize(stmt);
    }
    
    return exists;
}

// Helper function: check if URL has valid path
bool SubitemUpdaterV2::hasValidPath(const std::string& url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    
    std::string hostPart = url.substr(schemeEnd + 3);
    size_t pathStart = hostPart.find('/');
    
    // Must have a path after domain
    return (pathStart != std::string::npos && pathStart > 0);
}

// Import subitems from file
bool SubitemUpdaterV2::importSubitemsFromFile(const std::string& filePath, 
                                               const std::string& baseDir) {
    // Open file
    std::ifstream file(filePath);
    if (!file.is_open()) {
        Logger::write("ERROR: Cannot open file: " + filePath, LogLevel::ERR);
        return false;
    }
    
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Subitem Import Starting...", LogLevel::REPORT);
    Logger::write("File: " + filePath, LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    
    // Initialize counters
    int totalLines = 0;
    int successCount = 0;
    int skippedCount = 0;
    int failedCount = 0;
    std::vector<std::string> importedList;
    std::vector<std::string> skippedList;
    std::vector<std::string> failedList;
    
    // Get next sort value
    int nextSort = getNextSortValue();
    
    // Read file line by line
    std::string line;
    while (std::getline(file, line)) {
        totalLines++;
        
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Skip empty lines
        if (line.empty()) continue;
        
        // Parse: URL [space remarks]
        std::string url = line;
        std::string remarks;
        
        size_t spacePos = line.find(' ');
        if (spacePos != std::string::npos) {
            url = line.substr(0, spacePos);
            remarks = line.substr(spacePos + 1);
        }
        
        // Validate URL format
        if (!utils::isValidUrlFormat(url)) {
            failedCount++;
            failedList.push_back(url + " (invalid format - no valid domain)");
            Logger::write("ERROR: Invalid URL format: " + url, LogLevel::ERR);
            continue;
        }
        
        // Check if URL has valid path (warning only)
        if (!hasValidPath(url)) {
            Logger::write("WARN: URL has only domain, no path: " + url, LogLevel::WARN);
        }
        
        // Check for duplicates
        if (isUrlExists(url)) {
            skippedCount++;
            skippedList.push_back(url + " (already exists)");
            Logger::write("SKIPPED: URL already exists: " + url, LogLevel::WARN);
            continue;
        }
        
        // Generate subitem
        db::models::Subitem subitem;
        subitem.id = utils::generateUniqueId();
        subitem.remarks = remarks.empty() ? extractRemarksFromUrl(url) : remarks;
        subitem.url = url;
        subitem.enabled = "1";
        subitem.autoupdateinterval = "1440";
        subitem.updatetime = "0";
        subitem.sort = std::to_string(nextSort);
        
        // Insert into database
        bool insertSuccess = insertSubItem(db_, subitem);
        
        if (insertSuccess) {
            successCount++;
            importedList.push_back("[" + subitem.remarks + "] " + url);
            Logger::write("Imported: [" + subitem.remarks + "] " + url, LogLevel::INFO);
            nextSort += 10;
        } else {
            failedCount++;
            failedList.push_back(url + " (database insert failed)");
            Logger::write("ERROR: Failed to insert: " + url, LogLevel::ERR);
        }
    }
    
    file.close();
    
    // Print summary
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Subitem Import Summary", LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Total lines: " + std::to_string(totalLines), LogLevel::REPORT);
    Logger::write("Success: " + std::to_string(successCount), LogLevel::REPORT);
    Logger::write("Skipped (duplicates): " + std::to_string(skippedCount), LogLevel::REPORT);
    Logger::write("Failed (invalid format): " + std::to_string(failedCount), LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    
    if (!importedList.empty()) {
        Logger::write("Imported URLs:", LogLevel::INFO);
        for (size_t i = 0; i < importedList.size(); i++) {
            Logger::write("  " + std::to_string(i + 1) + ". " + importedList[i], LogLevel::INFO);
        }
    }

    if (!skippedList.empty()) {
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Skipped URLs (duplicates):", LogLevel::WARN);
        for (size_t i = 0; i < skippedList.size(); i++) {
            Logger::write("  " + std::to_string(i + 1) + ". " + skippedList[i], LogLevel::WARN);
        }
    }

    if (!failedList.empty()) {
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Failed URLs (invalid format):", LogLevel::ERR);
        for (size_t i = 0; i < failedList.size(); i++) {
            Logger::write("  " + std::to_string(i + 1) + ". " + failedList[i], LogLevel::ERR);
        }
    }

    Logger::write("========================================", LogLevel::REPORT);
    
    return failedCount == 0;
}

// Import single URL directly
bool SubitemUpdaterV2::importSingleUrl(const std::string& url) {
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Subitem Import Starting (Single URL)...", LogLevel::REPORT);
    Logger::write("URL: " + url, LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    
    // 1. Validate URL format
    if (!utils::isValidUrlFormat(url)) {
        Logger::write("ERROR: Invalid URL format: " + url, LogLevel::ERR);
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Import Summary: Success=0, Skipped=0, Failed=1", LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        return false;
    }
    
    // 2. Check if URL has valid path (warning only)
    if (!hasValidPath(url)) {
        Logger::write("WARN: URL has only domain, no path: " + url, LogLevel::WARN);
    }
    
    // 3. Check for duplicates
    if (isUrlExists(url)) {
        Logger::write("SKIPPED: URL already exists: " + url, LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Import Summary: Success=0, Skipped=1, Failed=0", LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        return true; // Not an error, just skipped
    }
    
    // 4. Get next sort value
    int nextSort = getNextSortValue();
    
    // 5. Generate subitem
    db::models::Subitem subitem;
    subitem.id = utils::generateUniqueId();
    subitem.remarks = extractRemarksFromUrl(url);
    subitem.url = url;
    subitem.enabled = "1";
    subitem.autoupdateinterval = "1440";
    subitem.updatetime = "0";
    subitem.sort = std::to_string(nextSort);
    
    // 6. Insert into database
    bool insertSuccess = insertSubItem(db_, subitem);
    
    // 7. Output result
    if (insertSuccess) {
        Logger::write("Imported: [" + subitem.remarks + "] " + url, LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Import Summary: Success=1, Skipped=0, Failed=0", LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        return true;
    } else {
        Logger::write("ERROR: Failed to insert: " + url, LogLevel::ERR);
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Import Summary: Success=0, Skipped=0, Failed=1", LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        return false;
    }
}

} // namespace update
