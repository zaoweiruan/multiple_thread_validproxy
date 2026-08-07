#ifndef XRAY_API_H
#define XRAY_API_H

#include <string>
#include <vector>
#include <cstdint>

#ifdef USE_GRPC_API
#include <boost/json.hpp>
#endif

#ifdef TEST_BUILD
class XrayApiDirectTest;  // forward decl for friend access in test builds
#endif

namespace xray {

class XrayApi {
public:
    XrayApi(const std::string& xrayPath, const std::string& serverAddr);

    // Subprocess-based methods (always available)
    bool addOutbound(const std::string& outboundJson, const std::string& tag, std::string& resultOutput);
    bool removeOutbound(const std::string& tag);
    bool listOutbounds();
    bool listOutboundsResult(std::string& output);
    bool ping(std::string& resultOutput);
    std::string getLastError() const;

#ifdef USE_GRPC_API
    // Direct gRPC methods — bypass subprocess overhead (~1700-2300ms per call)
    bool addOutboundDirect(const std::string& outboundJson, const std::string& tag, std::string& resultOutput);
    bool removeOutboundDirect(const std::string& tag);
    bool listOutboundsDirect(std::string& output);
#endif

private:
    std::string xrayPath_;
    std::string serverAddr_;  // format: "tcp://host:port" or "host:port"
    std::string lastError_;
    bool runCommand(const std::string& args, std::string& output);

#ifdef USE_GRPC_API
#ifdef TEST_BUILD
    friend class ::XrayApiDirectTest;
#endif

    // ---- Raw Winsock2 gRPC helpers ----
    // Parse serverAddr_ into host and port. Returns true on success.
    bool parseServerAddr(std::string& host, int& port) const;

    // TCP connect to host:port. Returns socket fd or -1 on failure.
    int grpcConnect(const std::string& host, int port);

    // Send HTTP/2 preface + SETTINGS frame on a connected socket.
    bool grpcSendPreface(int sock);

    // Send a gRPC request (HEADERS + DATA frames) and read the response.
    // Returns true on success; response contains the gRPC response body.
    bool grpcSendReceive(int sock, const std::string& path,
                         const std::string& requestPayload,
                         std::string& response);

    // Close a socket gracefully.
    void grpcClose(int sock);

    // Parse outbound JSON (ConfigGenerator format: {"outbounds":[{...}]}) into
    // tag, protobuf typeUrl, and value JSON (remaining fields minus tag).
    // When streamSettingsJson/muxJson are non-null, the outbound's
    // "streamSettings" / "mux" objects (if present) are serialized into them.
    // Returns true on success, false if JSON parsing fails.
    static bool parseOutboundJson(const std::string& outboundJson,
                                  std::string& tagOut,
                                  std::string& typeUrl,
                                  std::string& valueJson,
                                  std::string* streamSettingsJson = nullptr,
                                  std::string* muxJson = nullptr);

    // ---- Protobuf wire-format encoders (no library needed) ----
    static std::string encodeVarint(uint64_t value);
    static std::string encodeVarintField(int fieldNumber, uint64_t value);
    static std::string encodeLengthDelimited(int fieldNumber, const std::string& data);
    static std::string encodeString(int fieldNumber, const std::string& str);
    // ---- Protobuf encoder helpers for complex messages ----
    // Encode an address string (IPv4/IPv6/domain) as IPOrDomain protobuf.
    static std::string encodeIPOrDomain(const std::string& addr);
    // Build a User protobuf with the given account TypedMessage.
    static std::string encodeUser(const std::string& accountTypeUrl,
                                  const std::string& accountProto);
    // Build a ServerEndpoint protobuf.
    static std::string encodeServerEndpoint(const std::string& addr, int port,
                                            const std::string& userProto);
    // ---- Account protobuf encoders for each proxy protocol ----
    static std::string encodeVMessAccount(const std::string& id, int securityType);
    static std::string encodeVLESSAccount(const std::string& id,
                                          const std::string& flow,
                                          const std::string& encryption);
    static std::string encodeTrojanAccount(const std::string& password);
    static std::string encodeShadowsocksAccount(const std::string& password,
                                                const std::string& method);
    static std::string encodeSocksAccount(const std::string& username,
                                          const std::string& password);
    static std::string encodeHTTPAccount(const std::string& username,
                                         const std::string& password);
    // Convert JSON outbound config to protobuf wire-format binary for
    // known simple types (freedom.Config, blackhole.Config).  Returns
    // empty string for unknown types (caller falls back to raw JSON).
    static std::string jsonConfigToProtobuf(const std::string& typeUrl,
                                            const std::string& valueJson);

