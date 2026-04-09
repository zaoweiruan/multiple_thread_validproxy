// ShareLinkParser.h
#pragma once
#include "ProfileItem.h"
#include "ShareLinkParser.h"
#include <algorithm>
#include <sstream>
#include <random>
#include <chrono>
#include <boost/json.hpp>   // 用于 VMess JSON 解析

class ShareLinkParser {
public:
    static ProfileItem Parse(const std::string& shareLink);

private:
    static ProfileItem ParseVmess(const std::string& link);
    static ProfileItem ParseVless(const std::string& link);
    static ProfileItem ParseTrojan(const std::string& link);
    static ProfileItem ParseShadowsocks(const std::string& link);
    static ProfileItem ParseHysteria2(const std::string& link);   // 扩展支持

    static std::string Base64Decode(const std::string& input);
    static std::string UrlDecode(const std::string& str);
    static std::map<std::string, std::string> ParseQueryString(const std::string& query);
};



std::string ProfileItem::GenerateIndexId() {
    static std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t val = rng();
    return std::to_string(val);
}

// Base64 / UrlDecode 简单实现（生产环境可换用 boost::beast::detail::base64）
std::string ShareLinkParser::Base64Decode(const std::string& input) { /* 实现 Base64 decode，这里省略具体代码，可用现成库 */ return input; }
std::string ShareLinkParser::UrlDecode(const std::string& str) { /* 实现 URL decode */ return str; }

std::map<std::string, std::string> ShareLinkParser::ParseQueryString(const std::string& query) {
    std::map<std::string, std::string> params;
    std::stringstream ss(query);
    std::string token;
    while (std::getline(ss, token, '&')) {
        auto eq = token.find('=');
        if (eq != std::string::npos) {
            params[token.substr(0, eq)] = UrlDecode(token.substr(eq + 1));
        }
    }
    return params;
}

// ==================== VLESS (最常用，含 Reality) ====================
ProfileItem ShareLinkParser::ParseVless(const std::string& link) {
    ProfileItem item;
    item.IndexId = ProfileItem::GenerateIndexId();
    item.ConfigType = EConfigType::VLESS;

    std::string content = link.substr(8);  // 去掉 "vless://"
    size_t at = content.find('@');
    if (at == std::string::npos) throw std::runtime_error("Invalid vless link");

    item.Password = content.substr(0, at);

    std::string rest = content.substr(at + 1);
    size_t hash = rest.find('#');
    std::string addrPortQuery = (hash != std::string::npos) ? rest.substr(0, hash) : rest;
    item.Remarks = (hash != std::string::npos) ? UrlDecode(rest.substr(hash + 1)) : "VLESS-" + item.IndexId.substr(0, 8);

    size_t q = addrPortQuery.find('?');
    std::string addrPort = (q != std::string::npos) ? addrPortQuery.substr(0, q) : addrPortQuery;
    std::string query = (q != std::string::npos) ? addrPortQuery.substr(q + 1) : "";

    // address:port
    size_t colon = addrPort.find_last_of(':');
    if (colon != std::string::npos) {
        item.Address = addrPort.substr(0, colon);
        try { item.Port = std::stoi(addrPort.substr(colon + 1)); } catch (...) { item.Port = 443; }
    } else {
        item.Address = addrPort;
        item.Port = 443;
    }

    auto params = ParseQueryString(query);
    item.StreamSecurity = params.count("security") ? params["security"] : "none";
    item.Network = params.count("type") ? params["type"] : "tcp";
    item.Sni = params.count("sni") ? params["sni"] : item.Address;
    item.Fingerprint = params.count("fp") ? params["fp"] : "chrome";
    item.PublicKey = params.count("pbk") ? params["pbk"] : "";
    item.ShortId = params.count("sid") ? params["sid"] : "";
    item.Path = params.count("path") ? UrlDecode(params["path"]) : "";
    item.Alpn = params.count("alpn") ? params["alpn"] : "";

    return item;
}

