#include "SubitemUpdater.h"
#include <boost/json.hpp>
#include <stdexcept>
#include <random>

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string generateUniqueId() {
    static std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t val = rng();
    return std::to_string(val);
}

SubitemUpdater::SubitemUpdater(sqlite3* db, std::ofstream* logOut, const std::string& xrayPath, int xrayApiPort, int testTimeoutMs)
    : db_(db), logOut_(logOut), fallbackSubId_("5544178410297751350"),
      xrayPath_(xrayPath), xrayApiPort_(xrayApiPort), test_timeout_ms_(testTimeoutMs) {}

void SubitemUpdater::log(const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&time));
    
    std::string logMsg = std::string("[") + timeStr + "] " + msg;
    std::cout << logMsg << std::endl;
    if (logOut_ && logOut_->is_open()) {
        *logOut_ << logMsg << std::endl;
    }
}

std::string SubitemUpdater::decodeBase64(const std::string& input) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    size_t len = input.size();
    size_t pos = 0;
    
    auto isBase64Char = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
               (c >= '0' && c <= '9') || c == '+' || c == '/';
    };
    
    while (len-- && (input[pos] != '=') && isBase64Char(input[pos])) {
        char_array_4[i++] = input[pos]; pos++;
        if (i == 4) {
            for (int k = 0; k < 4; k++) {
                char* found = (char*)strchr(base64_chars, char_array_4[k]);
                char_array_4[k] = found ? (found - base64_chars) : 0;
            }
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (int k = 0; k < 3; k++)
                ret += char_array_3[k];
            i = 0;
        }
        j++;
    }
    
    if (i) {
        for (int k = i; k < 4; k++)
            char_array_4[k] = 0;
        for (int k = 0; k < 4; k++) {
            char* found = (char*)strchr(base64_chars, char_array_4[k]);
            char_array_4[k] = found ? (found - base64_chars) : 0;
        }
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
        
        for (int k = 0; k < i - 1; k++)
            ret += char_array_3[k];
    }
    
    return ret;
}

std::string SubitemUpdater::urlDecode(const std::string& input) {
    std::string ret;
    ret.reserve(input.size());
    
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '%' && i + 2 < input.length()) {
            int hex = 0;
            std::istringstream iss(input.substr(i + 1, 2));
            if (iss >> std::hex >> hex) {
                ret += static_cast<char>(hex);
                i += 2;
            } else {
                ret += input[i];
            }
        } else if (input[i] == '+') {
            ret += ' ';
        } else {
            ret += input[i];
        }
    }
    
    return ret;
}

std::string SubitemUpdater::fetchUrl(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        log("ERROR: curl_easy_init failed");
        return "";
    }
    
    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        log("ERROR: fetchUrl failed for " + url + " - " + curl_easy_strerror(res));
        return "";
    }
    
    log("INFO: fetchUrl success for " + url + " (" + std::to_string(readBuffer.size()) + " bytes)");
    return readBuffer;
}

std::string SubitemUpdater::fetchUrlViaProxy(const std::string& url, int socksPort) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        log("ERROR: curl_easy_init failed for proxy fetch");
        return "";
    }
    
    std::string proxyUrl = "socks5://127.0.0.1:" + std::to_string(socksPort);
    std::string readBuffer;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        log("ERROR: fetchUrlViaProxy failed for " + url + " - " + curl_easy_strerror(res));
        return "";
    }
    
    return readBuffer;
}

