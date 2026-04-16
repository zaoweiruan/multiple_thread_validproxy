#include <sstream>
#include <iomanip>
#include <algorithm>
#include <sqlite3.h>
#include <stdexcept>
#include <set>

#include "ConfigGenerator.h"
#include "Profileitem.h"
#include "Profileexitem.h"

namespace config {

bool isValidNetwork(const std::string& network);

ConfigGenerator::ConfigGenerator(sqlite3* db) : db_(db) {}

std::vector<db::models::Profileitem> ConfigGenerator::loadProfiles(const std::string& sqlQuery) {
    db::models::ProfileitemDAO dao(db_);
    auto profiles = dao.getAll(sqlQuery);
    
    std::cout << "[ConfigGenerator] SQL returned " << profiles.size() << " profiles" << std::endl;
    
    std::vector<db::models::Profileitem> validProfiles;
    for (auto& p : profiles) {
        if (p.network.empty() && p.configtype == "3") {
            p.network = "tcp";
        }
        
        if (p.network.empty()) {
            std::cout << "[ConfigGenerator] Skipping " << p.address << ":" << p.port << " - empty network" << std::endl;
            continue;
        }
        
        if (!isValidNetwork(p.network)) {
            std::cout << "[ConfigGenerator] Skipping " << p.address << ":" << p.port << " - invalid network: '" << p.network << "'" << std::endl;
            continue;
        }
        
        validProfiles.push_back(p);
    }
    
    std::cout << "[ConfigGenerator] Valid profiles: " << validProfiles.size() << std::endl;
    return validProfiles;
}

std::vector<db::models::Profileexitem> ConfigGenerator::loadProfileExItems() {
    db::models::ProfileexitemDAO dao(db_);
    return dao.getAll();
}

bool ConfigGenerator::updateProfileExItem(const db::models::Profileexitem& exitem) {
    std::ostringstream sql;
    sql << "INSERT OR REPLACE INTO ProfileExItem (IndexId, Delay, Speed, Sort, Message) VALUES (?, ?, ?, ?, ?)";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, exitem.indexid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, exitem.delay.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, exitem.speed.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, exitem.sort.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, exitem.message.c_str(), -1, SQLITE_TRANSIENT);
    
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

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

boost::json::object ConfigGenerator::buildStreamSettings(const db::models::Profileitem& p) {
    boost::json::object streamSettings;
    
    if (!p.network.empty()) {
        streamSettings["network"] = p.network;
    }
    
    if (!p.streamsecurity.empty()) {
        streamSettings["security"] = p.streamsecurity;
        
        if (p.streamsecurity == "tls") {
            boost::json::object tlsSettings;
            tlsSettings["allowInsecure"] = (p.allowinsecure == "1");
            if (!p.sni.empty()) {
                tlsSettings["serverName"] = p.sni;
            }
            if (!p.alpn.empty()) {
                std::vector<std::string> alpnList;
                std::stringstream ss(p.alpn);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    alpnList.push_back(item);
                }
                boost::json::array alpnArr;
                for (const auto& a : alpnList) {
                    alpnArr.push_back(boost::json::value(a));
                }
                tlsSettings["alpn"] = alpnArr;
            }
            if (!p.fingerprint.empty()) {
                tlsSettings["fingerprint"] = p.fingerprint;
            }
            if (!p.echconfiglist.empty()) {
                tlsSettings["echConfigList"] = boost::json::value(p.echconfiglist);
            }
            if (!p.echforcequery.empty()) {
                tlsSettings["echForceQuery"] = (p.echforcequery == "1");
            }
            if (!p.cert.empty()) {
                boost::json::array certsArr;
                boost::json::object certObj;
                std::stringstream certSs(p.cert);
                std::string line;
                boost::json::array certLines;
                while (std::getline(certSs, line)) {
                    certLines.push_back(boost::json::value(line));
                }
                certObj["certificate"] = certLines;
                certObj["usage"] = "verify";
                certsArr.push_back(certObj);
                
                boost::json::object certSettings;
                certSettings["certificates"] = certsArr;
                certSettings["disableSystemRoot"] = true;
                tlsSettings["allowInsecure"] = false;
                tlsSettings["disableSystemRoot"] = true;
            }
            if (!p.certsha.empty()) {
                tlsSettings["pinnedPeerCertSha256"] = p.certsha;
                tlsSettings["allowInsecure"] = false;
            }
            streamSettings["tlsSettings"] = tlsSettings;
        } else if (p.streamsecurity == "reality") {
            boost::json::object realitySettings;
            if (p.publickey.empty()) {
                throw std::runtime_error("REALITY配置错误：publicKey不能为空");
            }
            if (p.sni.empty()) {
                throw std::runtime_error("REALITY配置错误：sni(serverName)不能为空");
            }

            realitySettings["publicKey"] = p.publickey;
            realitySettings["serverName"] = p.sni;

            if (!p.shortid.empty()) {
                realitySettings["shortId"] = p.shortid;
            } else {
                realitySettings["shortId"] = "";
            }
            
            if (!p.spiderx.empty()) {
                realitySettings["spiderX"] = p.spiderx;
            } else {
                realitySettings["spiderX"] = "";
            }

            if (!p.fingerprint.empty()) {
                realitySettings["fingerprint"] = p.fingerprint;
            } else {
                realitySettings["fingerprint"] = "chrome";
            }

            streamSettings["realitySettings"] = realitySettings;
        }
    }
    
