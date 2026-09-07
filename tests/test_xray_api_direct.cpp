// Winsock2 must be included before <windows.h> (which gtest may pull in
// transitively on Windows) to avoid the winsock.h / winsock2.h clash.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>

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
    // Lifecycle helpers — friend access to XrayApi private gRPC members.
    int callGrpcConnect(xray::XrayApi& api, const std::string& host, int port) {
        return api.grpcConnect(host, port);
    }
    void callGrpcClose(xray::XrayApi& api, int sock) {
        api.grpcClose(sock);
    }
    int getConsecutiveFailures(const xray::XrayApi& api) const {
        return api.consecutiveConnectFailures_;
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

    static std::string encodeHpack(
        const std::vector<std::pair<std::string, std::string>>& headers) {
        return XrayApi::encodeHpack(headers);
    }
    static bool decodeHuffman(const std::string& data, std::string& out) {
        return XrayApi::decodeHuffman(data, out);
    }
    static bool grpcDecodeHpackHeaders(
        const std::string& block,
        std::vector<std::pair<std::string, std::string>>& headers) {
        return XrayApi::grpcDecodeHpackHeaders(block, headers);
    }
    static std::string encodeJsonConfigToProtobuf(const std::string& typeUrl,
                                                   const std::string& valueJson) {
        return XrayApi::jsonConfigToProtobuf(typeUrl, valueJson);
    }
    static std::string encodeTLSSettings(const boost::json::object& tls) {
        return XrayApi::encodeTLSSettings(tls);
    }
    static std::string base64Decode(const std::string& input) {
        return XrayApi::base64Decode(input);
    }
    static std::string hexDecode(const std::string& input) {
        return XrayApi::hexDecode(input);
    }
    static std::string encodeRealitySettings(const boost::json::object& reality) {
        return XrayApi::encodeRealitySettings(reality);
    }
    static std::string encodePackedInt64Field(int fieldNumber,
                                              const int64_t* values,
                                              int count) {
        return XrayApi::encodePackedInt64Field(fieldNumber, values, count);
    }
    static void parseSpiderYParams(const std::string& spiderX, int64_t* out) {
        XrayApi::parseSpiderYParams(spiderX, out);
    }
    // Decode varint-delimited fields from a protobuf message into a map<fieldNum, value>.
    // For varint fields: value is the decoded uint64.
    // For length-delimited fields: value is the length of the embedded data (used
    // to distinguish field presence).
    static std::map<int, uint64_t> decodeVarintFields(const std::string& data) {
        std::map<int, uint64_t> result;
        size_t pos = 0;
        while (pos < data.size()) {
        uint64_t tag = 0;
        int tagShift = 0;
        uint8_t byte;
        do {
            if (pos >= data.size()) return result;
            byte = static_cast<uint8_t>(data[pos++]);
            tag |= static_cast<uint64_t>(byte & 0x7F) << tagShift;
            tagShift += 7;
        } while (byte & 0x80);
        uint32_t fieldNum = static_cast<uint32_t>(tag >> 3);
        uint32_t wireType = tag & 0x7;
        if (wireType == 0) { // varint
            uint64_t value = 0;
            int shift = 0;
            do {
                if (pos >= data.size()) break;
                byte = static_cast<uint8_t>(data[pos++]);
                value |= static_cast<uint64_t>(byte & 0x7F) << shift;
                shift += 7;
            } while (byte & 0x80);
            result[fieldNum] = value;
            } else if (wireType == 2) { // length-delimited
                uint64_t len = 0;
                do {
                    if (pos >= data.size()) break;
                    byte = static_cast<uint8_t>(data[pos++]);
                    len = (len << 7) | (byte & 0x7F);
                } while (byte & 0x80);
                pos += len; // skip past the length-delimited data
                result[fieldNum] = static_cast<uint64_t>(len);
            } else {
                // Unknown wire type: skip one byte and hope for the best.
                pos++;
            }
        }
        return result;
    }
    // Decode string-delimited fields (fieldNum -> string value).
    static std::map<int, std::string> decodeStringFields(const std::string& data) {
        std::map<int, std::string> result;
        size_t pos = 0;
        while (pos < data.size()) {
            uint64_t tag = 0;
            uint8_t byte;
            int shift = 0;
            do {
                if (pos >= data.size()) return result;
                byte = static_cast<uint8_t>(data[pos++]);
                tag |= static_cast<uint64_t>(byte & 0x7F) << shift;
                shift += 7;
            } while (byte & 0x80);
            uint32_t fieldNum = static_cast<uint32_t>(tag >> 3);
            uint32_t wireType = tag & 0x7;
            if (wireType == 2) { // length-delimited (string)
                uint64_t len = 0;
                do {
                    if (pos >= data.size()) break;
                    byte = static_cast<uint8_t>(data[pos++]);
                    len = (len << 7) | (byte & 0x7F);
                } while (byte & 0x80);
                if (pos + len <= data.size()) {
                    result[fieldNum] = data.substr(pos, len);
                    pos += len;
                }
            } else {
                // Non-string wire type — skip.
                if (wireType == 0) { // varint
                    do {
                        if (pos >= data.size()) break;
                        byte = static_cast<uint8_t>(data[pos++]);
                    } while (byte & 0x80);
                } else {
                    pos++;
                }
            }
        }
        return result;
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
    EXPECT_EQ(host, "::1");   // brackets stripped
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

TEST_F(XrayApiDirectTest, ParseServerAddrTrailingGarbage) {
    std::string host;
    int port = 0;
    EXPECT_FALSE(parseServerAddr("tcp://host:8080abc", host, port));
}

TEST_F(XrayApiDirectTest, ParseServerAddrPortZero) {
    std::string host;
    int port = 0;
    EXPECT_FALSE(parseServerAddr("tcp://host:0", host, port));
}

TEST_F(XrayApiDirectTest, ParseServerAddrPortTooLarge) {
    std::string host;
    int port = 0;
    EXPECT_FALSE(parseServerAddr("tcp://host:70000", host, port));
}

TEST_F(XrayApiDirectTest, ParseServerAddrIpv6Success) {
    std::string host;
    int port = 0;
    EXPECT_TRUE(parseServerAddr("tcp://[::1]:443", host, port));
    EXPECT_EQ(host, "::1");
    EXPECT_EQ(port, 443);
}

TEST_F(XrayApiDirectTest, ParseServerAddrHostname) {
    std::string host;
    int port = 0;
    EXPECT_TRUE(parseServerAddr("localhost:8443", host, port));
    EXPECT_EQ(host, "localhost");
    EXPECT_EQ(port, 8443);
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

TEST_F(XrayApiDirectTest, ParseOutboundJsonShadowsocks2022) {
    // SS2022 ciphers must map to xray.proxy.shadowsocks_2022.ClientConfig.
    std::string json = R"({"outbounds":[{"tag":"ss2022","protocol":"shadowsocks","settings":{"servers":[{"address":"1.2.3.4","port":8388,"method":"2022-blake3-aes-128-gcm","password":"k3y-value"}]}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(tagOut, "ss2022");
    EXPECT_EQ(typeUrl, "xray.proxy.shadowsocks_2022.ClientConfig");
    EXPECT_TRUE(valueJson.find("2022-blake3") != std::string::npos);
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonShadowsocksLegacy) {
    // Legacy (non-2022) cipher keeps the legacy typeUrl.
    std::string json = R"({"outbounds":[{"tag":"ss-legacy","protocol":"shadowsocks","settings":{"servers":[{"address":"1.2.3.4","port":8388,"method":"aes-256-gcm","password":"pw"}]}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(tagOut, "ss-legacy");
    EXPECT_EQ(typeUrl, "xray.proxy.shadowsocks.ClientConfig");
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonShadowsocksNoServers) {
    // Malformed/empty servers must still map to the legacy typeUrl (fallback).
    std::string json = R"({"outbounds":[{"tag":"ss-empty","protocol":"shadowsocks","settings":{}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(tagOut, "ss-empty");
    EXPECT_EQ(typeUrl, "xray.proxy.shadowsocks.ClientConfig");
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufShadowsocks2022Wire) {
    // ClientConfig { address=1, port=2, method=3, key=4 } — flat layout,
    // no ServerEndpoint/User/TypedMessage nesting, key carries raw password.
    const std::string addr = "1.2.3.4";
    const int port = 8388;
    const std::string method = "2022-blake3-aes-128-gcm";
    const std::string key = "k3y-value";
    const std::string valueJson =
        "{\"settings\":{\"servers\":[{\"address\":\"" + addr + "\",\"port\":" +
        std::to_string(port) + ",\"method\":\"" + method +
        "\",\"password\":\"" + key + "\"}]}}";
    const std::string result =
        jsonConfigToProtobuf("xray.proxy.shadowsocks_2022.ClientConfig",
                             valueJson);
    ASSERT_FALSE(result.empty());
    // IPOrDomain{bytes ip = 1} holding 1.2.3.4 in network byte order.
    const std::string ipOrDomain =
        encodeLengthDelimited(1, std::string("\x01\x02\x03\x04", 4));
    const std::string expected =
        encodeLengthDelimited(1, ipOrDomain) +
        encodeVarintField(2, static_cast<uint64_t>(port)) +
        encodeString(3, method) + encodeString(4, key);
    EXPECT_EQ(result, expected);
    // No legacy Account TypedMessage may be embedded.
    EXPECT_EQ(result.find("xray.proxy.shadowsocks.Account"),
              std::string::npos);
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufShadowsocksLegacyWireUnchanged) {
    // Regression guard: legacy ciphers must still produce the legacy Account
    // wire format (TypedMessage carrying xray.proxy.shadowsocks.Account).
    const std::string valueJson =
        "{\"settings\":{\"servers\":[{\"address\":\"1.2.3.4\",\"port\":8388,"
        "\"method\":\"aes-256-gcm\",\"password\":\"pw\"}]}}";
    const std::string result =
        jsonConfigToProtobuf("xray.proxy.shadowsocks.ClientConfig", valueJson);
    ASSERT_FALSE(result.empty());
    EXPECT_NE(result.find("xray.proxy.shadowsocks.Account"),
              std::string::npos);
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufShadowsocks2022MissingKey) {
    // Missing password/key -> empty (caller falls back to subprocess path).
    const std::string valueJson =
        "{\"settings\":{\"servers\":[{\"address\":\"1.2.3.4\",\"port\":8388,"
        "\"method\":\"2022-blake3-aes-128-gcm\"}]}}";
    EXPECT_TRUE(jsonConfigToProtobuf(
        "xray.proxy.shadowsocks_2022.ClientConfig", valueJson).empty());
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufShadowsocks2022MissingMethod) {
    const std::string valueJson =
        "{\"settings\":{\"servers\":[{\"address\":\"1.2.3.4\",\"port\":8388,"
        "\"password\":\"k3y\"}]}}";
    EXPECT_TRUE(jsonConfigToProtobuf(
        "xray.proxy.shadowsocks_2022.ClientConfig", valueJson).empty());
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
    //     protocol_name(3)="websocket"
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
    //   security_type=3 STRING = "xray.transport.internet.tls.Config"
    //     (transport/internet/config.proto: "string security_type = 3; // Type
    //     of security. Must be a message name of the settings proto.")
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
    // tls.Config (transport/internet/tls/config.proto):
    //   allow_insecure=1 (varint), server_name=3 (string)
    tlsConfig += encodeVarintField(1, 1);
    tlsConfig += encodeString(3, "example.com");
    std::string secTyped;
    secTyped += encodeString(1, "xray.transport.internet.tls.Config");
    secTyped += encodeString(2, tlsConfig);

    std::string expected;
    expected += encodeString(5, "tcp");
    expected += encodeString(3, "xray.transport.internet.tls.Config");
    expected += encodeLengthDelimited(4, secTyped);

    std::string result = encodeStreamConfig(stream);
    EXPECT_EQ(result, expected);
}

TEST_F(XrayApiDirectTest, EncodeStreamConfigRealitySecurity) {
    // StreamConfig with REALITY security:
    //   protocol_name=5 "tcp"
    //   security_type=3 STRING = "xray.transport.internet.reality.Config"
    //   security_settings=4 -> TypedMessage{type_url(1)=reality.Config,
    //                                        settings(2)=Config}
    //   reality.Config: dest=2, public_key=23 (bytes: base64 decoded).
    boost::json::object realitySettings;
    realitySettings["dest"] = "example.com:443";
    // 43-char URL-safe base64 -> 32 zero bytes when decoded.
    realitySettings["publicKey"] =
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    boost::json::object stream;
    stream["network"] = "tcp";
    stream["security"] = "reality";
    stream["realitySettings"] = realitySettings;

    std::string realityConfig;
    realityConfig += encodeString(2, "example.com:443");
    realityConfig += encodeLengthDelimited(23, base64Decode(
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
    // field 27 (spider_y): always encoded even without spiderX, 10 zero slots.
    int64_t spiderY[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    realityConfig += encodePackedInt64Field(27, spiderY, 10);
    std::string secTyped;
    secTyped += encodeString(1, "xray.transport.internet.reality.Config");
    secTyped += encodeString(2, realityConfig);

    std::string expected;
    expected += encodeString(5, "tcp");
    expected += encodeString(3, "xray.transport.internet.reality.Config");
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

// ============================================================
// RFC 7541 Huffman decoding — vectors verified against Go's
// x/net/http2/hpack (v0.51.0, the version Xray-core pins) and
// RFC 7541 Appendix C.6. Generated by temp/huff_check.py.
// ============================================================

// Test helper: "1d75d0" hex string -> binary bytes.
static std::string hexBytes(const std::string& hex) {
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nib = [](char c) -> unsigned char {
            if (c >= '0' && c <= '9')
                return static_cast<unsigned char>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<unsigned char>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F')
                return static_cast<unsigned char>(c - 'A' + 10);
            return 0;
        };
        out += static_cast<char>((nib(hex[i]) << 4) | nib(hex[i + 1]));
    }
    return out;
}

TEST_F(XrayApiDirectTest, DecodeHuffmanOk) {
    std::string out;
    EXPECT_TRUE(decodeHuffman(hexBytes("3f5f"), out));
    EXPECT_EQ(out, "ok");
}

TEST_F(XrayApiDirectTest, DecodeHuffmanZero) {
    std::string out;
    EXPECT_TRUE(decodeHuffman(hexBytes("07"), out));
    EXPECT_EQ(out, "0");
}

TEST_F(XrayApiDirectTest, DecodeHuffmanGrpcStatus) {
    std::string out;
    EXPECT_TRUE(decodeHuffman(hexBytes("9acac8b21234da8f"), out));
    EXPECT_EQ(out, "grpc-status");
}

TEST_F(XrayApiDirectTest, DecodeHuffmanGrpcMessage) {
    std::string out;
    EXPECT_TRUE(decodeHuffman(hexBytes("9acac8b5254207317f"), out));
    EXPECT_EQ(out, "grpc-message");
}

TEST_F(XrayApiDirectTest, DecodeHuffmanContentType) {
    std::string out;
    EXPECT_TRUE(decodeHuffman(hexBytes("21ea496a4ac9f5597f"), out));
    EXPECT_EQ(out, "content-type");
}

TEST_F(XrayApiDirectTest, DecodeHuffmanAppGrpc) {
    std::string out;
    EXPECT_TRUE(decodeHuffman(hexBytes("1d75d0620d263d4c4d6564"), out));
    EXPECT_EQ(out, "application/grpc");
}

TEST_F(XrayApiDirectTest, DecodeHuffmanTrailers) {
    std::string out;
    EXPECT_TRUE(decodeHuffman(hexBytes("4d833505b11f"), out));
    EXPECT_EQ(out, "trailers");
}

TEST_F(XrayApiDirectTest, DecodeHuffmanEmpty) {
    std::string out;
    EXPECT_TRUE(decodeHuffman("", out));
    EXPECT_TRUE(out.empty());
}

TEST_F(XrayApiDirectTest, DecodeHuffmanBadPadding) {
    // "ok" = 3f5f; flipping the last padding bit to 0 makes the padding
    // non-ones, which RFC 7541 §5.2 forbids.
    std::string out;
    EXPECT_FALSE(decodeHuffman(hexBytes("3f5e"), out));
}

TEST_F(XrayApiDirectTest, DecodeHuffmanEosInvalid) {
    // 30 bits of 1 = EOS symbol (invalid to decode) plus padding.
    std::string out;
    EXPECT_FALSE(decodeHuffman(hexBytes("ffffffff"), out));
}

// ============================================================
// HPACK decoder (RFC 7541) — response headers/trailers from Xray's
// gRPC server (grpc-go encoding).
// ============================================================

TEST_F(XrayApiDirectTest, HpackDecodeIndexedStatus) {
    // 0x88 = indexed header field, index 8 = :status: 200.
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_TRUE(grpcDecodeHpackHeaders(hexBytes("88"), headers));
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0].first, ":status");
    EXPECT_EQ(headers[0].second, "200");
}

TEST_F(XrayApiDirectTest, HpackDecodeIndexed61) {
    // 0xBD = indexed header field, index 61 = www-authenticate (name only).
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_TRUE(grpcDecodeHpackHeaders(hexBytes("bd"), headers));
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0].first, "www-authenticate");
    EXPECT_TRUE(headers[0].second.empty());
}

TEST_F(XrayApiDirectTest, HpackDecodeResponseHeaders) {
    // grpc-go response headers block:
    //   0x88                :status: 200            (indexed, static 8)
    //   0x5F                content-type (static 31), literal w/ indexing
    //   0x8B + Huffman      value "application/grpc" (11 encoded bytes)
    std::string block = hexBytes("885f8b1d75d0620d263d4c4d6564");
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_TRUE(grpcDecodeHpackHeaders(block, headers));
    ASSERT_EQ(headers.size(), 2u);
    EXPECT_EQ(headers[0].first, ":status");
    EXPECT_EQ(headers[0].second, "200");
    EXPECT_EQ(headers[1].first, "content-type");
    EXPECT_EQ(headers[1].second, "application/grpc");
}

TEST_F(XrayApiDirectTest, HpackDecodeTrailers) {
    // grpc-go trailers block for a successful unary call:
    //   0x40      literal with incremental indexing, full name
    //   0x88 + H  name "grpc-status" (8 encoded bytes, Huffman)
    //   0x81 + H  value "0" (1 encoded byte, Huffman)
    std::string block = hexBytes("40889acac8b21234da8f8107");
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_TRUE(grpcDecodeHpackHeaders(block, headers));
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0].first, "grpc-status");
    EXPECT_EQ(headers[0].second, "0");
}

