#include "SubitemUpdater.h"
#include <boost/json.hpp>
#include <stdexcept>
#include <random>
#include <winsock2.h>
#include <ws2tcpip.h>

static std::string getJsonValueString(const boost::json::object& obj, const char* key, const char* defaultVal = "") {
    if (!obj.contains(key)) return defaultVal;
    try {
        auto& val = obj.at(key);
        if (val.is_string()) return val.as_string().c_str();
        if (val.is_int64()) return std::to_string(val.as_int64());
        if (val.is_double()) return std::to_string(static_cast<int>(val.as_double()));
    } catch (...) {}
    return defaultVal;
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string generateUniqueId() {
    static std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t val = rng();
    return std::to_string(val);
}

static bool isPortInUse(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    closesocket(sock);
    
    return result == SOCKET_ERROR;
}

static int findAvailablePort(int startPort, int maxAttempts = 100) {
    for (int i = 0; i < maxAttempts; ++i) {
        int port = startPort + i;
        if (port > 65535) port = 10000 + (port - 10000) % 50000;
        if (!isPortInUse(port)) {
            return port;
        }
    }
    return -1;
}

SubitemUpdater::SubitemUpdater(sqlite3* db, std::ofstream* logOut, const std::string& xrayPath, int xrayApiPort, int testTimeoutMs, int startPort, bool priorityProxyEnabled)
    : db_(db), logOut_(logOut), fallbackSubId_("5544178410297751350"),
      xrayPath_(xrayPath), xrayApiPort_(xrayApiPort), test_timeout_ms_(testTimeoutMs),
      xrayProcessId_(0), exItemDao_(db), startPort_(startPort), priorityProxyEnabled_(priorityProxyEnabled) {}

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
            
            std::ostringstream exOss;
            exOss << "INSERT INTO ProfileExItem (indexid, delay, speed, sort, message) VALUES (";
            exOss << "'" << p.indexid << "', ";
            exOss << "'0', ";  // delay
            exOss << "'0', ";  // speed
            exOss << "'0', ";  // sort
            exOss << "'NOT_TESTED')";  // message
            if (sqlite3_exec(db_, exOss.str().c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
                log("DEBUG: Failed to insert ProfileExItem - " + std::string(errMsg));
                sqlite3_free(errMsg);
            }
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
    
    int totalEnabled = enabledSubs.size();
    int directSuccessCount = 0;
    int proxySuccessCount = 0;
    int failCount = 0;
    std::vector<std::pair<std::string, std::string>> failedSubsList;
    int priorityProxySuccessCount = 0;
    
    if (priorityProxyEnabled_ && !xrayPath_.empty()) {
        log("========================================");
        log("DEBUG: Phase 0 - Priority proxy update for all subscriptions");
        log("========================================");
        
        std::vector<FallbackProxy> allProxies = getAllFallbackProxies(100);
        
        std::vector<FallbackProxy> validProxies;
        for (const auto& proxy : allProxies) {
            if (proxy.delay > 0) {
                validProxies.push_back(proxy);
            }
        }
        
        log("DEBUG: Found " + std::to_string(validProxies.size()) + " proxies with delay > 0");
        
        if (validProxies.empty()) {
            log("WARN: No proxies with delay > 0 found, skipping priority proxy mode");
        } else {
            bool subscriptionUpdated = false;
            HANDLE xrayJob = nullptr;
            
            for (size_t pIdx = 0; pIdx < validProxies.size() && !subscriptionUpdated; pIdx++) {
            const auto& proxy = validProxies[pIdx];
            log("DEBUG: Testing proxy " + std::to_string(pIdx + 1) + "/" + std::to_string(validProxies.size()) + 
                " (indexId=" + proxy.indexId + ", address=" + proxy.address + ", delay=" + std::to_string(proxy.delay) + "ms)");
                
                db::models::ProfileitemDAO profileDao(db_);
                std::string sql = "SELECT * FROM ProfileItem WHERE IndexId = '" + proxy.indexId + "';";
                auto profiles = profileDao.getAll(sql);
                
                if (profiles.empty()) {
                    log("WARN: Proxy profile not found: " + proxy.indexId);
                    continue;
                }
                
                auto profile = profiles[0];
                
                profile.presocksport = std::to_string(startPort_);
                
                try {
                    profile.checkRequired();
                } catch (const std::exception& e) {
                    log("WARN: Proxy config error: " + std::string(e.what()));
                    continue;
                }
                
                config::ConfigGenerator configGen(db_);
                auto xrayConfig = configGen.generateConfig(profile);
                
                log("DEBUG: Generated outbound config: " + xrayConfig.outbound_json);
                
                int actualStartPort = startPort_;
                int actualApiPort = xrayApiPort_;
                
                if (isPortInUse(actualStartPort)) {
                    actualStartPort = findAvailablePort(startPort_);
                    log("WARN: SOCKS port " + std::to_string(startPort_) + " in use, using " + std::to_string(actualStartPort));
                }
                if (isPortInUse(actualApiPort)) {
                    actualApiPort = findAvailablePort(xrayApiPort_);
                    log("WARN: API port " + std::to_string(xrayApiPort_) + " in use, using " + std::to_string(actualApiPort));
                }
                
                boost::json::object xrayConfigRoot;
                boost::json::array inboundArray;
                
                int apiPort = actualApiPort;
                
                boost::json::object socksInbound;
                socksInbound["tag"] = "socks-in";
                socksInbound["protocol"] = "mixed";
                socksInbound["port"] = actualStartPort;
                socksInbound["listen"] = "127.0.0.1";
                socksInbound["settings"] = boost::json::object({
                    {"auth", "noauth"},
                    {"udp", true}
                });
                inboundArray.push_back(socksInbound);
                
                boost::json::object apiInbound;
                apiInbound["tag"] = "api";
                apiInbound["listen"] = "127.0.0.1";
                apiInbound["port"] = apiPort;
                apiInbound["protocol"] = "dokodemo-door";
                apiInbound["settings"] = boost::json::object({
                    {"address", "127.0.0.1"}
                });
                inboundArray.push_back(apiInbound);
                
                log("DEBUG: Priority proxy config API port: " + std::to_string(apiPort));
                
                xrayConfigRoot["inbounds"] = inboundArray;
                
                boost::json::value outboundJson = boost::json::parse(xrayConfig.outbound_json);
                boost::json::array outbounds;
                
                boost::json::object proxyOutbound = outboundJson.at("outbounds").at(0).as_object();
                proxyOutbound["tag"] = "proxy";
                outbounds.push_back(proxyOutbound);
                
                outbounds.push_back(boost::json::parse(R"({"tag": "direct", "protocol": "freedom"})"));
                xrayConfigRoot["outbounds"] = outbounds;
                
                xrayConfigRoot["api"] = boost::json::object({
                    {"tag", "api"},
                    {"services", boost::json::array({"HandlerService", "LoggerService", "StatsService"})}
                });
                
                xrayConfigRoot["stats"] = boost::json::object();
                xrayConfigRoot["policy"] = boost::json::object({
                    {"levels", boost::json::object({
                        {"0", boost::json::object({
                            {"statsUserUplink", true},
                            {"statsUserDownlink", true}
                        })}
                    })},
                    {"system", boost::json::object({
                        {"statsInboundUplink", true},
                        {"statsInboundDownlink", true},
                        {"statsOutboundUplink", true},
                        {"statsOutboundDownlink", true}
                    })}
                });
                
                xrayConfigRoot["routing"] = boost::json::object({
                    {"domainStrategy", "AsIs"},
                    {"rules", boost::json::array({
                        boost::json::object({
                            {"type", "field"},
                            {"inboundTag", boost::json::array({"api"})},
                            {"outboundTag", "api"}
                        }),
                        boost::json::object({
                            {"type", "field"},
                            {"outboundTag", "proxy"},
                            {"network", "tcp"}
                        })
                    })}
                });
                
                xrayConfigRoot["log"] = boost::json::object({
                    {"access", "xray_sub_access.log"},
                    {"error", "xray_sub_error.log"},
                    {"loglevel", "debug"}
                });
                
                std::string configStr = boost::json::serialize(xrayConfigRoot);
                
                log("DEBUG: Full priority proxy xray config: " + configStr);
                
                std::string configFile = "E:\\eclipse_workspace\\multiple_thread_validproxy\\bin\\xray_priority_temp.json";
                std::ofstream configOut(configFile);
                configOut << configStr;
                configOut.close();
                
                std::string normalizedPath = configFile;
                for (char& c : normalizedPath) {
                    if (c == '/') c = '\\';
                }
                
                std::string cmd = "\"" + xrayPath_ + "\" run -c \"" + normalizedPath + "\"";
                
                log("DEBUG: Starting priority xray: " + cmd);
                
                STARTUPINFOA si = {0};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                
                PROCESS_INFORMATION pi = {0};
                DWORD createFlags = CREATE_SUSPENDED | CREATE_NO_WINDOW;
                
                if (!CreateProcessA(nullptr, (char*)cmd.c_str(), nullptr, nullptr, FALSE, 
                                    createFlags, nullptr, nullptr, &si, &pi)) {
                    log("ERROR: Failed to start priority xray, err=" + std::to_string(GetLastError()));
                    continue;
                }
                
                HANDLE job = CreateJobObjectA(nullptr, nullptr);
                if (job) {
                    AssignProcessToJobObject(job, pi.hProcess);
                }
                
                HANDLE xrayJob = job;
                
                ResumeThread(pi.hThread);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                
                log("DEBUG: Priority xray started, waiting 5s...");
                Sleep(5000);
                
                bool xrayReady = false;
                std::string apiAddr = "127.0.0.1:" + std::to_string(apiPort);
                
                for (int j = 0; j < 10; j++) {
                    Sleep(1000);
                    
                    xray::XrayApi testApi(xrayPath_, apiAddr);
                    if (testApi.listOutbounds()) {
                        log("DEBUG: Priority xray API ready after " + std::to_string(j+1) + "s");
                        xrayReady = true;
                        break;
                    }
                    log("DEBUG: Waited " + std::to_string(j+1) + "s for priority xray...");
                }
                
                if (!xrayReady) {
                    log("ERROR: Priority xray API not ready, trying next proxy...");
                    if (xrayJob) {
                        TerminateJobObject(xrayJob, 0);
                        CloseHandle(xrayJob);
                    }
                    continue;
                }
                
                log("INFO: Testing proxy connectivity via SOCKS port " + std::to_string(actualStartPort) + "...");
                long latencyMs = -1;
                std::string errorMsg;
                bool proxyWorking = testProxyConnectivity(actualStartPort, latencyMs, errorMsg);
                
                if (!proxyWorking) {
                    log("WARN: Proxy connectivity test failed: " + errorMsg + ", trying next proxy...");
                    if (xrayJob) {
                        TerminateJobObject(xrayJob, 0);
                        CloseHandle(xrayJob);
                    }
                    continue;
                }
                
                log("INFO: Proxy connectivity test SUCCEEDED (latency=" + std::to_string(latencyMs) + "ms)");
                log("INFO: ===== Updating all subscriptions via proxy =====");
                
                priorityProxySuccessCount++;
                
                for (const auto& sub : enabledSubs) {
                    log("--- Updating via priority proxy: " + sub.remarks + " ---");
                    
                    std::string content = fetchUrlViaProxy(sub.url, actualStartPort);
                    
                    if (content.empty()) {
                        log("WARN: Failed to fetch via proxy: " + sub.remarks);
                        failedSubsList.emplace_back(sub.id, sub.remarks);
                        continue;
                    }
                    
                    auto profiles = parseSubscription(content, sub.id);
                    if (profiles.empty()) {
                        log("WARN: No valid profiles parsed from: " + sub.remarks);
                        failedSubsList.emplace_back(sub.id, sub.remarks);
                        continue;
                    }
                    
                    if (updateProfileItems(sub.id, profiles)) {
                        log("INFO: Successfully updated via proxy: " + sub.remarks);
                        proxySuccessCount++;
                    } else {
                        log("ERROR: Failed to update profiles for: " + sub.remarks);
                        failedSubsList.emplace_back(sub.id, sub.remarks);
                    }
                }
                
                if (xrayJob) {
                    TerminateJobObject(xrayJob, 0);
                    CloseHandle(xrayJob);
                }
                log("INFO: Priority proxy xray stopped");
                
                int priorityProxySuccessCount = enabledSubs.size();
                
                log("========================================");
                log("INFO: Subscription update complete (priority proxy mode)");
                log("INFO: Total enabled: " + std::to_string(totalEnabled));
                log("INFO: Direct success: 0");
                log("INFO: Proxy update: " + std::to_string(priorityProxySuccessCount));
                log("INFO: Failed: 0");
                log("========================================");
                return true;
            }
        }
    }
    
    int successCount = 0;
    std::vector<std::string> failedSubIds;
    std::vector<db::models::Subitem> failedSubs;
    
    log("========================================");
    log("INFO: Phase 1 - Direct fetch for all subscriptions");
    log("========================================");
    
    for (const auto& sub : enabledSubs) {
        log("--- Processing: " + sub.remarks + " (id: " + sub.id + ") ---");
        log("INFO: URL: " + sub.url);
        
        std::string content = fetchUrl(sub.url);
        
        if (content.empty()) {
            log("WARN: Direct fetch failed");
            failedSubs.push_back(sub);
            failedSubIds.push_back(sub.id);
            failedSubsList.emplace_back(sub.id, sub.remarks);
            continue;
        }
        
        auto profiles = parseSubscription(content, sub.id);
        if (profiles.empty()) {
            log("WARN: No valid profiles parsed from: " + sub.remarks);
            failedSubs.push_back(sub);
            failedSubIds.push_back(sub.id);
            failedSubsList.emplace_back(sub.id, sub.remarks);
            continue;
        }
        
        if (updateProfileItems(sub.id, profiles)) {
            successCount++;
            directSuccessCount++;
            log("INFO: Successfully updated (direct): " + sub.remarks);
        } else {
            failedSubs.push_back(sub);
            failedSubIds.push_back(sub.id);
            failedSubsList.emplace_back(sub.id, sub.remarks);
            log("ERROR: Failed to update profiles for: " + sub.remarks);
        }
    }
    
    log("INFO: Phase 1 complete - Direct success: " + std::to_string(directSuccessCount) + ", Failed: " + std::to_string(failedSubs.size()));
    
    if (failedSubs.empty()) {
        failCount = 0;
        log("========================================");
        log("INFO: Subscription update complete (direct only)");
        log("INFO: Total enabled: " + std::to_string(totalEnabled));
        log("INFO: Direct success: " + std::to_string(directSuccessCount));
        log("INFO: Proxy update: 0");
        log("INFO: Failed: 0");
        log("========================================");
        return true;
    } else {
        failCount = failedSubsList.size();
    }
    
    log("========================================");
    log("INFO: Phase 2 - Testing and updating via proxy");
    log("========================================");
    
    if (xrayPath_.empty()) {
        log("ERROR: xrayPath not configured");
        failCount = failedSubsList.size();
        log("========================================");
        log("INFO: Subscription update complete (direct only)");
        log("INFO: Total enabled: " + std::to_string(totalEnabled));
        log("INFO: Direct success: " + std::to_string(directSuccessCount));
        log("INFO: Proxy update: 0");
        log("INFO: Failed: " + std::to_string(failCount));
        if (!failedSubsList.empty()) {
            log("INFO: Failed subscriptions:");
            for (const auto& f : failedSubsList) {
                log("INFO:   - [" + f.first + "] " + f.second);
            }
        }
        log("========================================");
        return directSuccessCount > 0;
    }
    
    config::ConfigGenerator configGen(db_);
    xray::XrayApi* xrayApi = nullptr;
    
    std::vector<FallbackProxy> fallbackProxies = getAllFallbackProxies(5);
    
    if (fallbackProxies.empty()) {
        log("ERROR: No fallback proxies available");
        failCount = failedSubsList.size();
        log("========================================");
        log("INFO: Subscription update complete (direct only)");
        log("INFO: Total enabled: " + std::to_string(totalEnabled));
        log("INFO: Direct success: " + std::to_string(directSuccessCount));
        log("INFO: Proxy update: 0");
        log("INFO: Failed: " + std::to_string(failCount));
        if (!failedSubsList.empty()) {
            log("INFO: Failed subscriptions:");
            for (const auto& f : failedSubsList) {
                log("INFO:   - [" + f.first + "] " + f.second);
            }
        }
        log("========================================");
        return directSuccessCount > 0;
    }
    
    for (size_t i = 0; i < fallbackProxies.size(); i++) {
        const auto& proxy = fallbackProxies[i];
        log("INFO: Testing proxy " + std::to_string(i + 1) + "/" + std::to_string(fallbackProxies.size()) + 
            " (indexId=" + proxy.indexId + ", address=" + proxy.address + ", delay=" + std::to_string(proxy.delay) + "ms)");
        
        db::models::ProfileitemDAO profileDao(db_);
        std::string sql = "SELECT * FROM ProfileItem WHERE IndexId = '" + proxy.indexId + "';";
        auto profiles = profileDao.getAll(sql);
        
        if (profiles.empty()) {
            log("WARN: Proxy profile not found: " + proxy.indexId);
            exItemDao_.updateTestResult(proxy.indexId, -1, false, "Profile not found");
            continue;
        }
        
        auto profile = profiles[0];
        log("DEBUG: profile.id=[" + profile.id + "], configtype=[" + profile.configtype + "], address=[" + profile.address + "], port=[" + profile.port + "]");
        
        int actualStartPort = startPort_;
        int actualApiPort = xrayApiPort_;
        
        if (isPortInUse(actualStartPort)) {
            actualStartPort = findAvailablePort(startPort_);
            log("WARN: SOCKS port " + std::to_string(startPort_) + " in use, using " + std::to_string(actualStartPort));
        }
        if (isPortInUse(actualApiPort)) {
            actualApiPort = findAvailablePort(xrayApiPort_);
            log("WARN: API port " + std::to_string(xrayApiPort_) + " in use, using " + std::to_string(actualApiPort));
        }
        
        profile.presocksport = std::to_string(actualStartPort);
        
        try {
        profile.checkRequired();
        } catch (const std::exception& e) {
            log("WARN: Proxy config error: " + std::string(e.what()));
            exItemDao_.updateTestResult(proxy.indexId, -1, false, e.what());
            continue;
        }
        
        config::ConfigGenerator configGen(db_);
        auto xrayConfig = configGen.generateConfig(profile);
        
        log("DEBUG: Generated outbound config: " + xrayConfig.outbound_json);
        
        boost::json::object xrayConfigRoot;
        boost::json::array inboundArray;
        
        int apiPort = actualApiPort;
        
        boost::json::object socksInbound;
        socksInbound["tag"] = "socks-in";
        socksInbound["protocol"] = "mixed";
        socksInbound["port"] = actualStartPort;
        socksInbound["listen"] = "127.0.0.1";
        socksInbound["settings"] = boost::json::object({
            {"auth", "noauth"},
            {"udp", true}
        });
        inboundArray.push_back(socksInbound);
        
        boost::json::object apiInbound;
        apiInbound["tag"] = "api";
        apiInbound["listen"] = "127.0.0.1";
        apiInbound["port"] = apiPort;
        apiInbound["protocol"] = "dokodemo-door";
        apiInbound["settings"] = boost::json::object({
            {"address", "127.0.0.1"}
        });
        inboundArray.push_back(apiInbound);
        
        log("DEBUG: Config API port set to: " + std::to_string(apiPort));
        
        xrayConfigRoot["inbounds"] = inboundArray;
        
        boost::json::value outboundJson = boost::json::parse(xrayConfig.outbound_json);
        boost::json::array outbounds;
        
        boost::json::object proxyOutbound = outboundJson.at("outbounds").at(0).as_object();
        proxyOutbound["tag"] = "proxy";
        outbounds.push_back(proxyOutbound);
        
        outbounds.push_back(boost::json::parse(R"({"tag": "direct", "protocol": "freedom"})"));
        xrayConfigRoot["outbounds"] = outbounds;
        
        xrayConfigRoot["api"] = boost::json::object({
            {"tag", "api"},
            {"services", boost::json::array({"HandlerService", "LoggerService", "StatsService"})}
        });
        
        xrayConfigRoot["stats"] = boost::json::object();
        xrayConfigRoot["policy"] = boost::json::object({
            {"levels", boost::json::object({
                {"0", boost::json::object({
                    {"statsUserUplink", true},
                    {"statsUserDownlink", true}
                })}
            })},
            {"system", boost::json::object({
                {"statsInboundUplink", true},
                {"statsInboundDownlink", true},
                {"statsOutboundUplink", true},
                {"statsOutboundDownlink", true}
            })}
        });
        
        xrayConfigRoot["routing"] = boost::json::object({
            {"domainStrategy", "AsIs"},
            {"rules", boost::json::array({
                boost::json::object({
                    {"type", "field"},
                    {"inboundTag", boost::json::array({"api"})},
                    {"outboundTag", "api"}
                }),
                boost::json::object({
                    {"type", "field"},
                    {"outboundTag", "proxy"},
                    {"network", "tcp"}
                })
            })}
        });
        
        xrayConfigRoot["log"] = boost::json::object({
            {"access", "xray_sub_access.log"},
            {"error", "xray_sub_error.log"},
            {"loglevel", "debug"}
        });
        
        std::string configStr = boost::json::serialize(xrayConfigRoot);
        
        log("DEBUG: Full xray config: " + configStr);
        std::string configFile = "E:\\eclipse_workspace\\multiple_thread_validproxy\\bin\\xray_sub_temp_" + std::to_string(i) + ".json";
        std::ofstream configOut(configFile);
        configOut << configStr;
        configOut.close();
        
        std::string normalizedPath = configFile;
        for (char& c : normalizedPath) {
            if (c == '/') c = '\\';
        }
        
        std::string cmd = "\"" + xrayPath_ + "\" run -c \"" + normalizedPath + "\"";
        
        log("DEBUG: Starting xray with CreateProcess: " + cmd);
        
        STARTUPINFOA si = {0};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        PROCESS_INFORMATION pi = {0};
        
        DWORD createFlags = CREATE_SUSPENDED | CREATE_NO_WINDOW;
        
        if (!CreateProcessA(nullptr, (char*)cmd.c_str(), nullptr, nullptr, FALSE, 
                            createFlags, nullptr, nullptr, &si, &pi)) {
            log("ERROR: Failed to start xray for proxy " + std::to_string(i + 1) + ", err=" + std::to_string(GetLastError()));
            exItemDao_.updateTestResult(proxy.indexId, -1, false, "CreateProcess failed");
            continue;
        }
        
        HANDLE job = CreateJobObjectA(nullptr, nullptr);
        if (job) {
            AssignProcessToJobObject(job, pi.hProcess);
        }
        
        HANDLE xrayJob = job;
        
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        
        log("DEBUG: Xray thread resumed, sleeping 5s for initialization...");
        Sleep(5000);
        
        log("DEBUG: Waiting for xray to fully start...");
        bool xrayReady = false;
        std::string apiAddr = "127.0.0.1:" + std::to_string(apiPort);
        log("DEBUG: Will use API port " + std::to_string(apiPort));
        
        for (int j = 0; j < 10; j++) {
            Sleep(1000);
            
            xray::XrayApi testApi(xrayPath_, apiAddr);
            if (testApi.listOutbounds()) {
                log("DEBUG: Xray API connected after " + std::to_string(j+1) + "s");
                xrayReady = true;
                break;
            }
            log("DEBUG: Waited " + std::to_string(j+1) + "s, API not ready...");
        }
        
        if (!xrayReady) {
            log("ERROR: Xray API not reachable after 10s wait");
            if (xrayJob) {
                TerminateJobObject(xrayJob, 0);
                CloseHandle(xrayJob);
            }
            log("DEBUG: Xray process stopped via job");
            exItemDao_.updateTestResult(proxy.indexId, -1, false, "XRAY_START_TIMEOUT");
            continue;
        }
        
        log("DEBUG: Creating XrayApi with server=" + apiAddr);
        
        xray::XrayApi xrayApi(xrayPath_, apiAddr);
        
        auto config = configGen.generateConfig(profile);
        
        log("DEBUG: outbound_json=" + config.outbound_json);
        
        std::string tag = "proxy";
        
        log("DEBUG: Calling removeOutbound...");
        xrayApi.removeOutbound(tag);
        Sleep(500);
        
        log("DEBUG: Calling addOutbound...");
        std::string addResult;
        if (!xrayApi.addOutbound(config.outbound_json, tag, addResult)) {
            log("ERROR: Failed to add outbound: " + xrayApi.getLastError() + " output: " + addResult);
            exItemDao_.updateTestResult(proxy.indexId, -1, false, "XRAY_ERROR: " + xrayApi.getLastError());
            
            if (xrayJob) {
                TerminateJobObject(xrayJob, 0);
                CloseHandle(xrayJob);
            }
            log("DEBUG: Xray process stopped via job");
            continue;
        }
        
        Sleep(500);
        
        long latencyMs = -1;
        std::string errorMsg;
        bool testResult = testProxyConnectivity(startPort_, latencyMs, errorMsg);
        
        if (testResult) {
            log("INFO: Proxy " + std::to_string(i + 1) + " test SUCCEEDED (latency=" + std::to_string(latencyMs) + "ms)");
            exItemDao_.updateTestResult(proxy.indexId, latencyMs, true, "OK");
            
            log("========================================");
            log("INFO: Updating failed subscriptions via proxy " + std::to_string(i + 1));
            log("========================================");
            
            for (const auto& sub : failedSubs) {
                log("--- Updating via proxy: " + sub.remarks + " (id: " + sub.id + ") ---");
                
                std::string content = fetchUrlViaProxy(sub.url, actualStartPort);
                
                if (content.empty()) {
                    log("ERROR: Failed to fetch via proxy: " + sub.remarks);
                    failedSubsList.emplace_back(sub.id, sub.remarks);
                    continue;
                }
                
                auto profiles = parseSubscription(content, sub.id);
                if (profiles.empty()) {
                    log("WARN: No valid profiles parsed from: " + sub.remarks);
                    continue;
                }
                
                if (updateProfileItems(sub.id, profiles)) {
                    successCount++;
                    proxySuccessCount++;
                    log("INFO: Successfully updated via proxy: " + sub.remarks);
                } else {
                    failedSubsList.emplace_back(sub.id, sub.remarks);
                    log("ERROR: Failed to update profiles for: " + sub.remarks);
                }
            }
            
            if (xrayJob) {
                TerminateJobObject(xrayJob, 0);
                CloseHandle(xrayJob);
            }
            log("INFO: Xray process stopped via job");
            
            failCount = failedSubsList.size();
            
            log("========================================");
            log("INFO: Subscription update complete");
            log("INFO: Total enabled: " + std::to_string(totalEnabled));
            log("INFO: Direct success: " + std::to_string(directSuccessCount));
            log("INFO: Proxy update: " + std::to_string(proxySuccessCount));
            log("INFO: Failed: " + std::to_string(failCount));
            if (!failedSubsList.empty()) {
                log("INFO: Failed subscriptions:");
                for (const auto& f : failedSubsList) {
                    log("INFO:   - [" + f.first + "] " + f.second);
                }
            }
            log("========================================");
            
            return successCount > 0;
        } else {
            log("INFO: Proxy " + std::to_string(i + 1) + " test FAILED (error: " + errorMsg + ")");
            exItemDao_.updateTestResult(proxy.indexId, latencyMs, false, errorMsg);
            
            if (xrayJob) {
                TerminateJobObject(xrayJob, 0);
                CloseHandle(xrayJob);
            }
            log("DEBUG: Xray process stopped via job");
        }
    }
    
    log("ERROR: All fallback proxies failed");
    failCount = failedSubsList.size();
    log("========================================");
    log("INFO: Subscription update complete (direct only)");
    log("INFO: Total enabled: " + std::to_string(totalEnabled));
    log("INFO: Direct success: " + std::to_string(directSuccessCount));
    log("INFO: Proxy update: 0");
    log("INFO: Failed: " + std::to_string(failCount));
    if (!failedSubsList.empty()) {
        log("INFO: Failed subscriptions:");
        for (const auto& f : failedSubsList) {
            log("INFO:   - [" + f.first + "] " + f.second);
        }
    }
    log("========================================");
    
    return directSuccessCount > 0;
}

std::vector<SubitemUpdater::FallbackProxy> SubitemUpdater::getAllFallbackProxies(int maxCount) {
    std::vector<FallbackProxy> proxies;
    
    std::string sql = "SELECT pi.IndexId, pi.Address, pi.Port, pi.PreSocksPort, COALESCE(pe.Delay, 999999) AS Delay "
                      "FROM ProfileItem pi "
                      "LEFT JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId "
                      "WHERE pi.Subid = '" + fallbackSubId_ + "' "
                      "AND pi.Address IS NOT NULL AND pi.Address != '' "
                      "ORDER BY Delay ASC "
                      "LIMIT " + std::to_string(maxCount) + ";";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        log("ERROR: SQL prepare failed - " + std::string(sqlite3_errmsg(db_)));
        return proxies;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FallbackProxy proxy;
        proxy.indexId = (const char*)sqlite3_column_text(stmt, 0);
        proxy.address = (const char*)sqlite3_column_text(stmt, 1);
        proxy.delay = std::stoi((const char*)sqlite3_column_text(stmt, 4));
        
        const char* preSocksPort = (const char*)sqlite3_column_text(stmt, 3);
        const char* port = (const char*)sqlite3_column_text(stmt, 2);
        
        if (preSocksPort && strlen(preSocksPort) > 0) {
            proxy.socksPort = std::stoi(preSocksPort);
        } else if (port && strlen(port) > 0) {
            proxy.socksPort = std::stoi(port);
        } else {
            continue;
        }
        
        proxies.push_back(proxy);
    }
    
    sqlite3_finalize(stmt);
    
    return proxies;
}

