#include "ConfigGenerator.h"
#include "config/ProfileConfigRepository.h"
#include "config/OutboundBuilderFactory.h"
#include "config/XrayConfigAssembler.h"
#include "Profileitem.h"
#include "Logger.h"
#include <stdexcept>

namespace config {

ConfigGenerator::ConfigGenerator(sqlite3* db) : db_(db) {}

std::vector<db::models::Profileitem> ConfigGenerator::loadProfiles(const std::string& sqlQuery) {
    ProfileConfigRepository repo(db_);
    return repo.loadProfiles(sqlQuery);
}

std::vector<db::models::ProfileExItem> ConfigGenerator::loadProfileExItems() {
    ProfileConfigRepository repo(db_);
    return repo.loadProfileExItems();
}

bool ConfigGenerator::updateProfileExItem(const db::models::ProfileExItem& exitem) {
    ProfileConfigRepository repo(db_);
    return repo.updateProfileExItem(exitem);
}

XrayConfig ConfigGenerator::generateConfig(const db::models::Profileitem& profile) {
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

    OutboundBuilderFactory factory;
    boost::json::object outbound = factory.create(profile, "proxy");

    XrayConfigAssembler assembler;
    boost::json::object root = assembler.assemble(outbound);

    XrayConfig config;
    config.outbound_json = boost::json::serialize(root);
    return config;
}

namespace {

// DNS block mirrored from bin/xray-config-template.json so pool members with
// domain addresses resolve correctly. Requires geoip.dat/geosite.dat at
// XRAY_LOCATION_ASSET (the pool launcher sets it). Kept as a parsed constant
// to stay self-contained (no runtime file dependency).
const char* kPoolDnsJson = R"DNS({
  "hosts": {
    "dns.google": ["8.8.8.8","8.8.4.4","2001:4860:4860::8888","2001:4860:4860::8844"],
    "dns.alidns.com": ["223.5.5.5","223.6.6.6","2400:3200::1","2400:3200:baba::1"],
    "one.one.one.one": ["1.1.1.1","1.0.0.1","2606:4700:4700::1111","2606:4700:4700::1001"],
    "1dot1dot1dot1.cloudflare-dns.com": ["1.1.1.1","1.0.0.1","2606:4700:4700::1111","2606:4700:4700::1001"],
    "cloudflare-dns.com": ["104.16.249.249","104.16.248.249","2606:4700::6810:f8f9","2606:4700::6810:f9f9"],
    "dns.cloudflare.com": ["104.16.132.229","104.16.133.229","2606:4700::6810:84e5","2606:4700::6810:85e5"],
    "dot.pub": ["1.12.12.12","120.53.53.53"],
    "doh.pub": ["1.12.12.12","120.53.53.53"],
    "dns.quad9.net": ["9.9.9.9","149.112.112.112","2620:fe::fe","2620:fe::9"],
    "dns.yandex.net": ["77.88.8.8","77.88.8.1","2a02:6b8::feed:0ff","2a02:6b8:0:1::feed:0ff"],
    "dns.sb": ["185.222.222.222","2a09::"],
    "dns.umbrella.com": ["208.67.220.220","208.67.222.222","2620:119:35::35","2620:119:53::53"],
    "dns.sse.cisco.com": ["208.67.220.220","208.67.222.222","2620:119:35::35","2620:119:53::53"],
    "engage.cloudflareclient.com": ["162.159.192.1"]
  },
  "servers": [
    {"address":"https://dns.alidns.com/dns-query","domains":["domain:alidns.com","domain:doh.pub","domain:dot.pub","domain:360.cn","domain:onedns.net","www.wto.org"],"skipFallback":true},
    {"address":"https://cloudflare-dns.com/dns-query","domains":["geosite:google"],"skipFallback":true},
    {"address":"https://dns.alidns.com/dns-query","domains":["geosite:private","geosite:cn"],"skipFallback":true},
    {"address":"223.5.5.5","domains":["full:dns.alidns.com","full:cloudflare-dns.com"],"skipFallback":true},
    "https://cloudflare-dns.com/dns-query"
  ]
})DNS";

boost::json::value buildPoolDns() {
    try {
        return boost::json::parse(kPoolDnsJson);
    } catch (const std::exception& e) {
        Logger::write(std::string("[ConfigGenerator] pool DNS parse failed: ") + e.what(),
                      LogLevel::WARN);
        return boost::json::object();
    }
}

} // namespace