int SubitemUpdater::findBestFallbackProxy() {
    std::string sql = "SELECT pi.IndexId, pe.Delay, pi.Port, pi.PreSocksPort "
                      "FROM ProfileItem pi "
                      "INNER JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId "
                      "WHERE pi.Subid = '" + fallbackSubId_ + "' "
                      "AND pe.Delay > 0 "
                      "ORDER BY CAST(pe.Delay AS INTEGER) ASC "
                      "LIMIT 1;";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        log("ERROR: SQL prepare failed for fallback proxy - " + std::string(sqlite3_errmsg(db_)));
        return -1;
    }
    
    int socksPort = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string indexId = (const char*)sqlite3_column_text(stmt, 0);
        std::string delay = (const char*)sqlite3_column_text(stmt, 1);
        const char* port = (const char*)sqlite3_column_text(stmt, 2);
        const char* preSocksPort = (const char*)sqlite3_column_text(stmt, 3);
        
        log("INFO: Found best fallback proxy: " + indexId + " (delay: " + delay + "ms)");
        
        if (preSocksPort && strlen(preSocksPort) > 0) {
            socksPort = std::stoi(preSocksPort);
        } else if (port && strlen(port) > 0) {
            socksPort = std::stoi(port);
            log("INFO: Using direct port instead of PreSocksPort: " + std::string(port));
        }
    }
    
    sqlite3_finalize(stmt);
    
    if (socksPort <= 0) {
        log("ERROR: No valid fallback proxy found for subid=" + fallbackSubId_ + ", trying to find any working proxy...");
        
        sql = "SELECT pi.IndexId, pi.Port, pi.PreSocksPort "
              "FROM ProfileItem pi "
              "INNER JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId "
              "WHERE pe.Delay > 0 AND pe.Message = 'OK' "
              "ORDER BY CAST(pe.Delay AS INTEGER) ASC "
              "LIMIT 1;";
        
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* port = (const char*)sqlite3_column_text(stmt, 1);
                const char* preSocksPort = (const char*)sqlite3_column_text(stmt, 2);
                
                if (preSocksPort && strlen(preSocksPort) > 0) {
                    socksPort = std::stoi(preSocksPort);
                } else if (port && strlen(port) > 0) {
                    socksPort = std::stoi(port);
                }
                
                log("INFO: Found alternative fallback proxy with port: " + std::to_string(socksPort));
            }
            sqlite3_finalize(stmt);
        }
    }
    
    if (socksPort <= 0) {
        log("ERROR: No fallback proxy available");
    }
    
    return socksPort;
}

