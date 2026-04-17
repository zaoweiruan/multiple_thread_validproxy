#include "SubitemUpdaterV2.h"
#include "Subitem.h"
#include "Profileitem.h"
#include "ConfigGenerator.h"
#include "XrayApi.h"
#include "PortManager.h"
#include "Utils.h"

#include <curl/curl.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <array>
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
        } catch (...) {}
        return defaultVal;
    }
}

SubitemUpdaterV2::SubitemUpdaterV2(sqlite3* db,
                                   const std::string& xrayPath,
                                   const config::AppConfig& config,
                                   std::ofstream* logOut,
                                   const std::string& baseDir)
    : db_(db), xrayPath_(xrayPath), config_(config), logOut_(logOut), baseDir_(baseDir),
      xrayMgr_(nullptr), proxyFinder_(nullptr), xrayProcessId_(0), xrayJob_(nullptr) {
}

bool SubitemUpdaterV2::run() {
    log("========================================");
    log("INFO: Starting SubitemUpdaterV2");
    log("INFO: Priority mode: " + config_.priority_mode);
    log("========================================");

    db::models::SubitemDAO subDao(db_);
    auto enabledSubs = subDao.getEnabledSubscriptions();

    if (enabledSubs.empty()) {
        log("INFO: No enabled subscriptions found");
        return false;
    }

    log("INFO: Found " + std::to_string(enabledSubs.size()) + " enabled subscriptions");

    Strategy strategy = parseStrategy(config_.priority_mode);
    
    int proxySocksPort = -1;
    int proxyApiPort = -1;
    (void)proxyApiPort;
    int totalSubs = enabledSubs.size();
    
    if (strategy != Strategy::DirectOnly && !enabledSubs.empty()) {
        std::string testUrl = enabledSubs[0].url;
        if (strategy == Strategy::DirectFirst) {
            log("INFO: Pre-finding fallback proxy (direct_first mode)...");
        } else {
            log("INFO: Pre-finding proxy (proxy_first mode)...");
        }
        auto result = getProxyPorts(testUrl);
        proxySocksPort = result.first;
        proxyApiPort = result.second;
        if (proxySocksPort > 0) {
            log("INFO: Pre-found working proxy, socks=" + std::to_string(proxySocksPort));
        } else {
            log("WARN: Failed to find working proxy");
        }
    }

    int successCount = 0;
    int directSuccessCount = 0;
    int directFailCount = 0;
    int proxySuccessCount = 0;
    int proxyFailCount = 0;

    std::vector<std::tuple<std::string, std::string, std::string>> failedSubs;

    bool runDirectPhase = (strategy == Strategy::DirectFirst || strategy == Strategy::DirectOnly);
    bool runProxyPhase = (strategy != Strategy::DirectOnly && proxySocksPort > 0);

    if (runDirectPhase) {
        log("========================================");
        log("INFO: Phase 1/2 - Direct connection");
        log("========================================");
        
        for (size_t i = 0; i < enabledSubs.size(); ++i) {
            const auto& sub = enabledSubs[i];
            log("INFO: [" + std::to_string(i + 1) + "/" + std::to_string(totalSubs) + "] Processing: " + sub.url);
            
            std::string content = fetchUrl(sub.url);
            if (!content.empty()) {
                log("INFO: Direct connection successful");
                auto profiles = parseSubscription(content, sub.id);
                if (!profiles.empty()) {
                    updateProfileItems(sub.id, profiles);
                    successCount++;
                    directSuccessCount++;
                    log("INFO: Updated successfully: " + sub.id);
                } else {
                    directFailCount++;
                    failedSubs.push_back({sub.id, sub.remarks, sub.url});
                    log("ERROR: Parse failed: " + sub.id);
                }
            } else {
                log("WARN: Direct connection failed");
                directFailCount++;
                failedSubs.push_back({sub.id, sub.remarks, sub.url});
                log("ERROR: Failed to update: " + sub.id);
            }
        }
    } else if (runProxyPhase) {
        for (const auto& sub : enabledSubs) {
            failedSubs.push_back({sub.id, sub.remarks, sub.url});
        }
        log("INFO: Phase 1/1 - Proxy only mode, queuing " + std::to_string(failedSubs.size()) + " subscriptions");
    }
    
    if (runProxyPhase && !failedSubs.empty()) {
        log("========================================");
        log("INFO: Phase 2/2 - Proxy connection (" + std::to_string(failedSubs.size()) + " failed subscriptions)");
        log("========================================");
        
        std::vector<std::tuple<std::string, std::string, std::string>> stillFailedSubs;
        
        for (size_t i = 0; i < failedSubs.size(); ++i) {
            const auto& sub = failedSubs[i];
            log("INFO: [" + std::to_string(i + 1) + "/" + std::to_string(failedSubs.size()) + "] Trying via proxy: " + std::get<2>(sub));
            
            std::string content = fetchUrlViaProxy(std::get<2>(sub), proxySocksPort);
            if (!content.empty()) {
                log("INFO: Proxy connection successful");
                auto profiles = parseSubscription(content, std::get<0>(sub));
                if (!profiles.empty()) {
                    updateProfileItems(std::get<0>(sub), profiles);
                    successCount++;
                    proxySuccessCount++;
                    log("INFO: Updated successfully: " + std::get<0>(sub));
                } else {
                    proxyFailCount++;
                    stillFailedSubs.push_back(sub);
                    log("ERROR: Parse failed: " + std::get<0>(sub));
                }
            } else {
                log("WARN: Proxy connection failed");
                proxyFailCount++;
                stillFailedSubs.push_back(sub);
                log("ERROR: Failed to update: " + std::get<0>(sub));
            }
        }
        failedSubs = stillFailedSubs;
    }

    releaseProxyPorts();

    log("========================================");
    log("INFO: Update Summary");
    log("INFO: Total subscriptions: " + std::to_string(totalSubs));
    log("INFO: Phase 1 (Direct) - Success: " + std::to_string(directSuccessCount) + ", Failed: " + std::to_string(directFailCount));
    if (runProxyPhase) {
        log("INFO: Phase 2 (Proxy) - Success: " + std::to_string(proxySuccessCount) + ", Failed: " + std::to_string(proxyFailCount));
    }
    log("INFO: Total - Success: " + std::to_string(successCount) + ", Failed: " + std::to_string(totalSubs - successCount));
    
    if (!failedSubs.empty()) {
        log("========================================");
        log("INFO: Failed subscriptions:");
        for (const auto& sub : failedSubs) {
            log("INFO:   Id: " + std::get<0>(sub) + ", Remarks: " + std::get<1>(sub) + ", URL: " + std::get<2>(sub));
        }
    }
    log("========================================");

    return successCount > 0;
}

