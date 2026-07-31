#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>

// The test fixture class XrayApiDirectTest is declared as friend in XrayApi.h,
// granting access to private members inside the #ifdef USE_GRPC_API block.
#ifdef USE_GRPC_API
#include "XrayApi.h"
#include <boost/json.hpp>

using namespace xray;

// Fixture class — declared as friend in XrayApi.h, so it can access private
// members. Subclasses (created by TEST_F) access private members through
// this class's protected helpers.
class XrayApiDirectTest : public ::testing::Test {
protected:
    // Static helpers: called from TEST_F subclasses; XrayApiDirectTest is a
    // friend so these can access XrayApi private members.
    static std::string encodeVarint(uint64_t value) {
        return XrayApi::encodeVarint(value);
    }
    static std::string encodeLengthDelimited(int fieldNumber,
                                              const std::string& data) {
        return XrayApi::encodeLengthDelimited(fieldNumber, data);
    }
    static std::string encodeString(int fieldNumber, const std::string& str) {
        return XrayApi::encodeString(fieldNumber, str);
    }
    static std::string encodeVarintField(int fieldNumber, uint64_t value) {
        return XrayApi::encodeVarintField(fieldNumber, value);
    }
    static std::string jsonConfigToProtobuf(const std::string& typeUrl,
                                            const std::string& valueJson) {
        return XrayApi::jsonConfigToProtobuf(typeUrl, valueJson);
    }
    bool parseServerAddr(const std::string& addr, std::string& host, int& port) {
        xray::XrayApi tempApi("xray.exe", addr);
        return tempApi.parseServerAddr(host, port);
    }
    static bool parseOutboundJson(const std::string& json,
                                  std::string& tagOut,
                                  std::string& typeUrl,
                                  std::string& valueJson) {
        return XrayApi::parseOutboundJson(json, tagOut, typeUrl, valueJson);
    }

    static std::string encodeStreamConfig(const boost::json::object& stream) {
        return XrayApi::encodeStreamConfig(stream);
    }

    static std::string encodeMultiplexConfig(const boost::json::object& mux) {
        return XrayApi::encodeMultiplexConfig(mux);
    }

    static std::string encodeSenderSettings(const boost::json::object* ss,
                                            const boost::json::object* mux) {
        return XrayApi::encodeSenderSettings(ss, mux);
    }
};

// ============================================================
// encodeVarint — protobuf varint encoding (static, accessed via XrayApi::)
// ============================================================
TEST_F(XrayApiDirectTest, EncodeVarintZero) {
    auto result = encodeVarint(0);
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x00);
}

TEST_F(XrayApiDirectTest, EncodeVarintOneByte) {
    auto result = encodeVarint(1);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x01);
}

TEST_F(XrayApiDirectTest, EncodeVarint127) {
    auto result = encodeVarint(127);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x7F);
}

TEST_F(XrayApiDirectTest, EncodeVarint128) {
    // 128 = 0x80 → needs two bytes: 10000000 00000001
    auto result = encodeVarint(128);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x80);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x01);
}

TEST_F(XrayApiDirectTest, EncodeVarint300) {
    // 300 = 0x12C → varint: 10101100 00000010
    auto result = encodeVarint(300);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0xAC);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x02);
}

TEST_F(XrayApiDirectTest, EncodeVarintMaxOneByte) {
    auto result = encodeVarint(255);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x01);
}

TEST_F(XrayApiDirectTest, EncodeVarintLarge) {
    // 16383 = 0x3FFF → varint: 11111111 01111111 (two bytes, both with msb)
    auto result = encodeVarint(16383);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x7F);
}