    if (p.network == "grpc") {
        boost::json::object grpcSettings;
        grpcSettings["serviceName"] = p.path;
        grpcSettings["multiMode"] = p.grpcMultiMode == 1;
        grpcSettings["idle_timeout"] = 60;
        grpcSettings["health_check_timeout"] = 20;
        grpcSettings["permit_without_stream"] = false;
        grpcSettings["initial_windows_size"] = 0;
        streamSettings["grpcSettings"] = grpcSettings;
    }

    if (p.network == "ws") {
        boost::json::object wsSettings;
        if (!p.path.empty()) {
            wsSettings["path"] = p.path;
        }
        if (!p.requesthost.empty()) {
            boost::json::object headers;
            headers["host"] = p.requesthost;
            wsSettings["headers"] = headers;
        }
        streamSettings["wsSettings"] = wsSettings;
    }

    if (p.network == "xhttp") {
        boost::json::object xhttpSettings;
        if (!p.path.empty()) {
            xhttpSettings["path"] = p.path;
        }
        if (!p.requesthost.empty()) {
            boost::json::object headers;
            headers["host"] = p.requesthost;
        }
        if (!p.headertype.empty()) {
            boost::json::object headers;
            headers["mode"] = p.headertype;
        }
        streamSettings["xhttpSettings"] = xhttpSettings;
    }

    if (p.network == "kcp") {
        boost::json::object kcpSettings;
        kcpSettings["mtu"] = p.kcpMtu;
        kcpSettings["tti"] = p.kcpTti;
        kcpSettings["uplinkCapacity"] = p.kcpUplink;
        kcpSettings["downlinkCapacity"] = p.kcpDownlink;
        kcpSettings["congestion"] = p.kcpCongestion == 1;
        kcpSettings["readBufferSize"] = p.kcpReadBufferSize;
        kcpSettings["writeBufferSize"] = p.kcpWriteBufferSize;
        if (!p.headertype.empty()) {
            kcpSettings["header"] = boost::json::object{ { "type", p.kcpHeaderType } };
        }
        streamSettings["kcpSettings"] = kcpSettings;
    }

    return streamSettings;
}

boost::json::object ConfigGenerator::buildVLESSOutbound(const db::models::Profileitem& p, const std::string& outboundTag) {
    boost::json::object outbound;

    outbound["tag"] = outboundTag;
    outbound["protocol"] = "vless";

    boost::json::object settings;
    boost::json::array vnextArr;
    boost::json::object vnext;

    vnext["address"] = p.address;
    vnext["port"] = std::stoi(p.port);

    boost::json::array usersArr;
    boost::json::object user;
    if (!p.id.empty()) {
        user["id"] = p.id;
    }
    user["email"] = "t@t.tt";
    user["encryption"] = "none";

    std::string flowValue = p.flow;
    if (!flowValue.empty()) {
        if (flowValue.find("xtls") != std::string::npos || flowValue.find("vision") != std::string::npos) {
            if (p.streamsecurity != "reality" && !p.publickey.empty()) {
                flowValue = "";
            }
        }
        user["flow"] = flowValue;
    }

    if (!p.security.empty() && p.security != "none") {
        user["security"] = p.security;
    }
    usersArr.push_back(user);

    vnext["users"] = usersArr;
    vnextArr.push_back(vnext);
    settings["vnext"] = vnextArr;
    outbound["settings"] = settings;

    boost::json::object streamSettings = buildStreamSettings(p);
    outbound["streamSettings"] = streamSettings;

    boost::json::object mux;
    mux["enabled"] = p.muxEnabled == 1;
    mux["concurrency"] = -1;
    outbound["mux"] = mux;

    return outbound;
}