bool SubitemUpdaterV2::runSingle(const std::string& subId) {
    log("INFO: runSingle - subId: " + subId);

    std::string sql = "SELECT * FROM SubItem WHERE Id = '" + subId + "';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        log("ERROR: SQL prepare failed - " + std::string(sqlite3_errmsg(db_)));
        return false;
    }

    db::models::Subitem sub;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        sub = db::models::Subitem::fromStmt(stmt);
    }
    sqlite3_finalize(stmt);

    if (sub.id.empty()) {
        log("ERROR: Subscription not found: " + subId);
        return false;
    }

    Strategy strategy = parseStrategy(config_.priority_mode);
    bool result = updateWithStrategy(sub.url, sub.id, strategy);

    releaseProxyPorts();

    return result;
}

bool SubitemUpdaterV2::runSingleWithProxy(const std::string& subId, int socksPort) {
    log("INFO: runSingleWithProxy - subId: " + subId + ", socksPort: " + std::to_string(socksPort));

    std::string sql = "SELECT * FROM SubItem WHERE Id = '" + subId + "';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        log("ERROR: SQL prepare failed - " + std::string(sqlite3_errmsg(db_)));
        return false;
    }

    db::models::Subitem sub;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        sub = db::models::Subitem::fromStmt(stmt);
    }
    sqlite3_finalize(stmt);

    if (sub.id.empty()) {
        log("ERROR: Subscription not found: " + subId);
        return false;
    }

    std::string content = fetchUrlViaProxy(sub.url, socksPort);
    if (content.empty()) {
        log("ERROR: Failed to fetch via proxy");
        return false;
    }

    auto profiles = parseSubscription(content, sub.id);
    return updateProfileItems(sub.id, profiles);
}