TEST_F(XrayApiDirectTest, HpackDecodeLiteralWithoutIndexing) {
    // 0x00 (literal w/o indexing, full name) + "test" + "ab".
    std::string block;
    block += static_cast<char>(0x00);
    block += static_cast<char>(0x04); block += "test";
    block += static_cast<char>(0x02); block += "ab";
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_TRUE(grpcDecodeHpackHeaders(block, headers));
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0].first, "test");
    EXPECT_EQ(headers[0].second, "ab");
}

TEST_F(XrayApiDirectTest, HpackDecodeNeverIndexed) {
    // 0x10 (literal never indexed, full name) + "foo" + "bar".
    std::string block;
    block += static_cast<char>(0x10);
    block += static_cast<char>(0x03); block += "foo";
    block += static_cast<char>(0x03); block += "bar";
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_TRUE(grpcDecodeHpackHeaders(block, headers));
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0].first, "foo");
    EXPECT_EQ(headers[0].second, "bar");
}

TEST_F(XrayApiDirectTest, HpackDecodeDynamicIndex) {
    // Add grpc-status:0 (becomes dynamic index 62), then reference it
    // with 0xBE (indexed, index 62) in the same block.
    std::string block = hexBytes("40889acac8b21234da8f8107be");
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_TRUE(grpcDecodeHpackHeaders(block, headers));
    ASSERT_EQ(headers.size(), 2u);
    EXPECT_EQ(headers[0].first, "grpc-status");
    EXPECT_EQ(headers[0].second, "0");
    EXPECT_EQ(headers[1].first, "grpc-status");
    EXPECT_EQ(headers[1].second, "0");
}