std::vector<db::models::Profileitem> SubitemUpdater::parseSubscription(const std::string& content, const std::string& subid) {
    std::vector<db::models::Profileitem> profiles;
    
    bool hasProtocol = (content.find("://") != std::string::npos);
    std::string decoded;
    
    if (hasProtocol) {
        log("INFO: Content appears to be plain text share links");
        decoded = content;
    } else {
        log("INFO: Attempting base64 decode...");
        decoded = decodeBase64(content);
        log("INFO: decodeBase64 result length: " + std::to_string(decoded.length()));
        
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
            if (jsonStr.empty()) continue;
            
            try {
                boost::json::value jv = boost::json::parse(jsonStr);
                boost::json::object obj = jv.as_object();
                
                profile.configtype = "1";
                profile.configversion = "2";
                profile.alterid = "0";
                profile.network = "tcp";
                profile.coretype = "0";
                profile.muxenabled = "0";
                profile.address = obj.contains("add") ? obj.at("add").as_string().c_str() : "";
                
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
                
                profile.id = obj.contains("id") ? obj.at("id").as_string().c_str() : "";
                
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
                
                profile.security = obj.contains("scy") ? obj.at("scy").as_string().c_str() : "auto";
                profile.network = obj.contains("net") ? obj.at("net").as_string().c_str() : "tcp";
                profile.remarks = obj.contains("ps") ? obj.at("ps").as_string().c_str() : "";
                profile.path = obj.contains("path") ? obj.at("path").as_string().c_str() : "";
                profile.requesthost = obj.contains("host") ? obj.at("host").as_string().c_str() : "";
                profile.streamsecurity = obj.contains("tls") ? obj.at("tls").as_string().c_str() : "";
                profile.sni = obj.contains("sni") ? obj.at("sni").as_string().c_str() : "";
                profile.flow = obj.contains("flow") ? obj.at("flow").as_string().c_str() : "";
                profile.fingerprint = obj.contains("fp") ? obj.at("fp").as_string().c_str() : "chrome";
                profile.alpn = obj.contains("alpn") ? obj.at("alpn").as_string().c_str() : "";
                profile.headertype = obj.contains("type") ? obj.at("type").as_string().c_str() : "";
            } catch (const std::exception& e) {
                log("WARN: Failed to parse vmess: " + std::string(e.what()));
                continue;
            }
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
            
            size_t colonPos = hostInfo.find(':');
            if (colonPos == std::string::npos) continue;
            profile.address = hostInfo.substr(0, colonPos);
            profile.port = hostInfo.substr(colonPos + 1);
            
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
            size_t hashPos = hostPart.find('#');
            std::string addrPort = (hashPos != std::string::npos) ? hostPart.substr(0, hashPos) : hostPart;
            
            size_t colonPos = addrPort.find(':');
            if (colonPos == std::string::npos) continue;
            profile.address = addrPort.substr(0, colonPos);
            profile.port = addrPort.substr(colonPos + 1);
            
            if (hashPos != std::string::npos) {
                profile.remarks = urlDecode(hostPart.substr(hashPos + 1));
            }
            
            size_t qPos = hostPart.find('?');
            if (qPos != std::string::npos && hashPos != std::string::npos) {
                std::string params = hostPart.substr(qPos + 1, hashPos - qPos - 1);
                std::istringstream paramStream(params);
                std::string param;
                while (std::getline(paramStream, param, '&')) {
                    size_t eqPos = param.find('=');
                    if (eqPos == std::string::npos) continue;
                    std::string key = param.substr(0, eqPos);
                    std::string val = urlDecode(param.substr(eqPos + 1));
                    
                    if (key == "sni") profile.sni = val;
                    else if (key == "alpn") profile.alpn = val;
                    else if (key == "obfs-password") profile.id = val;
                }
            }
        } else if (line.find("tuic://") == 0) {
            std::string uri = line.substr(7);
            size_t atPos = uri.find('@');
            if (atPos == std::string::npos) continue;
            
            profile.configtype = "11";
            profile.configversion = "2";
            profile.alterid = "0";
            profile.network = "tcp";
            profile.coretype = "0";
            profile.muxenabled = "0";
            profile.id = uri.substr(0, atPos);
            
            std::string hostPart = uri.substr(atPos + 1);
            size_t hashPos = hostPart.find('#');
            std::string addrPort = (hashPos != std::string::npos) ? hostPart.substr(0, hashPos) : hostPart;
            
            size_t colonPos = addrPort.find(':');
            if (colonPos != std::string::npos) {
                profile.address = addrPort.substr(0, colonPos);
                profile.port = addrPort.substr(colonPos + 1);
            }
            
            if (hashPos != std::string::npos) {
                profile.remarks = urlDecode(hostPart.substr(hashPos + 1));
            }
            
            size_t qPos = hostPart.find('?');
            if (qPos != std::string::npos && hashPos != std::string::npos) {
                std::string params = hostPart.substr(qPos + 1, hashPos - qPos - 1);
                std::istringstream paramStream(params);
                std::string param;
                while (std::getline(paramStream, param, '&')) {
                    size_t eqPos = param.find('=');
                    if (eqPos == std::string::npos) continue;
                    std::string key = param.substr(0, eqPos);
                    std::string val = urlDecode(param.substr(eqPos + 1));
                    
                    if (key == "sni") profile.sni = val;
                    else if (key == "uuid") profile.id = val;
                    else if (key == "alpn") profile.alpn = val;
                }
            }
        } else {
            continue;
        }
        
        if (!profile.address.empty() && !profile.port.empty() && !profile.id.empty()) {
            profile.indexid = generateUniqueId();
            profiles.push_back(profile);
            validCount++;
        }
    }
    
    log("INFO: Parsed " + std::to_string(profiles.size()) + " profiles from subscription (scanned " + std::to_string(lineCount) + " lines)");
    
    if (profiles.size() > 0) {
        log("INFO: Removing duplicates by Address:Port:Network...");
        std::set<std::string> seen;
        std::vector<db::models::Profileitem> uniqueProfiles;
        for (const auto& p : profiles) {
            std::string key = p.address + ":" + p.port + ":" + p.network;
            if (seen.find(key) == seen.end()) {
                seen.insert(key);
                uniqueProfiles.push_back(p);
            }
        }
        log("INFO: Removed " + std::to_string(profiles.size() - uniqueProfiles.size()) + " duplicates, remaining: " + std::to_string(uniqueProfiles.size()));
        return uniqueProfiles;
    }
    
    return profiles;
}

