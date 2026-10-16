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

// ---------------------------------------------------------------------------
// P0 可观测性辅助（纯统计，不改变任何解析行为）
//
// 刻意用自由函数预扫而不是给 decodeBase64 加 out 参数：P0 阶段必须保持
// decodeBase64 签名不变（零功能行为变更、零回归风险），P2 阶段再决定是否
// 改成 out 参数。预扫口径与 decodeBase64 的跳过规则保持一致：
//   '=' padding、空白、>127 字节、以及不在标准 base64 字母表内的字符均不计数。
// ---------------------------------------------------------------------------

// 字母表与 decodeBase64 的 base64_chars 保持一致（"A-Za-z0-9+/"）。
inline bool isBase64AlphabetChar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/';
}

// 载荷中真正参与 base64 分组的字符数。decodeBase64 在 i==2/3 时也会产出
// 非空结果（尾部补零追加 1~2 字节），所以不能用 jsonStr.empty() 判定载荷是否
// 「过短」—— decodeBase64("AA") 返回 1 字节非空串但同样无法解出 JSON。
size_t countAlphabetChars(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '=' || c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c > 127) continue;
        if (isBase64AlphabetChar(c)) ++n;
    }
    return n;
}

// decodeBase64 会静默丢弃的字符数（不在字母表内、且不是 padding/空白/高位字节）。
long countDroppedChars(const std::string& s) {
    long n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '=' || c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c > 127) continue;
        if (!isBase64AlphabetChar(c)) ++n;
    }
    return n;
}

// base64url 特征字符 '-' 与 '_' 的个数。它们不在标准 base64 字母表内，
// decodeBase64 会静默丢弃（4 字符分组因此可能错位）。
long countUrlAlphabetChars(const std::string& s) {
    long n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '-' || s[i] == '_') ++n;
    }
    return n;
}