TEST_F(XrayApiDirectTest, HpackDecodeTableSizeUpdate) {
    // 0x20 = dynamic table size update to 0 -> accepted, no headers.
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_TRUE(grpcDecodeHpackHeaders(hexBytes("20"), headers));
    EXPECT_TRUE(headers.empty());
}

TEST_F(XrayApiDirectTest, HpackDecodeErrorTruncated) {
    // Literal with incremental indexing + a name length byte but no name
    // bytes -> malformed.
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_FALSE(grpcDecodeHpackHeaders(hexBytes("408b"), headers));
}

TEST_F(XrayApiDirectTest, HpackDecodeErrorIndexZero) {
    // Indexed header field with index 0 is a protocol error.
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_FALSE(grpcDecodeHpackHeaders(hexBytes("80"), headers));
}

TEST_F(XrayApiDirectTest, HpackRoundTrip) {
    // encodeHpack output (literal-only encoder) must decode back to the
    // original headers.
    std::vector<std::pair<std::string, std::string>> input = {
        {":method", "POST"},
        {":path", "/xray.app.proxyman.command.HandlerService/AddOutbound"},
        {":scheme", "http"},
        {"content-type", "application/grpc"},
        {"te", "trailers"},
    };
    std::string block = encodeHpack(input);
    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_TRUE(grpcDecodeHpackHeaders(block, headers));
    EXPECT_EQ(headers, input);
}