TEST_F(XrayApiDirectTest, EncodeVarint64bit) {
    // 0xFFFFFFFF = 4294967295 → 5 bytes in varint
    auto result = encodeVarint(0xFFFFFFFFull);
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(result[2]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(result[3]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(result[4]), 0x0F);
}

// ============================================================
// encodeVarintField — protobuf field tag + varint value (wire type 0)
// ============================================================
TEST_F(XrayApiDirectTest, EncodeVarintField1Zero) {
    // field 1, wire-type 0 → key = (1<<3)|0 = 0x08, value 0 = 0x00
    auto result = encodeVarintField(1, 0);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x08);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x00);
}

TEST_F(XrayApiDirectTest, EncodeVarintField1One) {
    // field 1, value 1 → 0x08 0x01
    auto result = encodeVarintField(1, 1);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x08);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x01);
}

TEST_F(XrayApiDirectTest, EncodeVarintField4Val128) {
    // field 4, value 128 → key=(4<<3)|0=0x20, varint(0x80)=0x80 0x01
    auto result = encodeVarintField(4, 128);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x20);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x80);
    EXPECT_EQ(static_cast<unsigned char>(result[2]), 0x01);
}

TEST_F(XrayApiDirectTest, EncodeVarintField15Max) {
    // field 15, value 0xFFFFFFFF → key=(15<<3)|0=0x78, 5-byte varint
    auto result = encodeVarintField(15, 0xFFFFFFFFull);
    ASSERT_EQ(result.size(), 6u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x78);
    // 5-byte varint for 0xFFFFFFFF: 0xFF 0xFF 0xFF 0xFF 0x0F
    EXPECT_EQ(result.substr(1), std::string({'\xFF','\xFF','\xFF','\xFF','\x0F'}));
}

// ============================================================
// encodeLengthDelimited — protobuf field with length prefix
// ============================================================
TEST_F(XrayApiDirectTest, EncodeLengthDelimitedField1Empty) {
    // field 1, type 2 (length-delimited), empty data
    // wire = (1 << 3) | 2 = 0x0A, varint(0) = 0x00
    auto result = encodeLengthDelimited(1, "");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x0A);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x00);
}

TEST_F(XrayApiDirectTest, EncodeLengthDelimitedField2Hello) {
    // field 2, type 2 (length-delimited), "hello"
    // wire = (2 << 3) | 2 = 0x12, varint(5) = 0x05, then "hello"
    auto result = encodeLengthDelimited(2, "hello");
    ASSERT_EQ(result.size(), 7u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x12);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x05);
    EXPECT_EQ(result.substr(2), "hello");
}

TEST_F(XrayApiDirectTest, EncodeLengthDelimitedField15) {
    // field 15, type 2 → wire = (15 << 3) | 2 = 0x7A
    auto result = encodeLengthDelimited(15, "x");
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x7A);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x01);
    EXPECT_EQ(result.substr(2), "x");
}

TEST_F(XrayApiDirectTest, EncodeLengthDelimitedField16) {
    // field 16, type 2 → key = (16 << 3) | 2 = 130
    // varint(130) = 0x82 0x01 (two bytes: 0x82|continuation, 0x01|final)
    auto result = encodeLengthDelimited(16, "ab");
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x82);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x01);
    EXPECT_EQ(static_cast<unsigned char>(result[2]), 0x02);
    EXPECT_EQ(result.substr(3), "ab");
}

// ============================================================
// encodeString — protobuf string field shorthand (field 1, UTF-8)
// ============================================================
TEST_F(XrayApiDirectTest, EncodeStringField1) {
    // field 1, "test" → tag 0x0A, len 4, "test"
    auto result = encodeString(1, "test");
    ASSERT_EQ(result.size(), 6u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x0A);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x04);
    EXPECT_EQ(result.substr(2), "test");
}