bool SubitemUpdater::startXrayForSubscription() {
    if (xrayPath_.empty()) {
        log("ERROR: xrayPath is empty");
        return false;
    }
    
    log("DEBUG: Creating xray config with mixed inbound on port " + std::to_string(startPort_));
    
    int actualStartPort = startPort_;
    if (isPortInUse(actualStartPort)) {
        actualStartPort = findAvailablePort(startPort_);
        log("WARN: SOCKS port " + std::to_string(startPort_) + " in use, using " + std::to_string(actualStartPort));
    }
    
    boost::json::object config;
    boost::json::array inboundArray;
    
    boost::json::object socksInbound;
    socksInbound["tag"] = "socks-in";
    socksInbound["protocol"] = "mixed";
    socksInbound["port"] = actualStartPort;
    socksInbound["listen"] = "127.0.0.1";
    socksInbound["settings"] = boost::json::object({
        {"auth", "noauth"},
        {"udp", true}
    });
    
    inboundArray.push_back(socksInbound);
    config["inbounds"] = inboundArray;
    
    boost::json::array outbounds;
    outbounds.push_back(boost::json::parse(R"({"tag": "direct", "protocol": "freedom"})"));
    config["outbounds"] = outbounds;
    
    config["log"] = boost::json::object({
        {"access", ""},
        {"loglevel", "warning"}
    });
    
    std::string configStr = boost::json::serialize(config);
    log("DEBUG: xray config: " + configStr);
    
    std::string configFile = "E:\\eclipse_workspace\\multiple_thread_validproxy\\bin\\xray_subscription_temp.json";
    std::ofstream configOut(configFile);
    configOut << configStr;
    configOut.close();
    
    std::string normalizedPath = configFile;
    for (char& c : normalizedPath) {
        if (c == '/') c = '\\';
    }
    
    std::string cmd = "\"" + xrayPath_ + "\" run -c \"" + normalizedPath + "\"";
    log("DEBUG: xray command: " + cmd);
    
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = INVALID_HANDLE_VALUE;
    
    PROCESS_INFORMATION pi = {0};
    
    DWORD createFlags = CREATE_SUSPENDED | CREATE_NO_WINDOW;
    
    if (!CreateProcessA(nullptr, (char*)cmd.c_str(), nullptr, nullptr, FALSE, 
                        createFlags, nullptr, nullptr, &si, &pi)) {
        log("ERROR: CreateProcess failed for xray");
        return false;
    }
    
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    
    xrayProcessId_ = pi.dwProcessId;
    log("DEBUG: xray process started (PID=" + std::to_string(xrayProcessId_) + "), waiting 2s...");
    Sleep(2000);
    
    log("INFO: Xray process started successfully");
    return true;
}