// ============================================================
// B5 / B12 / B13 — encoding correctness fixes
// ============================================================

TEST_F(XrayApiDirectTest, JsonConfigToProtobufNonObjectServer) {
    // B5: servers[0] must be an object; a string or other non-object should
    // produce an empty result (failure signal) instead of crashing.
    std::string json = R"({
        "outbounds": [{
            "tag": "test",
            "protocol": "freedom",
            "settings": {
                "domainStrategy": "Asis",
                "servers": ["not-an-object"]
            }
        }]
    })";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    EXPECT_EQ(typeUrl, "xray.proxy.freedom.Config");
    // jsonConfigToProtobuf should return empty string, not throw.
    std::string encoded = encodeJsonConfigToProtobuf(typeUrl, valueJson);
    EXPECT_TRUE(encoded.empty()) << "expected empty string for non-object server entry";
}

TEST_F(XrayApiDirectTest, EncodeTLSSettingsSecurityFields) {
    // tls.Config per transport/internet/tls/config.proto (B12 verification):
    //   allow_insecure=1(varint), server_name=3(string), next_protocol=4(repeated string),
    //   min_version=7(string), max_version=8(string), cipher_suites=9(string),
    //   fingerprint=11(string), reject_unknown_sni=12(varint).
    boost::json::object tls;
    tls["serverName"] = "example.com";
    tls["allowInsecure"] = false;
    boost::json::array alpnArr;
    alpnArr.push_back("h2");
    alpnArr.push_back("http/1.1");
    tls["alpn"] = alpnArr;
    tls["rejectUnknownSni"] = true;
    tls["minVersion"] = "1.2";
    tls["maxVersion"] = "1.3";
    tls["cipher"] = "AES-GCM";
    tls["fingerprint"] = "chrome";

    std::string result = encodeTLSSettings(tls);

    std::map<int, std::string> strDecoded = decodeStringFields(result);
    std::map<int, uint64_t> varDecoded = decodeVarintFields(result);

    // server_name=3 should be "example.com"
    EXPECT_EQ(strDecoded[3], "example.com") << "server_name (field 3) mismatch";
    // allow_insecure=1 should be 0 (false)
    EXPECT_EQ(varDecoded[1], 0u) << "allow_insecure (field 1) mismatch";
    // reject_unknown_sni=12 should be 1 (true)
    EXPECT_EQ(varDecoded[12], 1u) << "reject_unknown_sni (field 12) mismatch";
    // min_version=7 / max_version=8 are STRINGS
    EXPECT_EQ(strDecoded[7], "1.2") << "min_version (field 7) mismatch";
    EXPECT_EQ(strDecoded[8], "1.3") << "max_version (field 8) mismatch";
    // cipher_suites=9 / fingerprint=11 are valid Config fields (strings)
    EXPECT_EQ(strDecoded[9], "AES-GCM") << "cipher_suites (field 9) mismatch";
    EXPECT_EQ(strDecoded[11], "chrome") << "fingerprint (field 11) mismatch";
    // alpn=4: at least one entry present
    EXPECT_TRUE(strDecoded.count(4) >= 1) << "alpn (field 4) missing";
}