bool SubitemUpdaterV2::updateWithStrategy(const std::string& subUrl, const std::string& subId, Strategy strategy) {
    bool tryDirect = (strategy != Strategy::ProxyFirst);
    bool tryProxy = (strategy != Strategy::DirectOnly);

    if (tryDirect) {
        log("INFO: Trying direct connection...");
        std::string content = fetchUrl(subUrl);
        if (!content.empty()) {
            log("INFO: Direct connection successful");
            auto profiles = parseSubscription(content, subId);
            if (!profiles.empty()) {
                return updateProfileItems(subId, profiles);
            }
        }
        log("WARN: Direct connection failed");
    }

    if (tryProxy && !tryDirect) {
        log("INFO: Trying proxy connection...");
        auto [socksPort, apiPort] = getProxyPorts(subUrl);
        if (socksPort > 0) {
            log("INFO: Using proxy at socks port: " + std::to_string(socksPort));
            std::string content = fetchUrlViaProxy(subUrl, socksPort);
            if (!content.empty()) {
                log("INFO: Proxy connection successful");
                auto profiles = parseSubscription(content, subId);
                if (!profiles.empty()) {
                    return updateProfileItems(subId, profiles);
                }
            }
        }
    }

    return false;
}

std::string SubitemUpdaterV2::fetchUrl(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        log("ERROR: fetchUrl failed - " + std::string(curl_easy_strerror(res)));
        return "";
    }

    return response;
}

std::string SubitemUpdaterV2::fetchUrlViaProxy(const std::string& url, int socksPort) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    std::string proxyStr = "socks5h://127.0.0.1:" + std::to_string(socksPort);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        log("ERROR: fetchUrlViaProxy failed - " + std::string(curl_easy_strerror(res)));
        return "";
    }

    return response;
}