// ============================================================
// jsonConfigToProtobuf — convert JSON settings to protobuf binary
// ============================================================
TEST_F(XrayApiDirectTest, JsonConfigToProtobufFreedomEmpty) {
    // Empty/freedom with no settings → empty protobuf (all defaults)
    auto result = jsonConfigToProtobuf("xray.proxy.freedom.Config", "{}");
    EXPECT_TRUE(result.empty());
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufFreedomDomainStrategy) {
    // freedom with domain_strategy=6 (FORCE_IP)
    // protobuf: field1 varint = 6 → 0x08 0x06
    auto result = jsonConfigToProtobuf("xray.proxy.freedom.Config",
        "{\"settings\":{\"domain_strategy\":6}}");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x08);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x06);
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufFreedomUserLevel) {
    // freedom with user_level=1 → field4 varint = 1 → 0x20 0x01
    auto result = jsonConfigToProtobuf("xray.proxy.freedom.Config",
        "{\"settings\":{\"user_level\":1}}");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x20);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x01);
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufFreedomAllFields) {
    // freedom with all 3 known fields
    // domain_strategy=6(FORCE_IP), user_level=1, proxy_protocol=2
    // → field1: 0x08 0x06, field4: 0x20 0x01, field6: 0x30 0x02
    auto result = jsonConfigToProtobuf("xray.proxy.freedom.Config",
        "{\"settings\":{\"domain_strategy\":6,\"user_level\":1,\"proxy_protocol\":2}}");
    ASSERT_EQ(result.size(), 6u);
    EXPECT_EQ(result.substr(0, 2), std::string({'\x08','\x06'}));
    EXPECT_EQ(result.substr(2, 2), std::string({'\x20','\x01'}));
    EXPECT_EQ(result.substr(4, 2), std::string({'\x30','\x02'}));
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufBlackholeEmpty) {
    // blackhole with no settings → empty protobuf (no response = none)
    auto result = jsonConfigToProtobuf("xray.proxy.blackhole.Config", "{}");
    EXPECT_TRUE(result.empty());
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufUnknownType) {
    // Unknown type → returns empty (caller falls back to raw JSON)
    auto result = jsonConfigToProtobuf("xray.proxy.vmess.Config",
        "{\"settings\":{\"vnext\":[]}}");
    EXPECT_TRUE(result.empty());
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufEmptyJson) {
    // Empty input string → empty output
    auto result = jsonConfigToProtobuf("xray.proxy.freedom.Config", "");
    EXPECT_TRUE(result.empty());
}

// ============================================================
// Outbound protobuf encoding — verify addOutboundDirect wire format
// ============================================================
TEST_F(XrayApiDirectTest, EncodeTypedMessage) {
    // TypedMessage { string type = 1; bytes value = 2; }
    // type=field1: tag=(1<<3)|2=0x0A, len=4, "test"
    // value=field2: tag=(2<<3)|2=0x12, len=5, "hello"
    std::string msg;
    msg += encodeString(1, "test");   // type
    msg += encodeString(2, "hello");  // value

    // Expected: 0x0A 0x04 "test" 0x12 0x05 "hello"
    ASSERT_EQ(msg.size(), 13u);
    EXPECT_EQ(static_cast<unsigned char>(msg[0]), 0x0A);  // field 1, wire type 2
    EXPECT_EQ(static_cast<unsigned char>(msg[1]), 0x04);  // length 4
    EXPECT_EQ(msg.substr(2, 4), "test");
    EXPECT_EQ(static_cast<unsigned char>(msg[6]), 0x12);  // field 2, wire type 2
    EXPECT_EQ(static_cast<unsigned char>(msg[7]), 0x05);  // length 5
    EXPECT_EQ(msg.substr(8, 5), "hello");
}

TEST_F(XrayApiDirectTest, EncodeOutboundHandlerTagOnly) {
    // OutboundHandlerOutbound { string tag = 1; }
    // tag=field1: tag=(1<<3)|2=0x0A, len=3, "tag"
    std::string handler;
    handler += encodeString(1, "tag");

    ASSERT_EQ(handler.size(), 5u);
    EXPECT_EQ(static_cast<unsigned char>(handler[0]), 0x0A);  // field 1, wire type 2
    EXPECT_EQ(static_cast<unsigned char>(handler[1]), 0x03);  // length 3
    EXPECT_EQ(handler.substr(2), "tag");
}