    // ---- Minimal HPACK encoder for HTTP/2 HEADERS ----
    // Encodes a list of header key-value pairs into HPACK format.
    static std::string encodeHpack(const std::vector<std::pair<std::string, std::string>>& headers);

    // ---- Minimal HPACK decoder (RFC 7541) for gRPC response headers ----
    // Decodes an HPACK header block into name/value pairs. Supports indexed
    // header fields, literal with/without indexing, literal never indexed,
    // Huffman-coded strings and integer values with continuation bits.
    // Returns false on malformed input.
    static bool grpcDecodeHpackHeaders(
        const std::string& block,
        std::vector<std::pair<std::string, std::string>>& headers);

    // ---- RFC 7541 Huffman decoder (MSB-first wire order) ----
    // Decodes a Huffman-coded string into out. Returns false on invalid
    // encoding (EOS symbol 256, padding > 7 bits, non-ones padding, or
    // bit patterns that match no code). Mirrors Go's x/net/http2/hpack.
    static bool decodeHuffman(const std::string& data, std::string& out);

    // ---- Transport (streamSettings) protobuf encoders ----
    // Encode a WebSocketConfig protobuf from the "wsSettings" JSON object.
    static std::string encodeWebSocketConfig(const boost::json::object& ws);
    // Encode a TLS Config protobuf from the "tlsSettings" JSON object.
    static std::string encodeTLSSettings(const boost::json::object& tls);
    // Decode a base64 (URL-safe or standard, padding optional) string to raw
    // bytes. Used to convert the "publicKey" JSON value into the 32-byte
    // X25519 key that reality.Config stores in its bytes field 23. Returns
    // an empty string on invalid input.
    static std::string base64Decode(const std::string& input);
    // Decode a hex string to raw bytes. Used to convert the "shortId" JSON
    // value into the bytes field 24 of reality.Config. Returns an empty
    // string on odd-length or non-hex input.
    static std::string hexDecode(const std::string& input);
    // Encode a RealityConfig protobuf from the "realitySettings" JSON object.
    static std::string encodeRealitySettings(const boost::json::object& reality);
    // Encode a gRPC Config protobuf from the "grpcSettings" JSON object.
    static std::string encodeGRPCSettings(const boost::json::object& grpc);
    // Encode a KCP Config protobuf from the "kcpSettings" JSON object.
    static std::string encodeKCPSettings(const boost::json::object& kcp);
    // Encode an HTTP/XHTTP Config protobuf from the "httpSettings" JSON object.
    static std::string encodeHTTPSettings(const boost::json::object& http);
    // Encode an xhttp.Config protobuf from the "xhttpSettings" JSON object.
    static std::string encodeXHTTPSettings(const boost::json::object& xhttp);
    // Encode a splithttp.Config protobuf from the "splithttpSettings" JSON object.
    static std::string encodeSplitHTTPSettings(const boost::json::object& splithttp);
    // Encode a StreamConfig protobuf from the full "streamSettings" JSON object.
    static std::string encodeStreamConfig(const boost::json::object& stream);
    // Encode a MultiplexConfig protobuf from the "mux" JSON object.
    static std::string encodeMultiplexConfig(const boost::json::object& mux);
    // Encode a SenderConfig protobuf combining optional streamSettings and mux.
    // Null pointers are treated as absent fields.
    static std::string encodeSenderSettings(const boost::json::object* streamSettings,
                                            const boost::json::object* mux);
#endif
};

} // namespace xray

#endif // XRAY_API_H