// 只解码 %XX 转义，**绝不把字面 '+' 变成空格**。
//
// 为什么不能用 SubscriptionParser::urlDecode()：它在第二轮
// （maxIterations == 1）会把每个字面 '+' 替换成空格，这是为 URL 查询串
// 的 application/x-www-form-urlencoded 设计的。而标准 base64 的 '+'
// 是 sextet 62，是合法且必需的数据。一旦用户信息里含 %XX（正是我们要
// 处理的情形），urlDecode 就会跑满两轮并把所有 '+' 吃掉，4 字符分组错位，
// 把一个能救回的节点变成乱码。
//
// 只做 %XX 也足以覆盖真实故障：面板 URL 编码分享链接时破坏的都是
// '=' padding（编码成 %3D）与极少量非字母表字符，而 '+' 本身在
// base64 文本里不会被编码（encodeURIComponent 不转义 '+'）。
std::string decodePercentEscapesOnly(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int value = 0;
            bool ok = true;
            for (int k = 1; k <= 2; ++k) {
                const char h = s[i + static_cast<size_t>(k)];
                int digit;
                if (h >= '0' && h <= '9') digit = h - '0';
                else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') digit = h - 'A' + 10;
                else { ok = false; break; }
                value = (value << 4) | digit;
            }
            if (ok) {
                out += static_cast<char>(static_cast<unsigned char>(value));
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

// 去掉第一个 '#' 及其后的 fragment（分享链接的节点名）。没有 '#' 时原样返回。
std::string stripFragment(const std::string& s) {
    const size_t hashPos = s.find('#');
    return (hashPos == std::string::npos) ? s : s.substr(0, hashPos);
}

// 判断 vmess:// 之后的载荷是不是「本应是 vless:// 的明文 URI」。
//
// 起因：部分上游把明文 VLESS 分享链接误挂到 "vmess://" 前缀上，例如
//   vmess://06d4d001-4168-4876-9416-7caecf578f40@178.105.168.171:211#备注
// 这段 payload 不是 base64，decodeBase64 会把 UUID 里的 4 个 '-' 与 ':'、'@'
// 一并丢弃，把十六进制字符当 base64 解，于是命中 "not JSON" 被整行丢弃。
//
// 判据（宁缺勿滥，避免误伤真正的 vmess base64 载荷）：
//   1) 含 '@'（标准 base64 字母表不含 '@'，所以真正的 vmess 载荷绝不会含它）；
//   2) '@' 之前是非空 id；'@' 之后形如 host:port，且 port 是 1..65535 的纯数字。
// 三条同时成立才认定，把误挂的行改写成真正的 vless:// 交给既有 vless 分支处理。
bool looksLikePlainVlessUri(const std::string& payload) {
    const size_t atPos = payload.find('@');
    if (atPos == std::string::npos || atPos == 0) return false;

    const std::string hostPart = payload.substr(atPos + 1);
    if (hostPart.empty()) return false;

    // vless 明文载荷是 uuid@host:port?query 形态 - fragment 已由调用方
    // stripFragment 剥掉，但 ? 之后的参数串必须在此截断，否则 port 切片
    // 会变成 80?ed=2560... 这类非纯数字串，判据恒为 false。
    // 真实数据：crackbest line 20/399 正是此形态（修前 mislabeled 恒 0）。
    std::string hostNoQuery = hostPart;
    const size_t qPos = hostNoQuery.find('?');
    if (qPos != std::string::npos) hostNoQuery = hostNoQuery.substr(0, qPos);

    const size_t colonPos = hostNoQuery.rfind(':');
    if (colonPos == std::string::npos || colonPos == 0 || colonPos + 1 >= hostNoQuery.size()) return false;

    const std::string port = hostNoQuery.substr(colonPos + 1);
    for (size_t i = 0; i < port.size(); ++i) {
        if (port[i] < '0' || port[i] > '9') return false;
    }
    const long portNum = std::stol(port);
    return portNum >= 1 && portNum <= 65535;
}

} // anonymous namespace

std::vector<db::models::Profileitem> SubscriptionParser::parse(const std::string& content, const std::string& subid) {
    ParseStats ignored;
    return parse(content, subid, ignored);
}

std::vector<db::models::Profileitem> SubscriptionParser::parse(const std::string& content, const std::string& subid,
                                                                ParseStats& stats) {
    std::vector<db::models::Profileitem> profiles;

    // 明细日志限流：必须是本函数的栈上局部变量，禁止 static 或成员变量，
    // 否则跨订阅共享「第 3 条」额度，只有第一个订阅有明细日志。
    int failureDetailBudget = 3;

    bool hasProtocol = (content.find("://") != std::string::npos);
    std::string decoded;

    stats.bodyHadProtocol = hasProtocol;

    if (hasProtocol) {
        Logger::write("INFO: Content appears to be plain text share links", LogLevel::INFO);
        decoded = content;
    } else {
        Logger::write("INFO: Attempting base64 decode...", LogLevel::INFO);
        stats.bodyDecodeApplied = true;
        // body 级特征统计：在解码**之前**预扫（decodeBase64 签名不变）
        stats.bodyUrlCharCount   = countUrlAlphabetChars(content);
        stats.bodyNonBase64Chars = countDroppedChars(content);
        decoded = decodeBase64(content);

        if (decoded.length() < 10 || decoded.find("://") == std::string::npos) {
            stats.bodyDecodeFellBack = true;
            Logger::write("WARN: Base64 decode produced invalid content, using original [subid=" + subid +
                              "] hadProtocol=" + (hasProtocol ? "1" : "0") +
                              " decodedLen=" + std::to_string(decoded.length()) +
                              " urlChars=" + std::to_string(stats.bodyUrlCharCount) +
                              " junk=" + std::to_string(stats.bodyNonBase64Chars) + " -> 0 nodes",
                          LogLevel::WARN);
            decoded = content;
        }
    }
    stats.bodyDecodedLength = static_cast<long>(decoded.length());

    Logger::write("INFO: Final content length: " + std::to_string(decoded.length()), LogLevel::INFO);

    std::istringstream stream(decoded);
    std::string line;

    int lineCount = 0;
    int validCount = 0;
    while (std::getline(stream, line)) {
        lineCount++;
        ++stats.lineCount;
        if (line.empty()) { ++stats.droppedEmptyLine; continue; }
        if (line.back() == '\r') line.pop_back();

        // 订阅元数据注释头：v2rayN / Clash Verge 等面板会在明文订阅里以
        // "#profile-title:"、"#subscription-userinfo:"、"#support-url:"、
        // "#profile-update-interval:" 开头的行给出展示信息与流量配额。
        // 这些行本来就不是节点，之前会掉进末尾的 else 分支被计入
        // unrecognizedLines，把「丢弃率」统计污染成假告警。
        // 判据：行首第一个字符是 '#'。没有任何分享链接 scheme 以 '#' 开头，
        // 所以这个判据不会误伤合法节点；共享链接的 '#' 是 fragment（位于行尾），
        // 也不受影响。
        if (line[0] == '#') {
            ++stats.metadataCommentLines;
            continue;
        }

        // 容错：部分上游把明文 VLESS 分享链接误挂到 "vmess://" 前缀上，例如
        //   vmess://06d4d001-4168-4876-9416-7caecf578f40@178.105.168.171:211#备注
        // 这段载荷不是 base64 —— decodeBase64 会把 UUID 里的 '-' 与 ':' '@'
        // 一并静默丢弃，把十六进制字符当 base64 解出乱码，命中 "not JSON"
        // 被整行丢弃（真实数据：crackbest 订阅 line 127/313 共 2 行）。
        //
        // 判据严格（looksLikePlainVlessUri）：载荷含 '@'、id 非空、host:port
        // 且 port ∈ 1..65535。标准 base64 字母表不含 '@'，真正的 vmess base64
        // 载荷绝不会含它，因此误判风险极低。
        //
        // 改写后控制流完全不变：line 落入下方既有的 vless:// 分支解析。
        // countedAsVmess 保持 false ⇒ 该行最终计 otherOk，等式不变量不受影响。
        if (line.compare(0, 8, "vmess://") == 0) {
            const std::string vlessPayload = stripFragment(line.substr(8));
            if (looksLikePlainVlessUri(vlessPayload)) {
                line = "vless://" + vlessPayload;
                ++stats.vmessMislabeledPlainVless;
            }
        }

        db::models::Profileitem profile;
        profile.subid = subid;
        profile.issub = "1";

        bool countedAsVmess = false;

        if (line.find("vmess://") == 0) {
            countedAsVmess = true;
            std::string encoded = line.substr(8);
            // payload 级特征统计：在解码之前预扫
            const long urlChars = countUrlAlphabetChars(encoded);
            stats.payloadUrlCharTotal += urlChars;
            if (urlChars > 0) ++stats.payloadUrlCharLines;
            stats.payloadNonBase64Chars += countDroppedChars(encoded);

            if (countAlphabetChars(encoded) < 2) { ++stats.vmessSkipEmpty; continue; }

            std::string jsonStr = decodeBase64(encoded);
            if (jsonStr.empty()) {
                jsonStr = urlDecode(encoded);
            }
            if (jsonStr.empty()) { ++stats.vmessSkipEmpty; continue; }

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
                                ++stats.vmessFailSalvaged;  // 子集标记，不进等式
                            } catch (...) {}
                        }
                    }
                }
                if (!parseSuccess) {
                    if (failureDetailBudget > 0) {
                        --failureDetailBudget;
                        Logger::write("WARN: vmess parse failed [subid=" + subid +
                                          " line=" + std::to_string(lineCount) + "] " + errMsg,
                                      LogLevel::WARN);
                    }
                }
            }
            if (!parseSuccess) { ++stats.vmessFailParse; continue; }
        } else if (line.find("vless://") == 0) {
            std::string uri = line.substr(8);
            size_t atPos = uri.find('@');
            if (atPos == std::string::npos) { ++stats.droppedMalformedLine; ++stats.droppedByProtocol[0]; continue; }

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
            if (addr.empty()) { ++stats.droppedMalformedLine; ++stats.droppedByProtocol[0]; continue; }
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
            // 旧式 SIP001：整个 "method:password@host:port" 被一次性 base64，
            // 解码后的明文里才有 '@'，分享链接本身看不到 '@'。SIP002（明文
            // userInfo + base64 host，或 base64 userInfo + 明文 host）都带 '@'，
            // 因此只有走到这里（无 '@'）才可能是 SIP001。
            // 容错：先整段解一次，若结果形如 "method:password@host:port"
            // （'@' 前有 ':'），就把 uri 改写成该明文并继续走既有 SIP002 路径。
            // sip001UserInfoIsPlaintext 记录「userInfo 此时已是明文，勿再 base64」。
            bool sip001UserInfoIsPlaintext = false;
            if (atPos == std::string::npos) {
                const std::string decodedWhole = decodeBase64(decodePercentEscapesOnly(uri));
                const size_t sipAt = decodedWhole.rfind('@');
                if (sipAt != std::string::npos && sipAt > 0) {
                    const size_t sipColon = decodedWhole.find(':');
                    if (sipColon != std::string::npos && sipColon < sipAt) {
                        uri = decodedWhole;
                        atPos = sipAt;
                        sip001UserInfoIsPlaintext = true;
                        ++stats.ssSip001Recovered;
                    }
                }
            }
            if (atPos == std::string::npos) { ++stats.droppedMalformedLine; ++stats.droppedByProtocol[1]; continue; }

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
            if (addr.empty()) { ++stats.droppedMalformedLine; ++stats.droppedByProtocol[1]; continue; }
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

            // ss 级特征统计：在解码之前预扫（:364 调用点专属字段，不得混入 payload*）。
            // SIP001 容错路径下 userInfo 已是解码后的明文，'-'/'_' 属密码字符
            // 而非 base64url 特征，不计入（保持该字段「base64url 特征」的口径）。
            stats.ssUrlCharCount += sip001UserInfoIsPlaintext ? 0 : countUrlAlphabetChars(userInfo);
            // 面板在生成分享链接时会对 userInfo 做 URL 编码，最常见的破坏是把
            // base64 padding 的 '=' 编码成 '%3D'。decodeBase64 会丢掉 '%'、把 '3D'
            // 当成两个有效数据字符，于是 32 字符的正确 base64 变成 33 字符，解出
            // 多一个字节 —— 实测 "aes-128-gcm:shadowsocks" 被解成
            // "aes-128-gcm:shadowsocks7"。**该串仍是可打印 ASCII，能通过下面
            // 的 isPrintableAscii 检查，节点照常入库，但密码是错的**，连接必然
            // 失败且无任何日志提示。这比「节点被丢弃」更隐蔽，务必先解 %XX。
            // 真实数据：crackbest 订阅 74 条 ss:// 中有 14 条命中此形态。
            // 注意此处必须用只解 %XX 的变体而不是 urlDecode()，否则含 '+' 的
            // 合法 base64 会被 urlDecode 第二轮替换成空格。
            // SIP001 容错路径的 userInfo 已是明文 method:password，直接采用，
            // 不得再喂 decodeBase64（会把明文搅成乱码）。
            std::string decodedInfo = sip001UserInfoIsPlaintext ? userInfo
                                           : decodeBase64(decodePercentEscapesOnly(userInfo));
            size_t methodPos = decodedInfo.find(':');
            if (methodPos == std::string::npos) {
                // No "method:password" separator -> malformed link, drop node.
                ++stats.droppedMalformedLine; ++stats.droppedByProtocol[1];
                continue;
            }
            const std::string method = decodedInfo.substr(0, methodPos);
            const std::string password = decodedInfo.substr(methodPos + 1);
            if (method.empty() || password.empty() ||
                !utils::isPrintableAscii(method) || !utils::isPrintableAscii(password)) {
                // Binary garbage (e.g. base64 of a URL-encoded username) can
                // never form a usable shadowsocks cipher/password -> drop node.
                ++stats.droppedMalformedLine; ++stats.droppedByProtocol[1];
                continue;
            }
            profile.security = method;
            profile.id = password;
        } else if (line.find("trojan://") == 0) {
            std::string uri = line.substr(9);  // "trojan://" is 9 chars
            size_t atPos = uri.find('@');
            if (atPos == std::string::npos) { ++stats.droppedMalformedLine; ++stats.droppedByProtocol[2]; continue; }

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
            if (addr.empty()) { ++stats.droppedMalformedLine; ++stats.droppedByProtocol[2]; continue; }
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
            if (atPos == std::string::npos) { ++stats.droppedMalformedLine; ++stats.droppedByProtocol[3]; continue; }

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
            if (colonPos == std::string::npos) { ++stats.droppedMalformedLine; ++stats.droppedByProtocol[3]; continue; }
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
            ++stats.unrecognizedLines;
            continue;
        }

        profiles.push_back(profile);
        validCount++;
        if (countedAsVmess) ++stats.vmessOk; else ++stats.otherOk;
    }

    Logger::write("INFO: Parsed " + std::to_string(validCount) + " valid profiles from " + std::to_string(lineCount) + " lines", LogLevel::INFO);

    // ---- P0.4 归因汇总（每个 subid 一行，字段覆盖 P4 的四个必答问题） ----
    Logger::write("WARN: vmess summary [subid=" + subid + "]"
                      " lines="          + std::to_string(stats.lineCount) +
                      " ok="             + std::to_string(stats.vmessOk) +
                      " otherOk="        + std::to_string(stats.otherOk) +
                      " fail="           + std::to_string(stats.vmessFailParse) +
                      " salvaged="       + std::to_string(stats.vmessFailSalvaged) +
                      " skipEmpty="      + std::to_string(stats.vmessSkipEmpty) +
                      " unrecognized="   + std::to_string(stats.unrecognizedLines) +
                      " malformed="      + std::to_string(stats.droppedMalformedLine) +
                      " emptyLine="      + std::to_string(stats.droppedEmptyLine) +
                      " metaComment="   + std::to_string(stats.metadataCommentLines) +
                      " mislabeled="    + std::to_string(stats.vmessMislabeledPlainVless) +
                      " sip001="        + std::to_string(stats.ssSip001Recovered) +
                      // P4 问题 2：载荷是否含 base64url 特征字符（行数 / 总数）
                      " payloadUrlLines=" + std::to_string(stats.payloadUrlCharLines) +
                      " payloadUrlChars=" + std::to_string(stats.payloadUrlCharTotal) +
                      " payloadJunk="     + std::to_string(stats.payloadNonBase64Chars) +
                      // P4 问题 3：明文 JSON / URL 编码（P3 才写入，P0 恒 0）
                      " payloadPlainJson=" + std::to_string(stats.payloadPlainJsonLines) +
                      " payloadUrlEncoded=" + std::to_string(stats.payloadUrlEncoded) +
                      // body 级：区分「整段 base64 解崩」与「逐行解码错位」
                      " bodyUrlChars="    + std::to_string(stats.bodyUrlCharCount) +
                      " bodyJunk="        + std::to_string(stats.bodyNonBase64Chars) +
                      " bodyDecodedLen="  + std::to_string(stats.bodyDecodedLength) +
                      " bodyFellBack="    + std::string(stats.bodyDecodeFellBack ? "1" : "0") +
                      " ssUrlChars="      + std::to_string(stats.ssUrlCharCount) +
                      " byProto=v"        + std::to_string(stats.droppedByProtocol[0]) +
                      ",s"                + std::to_string(stats.droppedByProtocol[1]) +
                      ",t"                + std::to_string(stats.droppedByProtocol[2]) +
                      ",h"                + std::to_string(stats.droppedByProtocol[3]),
                  LogLevel::WARN);

    // ---- P0.5 丢弃率告警（两档，互斥不重叠） ----
    // 注：v1.2 原定的「lineCount == 0 && !content.empty()」这一档恒假
    // （任何非空 content 至少产出 1 行 ⇒ lineCount >= 1），且生产不可达 ——
    // src/SubitemUpdaterV2.cpp:242/:421/:499 三个调用点都在 parse() 之前
    // 就挡掉了空 content 并打 INFO。空体场景由 :53-56 那条带 subid 的 WARN 覆盖。
    const long parsedOk = stats.vmessOk + stats.otherOk;
    if (stats.lineCount > 0 && parsedOk == 0) {
        Logger::write("WARN: subid=" + subid + ": parsed 0 nodes from " +
                          std::to_string(stats.lineCount) + " lines" +
                          " (fail=" + std::to_string(stats.vmessFailParse) +
                          " skipEmpty=" + std::to_string(stats.vmessSkipEmpty) +
                          " unrecognized=" + std::to_string(stats.unrecognizedLines) +
                          " malformed=" + std::to_string(stats.droppedMalformedLine) + ")",
                      LogLevel::WARN);
    } else if (parsedOk > 0 &&
               (stats.vmessFailParse + stats.unrecognizedLines + stats.droppedMalformedLine) * 2 > parsedOk) {
        const long dropped = stats.vmessFailParse + stats.unrecognizedLines + stats.droppedMalformedLine;
        const long pct = dropped * 100 / parsedOk;
        Logger::write("WARN: subid=" + subid + ": drop rate " + std::to_string(pct) + "%" +
                          " (ok=" + std::to_string(parsedOk) +
                          " fail=" + std::to_string(stats.vmessFailParse) +
                          " unrecognized=" + std::to_string(stats.unrecognizedLines) +
                          " malformed=" + std::to_string(stats.droppedMalformedLine) + ")",
                      LogLevel::WARN);
    }
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