TEST_F(XrayApiDirectTest, EncodeOutboundWithProxySettings) {
    // OutboundHandlerOutbound { string tag = 1; TypedMessage proxy_settings = 3; }
    // proxy_settings=field3: tag=(3<<3)|2=0x1A
    // Build a TypedMessage { string type = 1("proxy.Config"); bytes value = 2(protobuf-bin); }
    // value is empty protobuf binary (all defaults) — no longer a JSON string
    std::string typedMsg;
    typedMsg += encodeString(1, "xray.proxy.freedom.Config");
    typedMsg += encodeString(2, "");

    std::string handler;
    handler += encodeString(1, "my-tag");
    handler += encodeLengthDelimited(3, typedMsg);

    // Verify outer tag=1 field
    // tag=1: 0x0A, len=6, "my-tag"
    ASSERT_GE(handler.size(), 8u);
    EXPECT_EQ(static_cast<unsigned char>(handler[0]), 0x0A);  // field 1 tag
    EXPECT_EQ(static_cast<unsigned char>(handler[1]), 0x06);  // length 6
    EXPECT_EQ(handler.substr(2, 6), "my-tag");

    // Verify proxy_settings=field3
    // field 3 tag byte: (3<<3)|2 = 0x1A
    EXPECT_EQ(static_cast<unsigned char>(handler[8]), 0x1A);

    // The length of proxy_settings should equal typedMsg size
    ASSERT_GE(handler.size(), 10u);  // at least tag(2) + typedMsg
    size_t expectedLen = typedMsg.size();
    EXPECT_EQ(static_cast<unsigned char>(handler[9]), expectedLen);

    // Verify nested TypedMessage content
    std::string actualTypedMsg = handler.substr(10, expectedLen);
    EXPECT_EQ(actualTypedMsg, typedMsg);
}

TEST_F(XrayApiDirectTest, EncodeAddOutboundRequest) {
    // Full AddOutboundRequest { OutboundHandlerOutbound outbound = 1; }
    // outbound=field1: tag=(1<<3)|2=0x0A, length-delimited with nested handler
    // value is empty protobuf binary (default blackhole = no response)
    std::string typedMsg;
    typedMsg += encodeString(1, "xray.proxy.blackhole.Config");
    typedMsg += encodeString(2, "");

    std::string handler;
    handler += encodeString(1, "out-tag");
    handler += encodeLengthDelimited(3, typedMsg);

    std::string request = encodeLengthDelimited(1, handler);

    // Verify top-level tag=0x0A, then nested content
    ASSERT_GE(request.size(), 4u);
    EXPECT_EQ(static_cast<unsigned char>(request[0]), 0x0A);  // field 1, wire type 2
    // Length of handler
    EXPECT_EQ(static_cast<unsigned char>(request[1]), handler.size());
    // Verify handler is nested verbatim
    EXPECT_EQ(request.substr(2), handler);
}

// ============================================================
// parseServerAddr — parse "tcp://host:port" and "host:port"
// ============================================================
TEST_F(XrayApiDirectTest, ParseServerAddrTcpPrefix) {
    std::string host;
    int port = 0;
    EXPECT_TRUE(parseServerAddr("tcp://127.0.0.1:10080", host, port));
    EXPECT_EQ(host, "127.0.0.1");
    EXPECT_EQ(port, 10080);
}

TEST_F(XrayApiDirectTest, ParseServerAddrPlain) {
    std::string host;
    int port = 0;
    EXPECT_TRUE(parseServerAddr("localhost:8080", host, port));
    EXPECT_EQ(host, "localhost");
    EXPECT_EQ(port, 8080);
}