boost::json::object ConfigGenerator::buildVMessOutbound(const db::models::Profileitem& p, const std::string& outboundTag) {
    boost::json::object outbound;

    outbound["tag"] = outboundTag;
    outbound["protocol"] = "vmess";

    boost::json::object settings;
    boost::json::array vnextArr;
    boost::json::object vnext;

    vnext["address"] = p.address;
    vnext["port"] = std::stoi(p.port);

    boost::json::array usersArr;
    boost::json::object user;
    if (!p.id.empty()) {
        user["id"] = p.id;
    }
    user["alterId"] = 0;
    user["security"] = p.security.empty() ? "auto" : p.security;
    usersArr.push_back(user);

    vnext["users"] = usersArr;
    vnextArr.push_back(vnext);
    settings["vnext"] = vnextArr;
    outbound["settings"] = settings;

    boost::json::object streamSettings = buildStreamSettings(p);
    outbound["streamSettings"] = streamSettings;

    boost::json::object mux;
    mux["enabled"] = p.muxEnabled == 1;
    mux["concurrency"] = -1;
    outbound["mux"] = mux;

    return outbound;
}

boost::json::object ConfigGenerator::buildSSOutbound(const db::models::Profileitem& p, const std::string& outboundTag) {
    boost::json::object outbound;

    outbound["tag"] = outboundTag;
    outbound["protocol"] = "shadowsocks";

    boost::json::object settings;
    boost::json::array serversArr;
    boost::json::object server;

    server["address"] = p.address;
    server["port"] = std::stoi(p.port);
    server["method"] = p.security;
    server["password"] = p.id;

    serversArr.push_back(server);
    settings["servers"] = serversArr;
    outbound["settings"] = settings;

    boost::json::object streamSettings = buildStreamSettings(p);
    outbound["streamSettings"] = streamSettings;

    boost::json::object mux;
    mux["enabled"] = p.muxEnabled == 1;
    mux["concurrency"] = -1;
    outbound["mux"] = mux;

    return outbound;
}

boost::json::object ConfigGenerator::buildTrojanOutbound(const db::models::Profileitem& p, const std::string& outboundTag) {
    boost::json::object outbound;

    outbound["tag"] = outboundTag;
    outbound["protocol"] = "trojan";

    boost::json::object settings;
    boost::json::array serversArr;
    boost::json::object server;

    server["address"] = p.address;
    server["port"] = std::stoi(p.port);
    server["password"] = p.id;

    serversArr.push_back(server);
    settings["servers"] = serversArr;
    outbound["settings"] = settings;

    boost::json::object streamSettings = buildStreamSettings(p);
    outbound["streamSettings"] = streamSettings;

    boost::json::object mux;
    mux["enabled"] = p.muxEnabled == 1;
    mux["concurrency"] = -1;
    outbound["mux"] = mux;

    return outbound;
}

boost::json::object ConfigGenerator::buildSOCKSOutbound(const db::models::Profileitem& p, const std::string& outboundTag) {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "socks";

    boost::json::object settings;
    boost::json::array serversArr;
    boost::json::object server;
    server["address"] = p.address;
    server["port"] = std::stoi(p.port);

    if (!p.username.empty() && !p.id.empty()) {
        boost::json::object user;
        user["user"] = p.username;
        user["pass"] = p.id;
        boost::json::array usersArr;
        usersArr.push_back(user);
        server["users"] = usersArr;
    }

    serversArr.push_back(server);
    settings["servers"] = serversArr;
    outbound["settings"] = settings;

    return outbound;
}

boost::json::object ConfigGenerator::buildHTTPOutbound(const db::models::Profileitem& p, const std::string& outboundTag) {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "http";

    boost::json::object settings;
    boost::json::array serversArr;
    boost::json::object server;
    server["address"] = p.address;
    server["port"] = std::stoi(p.port);

    if (!p.username.empty() && !p.id.empty()) {
        server["user"] = p.username;
        server["pass"] = p.id;
    }

    if (!p.requesthost.empty()) {
        server["headers"] = boost::json::object({ {"host", p.requesthost} });
    }

    serversArr.push_back(server);
    settings["servers"] = serversArr;
    outbound["settings"] = settings;

    if (!p.streamsecurity.empty() && (p.streamsecurity == "tls" || p.streamsecurity == "reality")) {
        outbound["streamSettings"] = buildStreamSettings(p);
    }

    return outbound;
}

boost::json::object ConfigGenerator::buildHysteria2Outbound(const db::models::Profileitem& p, const std::string& outboundTag) {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "hysteria";

    boost::json::object settings;
    settings["address"] = p.address;
    settings["port"] = std::stoi(p.port);
    settings["password"] = p.id;

    if (!p.username.empty()) {
        settings["username"] = p.username;
    }

    if (!p.sni.empty()) {
        settings["sni"] = p.sni;
    }

    if (!p.alpn.empty()) {
        std::vector<std::string> alpnList;
        std::stringstream ss(p.alpn);
        std::string item;
        while (std::getline(ss, item, ',')) {
            alpnList.push_back(item);
        }
        boost::json::array alpnArr;
        for (const auto& a : alpnList) {
            alpnArr.push_back(boost::json::value(a));
        }
        settings["alpn"] = alpnArr;
    }

    if (!p.fingerprint.empty()) {
        settings["fingerprint"] = p.fingerprint;
    }

    if (p.allowinsecure == "1") {
        settings["insecure"] = true;
    }

    if (!p.path.empty()) {
        settings["obfs"] = boost::json::object({
            {"type", "wrand"},
            {"password", p.path}
        });
    }

    outbound["settings"] = settings;

    return outbound;
}