TEST_F(XrayApiDirectTest, EncodeTLSSettingsUnknownVersion) {
    // min_version/max_version are passed through as strings (fields 7/8).
    // A non-standard version string is still emitted verbatim.
    boost::json::object tls;
    tls["minVersion"] = "invalid";
    tls["maxVersion"] = "1.3";
    std::string result = encodeTLSSettings(tls);
    std::map<int, std::string> decoded = decodeStringFields(result);
    // min_version (field 7) present as verbatim string
    EXPECT_EQ(decoded[7], "invalid") << "minVersion should pass through as string";
    // max_version (field 8) present as "1.3"
    EXPECT_EQ(decoded[8], "1.3") << "maxVersion (field 8) mismatch";
}

TEST_F(XrayApiDirectTest, EncodeXHTTPTransport) {
    // B13: xhttp transport is remapped to splithttp. Xray v26.2.4+ removed
    // the xhttp protocol; its JSON adapter maps network "xhttp"->"splithttp".
    boost::json::object xhttpSettings;
    xhttpSettings["host"] = "example.com";
    xhttpSettings["path"] = "/xhttp";
    boost::json::object stream;
    stream["network"] = "xhttp";
    stream["xhttpSettings"] = xhttpSettings;

    std::string result = encodeStreamConfig(stream);
    EXPECT_FALSE(result.empty());
    std::map<int, std::string> decoded = decodeStringFields(result);
    // v26.2.4 registers only splithttp, so protocol_name must be 'splithttp'.
    EXPECT_EQ(decoded[5], "splithttp") << "protocol_name field 5 must be 'splithttp'";
    // Transport TypedMessage must use the splithttp Config type URL.
    EXPECT_NE(result.find("xray.transport.internet.splithttp.Config"),
              std::string::npos)
        << "transport type URL must be splithttp.Config";
    // The unregistered xhttp.Config type URL must not appear.
    EXPECT_EQ(result.find("xray.transport.internet.xhttp.Config"),
              std::string::npos)
        << "xhttp.Config type URL must not be encoded";
}

TEST_F(XrayApiDirectTest, EncodeSplitHTTPTransport) {
    // B13: splithttp transport with splithttpSettings.
    boost::json::object splithttpSettings;
    splithttpSettings["host"] = "example.com";
    splithttpSettings["path"] = "/splithttp";
    boost::json::object stream;
    stream["network"] = "splithttp";
    stream["splithttpSettings"] = splithttpSettings;

    std::string result = encodeStreamConfig(stream);
    EXPECT_FALSE(result.empty());
    std::map<int, std::string> decoded = decodeStringFields(result);
    EXPECT_EQ(decoded[5], "splithttp") << "protocol_name field 5 must be 'splithttp'";
}

TEST_F(XrayApiDirectTest, JsonConfigToProtobufEmptyServerEntry) {
    // B5: empty servers array -> empty result.
    std::string json = R"({"outbounds":[{"tag":"test","protocol":"freedom","settings":{"domainStrategy":"Asis","servers":[]}}]})";
    std::string tagOut, typeUrl, valueJson;
    EXPECT_TRUE(parseOutboundJson(json, tagOut, typeUrl, valueJson));
    std::string encoded = encodeJsonConfigToProtobuf(typeUrl, valueJson);
    EXPECT_TRUE(encoded.empty()) << "expected empty string for empty servers array";
}

TEST_F(XrayApiDirectTest, XHTTPStreamConfigWithTLS) {
    // B13: xhttp + TLS. xhttp is remapped to splithttp (v26.2.4+), and the
    // stream config must still carry both transport and security sections.
    boost::json::object xhttpSettings;
    xhttpSettings["host"] = "example.com";
    xhttpSettings["path"] = "/xhttp";
    boost::json::object tlsSettings;
    tlsSettings["serverName"] = "example.com";
    tlsSettings["allowInsecure"] = true;
    boost::json::object stream;
    stream["network"] = "xhttp";
    stream["xhttpSettings"] = xhttpSettings;
    stream["security"] = "tls";
    stream["tlsSettings"] = tlsSettings;

    std::string result = encodeStreamConfig(stream);
    EXPECT_FALSE(result.empty());
    // The result should have protocol_name=5 and security_type=3 with
    // security_settings=4.
    std::map<int, std::string> decodedStr = decodeStringFields(result);
    EXPECT_EQ(decodedStr[5], "splithttp");
    // security_type is a string message name, not an enum varint.
    EXPECT_EQ(decodedStr[3], "xray.transport.internet.tls.Config");
}