TEST_F(XrayApiDirectTest, ParseServerAddrIpv6) {
    std::string host;
    int port = 0;
    EXPECT_TRUE(parseServerAddr("tcp://[::1]:10080", host, port));
    EXPECT_EQ(host, "[::1]");
    EXPECT_EQ(port, 10080);
}

TEST_F(XrayApiDirectTest, ParseServerAddrNoPort) {
    std::string host;
    int port = 0;
    EXPECT_FALSE(parseServerAddr("tcp://127.0.0.1", host, port));
}

TEST_F(XrayApiDirectTest, ParseServerAddrEmpty) {
    std::string host;
    int port = 0;
    EXPECT_FALSE(parseServerAddr("", host, port));
}

TEST_F(XrayApiDirectTest, ParseServerAddrBadPort) {
    std::string host;
    int port = 0;
    EXPECT_FALSE(parseServerAddr("tcp://host:abc", host, port));
}

// ============================================================
// parseOutboundJson — parse ConfigGenerator JSON format
// ============================================================
TEST_F(XrayApiDirectTest, ParseOutboundJsonFreedom) {
    std::string json = R"({"outbounds":[{"tag":"direct-out","protocol":"freedom","settings":{}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(tagOut, "direct-out");
    EXPECT_EQ(typeUrl, "xray.proxy.freedom.Config");
    // valueJson must NOT contain the tag (it was removed)
    EXPECT_TRUE(valueJson.find("direct-out") == std::string::npos)
        << "valueJson should not contain tag: " << valueJson;
    // valueJson must still contain protocol and settings
    EXPECT_TRUE(valueJson.find("freedom") != std::string::npos)
        << "valueJson missing protocol: " << valueJson;
    EXPECT_TRUE(valueJson.find("settings") != std::string::npos)
        << "valueJson missing settings: " << valueJson;
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonBlackhole) {
    std::string json = R"({"outbounds":[{"tag":"block-out","protocol":"blackhole","settings":{"response":{"type":"none"}}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(tagOut, "block-out");
    EXPECT_EQ(typeUrl, "xray.proxy.blackhole.Config");
    EXPECT_TRUE(valueJson.find("tag") == std::string::npos);
    EXPECT_TRUE(valueJson.find("none") != std::string::npos);
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonVmess) {
    std::string json = R"({"outbounds":[{"tag":"proxy-vmess","protocol":"vmess","settings":{"vnext":[{"address":"example.com","port":443,"users":[{"id":"uuid-here"}]}]}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(tagOut, "proxy-vmess");
    EXPECT_EQ(typeUrl, "xray.proxy.vmess.outbound.Config");
    EXPECT_TRUE(valueJson.find("uuid-here") != std::string::npos);
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonSocks) {
    std::string json = R"({"outbounds":[{"tag":"socks-proxy","protocol":"socks","settings":{"servers":[{"address":"127.0.0.1","port":1080}]}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(tagOut, "socks-proxy");
    EXPECT_EQ(typeUrl, "xray.proxy.socks.ClientConfig");
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonHttp) {
    std::string json = R"({"outbounds":[{"tag":"http-out","protocol":"http","settings":{"accounts":[]}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(tagOut, "http-out");
    EXPECT_EQ(typeUrl, "xray.proxy.http.ClientConfig");
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonUnknownProtocol) {
    // Unknown protocol -> maps to xray.proxy.outbound.Config
    std::string json = R"({"outbounds":[{"tag":"custom","protocol":"myproto","settings":{}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(tagOut, "custom");
    EXPECT_EQ(typeUrl, "xray.proxy.outbound.Config");
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonInvalidJson) {
    std::string tagOut, typeUrl, valueJson;
    EXPECT_FALSE(parseOutboundJson("not-json", tagOut, typeUrl, valueJson));
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonMissingOutbounds) {
    // Valid JSON but missing "outbounds" key
    std::string tagOut, typeUrl, valueJson;
    EXPECT_FALSE(parseOutboundJson("{}", tagOut, typeUrl, valueJson));
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonEmptyOutbounds) {
    // "outbounds" is empty array
    std::string tagOut, typeUrl, valueJson;
    EXPECT_FALSE(parseOutboundJson(R"({"outbounds":[]})", tagOut, typeUrl, valueJson));
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonNullTag) {
    // Missing "tag" key -> boost::json::system_error
    std::string json = R"({"outbounds":[{"protocol":"freedom"}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_FALSE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
}