std::vector<db::models::Profileitem> SubitemUpdaterV2::parseSubscription(const std::string& content, const std::string& subid) {
    std::vector<db::models::Profileitem> profiles;
    
    bool hasProtocol = (content.find("://") != std::string::npos);
    std::string decoded;
    
    if (hasProtocol) {
        log("INFO: Content appears to be plain text share links");
        decoded = content;
    } else {
        log("INFO: Attempting base64 decode...");
        decoded = decodeBase64(content);
        
        if (decoded.length() < 10 || decoded.find("://") == std::string::npos) {
            log("WARN: Base64 decode produced invalid content, using original");
            decoded = content;
        }
    }
    
    log("INFO: Final content length: " + std::to_string(decoded.length()));
    
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
                profile.coretype = "0";
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
                profile.remarks = getJsonValueString(obj, "ps", "");
                profile.path = getJsonValueString(obj, "path", "");
                profile.requesthost = getJsonValueString(obj, "host", "");
                profile.streamsecurity = getJsonValueString(obj, "tls", "");
                profile.sni = getJsonValueString(obj, "sni", "");
                profile.flow = getJsonValueString(obj, "flow", "");
                profile.fingerprint = getJsonValueString(obj, "fp", "chrome");
                profile.alpn = getJsonValueString(obj, "alpn", "");
                profile.headertype = getJsonValueString(obj, "type", "");
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
                                profile.coretype = "0";
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
                    log("WARN: Failed to parse vmess: " + errMsg);
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
            profile.coretype = "0";
            profile.muxenabled = "0";
            profile.security = "none";
            profile.id = uri.substr(0, atPos);
            
            std::string hostPart = uri.substr(atPos + 1);
            size_t qPos = hostPart.find('?');
            std::string addrPart = (qPos != std::string::npos) ? hostPart.substr(0, qPos) : hostPart;
            
            auto [addr, port] = parseAddressPort(addrPart);
            if (addr.empty()) continue;
            profile.address = addr;
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
            profile.network = "tcp";
            profile.coretype = "0";
            profile.muxenabled = "0";
            
            std::string userInfo = uri.substr(0, atPos);
            std::string hostInfo = uri.substr(atPos + 1);
            
            auto [addr, portWithParams] = parseAddressPort(hostInfo);
            if (addr.empty()) continue;
            profile.address = addr;
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
                    
                    if (key == "plugin") profile.extra = val;
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
            profile.network = "tcp";
            profile.coretype = "0";
            profile.muxenabled = "0";
            profile.id = uri.substr(0, atPos);
            
            std::string hostPart = uri.substr(atPos + 1);
            size_t qPos = hostPart.find('?');
            std::string addrPart = (qPos != std::string::npos) ? hostPart.substr(0, qPos) : hostPart;
            
            auto [addr, port] = parseAddressPort(addrPart);
            if (addr.empty()) continue;
            profile.address = addr;
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
                    else if (key == "allowInsecure") profile.allowinsecure = val;
                    else if (key == "alpn") profile.alpn = val;
                    else if (key == "fp") profile.fingerprint = val;
                    else if (key == "security") profile.security = val;
                }
                
                if (hashPos != std::string::npos) {
                    profile.remarks = urlDecode(params.substr(hashPos + 1));
                }
            }
        } else if (line.find("hysteria2://") == 0 || line.find("hy2://") == 0) {
            std::string uri = line.find("hysteria2://") == 0 ? line.substr(12) : line.substr(5);
            size_t atPos = uri.find('@');
            if (atPos == std::string::npos) continue;
            
            profile.configtype = "10";
            profile.configversion = "2";
            profile.alterid = "0";
            profile.network = "tcp";
            profile.coretype = "0";
            profile.muxenabled = "0";
            profile.id = uri.substr(0, atPos);
            
            std::string hostPart = uri.substr(atPos + 1);
            size_t qPos = hostPart.find('?');
            std::string addrPart = (qPos != std::string::npos) ? hostPart.substr(0, qPos) : hostPart;
            
            size_t colonPos = addrPart.find(':');
            if (colonPos == std::string::npos) continue;
            profile.address = addrPart.substr(0, colonPos);
            profile.port = addrPart.substr(colonPos + 1);
            
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
                    else if (key == "up") profile.extra = "up=" + val;
                    else if (key == "down") profile.extra = profile.extra + ",down=" + val;
                }
                
                if (hashPos != std::string::npos) {
                    profile.remarks = urlDecode(params.substr(hashPos + 1));
                }
            }
} else {
            continue;
        }
        
        profile.indexid = utils::generateUniqueId();
        profiles.push_back(profile);
        validCount++;
    }
    
    log("INFO: Parsed " + std::to_string(validCount) + " valid profiles from " + std::to_string(lineCount) + " lines");
    return profiles;
}