// ==================== VMess (Base64 + JSON) ====================
ProfileItem ShareLinkParser::ParseVmess(const std::string& link) {
    ProfileItem item;
    item.IndexId = ProfileItem::GenerateIndexId();
    item.ConfigType = EConfigType::VMess;

    std::string b64 = link.substr(8);  // 去掉 "vmess://"
    std::string jsonStr = Base64Decode(b64);

    boost::json::value v = boost::json::parse(jsonStr);
    auto& obj = v.as_object();

    item.Password = obj["id"].as_string().c_str();   // uuid
    item.Address = obj["add"].as_string().c_str();
    item.Port = std::stoi(obj["port"].as_string().c_str());
    item.Remarks = obj.contains("ps") ? obj["ps"].as_string().c_str() : "VMess";
    item.Network = obj.contains("net") ? obj["net"].as_string().c_str() : "tcp";
    item.Path = obj.contains("path") ? obj["path"].as_string().c_str() : "";
    item.Sni = obj.contains("sni") ? obj["sni"].as_string().c_str() : item.Address;

    // 更多字段可继续解析...

    return item;
}

// ==================== Trojan ====================
ProfileItem ShareLinkParser::ParseTrojan(const std::string& link) {
    ProfileItem item;
    item.IndexId = ProfileItem::GenerateIndexId();
    item.ConfigType = EConfigType::Trojan;

    std::string content = link.substr(9);  // "trojan://"
    size_t at = content.find('@');
    item.Password = content.substr(0, at);

    std::string rest = content.substr(at + 1);
    size_t hash = rest.find('#');
    std::string addrPort = (hash != std::string::npos) ? rest.substr(0, hash) : rest;
    item.Remarks = (hash != std::string::npos) ? UrlDecode(rest.substr(hash + 1)) : "Trojan";

    size_t colon = addrPort.find_last_of(':');
    if (colon != std::string::npos) {
        item.Address = addrPort.substr(0, colon);
        item.Port = std::stoi(addrPort.substr(colon + 1));
    }

    auto params = ParseQueryString(addrPort.find('?') != std::string::npos ? addrPort.substr(addrPort.find('?') + 1) : "");
    item.Sni = params.count("sni") ? params["sni"] : item.Address;
    item.StreamSecurity = "tls";

    return item;
}

// ==================== Shadowsocks ====================
ProfileItem ShareLinkParser::ParseShadowsocks(const std::string& link) {
    ProfileItem item;
    item.IndexId = ProfileItem::GenerateIndexId();
    item.ConfigType = EConfigType::Shadowsocks;

    // SS 链接支持两种格式，这里实现常见 base64 格式
    std::string content = link.substr(5);  // "ss://"
    size_t hash = content.find('#');
    std::string b64Part = (hash != std::string::npos) ? content.substr(0, hash) : content;
    item.Remarks = (hash != std::string::npos) ? UrlDecode(content.substr(hash + 1)) : "SS";

    std::string decoded = Base64Decode(b64Part);
    // decoded 格式通常为 method:password@address:port
    size_t at = decoded.find('@');
    if (at != std::string::npos) {
        std::string methodPass = decoded.substr(0, at);
        std::string addrPort = decoded.substr(at + 1);
        size_t colonPass = methodPass.find(':');
        item.Password = methodPass.substr(colonPass + 1);
        // method 可存入 ProtoExtra

        size_t colon = addrPort.find_last_of(':');
        item.Address = addrPort.substr(0, colon);
        item.Port = std::stoi(addrPort.substr(colon + 1));
    }

    return item;
}

// Hysteria2 / TUIC 可类似实现（参数较少）

ProfileItem ShareLinkParser::Parse(const std::string& shareLink) {
    std::string lower = shareLink;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.rfind("vmess://", 0) == 0) return ParseVmess(shareLink);
    if (lower.rfind("vless://", 0) == 0) return ParseVless(shareLink);
    if (lower.rfind("trojan://", 0) == 0) return ParseTrojan(shareLink);
    if (lower.rfind("ss://", 0) == 0) return ParseShadowsocks(shareLink);
    // 可继续添加 hysteria2:// tuic:// 等

    throw std::runtime_error("Unsupported share link protocol");
}