// ---------------------------------------------------------------------------
// Stream/Multiplex/Sender config encoders (SenderConfig for AddOutbound)
// ---------------------------------------------------------------------------

TEST_F(XrayApiDirectTest, EncodeMultiplexConfigEnabled) {
    // MultiplexingConfig: enabled=1 (bool) -> varint field 1.
    boost::json::object mux;
    mux["enabled"] = true;
    std::string expected;
    expected += encodeVarintField(1, 1);  // 0x08 0x01
    std::string result = encodeMultiplexConfig(mux);
    EXPECT_EQ(result, expected);

    mux["enabled"] = false;
    expected = encodeVarintField(1, 0);  // 0x08 0x00
    result = encodeMultiplexConfig(mux);
    EXPECT_EQ(result, expected);
}

TEST_F(XrayApiDirectTest, EncodeMultiplexConfigConcurrency) {
    // MultiplexingConfig: concurrency=2 (int32) -> varint field 2.
    boost::json::object mux;
    mux["concurrency"] = 4;
    std::string expected;
    expected += encodeVarintField(2, 4);  // 0x10 0x04
    std::string result = encodeMultiplexConfig(mux);
    EXPECT_EQ(result, expected);

    // Negative concurrency (-1 = auto) is omitted entirely.
    boost::json::object negMux;
    negMux["concurrency"] = -1;
    result = encodeMultiplexConfig(negMux);
    EXPECT_TRUE(result.empty());
}

TEST_F(XrayApiDirectTest, EncodeStreamConfigProtocolName) {
    // StreamConfig: protocol_name=5 (string) -> "websocket" for ws.
    boost::json::object stream;
    stream["network"] = "ws";
    std::string expected;
    expected += encodeString(5, "websocket");  // 0x2A 0x09 websocket
    std::string result = encodeStreamConfig(stream);
    EXPECT_EQ(result, expected);

    // tcp maps to itself.
    stream["network"] = "tcp";
    expected = encodeString(5, "tcp");  // 0x2A 0x03 tcp
    result = encodeStreamConfig(stream);
    EXPECT_EQ(result, expected);
}

TEST_F(XrayApiDirectTest, EncodeStreamConfigWsTransport) {
    // StreamConfig with websocket transport:
    //   protocol_name=5 "websocket"
    //   transport_settings=2 -> TransportConfig{
    //     settings(2)=TypedMessage{type_url(1)=websocket.Config,
    //                              settings(2)=WebSocketConfig},
    //     protocol_name(3)="websocket" }
    //   WebSocketConfig: host=1, path=2.
    boost::json::object wsSettings;
    wsSettings["host"] = "example.com";
    wsSettings["path"] = "/ws";
    boost::json::object stream;
    stream["network"] = "ws";
    stream["wsSettings"] = wsSettings;

    std::string wsConfig;
    wsConfig += encodeString(1, "example.com");
    wsConfig += encodeString(2, "/ws");
    std::string transportTyped;
    transportTyped += encodeString(1, "xray.transport.internet.websocket.Config");
    transportTyped += encodeString(2, wsConfig);
    std::string transportConfig;
    transportConfig += encodeLengthDelimited(2, transportTyped);
    transportConfig += encodeString(3, "websocket");

    std::string expected;
    expected += encodeString(5, "websocket");
    expected += encodeLengthDelimited(2, transportConfig);

    std::string result = encodeStreamConfig(stream);
    EXPECT_EQ(result, expected);
}

