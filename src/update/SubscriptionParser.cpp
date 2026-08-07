#include "update/SubscriptionParser.h"
#include "Utils.h"
#include "Logger.h"

#include <sstream>
#include <algorithm>
#include <boost/json.hpp>

namespace update {

namespace {

std::string getJsonValueString(const boost::json::object& obj, const char* key, const char* defaultVal = "") {
    if (!obj.contains(key)) return defaultVal;
    try {
        const boost::json::value& val = obj.at(key);
        if (val.is_string()) return val.as_string().c_str();
        if (val.is_int64()) return std::to_string(val.as_int64());
        if (val.is_double()) return std::to_string(static_cast<int>(val.as_double()));
    } catch (...) {
        Logger::write("getJsonValueString: exception for key " + std::string(key ? key : "(null)"), LogLevel::WARN);
    }
    return defaultVal;
}

void sanitizeNetwork(std::string& network, const std::string& indexid = "") {
    if (network == "splithttp") {
        network = "xhttp";
    }
    if (!utils::isValidNetwork(network)) {
        if (!indexid.empty()) {
            Logger::write("Using default network 'tcp' for " + indexid + " (original: " + network + ")", LogLevel::DEBUG);
        }
        network = "tcp";
    }
}

} // anonymous namespace

std::vector<db::models::Profileitem> SubscriptionParser::parse(const std::string& content, const std::string& subid) {
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
                profile.indexid = utils::generateUniqueId();

                if (obj.contains("port")) {
                    boost::json::value& portVal = obj.at("port");
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
                    boost::json::value& aidVal = obj.at("aid");
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
                sanitizeNetwork(profile.network, profile.indexid);
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
                                profile.indexid = utils::generateUniqueId();
                                profile.coretype = "";
                                profile.muxenabled = "0";
                                profile.address = getJsonValueString(obj, "add", "");
                                profile.id = getJsonValueString(obj, "id", "");
                                profile.security = getJsonValueString(obj, "scy", "auto");
                                profile.remarks = getJsonValueString(obj, "ps", "");
                                sanitizeNetwork(profile.network, profile.indexid);
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
            profile.indexid = utils::generateUniqueId();

            std::string hostPart = uri.substr(atPos + 1);
            size_t qPos = hostPart.find('?');
            std::string addrPart = (qPos != std::string::npos) ? hostPart.substr(0, qPos) : hostPart;

            std::pair<std::string, std::string> addrPort = parseAddressPort(addrPart);
            std::string addr = addrPort.first;
            std::string port = addrPort.second;
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

                sanitizeNetwork(profile.network, profile.indexid);

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
            profile.indexid = utils::generateUniqueId();

            std::string userInfo = uri.substr(0, atPos);
            std::string hostInfo = uri.substr(atPos + 1);

            std::pair<std::string, std::string> addrPort2 = parseAddressPort(hostInfo);
            std::string addr = addrPort2.first;
            std::string portWithParams = addrPort2.second;
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
                        if (!profile.sni.empty()) {
                            profile.streamsecurity = "tls";
                            if (profile.sni.find('@') != std::string::npos || 
                                profile.sni.find("\xE2\x80") != std::string::npos ||
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
                        profile.sni.find("\xE2\x80") != std::string::npos ||
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
            if (methodPos == std::string::npos) {
                // No "method:password" separator -> malformed link, drop node.
                continue;
            }
            const std::string method = decodedInfo.substr(0, methodPos);
            const std::string password = decodedInfo.substr(methodPos + 1);
            if (method.empty() || password.empty() ||
                !utils::isPrintableAscii(method) || !utils::isPrintableAscii(password)) {
                // Binary garbage (e.g. base64 of a URL-encoded username) can
                // never form a usable shadowsocks cipher/password -> drop node.
                continue;
            }
            profile.security = method;
            profile.id = password;
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
            profile.indexid = utils::generateUniqueId();

            std::string hostPart = uri.substr(atPos + 1);
            size_t qPos = hostPart.find('?');
            std::string addrPart = (qPos != std::string::npos) ? hostPart.substr(0, qPos) : hostPart;

            std::pair<std::string, std::string> addrPort3 = parseAddressPort(addrPart);
            std::string addr = addrPort3.first;
            std::string port = addrPort3.second;
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

                sanitizeNetwork(profile.network, profile.indexid);

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
            profile.indexid = utils::generateUniqueId();

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

        profiles.push_back(profile);
        validCount++;
    }

    Logger::write("INFO: Parsed " + std::to_string(validCount) + " valid profiles from " + std::to_string(lineCount) + " lines", LogLevel::INFO);
    return profiles;
}

std::string SubscriptionParser::decodeBase64(const std::string& input) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (size_t idx = 0; idx < input.size(); idx++) {
        const char c = input[idx];

        // Padding / whitespace: ignore.
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            continue;
        }
        // Non-ASCII bytes are never valid base64: ignore.
        if (static_cast<unsigned char>(c) > 127) {
            continue;
        }

        // Locate c in the base64 alphabet. Non-alphabet characters (e.g. '%',
        // '@', ':', '_', '-') MUST be skipped — decoding them as index 0
        // corrupts the output stream (root cause of binary-garbage
        // method/password bytes for malformed ss:// links).
        size_t pos = 0;
        bool found = false;
        for (size_t k = 0; k < sizeof(base64_chars); k++) {
            if (base64_chars[k] == c) {
                pos = k;
                found = true;
                break;
            }
        }
        if (!found) {
            continue;
        }

        char_array_4[i] = static_cast<unsigned char>(pos);
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

std::string SubscriptionParser::urlDecode(const std::string& input) {
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

std::pair<std::string, std::string> SubscriptionParser::parseAddressPort(const std::string& addrPart) {
    std::function<std::string(std::string)> trimTrailingSlash = [](std::string s) -> std::string {
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

} // namespace update