bool SubitemUpdaterV2::updateProfileItems(const std::string& subid, const std::vector<db::models::Profileitem>& profiles) {
    if (profiles.empty()) return false;

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    char* errMsg = nullptr;

    std::string deleteExSql = "DELETE FROM ProfileExItem WHERE IndexId IN "
                               "(SELECT IndexId FROM ProfileItem WHERE Subid = '" + subid + "');";
    if (sqlite3_exec(db_, deleteExSql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log("ERROR: Failed to delete old profileex items - " + std::string(errMsg));
        sqlite3_free(errMsg);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    std::string deleteSql = "DELETE FROM ProfileItem WHERE Subid = '" + subid + "';";
    if (sqlite3_exec(db_, deleteSql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log("ERROR: Failed to delete old profiles - " + std::string(errMsg));
        sqlite3_free(errMsg);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    int inserted = 0;
    for (const auto& p : profiles) {
        std::ostringstream oss;
        oss << "INSERT INTO ProfileItem (IndexId, ConfigType, ConfigVersion, Address, Port, Id, "
            << "AlterId, Security, Network, Remarks, HeaderType, RequestHost, Path, StreamSecurity, "
            << "AllowInsecure, Subid, IsSub, Flow, Sni, Alpn, CoreType, PreSocksPort, Fingerprint, "
            << "DisplayLog, PublicKey, ShortId, SpiderX, Extra, Ports, Mldsa65Verify, MuxEnabled, Cert) VALUES (";
        oss << "'" << p.indexid << "', ";
        oss << "'" << p.configtype << "', ";
        oss << "'" << p.configversion << "', ";
        oss << "'" << p.address << "', ";
        oss << "'" << p.port << "', ";
        oss << "'" << p.id << "', ";
        oss << "'" << p.alterid << "', ";
        oss << "'" << p.security << "', ";
        oss << "'" << p.network << "', ";
        oss << "'" << p.remarks << "', ";
        oss << "'" << p.headertype << "', ";
        oss << "'" << p.requesthost << "', ";
        oss << "'" << p.path << "', ";
        oss << "'" << p.streamsecurity << "', ";
        oss << "'" << p.allowinsecure << "', ";
        oss << "'" << p.subid << "', ";
        oss << "'" << p.issub << "', ";
        oss << "'" << p.flow << "', ";
        oss << "'" << p.sni << "', ";
        oss << "'" << p.alpn << "', ";
        oss << "'" << p.coretype << "', ";
        oss << "'" << p.presocksport << "', ";
        oss << "'" << p.fingerprint << "', ";
        oss << "'" << p.displaylog << "', ";
        oss << "'" << p.publickey << "', ";
        oss << "'" << p.shortid << "', ";
        oss << "'" << p.spiderx << "', ";
        oss << "'" << p.extra << "', ";
        oss << "'" << p.ports << "', ";
        oss << "'" << p.mldsa65verify << "', ";
        oss << "'" << p.muxenabled << "', ";
        oss << "'" << p.cert << "');";

        if (sqlite3_exec(db_, oss.str().c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            log("WARN: Insert failed - " + std::string(errMsg));
            sqlite3_free(errMsg);
        } else {
            inserted++;

            std::ostringstream exOss;
            exOss << "INSERT INTO ProfileExItem (indexid, delay, speed, sort, message) VALUES (";
            exOss << "'" << p.indexid << "', ";
            exOss << "'0', ";
            exOss << "'0', ";
            exOss << "'0', ";
            exOss << "'NOT_TESTED')";
            if (sqlite3_exec(db_, exOss.str().c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
                log("DEBUG: Failed to insert ProfileExItem - " + std::string(errMsg));
                sqlite3_free(errMsg);
            }
        }
    }

    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    log("INFO: Inserted " + std::to_string(inserted) + " profiles");
    return inserted > 0;
}

std::pair<int, int> SubitemUpdaterV2::getProxyPorts(const std::string& targetUrl) {
    log("INFO: Getting proxy ports via ProxyFinder...");

    std::string configDir = baseDir_.empty() ? "bin/config" : baseDir_ + "/config";
    xrayMgr_ = XrayManager::getInstance(xrayPath_, configDir, config_.xray_workers, logOut_);

    int started = xrayMgr_->start(1, config_.xray_start_port, config_.xray_api_port);
    if (started == 0) {
        log("ERROR: Failed to start xray instance");
        XrayManager::release();
        xrayMgr_ = nullptr;
        return {-1, -1};
    }

    proxyFinder_ = new ProxyFinder(db_, xrayMgr_, xrayPath_,
                                   config_.test_url,
                                   targetUrl,
                                   config_.test_timeout_ms,
                                   logOut_);

    auto result = proxyFinder_->findFirstWorkingProxy(targetUrl);
    log("INFO: ProxyFinder returned socks=" + std::to_string(result.first) + ", api=" + std::to_string(result.second));

    return result;
}

void SubitemUpdaterV2::releaseProxyPorts() {
    if (!xrayMgr_ && !proxyFinder_) {
        return;
    }
    log("INFO: Releasing proxy ports...");

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

    log("INFO: Proxy ports released");
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

SubitemUpdaterV2::Strategy SubitemUpdaterV2::parseStrategy(const std::string& mode) {
    if (mode == "proxy_first") return Strategy::ProxyFirst;
    if (mode == "direct_only") return Strategy::DirectOnly;
    return Strategy::DirectFirst;
}

std::string SubitemUpdaterV2::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}

std::pair<std::string, std::string> SubitemUpdaterV2::parseAddressPort(const std::string& addrPart) {
    if (addrPart.find("[|:") == 0) {
        size_t ipStart = addrPart.find("ffff:") + 5;
        size_t ipEnd = addrPart.find("]:", ipStart);
        if (ipStart != std::string::npos && ipEnd != std::string::npos && ipEnd > ipStart) {
            std::string ip = addrPart.substr(ipStart, ipEnd - ipStart);
            std::string port = addrPart.substr(ipEnd + 2);
            return {ip, port};
        }
    }

    if (addrPart.find('[') == 0) {
        size_t closeBracket = addrPart.find(']');
        if (closeBracket != std::string::npos && closeBracket + 1 < addrPart.length()) {
            std::string ip = addrPart.substr(1, closeBracket - 1);
            std::string port = addrPart.substr(closeBracket + 2);
            return {ip, port};
        }
    }

    size_t colonPos = addrPart.find(':');
    if (colonPos == std::string::npos) {
        return {addrPart, ""};
    }
    return {addrPart.substr(0, colonPos), addrPart.substr(colonPos + 1)};
}

void SubitemUpdaterV2::log(const std::string& msg) {
    std::cout << msg << std::endl;
    if (logOut_ && !logOut_->fail()) {
        *logOut_ << msg << std::endl;
        logOut_->flush();
    }
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
    std::string result;
    for (size_t i = 0; i < input.size(); i++) {
        if (input[i] == '%' && i + 2 < input.size()) {
            int value;
            std::istringstream iss(input.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += input[i];
            }
        } else if (input[i] == '+') {
            result += ' ';
        } else {
            result += input[i];
        }
    }
    return result;
}

int SubitemUpdaterV2::deduplicatePhase0() {
    if (config_.dedup_subids.empty()) {
        log("INFO: Phase 0 skipped - no dedup_subids configured");
        return 0;
    }
    
    int updated = 0;
    
    std::string protectedSubId = config_.dedup_subids[0];
    
    std::string sql = "UPDATE ProfileItem SET SubId = '" + protectedSubId + "' WHERE IndexId IN (SELECT pi.IndexId FROM ProfileItem pi JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId WHERE pe.Delay > 0 AND pe.Delay != '-1')";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log("ERROR: Phase0 update failed - " + std::string(errMsg));
        sqlite3_free(errMsg);
        return 0;
    }
    
    updated += sqlite3_changes(db_);
    
    if (config_.dedup_subids.size() > 1) {
        std::string fallbackSubId = config_.dedup_subids[1];
        
        sql = "UPDATE ProfileItem SET SubId = '" + fallbackSubId + "' WHERE SubId = '" + protectedSubId + "' AND IndexId IN (SELECT pi.IndexId FROM ProfileItem pi JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId WHERE pe.Delay <= 0 OR pe.Delay = '-1')";
        
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            log("ERROR: Phase0 fallback update failed - " + std::string(errMsg));
            sqlite3_free(errMsg);
            return updated;
        }
        
        updated += sqlite3_changes(db_);
        log("INFO: Phase 0 updated: " + std::to_string(updated) + " proxies (protected: " + protectedSubId + ", fallback: " + fallbackSubId + ")");
    } else {
        log("INFO: Phase 0 updated: " + std::to_string(updated) + " proxies to subid: " + protectedSubId);
    }
    
    return updated;
}

int SubitemUpdaterV2::deduplicatePhase1() {
    std::string sql = R"(
        DELETE FROM ProfileItem 
        WHERE 
            Address LIKE '10.%'
            OR Address LIKE '172.16.%' OR Address LIKE '172.17.%' OR Address LIKE '172.18.%'
            OR Address LIKE '172.19.%' OR Address LIKE '172.20.%' OR Address LIKE '172.21.%'
            OR Address LIKE '172.22.%' OR Address LIKE '172.23.%' OR Address LIKE '172.24.%'
            OR Address LIKE '172.25.%' OR Address LIKE '172.26.%' OR Address LIKE '172.27.%'
            OR Address LIKE '172.28.%' OR Address LIKE '172.29.%' OR Address LIKE '172.30.%'
            OR Address LIKE '172.31.%' OR Address LIKE '192.168.%'
            OR LENGTH(Address) < 5
            OR Address NOT LIKE '%.%'
            OR Address LIKE '127.%'
            OR Address = '0.0.0.0'
            OR Address LIKE '% %'
            OR Address LIKE '[%'
            OR Address LIKE '%:%'
            OR Address LIKE '%[%]%'
            OR Address LIKE '%@%'
            OR Address LIKE 'http://%'
            OR Address LIKE 'https://%'
            OR Address LIKE '%.'
    )";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log("ERROR: Phase1 dedup failed - " + std::string(errMsg));
        sqlite3_free(errMsg);
        return 0;
    }
    
    int deleted = sqlite3_changes(db_);
    log("INFO: Phase 1 deleted: " + std::to_string(deleted) + " (invalid addresses)");
    return deleted;
}

int SubitemUpdaterV2::deduplicatePhase2() {
    std::string sql = R"(
        DELETE FROM ProfileItem 
        WHERE IndexId IN (
            SELECT pi.IndexId FROM ProfileItem pi
            JOIN (
                SELECT Address, Port, Network, MIN(pe.Delay) as MinDelay
                FROM ProfileItem p
                JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
                WHERE pe.Delay > 0 AND pe.Delay != '-1'
                GROUP BY Address, Port, Network
            ) valid ON pi.Address = valid.Address AND pi.Port = valid.Port AND pi.Network = valid.Network
            JOIN ProfileExItem pe2 ON pi.IndexId = pe2.IndexId
            WHERE pe2.Delay > valid.MinDelay
        )
    )";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log("ERROR: Phase2 dedup failed - " + std::string(errMsg));
        sqlite3_free(errMsg);
        return 0;
    }
    
    int deleted = sqlite3_changes(db_);
    log("INFO: Phase 2 deleted: " + std::to_string(deleted));
    return deleted;
}