std::string ConfigGenerator::buildPoolConfig(int socksPort, int apiPort, const StandalonePoolConfig& cfg) {
    boost::json::object root;

    boost::json::object logObj;
    logObj["loglevel"] = "warning";
    root["log"] = logObj;

    // DNS: mirror xray-config-template.json (hosts + DoH servers) so member
    // outbounds with domain addresses resolve. Needs geoip.dat/geosite.dat at
    // XRAY_LOCATION_ASSET (set by the pool launcher).
    root["dns"] = buildPoolDns();

    // Control-plane API (handler/routing/observatory services enabled).
    boost::json::object apiObj;
    apiObj["tag"] = "api";
    apiObj["listen"] = std::string("127.0.0.1:") + std::to_string(apiPort);
    boost::json::array apiServices;
    apiServices.push_back(boost::json::value("HandlerService"));
    apiServices.push_back(boost::json::value("RoutingService"));
    apiServices.push_back(boost::json::value("ObservatoryService"));
    apiObj["services"] = apiServices;
    root["api"] = apiObj;

    // Mixed socks inbound for the pool's client traffic.
    boost::json::object socksIn;
    socksIn["tag"] = "socks-in";
    socksIn["listen"] = "127.0.0.1";
    socksIn["port"] = socksPort;
    socksIn["protocol"] = "mixed";
    boost::json::object socksSettings;
    socksSettings["auth"] = "noauth";
    socksSettings["udp"] = true;
    socksSettings["allowTransparent"] = false;
    socksSettings["userLevel"] = 0;
    socksIn["settings"] = socksSettings;
    boost::json::object sniff;
    sniff["enabled"] = true;
    sniff["routeOnly"] = false;
    boost::json::array destOverride;
    destOverride.push_back(boost::json::value("http"));
    destOverride.push_back(boost::json::value("tls"));
    destOverride.push_back(boost::json::value("quic"));
    sniff["destOverride"] = destOverride;
    socksIn["sniffing"] = sniff;
    boost::json::array inbounds;
    inbounds.push_back(socksIn);
    root["inbounds"] = inbounds;

    boost::json::array outbounds;

    boost::json::object directOut;
    directOut["tag"] = "direct";
    directOut["protocol"] = "freedom";
    boost::json::object directSettings;
    directSettings["domainStrategy"] = "AsIs";
    directOut["settings"] = directSettings;
    outbounds.push_back(directOut);

    // Blackhole outbound targeted by the UDP/443 (QUIC) block routing rule below.
    boost::json::object blockOut;
    blockOut["tag"] = "block";
    blockOut["protocol"] = "blackhole";
    outbounds.push_back(blockOut);

    // NOTE: no "balancer-out" outbound is declared here. The balancer is
    // declared as routing.balancers (see below). Member outbounds
    // (px-<indexId>) are injected at runtime via the xray gRPC API; the
    // prefix selector ["px-"] matches them once present.

    root["outbounds"] = outbounds;

    // Observatory health discovery (prefix subjectSelector is supported).
    boost::json::object observatory;
    boost::json::array obsSelector;
    obsSelector.push_back(boost::json::value("px-"));
    observatory["subjectSelector"] = obsSelector;
    // Reuse the configured test url (config.json test.url) when present so the
    // pool's health probe matches the rest of the app; otherwise fall back to
    // the dedicated observatory.destination.
    std::string probeUrl = cfg.probeUrl.empty() ? cfg.observatory.destination : cfg.probeUrl;
    observatory["probeURL"] = probeUrl;
    observatory["probeInterval"] = std::to_string(cfg.observatory.intervalSec) + "s";
    root["observatory"] = observatory;

    boost::json::object routing;
    routing["domainStrategy"] = "AsIs";
    // Balancer declaration (xray 26.x): the legacy outbound protocol
    // "balancing" is no longer registered, so the balancer lives under
    // routing.balancers. Members are selected by the "px-" prefix.
    boost::json::array balancers;
    boost::json::object balancer;
    balancer["tag"] = "balancer-out";
    boost::json::array selector;
    selector.push_back(boost::json::value("px-"));
    balancer["selector"] = selector;
    boost::json::object strategy;
    strategy["type"] = cfg.balancerStrategy;
    balancer["strategy"] = strategy;
    // Wire an observatory directly to the balancer. xray 26.x only surfaces an
    // outbound via GetOutboundStatus when its observatory is attached to a
    // balancer; a bare top-level observatory is not reported. Without this the
    // pool's health probe returns zero entries and every member is frozen at
    // its injection state (active / -1 / 否 / failStreak=0).
    boost::json::object observation;
    boost::json::array obsSubject;
    obsSubject.push_back(boost::json::value("px-"));
    observation["subjectSelector"] = obsSubject;
    observation["probeURL"] = probeUrl;
    observation["probeInterval"] = std::to_string(cfg.observatory.intervalSec) + "s";
    balancer["observation"] = observation;
    balancers.push_back(balancer);
    routing["balancers"] = balancers;
    boost::json::array rules;

    // 1) Control-plane API traffic -> built-in "api" outbound (highest priority).
    {
        boost::json::object r;
        r["type"] = "field";
        boost::json::array inTag;
        inTag.push_back(boost::json::value("api"));
        r["inboundTag"] = inTag;
        r["outboundTag"] = "api";
        rules.push_back(r);
    }
    // 2) Block QUIC (udp/443) to prevent leaks (mirrors xray-config-template.json).
    {
        boost::json::object r;
        r["type"] = "field";
        r["network"] = "udp";
        r["port"] = "443";
        r["outboundTag"] = "block";
        rules.push_back(r);
    }
    // 3) Private network IPs -> direct.
    {
        boost::json::object r;
        r["type"] = "field";
        r["outboundTag"] = "direct";
        boost::json::array ip;
        ip.push_back(boost::json::value("geoip:private"));
        r["ip"] = ip;
        rules.push_back(r);
    }
    // 4) Private domains -> direct.
    {
        boost::json::object r;
        r["type"] = "field";
        r["outboundTag"] = "direct";
        boost::json::array dom;
        dom.push_back(boost::json::value("geosite:private"));
        r["domain"] = dom;
        rules.push_back(r);
    }
    // 4b) Explicit mainland DNS resolver IPs -> direct (mirrors template; literal
    // IPs, no geo dependency).
    {
        boost::json::object r;
        r["type"] = "field";
        r["outboundTag"] = "direct";
        boost::json::array ip;
        ip.push_back(boost::json::value("223.5.5.5"));
        ip.push_back(boost::json::value("223.6.6.6"));
        ip.push_back(boost::json::value("2400:3200::1"));
        ip.push_back(boost::json::value("2400:3200:baba::1"));
        ip.push_back(boost::json::value("119.29.29.29"));
        ip.push_back(boost::json::value("1.12.12.12"));
        ip.push_back(boost::json::value("120.53.53.53"));
        ip.push_back(boost::json::value("2402:4e00::"));
        ip.push_back(boost::json::value("2402:4e00:1::"));
        ip.push_back(boost::json::value("180.76.76.76"));
        ip.push_back(boost::json::value("2400:da00::6666"));
        ip.push_back(boost::json::value("114.114.114.114"));
        ip.push_back(boost::json::value("114.114.115.115"));
        ip.push_back(boost::json::value("114.114.114.119"));
        ip.push_back(boost::json::value("114.114.115.119"));
        ip.push_back(boost::json::value("114.114.114.110"));
        ip.push_back(boost::json::value("114.114.115.110"));
        ip.push_back(boost::json::value("180.184.1.1"));
        ip.push_back(boost::json::value("180.184.2.2"));
        ip.push_back(boost::json::value("101.226.4.6"));
        ip.push_back(boost::json::value("218.30.118.6"));
        ip.push_back(boost::json::value("123.125.81.6"));
        ip.push_back(boost::json::value("140.207.198.6"));
        ip.push_back(boost::json::value("1.2.4.8"));
        ip.push_back(boost::json::value("210.2.4.8"));
        ip.push_back(boost::json::value("52.80.66.66"));
        ip.push_back(boost::json::value("117.50.22.22"));
        ip.push_back(boost::json::value("2400:7fc0:849e:200::4"));
        ip.push_back(boost::json::value("2404:c2c0:85d8:901::4"));
        ip.push_back(boost::json::value("117.50.10.10"));
        ip.push_back(boost::json::value("52.80.52.52"));
        ip.push_back(boost::json::value("2400:7fc0:849e:200::8"));
        ip.push_back(boost::json::value("2404:c2c0:85d8:901::8"));
        ip.push_back(boost::json::value("117.50.60.30"));
        ip.push_back(boost::json::value("52.80.60.30"));
        r["ip"] = ip;
        rules.push_back(r);
    }
    // 4c) Domestic DNS provider domains -> direct (mirrors template).
    {
        boost::json::object r;
        r["type"] = "field";
        r["outboundTag"] = "direct";
        boost::json::array dom;
        dom.push_back(boost::json::value("domain:alidns.com"));
        dom.push_back(boost::json::value("domain:doh.pub"));
        dom.push_back(boost::json::value("domain:dot.pub"));
        dom.push_back(boost::json::value("domain:360.cn"));
        dom.push_back(boost::json::value("domain:onedns.net"));
        r["domain"] = dom;
        rules.push_back(r);
    }
    // 5) Mainland China IPs -> direct.
    {
        boost::json::object r;
        r["type"] = "field";
        r["outboundTag"] = "direct";
        boost::json::array ip;
        ip.push_back(boost::json::value("geoip:cn"));
        r["ip"] = ip;
        rules.push_back(r);
    }
    // 6) Mainland China domains -> direct.
    {
        boost::json::object r;
        r["type"] = "field";
        r["outboundTag"] = "direct";
        boost::json::array dom;
        dom.push_back(boost::json::value("geosite:cn"));
        r["domain"] = dom;
        rules.push_back(r);
    }
    // 7) Catch-all: client traffic from socks-in -> balancer (overseas via px-*).
    {
        boost::json::object r;
        r["type"] = "field";
        boost::json::array inTag;
        inTag.push_back(boost::json::value("socks-in"));
        r["inboundTag"] = inTag;
        r["balancerTag"] = "balancer-out";
        rules.push_back(r);
    }
    routing["rules"] = rules;
    root["routing"] = routing;

    return boost::json::serialize(root);
}

} // namespace config
