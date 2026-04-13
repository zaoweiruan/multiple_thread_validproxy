// ProfileItem.h
#pragma once
#include <string>
#include <map>
#include <boost/json.hpp>

enum class EConfigType {
    VMess = 1,
    VLESS = 2,
    Trojan = 3,
    Shadowsocks = 4,
    Hysteria2 = 5,
    TUIC = 6,
    Socks = 7,
    Http = 8
};

enum class ECoreType {
    Xray = 0,
    Singbox = 1,
    Clash = 2
};

struct ProfileItem {
    std::string IndexId;           // 自动生成
    EConfigType ConfigType = EConfigType::VLESS;
    ECoreType CoreType = ECoreType::Xray;
    int ConfigVersion = 3;
    std::string Subid = "custom";
    std::string Remarks;
    std::string Address;
    int Port = 443;
    std::string Password;          // uuid / password / method 等
    std::string Username;
    std::string Network = "tcp";
    std::string StreamSecurity = "none";   // tls / reality
    std::string Security;
    std::string Sni;
    std::string Alpn;
    std::string Fingerprint = "chrome";
    std::string PublicKey;         // Reality pbk
    std::string ShortId;           // Reality sid
    std::string Path;
    std::string ProtoExtra;        // 复杂协议额外 JSON（如 Hysteria2、TUIC、SS2022）
    bool Enabled = true;
    int Sort = 1000;

    static std::string GenerateIndexId();   // 64位大整数风格，与 v2rayN 一致
};