int SubitemUpdaterV2::deduplicatePhase3() {
    if (config_.dedup_subids.empty()) {
        std::string sql = R"(
            DELETE FROM ProfileItem 
            WHERE IndexId IN (
                SELECT pi.IndexId FROM ProfileItem pi
                JOIN (
                    SELECT Address, Port, Network, MIN(IndexId) as MinIndexId
                    FROM ProfileItem
                    GROUP BY Address, Port, Network
                ) dup ON pi.Address = dup.Address AND pi.Port = dup.Port AND pi.Network = dup.Network
                WHERE pi.IndexId > dup.MinIndexId
            )
        )";
        
        char* errMsg = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            log("ERROR: Phase3 dedup failed - " + std::string(errMsg));
            sqlite3_free(errMsg);
            return 0;
        }
        
        int deleted = sqlite3_changes(db_);
        log("INFO: Phase 3 deleted: " + std::to_string(deleted));
        return deleted;
    }
    
    std::string subidsList;
    for (size_t i = 0; i < config_.dedup_subids.size(); ++i) {
        if (i > 0) subidsList += ", ";
        subidsList += "'" + config_.dedup_subids[i] + "'";
    }
    
    std::string sql = "DELETE FROM ProfileItem WHERE SubId NOT IN (" + subidsList + ") AND IndexId IN (SELECT pi.IndexId FROM ProfileItem pi JOIN (SELECT Address, Port, Network, MIN(IndexId) as MinIndexId FROM ProfileItem WHERE SubId NOT IN (" + subidsList + ") GROUP BY Address, Port, Network) dup ON pi.Address = dup.Address AND pi.Port = dup.Port AND pi.Network = dup.Network WHERE pi.IndexId > dup.MinIndexId)";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log("ERROR: Phase3 dedup failed - " + std::string(errMsg));
        sqlite3_free(errMsg);
        return 0;
    }
    
    int deleted = sqlite3_changes(db_);
    log("INFO: Phase 3 deleted: " + std::to_string(deleted));
    return deleted;
}

