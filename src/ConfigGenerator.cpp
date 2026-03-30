#include <sstream>
#include <iomanip>
#include <sqlite3.h>
#include <stdexcept>
#include <set>

#include "ConfigGenerator.h"
#include "Profileitem.h"
#include "Profileexitem.h"

namespace config {

ConfigGenerator::ConfigGenerator(sqlite3* db) : db_(db) {}

std::vector<db::models::Profileitem> ConfigGenerator::loadProfiles(const std::string& sqlQuery) {
    db::models::ProfileitemDAO dao(db_);
    return dao.getAll(sqlQuery);
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
    static const std::set<std::string> valid = {
        "tcp","ws","grpc","h2","httpupgrade","kcp","xhttp"
    };
    return valid.count(network) > 0;
}

boost::json::object ConfigGenerator::buildStreamSettings(const db::models::Profileitem& p) {
    boost::json::object streamSettings;
    streamSettings["network"] = p.network;
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
        streamSettings["tlsSettings"] = tlsSettings;
    }
    else if (p.streamsecurity == "reality") {
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

        if (!p.mldsa65verify.empty()) {
            realitySettings["mldsa65Verify"] = p.mldsa65verify;
        } else {
            realitySettings["mldsa65Verify"] = "";
        }

        if (!p.fingerprint.empty()) {
            realitySettings["fingerprint"] = p.fingerprint;
        } else {
            realitySettings["fingerprint"] = "chrome";
        }

        streamSettings["realitySettings"] = realitySettings;
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
    user["id"] = p.id;
    user["email"] = "t@t.tt";
    user["encryption"] = "none";

    if (!p.flow.empty()) {
        user["flow"] = p.flow;
    }

    if (!p.security.empty()) {
        user["security"] = "auto";
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
    user["id"] = p.id;
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