boost::json::object ConfigGenerator::buildTUICOutbound(const db::models::Profileitem& p, const std::string& outboundTag) {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "tuic";

    boost::json::object settings;
    settings["address"] = p.address;
    settings["port"] = std::stoi(p.port);
    settings["password"] = p.id;
    settings["uuid"] = p.id;

    if (!p.username.empty()) {
        settings["username"] = p.username;
    }

    if (!p.sni.empty()) {
        settings["sni"] = p.sni;
    }

    if (!p.alpn.empty()) {
        std::vector<std::string> alpnList;
        std::stringstream ss(p.alpn);
        std::string item;
        while (std::getline(ss, item, ',')) {
            alpnList.push_back(item);
        }
        boost::json::array alpnArr;
        for (const auto& a : alpnList) {
            alpnArr.push_back(boost::json::value(a));
        }
        settings["alpn"] = alpnArr;
    }

    if (p.allowinsecure == "1") {
        settings["allowinsecure"] = true;
    }

    if (!p.publickey.empty()) {
        settings["publicKey"] = p.publickey;
    }

    outbound["settings"] = settings;

    return outbound;
}

boost::json::object ConfigGenerator::buildWireGuardOutbound(const db::models::Profileitem& p, const std::string& outboundTag) {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "wireguard";

    boost::json::object settings;

    if (!p.address.empty()) {
        boost::json::array addressArr;
        addressArr.push_back(boost::json::value(p.address));
        settings["localAddresses"] = addressArr;
    }

    settings["privateKey"] = p.id;

    boost::json::array peersArr;
    boost::json::object peer;
    peer["publicKey"] = p.publickey;
    peer["endpoint"] = p.address + ":" + p.port;

    if (!p.endpoint.empty()) {
        peer["endpoint"] = p.endpoint;
    }

    if (!p.presocksport.empty()) {
        boost::json::array reserved;
        reserved.push_back(boost::json::value(p.presocksport));
        peer["reserved"] = reserved;
    }

    peersArr.push_back(peer);
    settings["peers"] = peersArr;

    outbound["settings"] = settings;

    return outbound;
}

XrayConfig ConfigGenerator::generateConfig(const db::models::Profileitem& profile) {
    XrayConfig config;

    if (profile.address.empty()) {
        throw std::runtime_error("代理地址不能为空");
    }
    if (std::stoi(profile.port) <= 0 || std::stoi(profile.port) > 65535) {
        throw std::runtime_error("无效的端口号: " + profile.port);
    }
    if (profile.id.empty()) {
        throw std::runtime_error("代理ID/密码不能为空");
    }

    if (profile.streamsecurity == "reality") {
        if (profile.publickey.empty()) {
            throw std::runtime_error("REALITY配置缺少publicKey");
        }
        if (profile.sni.empty()) {
            throw std::runtime_error("REALITY配置缺少sni(serverName)");
        }
    }

    boost::json::object outbound;
    std::string outboundTag = "proxy";

    if (profile.configtype == "3") {
        outbound = buildSSOutbound(profile, outboundTag);
    } else if (profile.configtype == "1") {
        outbound = buildVMessOutbound(profile, outboundTag);
    } else if (profile.configtype == "5") {
        outbound = buildVLESSOutbound(profile, outboundTag);
    } else if (profile.configtype == "6") {
        outbound = buildTrojanOutbound(profile, outboundTag);
    } else if (profile.configtype == "4") {
        outbound = buildSOCKSOutbound(profile, outboundTag);
    } else if (profile.configtype == "10") {
        outbound = buildHTTPOutbound(profile, outboundTag);
    } else if (profile.configtype == "7") {
        outbound = buildHysteria2Outbound(profile, outboundTag);
    } else if (profile.configtype == "8") {
        outbound = buildTUICOutbound(profile, outboundTag);
    } else if (profile.configtype == "9") {
        outbound = buildWireGuardOutbound(profile, outboundTag);
    } else if (profile.configtype == "11") {
        throw std::runtime_error("ConfigType 11 (Anytls) 暂不支持生成配置");
    } else if (profile.configtype == "12") {
        throw std::runtime_error("ConfigType 12 (Naive) 暂不支持生成配置");
    } else {
        throw std::runtime_error("不支持的协议类型: " + profile.configtype);
    }

    boost::json::array outboundsArr;
    outboundsArr.push_back(outbound);
    boost::json::object root;
    root["outbounds"] = outboundsArr;

    config.outbound_json = boost::json::serialize(root);
    return config;
}

} // namespace config