TEST_F(XrayApiDirectTest, Base64UrlDecodeRawBytes) {
    // 32 zero bytes URL-safe base64 -> 43 chars, no padding.
    std::string zeros = base64Decode(
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    EXPECT_EQ(zeros.size(), 32u);
    EXPECT_EQ(static_cast<unsigned char>(zeros[0]), 0u);
    EXPECT_EQ(static_cast<unsigned char>(zeros[31]), 0u);
    // Standard alphabet + padding also accepted.
    std::string hello = base64Decode("SGVsbG8=");
    EXPECT_EQ(hello, "Hello");
    // Invalid characters rejected.
    EXPECT_TRUE(base64Decode("not!valid!").empty());
}

TEST_F(XrayApiDirectTest, HexDecodeRawBytes) {
    std::string sid = hexDecode("825d392c67e4");
    EXPECT_EQ(sid.size(), 6u);
    EXPECT_EQ(static_cast<unsigned char>(sid[0]), 0x82u);
    EXPECT_EQ(static_cast<unsigned char>(sid[5]), 0xE4u);
    // Uppercase accepted.
    EXPECT_EQ(hexDecode("DEADBEEF").size(), 4u);
    // Odd length and non-hex rejected.
    EXPECT_TRUE(hexDecode("abc").empty());
    EXPECT_TRUE(hexDecode("zz").empty());
}

TEST_F(XrayApiDirectTest, EncodeRealitySettings) {
    // REALITY: public_key (23) and short_id (24) are bytes fields. The JSON
    // adapter decodes base64/hex before filling the proto; the encoder must
    // do the same or every REALITY handshake fails (X25519 rejects the
    // garbage key). String fields stay verbatim.
    boost::json::object reality;
    reality["publicKey"] =
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";  // 43-char -> 32 bytes
    reality["shortId"] = "825d392c67e4";                // hex -> 6 bytes
    reality["serverName"] = "ozon.ru";
    reality["fingerprint"] = "firefox";
    reality["spiderX"] = "/";

    std::string result = encodeRealitySettings(reality);
    EXPECT_FALSE(result.empty());
    // Length-delimited field lens: 23 -> 32 decoded bytes, 24 -> 6 bytes.
    std::map<int, uint64_t> lens = decodeVarintFields(result);
    EXPECT_EQ(lens[23], 32u) << "public_key must be the decoded 32-byte key";
    EXPECT_EQ(lens[24], 6u) << "short_id must be the hex-decoded bytes";
    // String fields preserved verbatim.
    std::map<int, std::string> str = decodeStringFields(result);
    EXPECT_EQ(str[22], "ozon.ru");
    EXPECT_EQ(str[21], "firefox");
    EXPECT_EQ(str[26], "/");
}

TEST_F(XrayApiDirectTest, EncodeRealitySettingsRealisticKey) {
    // Real database sample: 43-char URL-safe base64 public key -> 32 bytes.
    std::string decoded = base64Decode("XBePIY00h9fQ2Kc1mHxLs4dR7v8w9y0aBcDeFgHiJkLmNo");
    if (decoded.size() == 32u) {
        boost::json::object reality;
        reality["publicKey"] =
            "XBePIY00h9fQ2Kc1mHxLs4dR7v8w9y0aBcDeFgHiJkLmNo";
        reality["shortId"] = "6d6f13013d3e1d0c";
        reality["serverName"] = "ozon.ru";
        std::string result = encodeRealitySettings(reality);
        std::map<int, uint64_t> lens = decodeVarintFields(result);
        EXPECT_EQ(lens[23], 32u);
        EXPECT_EQ(lens[24], 8u);
    }
}

// ---- spider_y (protobuf field 27) regression ----
// xray-core infra/conf/transport_internet.go always builds a 10-element
// SpiderY slice from the spiderX query params (p/c/t/i/r); the gRPC path must
// send the same 10 values or the receiving process hits reality.go:273
// (config.SpiderY[8]/[9]) with a nil slice and panics the whole xray process.

TEST_F(XrayApiDirectTest, ParseSpiderYParams) {
    int64_t arr[10];

    // p=3-5 -> padding [0]=3 [1]=5, all other slots stay 0.
    std::memset(arr, 0x7F, sizeof(arr));
    parseSpiderYParams("/?p=3-5", arr);
    EXPECT_EQ(arr[0], 3);
    EXPECT_EQ(arr[1], 5);
    EXPECT_EQ(arr[2], 0);
    EXPECT_EQ(arr[3], 0);
    EXPECT_EQ(arr[4], 0);
    EXPECT_EQ(arr[5], 0);
    EXPECT_EQ(arr[6], 0);
    EXPECT_EQ(arr[7], 0);
    EXPECT_EQ(arr[8], 0);
    EXPECT_EQ(arr[9], 0);

    // "/" (no query) -> all zeros.
    std::memset(arr, 0x7F, sizeof(arr));
    parseSpiderYParams("/", arr);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(arr[i], 0) << "slot " << i;
    }

    // t=2 (single segment) -> both times slots get the same value.
    std::memset(arr, 0x7F, sizeof(arr));
    parseSpiderYParams("/?t=2", arr);
    EXPECT_EQ(arr[4], 2);
    EXPECT_EQ(arr[5], 2);
    EXPECT_EQ(arr[0], 0);

    // c=1-3-9 -> only the first two segments are used (mirrors Go Split[0/1]).
    std::memset(arr, 0x7F, sizeof(arr));
    parseSpiderYParams("/?c=1-3-9", arr);
    EXPECT_EQ(arr[2], 1);
    EXPECT_EQ(arr[3], 3);

    // All params at once.
    std::memset(arr, 0x7F, sizeof(arr));
    parseSpiderYParams("/?p=1-2&c=3-4&t=5-6&i=7-8&r=9-10", arr);
    EXPECT_EQ(arr[0], 1);
    EXPECT_EQ(arr[1], 2);
    EXPECT_EQ(arr[2], 3);
    EXPECT_EQ(arr[3], 4);
    EXPECT_EQ(arr[4], 5);
    EXPECT_EQ(arr[5], 6);
    EXPECT_EQ(arr[6], 7);
    EXPECT_EQ(arr[7], 8);
    EXPECT_EQ(arr[8], 9);
    EXPECT_EQ(arr[9], 10);

    // Empty string -> all zeros.
    std::memset(arr, 0x7F, sizeof(arr));
    parseSpiderYParams("", arr);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(arr[i], 0) << "slot " << i;
    }

    // Invalid number -> slot stays 0 (mirrors strconv.ParseInt failure).
    std::memset(arr, 0x7F, sizeof(arr));
    parseSpiderYParams("/?p=abc", arr);
    EXPECT_EQ(arr[0], 0);
    EXPECT_EQ(arr[1], 0);
}