TEST_F(XrayApiDirectTest, EncodeStreamConfigTlsSecurity) {
    // StreamConfig with TLS security:
    //   protocol_name=5 "tcp"
    //   security_type=3 "tls"
    //   security_settings=4 -> TypedMessage{type_url(1)=tls.Config,
    //                                        settings(2)=Config}
    //   tls.Config: allow_insecure=1 (bool), server_name=3.
    boost::json::object tlsSettings;
    tlsSettings["serverName"] = "example.com";
    tlsSettings["allowInsecure"] = true;
    boost::json::object stream;
    stream["network"] = "tcp";
    stream["security"] = "tls";
    stream["tlsSettings"] = tlsSettings;

    std::string tlsConfig;
    tlsConfig += encodeVarintField(1, 1);
    tlsConfig += encodeString(3, "example.com");
    std::string secTyped;
    secTyped += encodeString(1, "xray.transport.internet.tls.Config");
    secTyped += encodeString(2, tlsConfig);

    std::string expected;
    expected += encodeString(5, "tcp");
    expected += encodeString(3, "tls");
    expected += encodeLengthDelimited(4, secTyped);

    std::string result = encodeStreamConfig(stream);
    EXPECT_EQ(result, expected);
}

TEST_F(XrayApiDirectTest, EncodeStreamConfigPort) {
    // StreamConfig: port=9 (uint32) -> varint field 9.
    boost::json::object stream;
    stream["network"] = "tcp";
    stream["port"] = 443;

    std::string expected;
    expected += encodeString(5, "tcp");
    expected += encodeVarintField(9, 443);  // 0x48 0xBB 0x03

    std::string result = encodeStreamConfig(stream);
    EXPECT_EQ(result, expected);
}

TEST_F(XrayApiDirectTest, EncodeSenderSettingsStreamOnly) {
    // SenderConfig: stream_settings=2 -> encodeStreamConfig result.
    boost::json::object ss;
    ss["network"] = "tcp";
    std::string expected;
    expected += encodeLengthDelimited(2, encodeStreamConfig(ss));
    std::string result = encodeSenderSettings(&ss, nullptr);
    EXPECT_EQ(result, expected);
}

TEST_F(XrayApiDirectTest, EncodeSenderSettingsMuxOnly) {
    // SenderConfig: multiplex_settings=4 -> encodeMultiplexConfig result.
    boost::json::object mux;
    mux["enabled"] = true;
    mux["concurrency"] = 4;
    std::string expected;
    expected += encodeLengthDelimited(4, encodeMultiplexConfig(mux));
    std::string result = encodeSenderSettings(nullptr, &mux);
    EXPECT_EQ(result, expected);
}

TEST_F(XrayApiDirectTest, EncodeSenderSettingsBoth) {
    // SenderConfig: stream_settings=2 then multiplex_settings=4.
    boost::json::object ss;
    ss["network"] = "tcp";
    boost::json::object mux;
    mux["enabled"] = true;
    mux["concurrency"] = 4;
    std::string expected;
    expected += encodeLengthDelimited(2, encodeStreamConfig(ss));
    expected += encodeLengthDelimited(4, encodeMultiplexConfig(mux));
    std::string result = encodeSenderSettings(&ss, &mux);
    EXPECT_EQ(result, expected);
}

TEST_F(XrayApiDirectTest, EncodeSenderSettingsNeither) {
    // Both pointers null -> empty SenderConfig.
    std::string result = encodeSenderSettings(nullptr, nullptr);
    EXPECT_TRUE(result.empty());
}

#else
// When USE_GRPC_API is OFF, we still need a placeholder test so ctest passes.
#include <gtest/gtest.h>

struct XrayApiDirectPlaceholder : public ::testing::Test {};

TEST_F(XrayApiDirectPlaceholder, PlaceholderSkip) {
    GTEST_SKIP() << "Skipped: USE_GRPC_API not enabled";
}
#endif