bool SubitemUpdater::testSubscriptionViaXray(int socksPort, const std::string& subUrl) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        log("ERROR: curl_easy_init failed");
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, subUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    std::string proxyUrl = "socks5://127.0.0.1:" + std::to_string(socksPort);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    
    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    CURLcode res = curl_easy_perform(curl);
    
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK) {
        log("DEBUG: CURL result=OK, httpCode=" + std::to_string(httpCode) + ", responseSize=" + std::to_string(response.size()));
        return !response.empty();
    } else {
        log("DEBUG: CURL result=" + std::string(curl_easy_strerror(res)) + " (" + std::to_string(res) + ")");
        return false;
    }
}

void SubitemUpdater::cleanupXray() {
    if (xrayProcessId_ != 0) {
        log("INFO: Killing xray process (PID=" + std::to_string(xrayProcessId_) + ")");
        std::string cmd = "taskkill /F /PID " + std::to_string(xrayProcessId_) + " >NUL 2>&1";
        system(cmd.c_str());
        xrayProcessId_ = 0;
    } else {
        log("DEBUG: No xray process to kill");
    }
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
    
    std::string content = fetchUrl(sub.url);
    
    if (content.empty()) {
        log("WARN: Direct fetch failed, trying priority proxy mode...");
        
        std::vector<FallbackProxy> allProxies = getAllFallbackProxies(100);
        std::vector<FallbackProxy> validProxies;
        for (const auto& proxy : allProxies) {
            if (proxy.delay > 0) {
                validProxies.push_back(proxy);
            }
        }
        
        log("INFO: Found " + std::to_string(validProxies.size()) + " proxies with delay > 0");
        
        if (validProxies.empty()) {
            log("ERROR: No working proxies available");
            return false;
        }
        
        for (size_t pIdx = 0; pIdx < validProxies.size(); pIdx++) {
            const auto& proxy = validProxies[pIdx];
            log("INFO: Testing priority proxy " + std::to_string(pIdx + 1) + "/" + std::to_string(validProxies.size()) + 
                " (indexId=" + proxy.indexId + ", address=" + proxy.address + ", delay=" + std::to_string(proxy.delay) + "ms)");
            
            db::models::ProfileitemDAO profileDao(db_);
            std::string profileSql = "SELECT * FROM ProfileItem WHERE IndexId = '" + proxy.indexId + "';";
            auto profiles = profileDao.getAll(profileSql);
            
            if (profiles.empty()) {
                log("WARN: Proxy profile not found: " + proxy.indexId);
                continue;
            }
            
            auto profile = profiles[0];
            
            int actualStartPort = startPort_;
            int actualApiPort = xrayApiPort_;
            
            if (isPortInUse(actualStartPort)) {
                actualStartPort = findAvailablePort(startPort_);
                log("WARN: SOCKS port " + std::to_string(startPort_) + " in use, using " + std::to_string(actualStartPort));
            }
            if (isPortInUse(actualApiPort)) {
                actualApiPort = findAvailablePort(xrayApiPort_);
                log("WARN: API port " + std::to_string(xrayApiPort_) + " in use, using " + std::to_string(actualApiPort));
            }
            
            profile.presocksport = std::to_string(actualStartPort);
            
            try {
                profile.checkRequired();
            } catch (const std::exception& e) {
                log("WARN: Proxy config error: " + std::string(e.what()));
                continue;
            }
            
            config::ConfigGenerator configGen(db_);
            auto xrayConfig = configGen.generateConfig(profile);
            
            boost::json::object xrayConfigRoot;
            boost::json::array inboundArray;
            
            int apiPort = actualApiPort;
            
            boost::json::object socksInbound;
            socksInbound["tag"] = "socks-in";
            socksInbound["protocol"] = "mixed";
            socksInbound["port"] = actualStartPort;
            socksInbound["listen"] = "127.0.0.1";
            socksInbound["settings"] = boost::json::object({
                {"auth", "noauth"},
                {"udp", true}
            });
            inboundArray.push_back(socksInbound);
            
            boost::json::object apiInbound;
            apiInbound["tag"] = "api";
            apiInbound["listen"] = "127.0.0.1";
            apiInbound["port"] = apiPort;
            apiInbound["protocol"] = "dokodemo-door";
            apiInbound["settings"] = boost::json::object({
                {"address", "127.0.0.1"}
            });
            inboundArray.push_back(apiInbound);
            
            xrayConfigRoot["inbounds"] = inboundArray;
            
            boost::json::value outboundJson = boost::json::parse(xrayConfig.outbound_json);
            boost::json::array outbounds;
            
            boost::json::object proxyOutbound = outboundJson.at("outbounds").at(0).as_object();
            proxyOutbound["tag"] = "proxy";
            outbounds.push_back(proxyOutbound);
            outbounds.push_back(boost::json::parse(R"({"tag": "direct", "protocol": "freedom"})"));
            xrayConfigRoot["outbounds"] = outbounds;
            
            xrayConfigRoot["api"] = boost::json::object({
                {"tag", "api"},
                {"services", boost::json::array({"HandlerService", "LoggerService", "StatsService"})}
            });
            
            xrayConfigRoot["stats"] = boost::json::object();
            xrayConfigRoot["policy"] = boost::json::object({
                {"levels", boost::json::object({
                    {"0", boost::json::object({
                        {"statsUserUplink", true},
                        {"statsUserDownlink", true}
                    })}
                })},
                {"system", boost::json::object({
                    {"statsInboundUplink", true},
                    {"statsInboundDownlink", true},
                    {"statsOutboundUplink", true},
                    {"statsOutboundDownlink", true}
                })}
            });
            
            xrayConfigRoot["routing"] = boost::json::object({
                {"domainStrategy", "AsIs"},
                {"rules", boost::json::array({
                    boost::json::object({
                        {"type", "field"},
                        {"inboundTag", boost::json::array({"api"})},
                        {"outboundTag", "api"}
                    }),
                    boost::json::object({
                        {"type", "field"},
                        {"outboundTag", "proxy"},
                        {"network", "tcp"}
                    })
                })}
            });
            
            xrayConfigRoot["log"] = boost::json::object({
                {"access", "xray_sub_access.log"},
                {"error", "xray_sub_error.log"},
                {"loglevel", "debug"}
            });
            
            std::string configStr = boost::json::serialize(xrayConfigRoot);
            log("DEBUG: Full xray config: " + configStr);
            
            std::string configFile = "E:\\eclipse_workspace\\multiple_thread_validproxy\\bin\\xray_priority_single.json";
            std::ofstream configOut(configFile);
            configOut << configStr;
            configOut.close();
            
            std::string normalizedPath = configFile;
            for (char& c : normalizedPath) {
                if (c == '/') c = '\\';
            }
            
            std::string cmd = "\"" + xrayPath_ + "\" run -c \"" + normalizedPath + "\"";
            log("DEBUG: Starting priority xray: " + cmd);
            
            STARTUPINFOA si = {0};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            
            PROCESS_INFORMATION pi = {0};
            DWORD createFlags = CREATE_SUSPENDED | CREATE_NO_WINDOW;
            
            if (!CreateProcessA(nullptr, (char*)cmd.c_str(), nullptr, nullptr, FALSE, 
                                createFlags, nullptr, nullptr, &si, &pi)) {
                log("ERROR: Failed to start xray, err=" + std::to_string(GetLastError()));
                continue;
            }
            
            HANDLE job = CreateJobObjectA(nullptr, nullptr);
            if (job) {
                AssignProcessToJobObject(job, pi.hProcess);
            }
            
            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            
            log("DEBUG: Priority xray started, waiting 5s...");
            Sleep(5000);
            
            bool xrayReady = false;
            std::string apiAddr = "127.0.0.1:" + std::to_string(apiPort);
            
            for (int j = 0; j < 10; j++) {
                Sleep(1000);
                xray::XrayApi testApi(xrayPath_, apiAddr);
                if (testApi.listOutbounds()) {
                    log("DEBUG: Priority xray API ready after " + std::to_string(j+1) + "s");
                    xrayReady = true;
                    break;
                }
                log("DEBUG: Waited " + std::to_string(j+1) + "s for priority xray...");
            }
            
            if (!xrayReady) {
                log("ERROR: Priority xray API not ready, trying next proxy...");
                if (job) {
                    TerminateJobObject(job, 0);
                    CloseHandle(job);
                }
                continue;
            }
            
            log("INFO: Testing proxy connectivity via SOCKS port " + std::to_string(actualStartPort) + "...");
            long latencyMs = -1;
            std::string errorMsg;
            bool proxyWorking = testProxyConnectivity(actualStartPort, latencyMs, errorMsg);
            
            if (!proxyWorking) {
                log("WARN: Proxy connectivity test failed: " + errorMsg + ", trying next proxy...");
                if (job) {
                    TerminateJobObject(job, 0);
                    CloseHandle(job);
                }
                continue;
            }
            
            log("INFO: Proxy connectivity test SUCCEEDED (latency=" + std::to_string(latencyMs) + "ms)");
            
            content = fetchUrlViaProxy(sub.url, actualStartPort);
            
            if (job) {
                TerminateJobObject(job, 0);
                CloseHandle(job);
            }
            
            if (!content.empty()) {
                log("INFO: Successfully fetched subscription via proxy");
                break;
            }
            
            log("WARN: Failed to fetch via proxy, trying next...");
        }
        
        if (content.empty()) {
            log("ERROR: All priority proxies failed");
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

bool SubitemUpdater::testProxyConnectivity(int socksPort, long& latencyMs, std::string& errorMsg) {
    latencyMs = -1;
    errorMsg = "";

    CURL* curl = curl_easy_init();
    if (!curl) {
        errorMsg = "curl_init_failed";
        return false;
    }

    std::string proxyUrl = "http://127.0.0.1:" + std::to_string(socksPort);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, "https://www.google.com/generate_204");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, test_timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    double totalTime = 0;
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
    latencyMs = static_cast<long>(totalTime * 1000);

    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        errorMsg = curl_easy_strerror(res);
    } else if (responseCode != 200 && responseCode != 204) {
        errorMsg = "http_" + std::to_string(responseCode);
    }

    return res == CURLE_OK && (responseCode == 200 || responseCode == 204);
}