TEST_F(XrayApiDirectTest, EncodeRealitySettingsSpiderY) {
    // spiderX with query params -> field 27 carries the parsed 10 values.
    boost::json::object reality;
    reality["serverName"] = "ozon.ru";
    reality["spiderX"] = "/?p=3-5";

    std::string result = encodeRealitySettings(reality);
    std::map<int, uint64_t> lens = decodeVarintFields(result);
    EXPECT_EQ(lens[27], 10u) << "spider_y must be a 10-element packed array";

    // Raw wire check: tag for field 27 wire type 2 is (27<<3)|2 = 218 = 0xDA,
    // varint-encoded as 0xDA 0x01, followed by length 0x0A then 10 payload bytes.
    const std::string needle = std::string("\xDA\x01\x0A", 3);
    size_t pos = result.find(needle);
    ASSERT_NE(pos, std::string::npos) << "packed spider_y tag/length missing";
    // p=3-5 -> payload[0]=3 (varint 0x03), payload[1]=5 (varint 0x05), rest 0.
    EXPECT_EQ(static_cast<unsigned char>(result[pos + 3]), 0x03u);
    EXPECT_EQ(static_cast<unsigned char>(result[pos + 4]), 0x05u);
    for (size_t i = pos + 5; i < pos + 13; ++i) {
        EXPECT_EQ(static_cast<unsigned char>(result[i]), 0x00u) << "byte " << i;
    }
}

TEST_F(XrayApiDirectTest, EncodeRealitySettingsSpiderYAlwaysPresent) {
    // No spiderX at all -> field 27 must STILL be encoded with all zeros.
    // Omitting it leaves SpiderY nil on the xray side (gRPC path skips the
    // JSON adapter) and any failed REALITY handshake panics at reality.go:273.
    boost::json::object reality;
    reality["serverName"] = "ozon.ru";

    std::string result = encodeRealitySettings(reality);
    std::map<int, uint64_t> lens = decodeVarintFields(result);
    EXPECT_EQ(lens[27], 10u) << "spider_y must be encoded even without spiderX";

    // All ten payload bytes must be zero.
    const std::string needle = std::string("\xDA\x01\x0A", 3);
    size_t pos = result.find(needle);
    ASSERT_NE(pos, std::string::npos);
    for (size_t i = pos + 3; i < pos + 13; ++i) {
        EXPECT_EQ(static_cast<unsigned char>(result[i]), 0x00u) << "byte " << i;
    }
}

// ---- Lifecycle: consecutive gRPC connect failure detection ----

TEST_F(XrayApiDirectTest, ConsecutiveConnectFailuresTriggerHealthHook) {
    // serverAddr uses a closed port (1) so every connect fails fast with
    // WSAECONNREFUSED — no real Xray process is involved.
    xray::XrayApi api("xray.exe", "127.0.0.1:1");
    int hookCount = 0;
    api.setConnectFailureHook([&hookCount]() { hookCount++; });

    EXPECT_LT(callGrpcConnect(api, "127.0.0.1", 1), 0);
    EXPECT_EQ(getConsecutiveFailures(api), 1);
    EXPECT_EQ(hookCount, 0);

    // Second consecutive failure: counter reaches 2, hook fires exactly once.
    EXPECT_LT(callGrpcConnect(api, "127.0.0.1", 1), 0);
    EXPECT_EQ(getConsecutiveFailures(api), 2);
    EXPECT_EQ(hookCount, 1);

    // Third consecutive failure must NOT re-fire the hook.
    EXPECT_LT(callGrpcConnect(api, "127.0.0.1", 1), 0);
    EXPECT_EQ(getConsecutiveFailures(api), 3);
    EXPECT_EQ(hookCount, 1);
}

TEST_F(XrayApiDirectTest, GrpcConnectSuccessResetsFailureCounter) {
    xray::XrayApi api("xray.exe", "127.0.0.1:1");
    EXPECT_LT(callGrpcConnect(api, "127.0.0.1", 1), 0);
    EXPECT_LT(callGrpcConnect(api, "127.0.0.1", 1), 0);
    EXPECT_EQ(getConsecutiveFailures(api), 2);

    // Open a real listening socket on an ephemeral port so connect succeeds.
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(listener, INVALID_SOCKET);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);  // ephemeral port
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    ASSERT_EQ(bind(listener, reinterpret_cast<struct sockaddr*>(&addr),
                   static_cast<int>(sizeof(addr))), 0);
    ASSERT_EQ(listen(listener, 1), 0);

    int addrLen = static_cast<int>(sizeof(addr));
    ASSERT_EQ(getsockname(listener, reinterpret_cast<struct sockaddr*>(&addr),
                          &addrLen), 0);
    int port = ntohs(addr.sin_port);

    int sock = callGrpcConnect(api, "127.0.0.1", port);
    EXPECT_GE(sock, 0);
    if (sock >= 0) {
        callGrpcClose(api, sock);
    }

    closesocket(listener);
    WSACleanup();

    // A successful connect resets the consecutive-failure counter.
    EXPECT_EQ(getConsecutiveFailures(api), 0);
}

// ---- SplitHTTP nil-request panic guard ----
// Xray-core splithttp OpenStream discards the error from
// http.NewRequestWithContext; a host/path containing control or whitespace
// characters (<= 0x20 or 0x7F) makes the request URL unparseable -> nil
// request -> FillStreamRequest panics the whole xray process.
// validateSplitHTTPSettings must reject such streamSettings pre-injection.

TEST_F(XrayApiDirectTest, ValidateSplitHTTPSettingsOk) {
    std::string errorOut;
    const std::string json =
        "{\"network\":\"splithttp\","
        "\"splithttpSettings\":{\"host\":\"example.com\",\"path\":\"/a/b/\"}}";
    EXPECT_TRUE(XrayApi::validateSplitHTTPSettings(json, errorOut));
}

TEST_F(XrayApiDirectTest, ValidateSplitHTTPSettingsEmptyHostOk) {
    // Empty host is allowed: the Xray-core dialer falls back to the server
    // address (dest.Address.String()), which is not a panic source.
    std::string errorOut;
    const std::string json =
        "{\"network\":\"splithttp\","
        "\"splithttpSettings\":{\"path\":\"/cdn/\"}}";
    EXPECT_TRUE(XrayApi::validateSplitHTTPSettings(json, errorOut));
}

