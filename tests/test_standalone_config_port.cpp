#include "StandaloneConfigPort.h"

#include <boost/json.hpp>
#include <gtest/gtest.h>

namespace json = boost::json;

TEST(StandaloneConfigPortTest, FindSocksInboundLocatesSocksRegardlessOfOrder) {
    // xray template: api (dokodemo-door) first, socks second.
    json::value cfg = json::parse(R"({
        "inbounds": [
            {"protocol":"dokodemo-door","tag":"api","port":62826,"address":"127.0.0.1"},
            {"protocol":"socks","tag":"socks","port":10808}
        ]
    })");
    json::array& inbounds = cfg.as_object()["inbounds"].as_array();
    EXPECT_EQ(standalone_config::findSocksInboundIndex(inbounds), 1);

    // "tag":"socks" wins even when protocol differs from plain socks.
    json::value cfg2 = json::parse(R"({
        "inbounds": [
            {"protocol":"dokodemo-door","tag":"api","port":62826},
            {"protocol":"mixed","tag":"socks","port":10808}
        ]
    })");
    json::array& in2 = cfg2.as_object()["inbounds"].as_array();
    EXPECT_EQ(standalone_config::findSocksInboundIndex(in2), 1);

    // sing-box: type "mixed" identifies the SOCKS listener.
    json::value cfg3 = json::parse(R"({
        "inbounds": [
            {"type":"direct","tag":"direct"},
            {"type":"mixed","tag":"socks-in","listen_port":10808}
        ]
    })");
    json::array& in3 = cfg3.as_object()["inbounds"].as_array();
    EXPECT_EQ(standalone_config::findSocksInboundIndex(in3), 1);
}

TEST(StandaloneConfigPortTest, ApplySocksPortRewritesSocksNotInbounds0) {
    json::value cfg = json::parse(R"({
        "inbounds": [
            {"protocol":"dokodemo-door","tag":"api","port":62826,"address":"127.0.0.1"},
            {"protocol":"socks","tag":"socks","port":10808}
        ]
    })");
    standalone_config::applySocksPort(cfg.as_object(), 12345);
    json::array& inbounds = cfg.as_object()["inbounds"].as_array();
    // api inbound untouched
    EXPECT_EQ(inbounds[0].as_object()["port"].as_int64(), 62826);
    // socks inbound got the newly allocated port
    EXPECT_EQ(inbounds[1].as_object()["port"].as_int64(), 12345);
}

TEST(StandaloneConfigPortTest, ApplySocksPortHandlesSingboxListenPort) {
    json::value cfg = json::parse(R"({
        "inbounds": [
            {"type":"socks","tag":"socks-in","listen":"127.0.0.1","listen_port":10808}
        ]
    })");
    standalone_config::applySocksPort(cfg.as_object(), 9876);
    json::array& inbounds = cfg.as_object()["inbounds"].as_array();
    EXPECT_EQ(inbounds[0].as_object()["listen_port"].as_int64(), 9876);
}

TEST(StandaloneConfigPortTest, ApplySocksPortFallsBackToInbounds0) {
    json::value cfg = json::parse(R"({
        "inbounds": [ {"protocol":"vmess","port":9000} ]
    })");
    standalone_config::applySocksPort(cfg.as_object(), 5000);
    json::array& inbounds = cfg.as_object()["inbounds"].as_array();
    EXPECT_EQ(inbounds[0].as_object()["port"].as_int64(), 5000);
}