bool SubitemUpdater::updateProfileItems(const std::string& subid, const std::vector<db::models::Profileitem>& profiles) {
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    
    char* errMsg = nullptr;
    
    std::string selectSql = "SELECT IndexId FROM ProfileItem WHERE Subid = '" + subid + "';";
    sqlite3_stmt* stmt = nullptr;
    std::vector<std::string> oldIndexIds;
    if (sqlite3_prepare_v2(db_, selectSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* idx = (const char*)sqlite3_column_text(stmt, 0);
            if (idx) oldIndexIds.push_back(idx);
        }
        sqlite3_finalize(stmt);
    }
    
    for (const auto& idx : oldIndexIds) {
        std::string deleteExSql = "DELETE FROM ProfileExItem WHERE IndexId = '" + idx + "';";
        sqlite3_exec(db_, deleteExSql.c_str(), nullptr, nullptr, nullptr);
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
        oss << "'" << p.cert << "')";
        
        if (sqlite3_exec(db_, oss.str().c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            log("WARN: Failed to insert profile - " + std::string(errMsg));
            sqlite3_free(errMsg);
        } else {
            inserted++;
        }
    }
    
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    log("INFO: Updated " + std::to_string(inserted) + " profiles for subid=" + subid);
    return inserted > 0;
}

bool SubitemUpdater::run() {
    log("========================================");
    log("INFO: Starting subscription update");
    log("========================================");
    
    db::models::SubitemDAO subDao(db_);
    auto enabledSubs = subDao.getEnabledSubscriptions();
    
    if (enabledSubs.empty()) {
        log("INFO: No enabled subscriptions found");
        return false;
    }
    
    log("INFO: Found " + std::to_string(enabledSubs.size()) + " enabled subscriptions");
    
    int successCount = 0;
    int failCount = 0;
    
    for (const auto& sub : enabledSubs) {
        log("--- Processing: " + sub.remarks + " (id: " + sub.id + ") ---");
        log("INFO: URL: " + sub.url);
        
        std::string content = fetchUrl(sub.url);
        
        if (content.empty()) {
            log("WARN: Direct fetch failed, trying fallback proxy...");
            
            int socksPort = findBestFallbackProxy();
            if (socksPort > 0) {
                content = fetchUrlViaProxy(sub.url, socksPort);
            }
        }
        
        if (content.empty()) {
            log("ERROR: Failed to fetch subscription: " + sub.remarks);
            failCount++;
            continue;
        }
        
        auto profiles = parseSubscription(content, sub.id);
        if (profiles.empty()) {
            log("WARN: No valid profiles parsed from: " + sub.remarks);
            failCount++;
            continue;
        }
        
        if (updateProfileItems(sub.id, profiles)) {
            successCount++;
            log("INFO: Successfully updated: " + sub.remarks);
        } else {
            failCount++;
    log("ERROR: Failed to update profiles for: " + sub.remarks);
    }
    
    log("========================================");
    log("INFO: Subscription update complete");
    log("INFO: Success: " + std::to_string(successCount) + ", Failed: " + std::to_string(failCount));
    log("========================================");
    
    return successCount > 0;
}

bool SubitemUpdater::runSingle(const std::string& subId) {
    log("========================================");
    log("INFO: Starting single subscription update for subId: " + subId);
    log("========================================");
    
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
    
    log("--- Processing: " + sub.remarks + " (id: " + sub.id + ") ---");
    log("INFO: URL: " + sub.url);
    
    std::string content;
    
    content = fetchUrl(sub.url);
    
    if (content.empty()) {
        log("WARN: Direct fetch failed, trying fallback proxies...");
        
        std::vector<FallbackProxy> fallbackProxies = getAllFallbackProxies(5);
        
        if (fallbackProxies.empty()) {
            log("ERROR: No fallback proxies available");
            return false;
        }
        
        log("INFO: Found " + std::to_string(fallbackProxies.size()) + " fallback proxies");
        
        if (!startXrayForSubscription()) {
            log("ERROR: Failed to start xray for subscription testing");
            return false;
        }
        
        bool success = false;
        for (size_t i = 0; i < fallbackProxies.size(); i++) {
            log("INFO: Testing fallback proxy " + std::to_string(i + 1) + "/" + std::to_string(fallbackProxies.size()) + 
                " (socksPort=" + std::to_string(fallbackProxies[i].socksPort) + ", delay=" + 
                std::to_string(fallbackProxies[i].delay) + ")");
            
            if (testSubscriptionViaXray(fallbackProxies[i].socksPort, sub.url)) {
                log("INFO: Fallback proxy " + std::to_string(i + 1) + " succeeded");
                content = fetchUrlViaProxy(sub.url, fallbackProxies[i].socksPort);
                success = true;
                break;
            } else {
                log("INFO: Fallback proxy " + std::to_string(i + 1) + " failed");
            }
        }
        
        cleanupXray();
        
        if (!success) {
            log("ERROR: All fallback proxies failed");
            return false;
        }
    }
    
    if (content.empty()) {
        log("ERROR: Failed to fetch subscription: " + sub.remarks);
        return false;
    }
    
    auto profiles = parseSubscription(content, sub.id);
    if (profiles.empty()) {
        log("WARN: No valid profiles parsed from: " + sub.remarks);
        return false;
    }
    
    bool success = updateProfileItems(sub.id, profiles);
    if (success) {
        log("INFO: Successfully updated: " + sub.remarks);
    } else {
        log("ERROR: Failed to update profiles for: " + sub.remarks);
    }
    
    log("========================================");
    log("INFO: Single subscription update complete");
    log("========================================");
    
    return success;
}

bool SubitemUpdater::runSingleWithProxy(const std::string& subId, int socksPort) {
    log("========================================");
    log("INFO: Starting single subscription update for subId: " + subId);
    log("========================================");
    
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
    
    log("--- Processing: " + sub.remarks + " (id: " + sub.id + ") ---");
    log("INFO: URL: " + sub.url);
    
    std::string content;
    int maxRetries = 3;
    
    if (socksPort > 0) {
        log("INFO: Trying fetch via proxy on port " + std::to_string(socksPort));
        for (int retry = 0; retry < maxRetries && content.empty(); retry++) {
            if (retry > 0) {
                log("WARN: Proxy retry " + std::to_string(retry) + "/" + std::to_string(maxRetries) + " after 3 seconds...");
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
            content = fetchUrlViaProxy(sub.url, socksPort);
        }
    }
    
    if (content.empty()) {
        log("WARN: Proxy fetch failed, trying direct fetch...");
        for (int retry = 0; retry < maxRetries && content.empty(); retry++) {
            if (retry > 0) {
                log("WARN: Direct retry " + std::to_string(retry) + "/" + std::to_string(maxRetries) + " after 3 seconds...");
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
            content = fetchUrl(sub.url);
        }
    }
    
    if (content.empty()) {
        log("ERROR: Failed to fetch subscription: " + sub.remarks);
        return false;
    }
    
    auto profiles = parseSubscription(content, sub.id);
    if (profiles.empty()) {
        log("WARN: No valid profiles parsed from: " + sub.remarks);
        return false;
    }
    
    bool success = updateProfileItems(sub.id, profiles);
    if (success) {
        log("INFO: Successfully updated: " + sub.remarks);
    } else {
        log("ERROR: Failed to update profiles for: " + sub.remarks);
    }
    
    log("========================================");
    log("INFO: Single subscription update complete");
    log("========================================");
    
    return success;
}