TEST_F(XrayApiDirectTest, ValidateSplitHTTPSettingsRejectsHostSpace) {
    std::string errorOut;
    const std::string json =
        "{\"network\":\"splithttp\","
        "\"splithttpSettings\":{\"host\":\"my host\",\"path\":\"/cdn/\"}}";
    EXPECT_FALSE(XrayApi::validateSplitHTTPSettings(json, errorOut));
    EXPECT_FALSE(errorOut.empty());
}

TEST_F(XrayApiDirectTest, ValidateSplitHTTPSettingsRejectsHostNewline) {
    std::string errorOut;
    const std::string json =
        "{\"network\":\"splithttp\","
        "\"splithttpSettings\":{\"host\":\"example.com\\n\",\"path\":\"/cdn/\"}}";
    EXPECT_FALSE(XrayApi::validateSplitHTTPSettings(json, errorOut));
}

TEST_F(XrayApiDirectTest, ValidateSplitHTTPSettingsRejectsPathControlChar) {
    // The query part travels verbatim in url.URL.RawQuery; a space in it
    // also makes url.Parse fail (e.g. path "/cdn/?x=1 y=2").
    std::string errorOut;
    const std::string json =
        "{\"network\":\"splithttp\","
        "\"splithttpSettings\":{\"host\":\"example.com\","
        "\"path\":\"/cdn/?x=1 y=2\"}}";
    EXPECT_FALSE(XrayApi::validateSplitHTTPSettings(json, errorOut));
}

TEST_F(XrayApiDirectTest, ValidateSplitHTTPSettingsXhttp) {
    std::string errorOut;
    const std::string okJson =
        "{\"network\":\"xhttp\","
        "\"xhttpSettings\":{\"host\":\"example.com\",\"path\":\"/cdn/\"}}";
    EXPECT_TRUE(XrayApi::validateSplitHTTPSettings(okJson, errorOut));

    const std::string badJson =
        "{\"network\":\"xhttp\","
        "\"xhttpSettings\":{\"host\":\"bad host\",\"path\":\"/cdn/\"}}";
    EXPECT_FALSE(XrayApi::validateSplitHTTPSettings(badJson, errorOut));
}

TEST_F(XrayApiDirectTest, ValidateSplitHTTPSettingsNonSplitHTTPPasses) {
    // ws / tcp / missing network / empty input must not be intercepted.
    std::string errorOut;
    const std::string wsJson =
        "{\"network\":\"ws\","
        "\"wsSettings\":{\"path\":\"/abc def/\"}}";
    EXPECT_TRUE(XrayApi::validateSplitHTTPSettings(wsJson, errorOut));

    const std::string noNetJson = "{\"network\":\"tcp\"}";
    EXPECT_TRUE(XrayApi::validateSplitHTTPSettings(noNetJson, errorOut));

    EXPECT_TRUE(XrayApi::validateSplitHTTPSettings("", errorOut));
    EXPECT_TRUE(XrayApi::validateSplitHTTPSettings("[]", errorOut));
}

// ---- Observatory GetOutboundStatus gRPC path -----------------------------

TEST_F(XrayApiDirectTest, ObservatoryStatusPathIsCorrect) {
    // Verified at runtime via gRPC reflection + direct probe against
    // Xray 26.3.27: the observatory command service is registered as
    //   xray.core.app.observatory.command.ObservatoryService
    // (legacy "xray.core.app..." prefix). The plain proto package form
    // "xray.app.observatory.command.ObservatoryService" is NOT registered,
    // so getOutboundStatusDirect must target the runtime name to avoid
    // "unknown service".
    EXPECT_STREQ(XrayApi::observatoryStatusPath(),
                 "/xray.core.app.observatory.command.ObservatoryService/GetOutboundStatus");
    std::string p = XrayApi::observatoryStatusPath();
    EXPECT_NE(p.find("core.app.observatory.command"), std::string::npos)
        << "must target the runtime-registered xray.core.app prefix";
    EXPECT_NE(p.find("observatory.command"), std::string::npos)
        << "must target the observatory command service";
}

// ---- parseOutboundJson contract used by StandaloneProxyPool ------------

// StandaloneProxyPool now wraps the built outbound as {"outbounds":[ob]} with
// the pool tag "px-<id>". parseOutboundJson (used by addOutboundDirect) must
// extract the tag and the correct protobuf typeUrl from that wrapped form.
TEST_F(XrayApiDirectTest, ParseOutboundJsonWrappedFormVless) {
    const std::string json =
        R"({"outbounds":[{"tag":"px-123","protocol":"vless",)"
        R"("settings":{"vnext":[{"address":"1.2.3.4","port":443,)"
        R"("users":[{"id":"11111111-1111-1111-1111-111111111111"}]}]}}]})";
    std::string tag, typeUrl, value;
    ASSERT_TRUE(parseOutboundJson(json, tag, typeUrl, value));
    EXPECT_EQ(tag, "px-123");
    EXPECT_EQ(typeUrl, "xray.proxy.vless.outbound.Config");
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonWrappedFormFreedom) {
    const std::string json =
        R"({"outbounds":[{"tag":"px-9","protocol":"freedom",)"
        R"("settings":{"domainStrategy":0}}]})";
    std::string tag, typeUrl, value;
    ASSERT_TRUE(parseOutboundJson(json, tag, typeUrl, value));
    EXPECT_EQ(tag, "px-9");
    EXPECT_EQ(typeUrl, "xray.proxy.freedom.Config");
}

TEST_F(XrayApiDirectTest, ParseOutboundJsonRejectsBareObject) {
    // Guard: a bare outbound (the old StandaloneProxyPool output) must NOT be
    // accepted, confirming the wrapper is now required (and injected).
    const std::string json =
        R"({"tag":"px-1","protocol":"vless","settings":{}})";
    std::string tag, typeUrl, value;
    EXPECT_FALSE(parseOutboundJson(json, tag, typeUrl, value));
}

#else
// When USE_GRPC_API is OFF, we still need a placeholder test so ctest passes.
#include <gtest/gtest.h>

struct XrayApiDirectPlaceholder : public ::testing::Test {};

TEST_F(XrayApiDirectPlaceholder, PlaceholderSkip) {
    GTEST_SKIP() << "Skipped: USE_GRPC_API not enabled";
}
#endif