int SubitemUpdaterV2::deduplicatePhase4() {
    if (config_.dedup_subids.empty()) {
        std::string sql = R"(
            DELETE FROM ProfileItem 
            WHERE IndexId IN (
                SELECT pi.IndexId FROM ProfileItem pi
                JOIN (
                    SELECT Address, Port, Network, MIN(IndexId) as MinIndexId
                    FROM ProfileItem
                    GROUP BY Address, Port, Network
                ) dup ON pi.Address = dup.Address AND pi.Port = dup.Port AND pi.Network = dup.Network
                WHERE pi.IndexId > dup.MinIndexId
            )
        )";
        
        char* errMsg = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            log("ERROR: Phase4 dedup failed - " + std::string(errMsg));
            sqlite3_free(errMsg);
            return 0;
        }
        
        int deleted = sqlite3_changes(db_);
        log("INFO: Phase 4 deleted: " + std::to_string(deleted));
        return deleted;
    }
    
    std::string subidsList;
    for (size_t i = 0; i < config_.dedup_subids.size(); ++i) {
        if (i > 0) subidsList += ", ";
        subidsList += "'" + config_.dedup_subids[i] + "'";
    }
    
    std::string sql = "DELETE FROM ProfileItem WHERE SubId NOT IN (" + subidsList + ") AND IndexId IN (SELECT pi.IndexId FROM ProfileItem pi JOIN (SELECT Address, Port, Network, MIN(IndexId) as MinIndexId FROM ProfileItem WHERE SubId NOT IN (" + subidsList + ") GROUP BY Address, Port, Network) dup ON pi.Address = dup.Address AND pi.Port = dup.Port AND pi.Network = dup.Network WHERE pi.IndexId > dup.MinIndexId)";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log("ERROR: Phase4 dedup failed - " + std::string(errMsg));
        sqlite3_free(errMsg);
        return 0;
    }
    
    int deleted = sqlite3_changes(db_);
    log("INFO: Phase 4 deleted: " + std::to_string(deleted));
    return deleted;
}

void SubitemUpdaterV2::cleanupProfileExItem() {
    std::string sql = "DELETE FROM ProfileExItem WHERE IndexId NOT IN (SELECT IndexId FROM ProfileItem)";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log("ERROR: ProfileExItem cleanup failed - " + std::string(errMsg));
        sqlite3_free(errMsg);
        return;
    }
    
    int deleted = sqlite3_changes(db_);
    log("INFO: ProfileExItem cleaned: " + std::to_string(deleted) + " orphaned records");
}

bool SubitemUpdaterV2::deduplicate() {
    log("========================================");
    log("INFO: Starting Deduplication");
    log("========================================");
    
    std::string countSql = "SELECT COUNT(*) FROM ProfileItem";
    sqlite3_stmt* stmt = nullptr;
    int totalBefore = 0;
    if (sqlite3_prepare_v2(db_, countSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            totalBefore = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    log("INFO: Total proxies before: " + std::to_string(totalBefore));
    
    log("INFO: Phase 0/5 - Marking working proxies with protected subid");
    int p0 = deduplicatePhase0();
    
    log("INFO: Phase 1/5 - Removing invalid addresses (private IPs)");
    int p1 = deduplicatePhase1();
    
    log("INFO: Phase 2/5 - Removing duplicates with delay>0 proxies");
    int p2 = deduplicatePhase2();
    
    log("INFO: Phase 3/5 - Keeping dedup_subids, removing duplicates");
    int p3 = deduplicatePhase3();
    
    log("INFO: Phase 4/5 - Full deduplication excluding subids");
    int p4 = deduplicatePhase4();
    
    log("INFO: Cleaning up ProfileExItem...");
    cleanupProfileExItem();
    
    int totalAfter = 0;
    if (sqlite3_prepare_v2(db_, countSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            totalAfter = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    int totalDeleted = totalBefore - totalAfter;
    
    log("========================================");
    log("INFO: Deduplication Summary");
    log("========================================");
    log("INFO: Total deleted: " + std::to_string(totalDeleted));
    log("INFO: Total remaining: " + std::to_string(totalAfter));
    log("INFO: Dedup completed successfully");
    
    return true;
}

} // namespace update