#include <algorithm>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifdef USE_GRPC_API
#include <winsock2.h>
#include <ws2tcpip.h>
#include <boost/json.hpp>
#include <cstring>
#endif
#include <windows.h>
#include "XrayApi.h"
#include "Logger.h"

namespace xray {

static std::string normalizePath(const std::string& path) {
    std::string result = path;
    for (char& c : result) {
        if (c == '/') c = '\\';
    }
    return result;
}

// -------------------------------------------------------------------
// Helper: run a process with CREATE_NO_WINDOW and capture stdout,
// optionally providing stdin data (e.g., piping JSON into xray api).
// Returns exit code (negative on creation failure).
// -------------------------------------------------------------------
static int runProcess(const std::string& cmd, std::string& output,
                      const std::string* stdinData = nullptr) {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

    // Create stdout pipe (child writes, parent reads)
    HANDLE hOutRead = nullptr, hOutWrite = nullptr;
    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0))
        return -1;
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);

    // Create stdin pipe only when input is provided
    HANDLE hInRead = nullptr, hInWrite = nullptr;
    if (stdinData) {
        if (!CreatePipe(&hInRead, &hInWrite, &sa, 0)) {
            CloseHandle(hOutRead); CloseHandle(hOutWrite);
            return -1;
        }
        SetHandleInformation(hInWrite, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOA si = {0};
    si.cb           = sizeof(si);
    si.dwFlags      = STARTF_USESTDHANDLES;
    si.hStdOutput   = hOutWrite;
    si.hStdError    = hOutWrite;  // merge stderr with stdout
    si.hStdInput    = stdinData ? hInRead : INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi = {0};

    // CreateProcessA modifies the command line buffer
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr,
                                  TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                  &si, &pi);

    // Parent no longer needs the child-side write/stdin ends
    CloseHandle(hOutWrite);
    if (hInRead) CloseHandle(hInRead);

    if (!created) {
        CloseHandle(hOutRead);
        if (hInWrite) CloseHandle(hInWrite);
        return -1;
    }

    // Write stdin data if requested (pipe JSON to xray api ado)
    if (stdinData && hInWrite) {
        DWORD written = 0;
        WriteFile(hInWrite, stdinData->c_str(), (DWORD)stdinData->size(),
                  &written, nullptr);
        CloseHandle(hInWrite);
    }

    // Read all stdout output
    char buf[4096];
    DWORD bytesRead = 0;
    output.clear();
    while (ReadFile(hOutRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) &&
           bytesRead > 0) {
        buf[bytesRead] = '\0';
        output += buf;
    }

    WaitForSingleObject(pi.hProcess, 5000);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(hOutRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

XrayApi::XrayApi(const std::string& xrayPath, const std::string& serverAddr)
    : xrayPath_(normalizePath(xrayPath)), serverAddr_(serverAddr) {}

bool XrayApi::runCommand(const std::string& args, std::string& output) {
    std::string cmd = "\"" + xrayPath_ + "\" " + args;
    int exitCode = runProcess(cmd, output);
    if (exitCode < 0) {
        lastError_ = "Failed to run command: " + cmd;
        return false;
    }
    return exitCode == 0;
}

bool XrayApi::addOutbound(const std::string& outboundJson, const std::string& tag, std::string& resultOutput) {
    Logger::write("[XrayApi] addOutbound called: tag=" + tag + ", xrayPath=" + xrayPath_ + ", serverAddr=" + serverAddr_, LogLevel::DEBUG);
    Logger::write("[XrayApi] outbound JSON: " + outboundJson, LogLevel::DEBUG);
    
    std::string normalizedXray = xrayPath_;
    for (char& c : normalizedXray) {
        if (c == '/') c = '\\';
    }
    
    std::string normalizedServer = serverAddr_;
    for (char& c : normalizedServer) {
        if (c == '/') c = '\\';
    }
    
    // Run xray api ado with JSON as stdin (no cmd.exe wrapper, no console flash)
    std::string cmd = "\"" + normalizedXray + "\" api ado --server=" + normalizedServer + " stdin:";
    Logger::write("[XrayApi] command: " + cmd, LogLevel::DEBUG);

    std::string output;
    int exitCode = runProcess(cmd, output, &outboundJson);
    if (exitCode < 0) {
        lastError_ = "Failed to run xray api ado command";
        return false;
    }

    resultOutput = output;

    bool success = (exitCode == 0) || (output.find("adding") != std::string::npos);
    
    if (!success) {
        lastError_ = "xray api ado failed with code: " + std::to_string(exitCode) + " output: " + output;
        Logger::write("[XrayApi] addOutbound FAILED: exitCode=" + std::to_string(exitCode) + ", output=" + output, LogLevel::ERR);
        Logger::write("[XrayApi] addOutbound FAILED error: " + lastError_, LogLevel::ERR);
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    Logger::write("[XrayApi] addOutbound SUCCESS for tag: " + tag, LogLevel::DEBUG);

    // List outbounds after add to confirm
    std::string lsoCmd = "\"" + xrayPath_ + "\" api lso --server=" + serverAddr_;
    std::string lsoOutput;
    int lsoCode = runProcess(lsoCmd, lsoOutput);
    if (lsoCode >= 0 && !lsoOutput.empty()) {
        resultOutput += "\n[Outbounds]:\n" + lsoOutput;
    }

    return true;
}

bool XrayApi::removeOutbound(const std::string& tag) {
    Logger::write("[XrayApi] removeOutbound called: tag=" + tag, LogLevel::DEBUG);
    
    std::string cleanTag;
    for (char c : tag) {
        if (c >= 'a' && c <= 'z') cleanTag += c;
        else if (c >= 'A' && c <= 'Z') cleanTag += c;
        else if (c >= '0' && c <= '9') cleanTag += c;
        else if (c == '_' || c == '-') cleanTag += c;
    }
    if (cleanTag.empty()) cleanTag = "proxy";
    
    Logger::write("[XrayApi] removeOutbound: cleaned tag=" + cleanTag + ", cmd tag=\"" + cleanTag + "\"", LogLevel::DEBUG);
    
    std::string cmd = "\"" + xrayPath_ + "\" api rmo --server " + serverAddr_ + " \"" + cleanTag + "\"";

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    PROCESS_INFORMATION pi = {0};

    // CreateProcessA modifies the command line buffer
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    BOOL success = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    
    if (!success) {
        lastError_ = "Failed to create process: " + std::to_string(GetLastError());
        return false;
    }

    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    Logger::write("[XrayApi] removeOutbound SUCCESS for tag: " + tag, LogLevel::DEBUG);

    return true;
}

bool XrayApi::listOutbounds() {
    std::string output;
    return listOutboundsResult(output);
}

bool XrayApi::listOutboundsResult(std::string& output) {
    std::string cmd = "\"" + xrayPath_ + "\" api lso --server " + serverAddr_;

    int exitCode = runProcess(cmd, output);
    if (exitCode < 0) {
        lastError_ = "Failed to run xray api lso command";
        return false;
    }

    // Log output for debugging
    if (!output.empty())
        Logger::write("[XrayApi] listOutboundsResult:\n" + output, LogLevel::DEBUG);

    return exitCode == 0;
}

bool XrayApi::ping(std::string& resultOutput) {
    std::string cmd = "\"" + xrayPath_ + "\" api lsi --server " + serverAddr_;

    std::string output;
    int exitCode = runProcess(cmd, output);
    if (exitCode < 0) {
        lastError_ = "Failed to run xray api lsi command";
        return false;
    }

    resultOutput = output;
    return exitCode == 0;
}

std::string XrayApi::getLastError() const {
    return lastError_;
}

// ============================================================
// Direct gRPC implementation (bypasses subprocess, ~2s savings per call)
// ============================================================
#ifdef USE_GRPC_API

// ----- Protobuf wire-format helpers -----

std::string XrayApi::encodeVarint(uint64_t value) {
    std::string result;
    do {
        unsigned char byte = value & 0x7F;
        value >>= 7;
        if (value) byte |= 0x80;
        result += static_cast<char>(byte);
    } while (value);
    return result;
}

std::string XrayApi::encodeVarintField(int fieldNumber, uint64_t value) {
    uint64_t key = (static_cast<uint64_t>(fieldNumber) << 3) | 0;
    std::string result = encodeVarint(key);
    result += encodeVarint(value);
    return result;
}

std::string XrayApi::encodeLengthDelimited(int fieldNumber, const std::string& data) {
    uint64_t key = (static_cast<uint64_t>(fieldNumber) << 3) | 2;
    std::string result = encodeVarint(key);
    result += encodeVarint(static_cast<uint64_t>(data.size()));
    result += data;
    return result;
}

std::string XrayApi::encodeString(int fieldNumber, const std::string& str) {
    return encodeLengthDelimited(fieldNumber, str);
}

// ----- Protobuf encoder helpers for complex messages -----

// Encode address as IPOrDomain { oneof: bytes ip=1 | string domain=2 }
std::string XrayApi::encodeIPOrDomain(const std::string& addr) {
    struct in_addr ipv4;
    if (inet_pton(AF_INET, addr.c_str(), &ipv4) == 1) {
        std::string ipBytes(reinterpret_cast<const char*>(&ipv4), 4);
        return encodeLengthDelimited(1, ipBytes);  // bytes ip = 1
    }
    struct in6_addr ipv6;
    if (inet_pton(AF_INET6, addr.c_str(), &ipv6) == 1) {
        std::string ipBytes(reinterpret_cast<const char*>(&ipv6), 16);
        return encodeLengthDelimited(1, ipBytes);  // bytes ip = 1
    }
    return encodeString(2, addr);  // string domain = 2
}

// Build User protobuf with account as TypedMessage
// User { uint32 level=1, string email=2, TypedMessage account=3 }
std::string XrayApi::encodeUser(const std::string& accountTypeUrl,
                                const std::string& accountProto) {
    std::string typedMsg;
    typedMsg += encodeString(1, accountTypeUrl);     // TypedMessage.type
    typedMsg += encodeString(2, accountProto);        // TypedMessage.value (bytes)

    std::string user;
    user += encodeVarintField(1, 0);                  // level = 0
    user += encodeLengthDelimited(3, typedMsg);       // account
    return user;
}

// Build ServerEndpoint { IPOrDomain address=1, uint32 port=2, User user=3 }
std::string XrayApi::encodeServerEndpoint(const std::string& addr, int port,
                                          const std::string& userProto) {
    std::string ep;
    ep += encodeLengthDelimited(1, encodeIPOrDomain(addr));  // address
    ep += encodeVarintField(2, static_cast<uint64_t>(port));  // port
    ep += encodeLengthDelimited(3, userProto);                // user
    return ep;
}

// ---- Account protobuf encoders ----

// VMess Account { string id=1, SecurityConfig security_settings=3 }
// SecurityConfig { SecurityType type=1 }:
//   UNKNOWN=0, AUTO=2, AES128_GCM=3, CHACHA20_POLY1305=4
std::string XrayApi::encodeVMessAccount(const std::string& id, int securityType) {
    std::string secCfg = encodeVarintField(1, static_cast<uint64_t>(securityType));
    std::string account;
    account += encodeString(1, id);
    account += encodeLengthDelimited(3, secCfg);  // security_settings
    return account;
}

// VLESS Account { string id=1, string flow=2, string encryption=3 }
std::string XrayApi::encodeVLESSAccount(const std::string& id,
                                        const std::string& flow,
                                        const std::string& encryption) {
    std::string account;
    account += encodeString(1, id);
    if (!flow.empty()) account += encodeString(2, flow);
    if (!encryption.empty()) account += encodeString(3, encryption);
    return account;
}

// Trojan Account { string password=1 }
std::string XrayApi::encodeTrojanAccount(const std::string& password) {
    return encodeString(1, password);
}

// SS Account { string password=1, CipherType cipher_type=2, bool iv_check=3 }
// CipherType: UNKNOWN=0, AES_128_GCM=5, AES_256_GCM=6,
//             CHACHA20_IETF_POLY1305=7, XCHACHA20_IETF_POLY1305=8, NONE=9
std::string XrayApi::encodeShadowsocksAccount(const std::string& password,
                                              const std::string& method) {
    uint64_t ct = 0;  // UNKNOWN
    if (method == "aes-128-gcm")                ct = 5;
    else if (method == "aes-256-gcm")           ct = 6;
    else if (method == "chacha20-ietf-poly1305") ct = 7;
    else if (method == "xchacha20-ietf-poly1305") ct = 8;
    else if (method == "none")                  ct = 9;

    std::string account;
    account += encodeString(1, password);
    account += encodeVarintField(2, ct);   // cipher_type
    account += encodeVarintField(3, 1);    // iv_check = true
    return account;
}

// SOCKS Account { string username=1, string password=2 }
std::string XrayApi::encodeSocksAccount(const std::string& username,
                                        const std::string& password) {
    std::string account;
    if (!username.empty()) account += encodeString(1, username);
    if (!password.empty()) account += encodeString(2, password);
    return account;
}

// HTTP Account { string username=1, string password=2 }
std::string XrayApi::encodeHTTPAccount(const std::string& username,
                                       const std::string& password) {
    std::string account;
    if (!username.empty()) account += encodeString(1, username);
    if (!password.empty()) account += encodeString(2, password);
    return account;
}

std::string XrayApi::jsonConfigToProtobuf(const std::string& typeUrl,
                                          const std::string& valueJson) {
    // Convert JSON settings to protobuf wire format for known outbound types.
    // For unknown types, return empty (caller sends raw JSON bytes as before).
    if (valueJson.empty()) return {};

    const bool isFreedom    = (typeUrl == "xray.proxy.freedom.Config");
    const bool isBlackhole  = (typeUrl == "xray.proxy.blackhole.Config");
    const bool isVMess      = (typeUrl == "xray.proxy.vmess.outbound.Config");
    const bool isVLESS      = (typeUrl == "xray.proxy.vless.outbound.Config");
    const bool isTrojan     = (typeUrl == "xray.proxy.trojan.ClientConfig");
    const bool isShadowsocks = (typeUrl == "xray.proxy.shadowsocks.ClientConfig");
    const bool isSocks      = (typeUrl == "xray.proxy.socks.ClientConfig");
    const bool isHTTP       = (typeUrl == "xray.proxy.http.ClientConfig");

    if (!isFreedom && !isBlackhole && !isVMess && !isVLESS && !isTrojan &&
        !isShadowsocks && !isSocks && !isHTTP) {
        return {};
    }

    boost::system::error_code ec;
    boost::json::value jv = boost::json::parse(valueJson, ec);
    if (ec) return {};
    if (!jv.is_object()) return {};

    const boost::json::object& obj = jv.as_object();

    // Extract the "settings" sub-object if present
    boost::json::object settings;
    auto sit = obj.find("settings");
    if (sit != obj.end() && sit->value().is_object()) {
        settings = sit->value().as_object();
    }

    std::string result;

    if (isFreedom) {
        // freedom.Config protobuf fields:
        //   1: domain_strategy (DomainStrategy enum, varint)
        //   4: user_level (uint32, varint)
        //   6: proxy_protocol (uint32, varint)

        auto it = settings.find("domain_strategy");
        if (it != settings.end() && it->value().is_int64()) {
            int64_t val = it->value().as_int64();
            if (val != 0) {
                result += encodeVarintField(1,
                    static_cast<uint64_t>(val));
            }
        }

        it = settings.find("user_level");
        if (it != settings.end() && it->value().is_int64()) {
            int64_t val = it->value().as_int64();
            if (val != 0) {
                result += encodeVarintField(4,
                    static_cast<uint64_t>(val));
            }
        }

        it = settings.find("proxy_protocol");
        if (it != settings.end() && it->value().is_int64()) {
            int64_t val = it->value().as_int64();
            if (val != 0) {
                result += encodeVarintField(6,
                    static_cast<uint64_t>(val));
            }
        }
        return result;
    }

    if (isBlackhole) {
        // blackhole.Config: field 1 (response TypedMessage) skipped for MVP.
        // Empty protobuf = no response set (default behavior).
        return {};
    }

    // ---- Server-based protocols: extract server array from JSON ----
    boost::json::array servers;
    if (isVMess || isVLESS) {
        auto vit = settings.find("vnext");
        if (vit != settings.end() && vit->value().is_array() &&
            !vit->value().as_array().empty()) {
            servers = vit->value().as_array();
        }
    } else {
        auto sit2 = settings.find("servers");
        if (sit2 != settings.end() && sit2->value().is_array() &&
            !sit2->value().as_array().empty()) {
            servers = sit2->value().as_array();
        }
    }

    if (servers.empty()) return {};

    const boost::json::object& srv = servers[0].as_object();

    // Extract address and port
    std::string addr;
    if (srv.contains("address") && srv.at("address").is_string())
        addr = srv.at("address").as_string().c_str();
    int port = 0;
    if (srv.contains("port")) {
        const auto& pv = srv.at("port");
        if (pv.is_int64())
            port = static_cast<int>(pv.as_int64());
        else if (pv.is_uint64())
            port = static_cast<int>(pv.as_uint64());
    }
    if (addr.empty() || port == 0) return {};

    // ---- Build account proto ----
    std::string accountProto;
    std::string accountTypeUrl;

    if (isVMess) {
        accountTypeUrl = "xray.proxy.vmess.Account";
        std::string id = "00000000-0000-0000-0000-000000000000";
        int securityType = 2;  // AUTO
        auto uit = srv.find("users");
        if (uit != srv.end() && uit->value().is_array() &&
            !uit->value().as_array().empty()) {
            const boost::json::object& u = uit->value().as_array()[0].as_object();
            if (u.contains("id") && u.at("id").is_string())
                id = u.at("id").as_string().c_str();
            if (u.contains("security") && u.at("security").is_string()) {
                std::string sec = u.at("security").as_string().c_str();
                if (sec == "auto")                     securityType = 2;
                else if (sec == "aes-128-gcm")         securityType = 3;
                else if (sec == "chacha20-poly1305")   securityType = 4;
                else                                   securityType = 0;  // UNKNOWN
            }
        }
        accountProto = encodeVMessAccount(id, securityType);

    } else if (isVLESS) {
        accountTypeUrl = "xray.proxy.vless.Account";
        std::string id, flow, encryption;
        auto uit = srv.find("users");
        if (uit != srv.end() && uit->value().is_array() &&
            !uit->value().as_array().empty()) {
            const boost::json::object& u = uit->value().as_array()[0].as_object();
            if (u.contains("id") && u.at("id").is_string())
                id = u.at("id").as_string().c_str();
            if (u.contains("flow") && u.at("flow").is_string())
                flow = u.at("flow").as_string().c_str();
            if (u.contains("encryption") && u.at("encryption").is_string())
                encryption = u.at("encryption").as_string().c_str();
        }
        accountProto = encodeVLESSAccount(id, flow, encryption);

    } else if (isTrojan) {
        accountTypeUrl = "xray.proxy.trojan.Account";
        std::string password;
        if (srv.contains("password") && srv.at("password").is_string())
            password = srv.at("password").as_string().c_str();
        accountProto = encodeTrojanAccount(password);

    } else if (isShadowsocks) {
        accountTypeUrl = "xray.proxy.shadowsocks.Account";
        std::string password, method;
        if (srv.contains("password") && srv.at("password").is_string())
            password = srv.at("password").as_string().c_str();
        if (srv.contains("method") && srv.at("method").is_string())
            method = srv.at("method").as_string().c_str();
        accountProto = encodeShadowsocksAccount(password, method);

    } else if (isSocks) {
        accountTypeUrl = "xray.proxy.socks.Account";
        std::string username, password;
        auto uit = srv.find("users");
        if (uit != srv.end() && uit->value().is_array() &&
            !uit->value().as_array().empty()) {
            const boost::json::object& u = uit->value().as_array()[0].as_object();
            if (u.contains("user") && u.at("user").is_string())
                username = u.at("user").as_string().c_str();
            if (u.contains("pass") && u.at("pass").is_string())
                password = u.at("pass").as_string().c_str();
        }
        accountProto = encodeSocksAccount(username, password);

    } else if (isHTTP) {
        accountTypeUrl = "xray.proxy.http.Account";
        std::string username, password;
        if (srv.contains("user") && srv.at("user").is_string())
            username = srv.at("user").as_string().c_str();
        if (srv.contains("pass") && srv.at("pass").is_string())
            password = srv.at("pass").as_string().c_str();
        accountProto = encodeHTTPAccount(username, password);
    }

    if (accountProto.empty()) return {};

    // ---- Build ServerEndpoint from address/port + account ----
    std::string userProto = encodeUser(accountTypeUrl, accountProto);
    std::string ep = encodeServerEndpoint(addr, port, userProto);

    // ---- Build enclosing config message ----
    // VMess outbound: Config { repeated ServerEndpoint Receiver = 1 }
    // VLESS outbound: Config { repeated ServerEndpoint vnext = 1 }
    // Trojan/SS/SOCKS/HTTP: ClientConfig { repeated ServerEndpoint server = 1 }
    // All use field number 1 with repeated (same wire format as singular).
    result += encodeLengthDelimited(1, ep);

    return result;
}

bool XrayApi::parseOutboundJson(const std::string& outboundJson,
                                std::string& tagOut,
                                std::string& typeUrl,
                                std::string& valueJson,
                                std::string* streamSettingsJson,
                                std::string* muxJson) {
    // Parse the outbound JSON: ConfigGenerator produces the full xray config format
    //   { "outbounds": [ { "tag": "...", "protocol": "freedom",
    //                      "settings": {...}, ... } ] }
    // Extract tag + protocol, map protocol to protobuf type URL, and serialize
    // the remaining config (minus tag) as TypedMessage.value JSON bytes.
    namespace bj = boost::json;
    try {
        bj::value root = bj::parse(outboundJson);
        bj::object& rootObj = root.as_object();
        bj::array& outboundsArr = rootObj.at("outbounds").as_array();
        bj::object& outboundObj = outboundsArr.at(0).as_object();

        tagOut = bj::value_to<std::string>(outboundObj.at("tag"));

        // Map protocol to protobuf type URL
        std::string protocol = bj::value_to<std::string>(outboundObj.at("protocol"));
        if (protocol == "freedom") {
            typeUrl = "xray.proxy.freedom.Config";
        } else if (protocol == "blackhole") {
            typeUrl = "xray.proxy.blackhole.Config";
        } else if (protocol == "vmess") {
            typeUrl = "xray.proxy.vmess.outbound.Config";
        } else if (protocol == "vless") {
            typeUrl = "xray.proxy.vless.outbound.Config";
        } else if (protocol == "trojan") {
            typeUrl = "xray.proxy.trojan.ClientConfig";
        } else if (protocol == "shadowsocks") {
            typeUrl = "xray.proxy.shadowsocks.ClientConfig";
        } else if (protocol == "socks") {
            typeUrl = "xray.proxy.socks.ClientConfig";
        } else if (protocol == "http") {
            typeUrl = "xray.proxy.http.ClientConfig";
        } else {
            typeUrl = "xray.proxy.outbound.Config";
        }

        // Build config JSON: copy outbound object, remove tag (already set via
        // protobuf tag field), serialize as TypedMessage.value bytes.
        bj::object configObj = outboundObj;
        configObj.erase("tag");
        valueJson = bj::serialize(configObj);

        // Optional: expose streamSettings and mux as serialized JSON so the
        // caller can build OutboundHandlerConfig.sender_settings (field 2).
        if (streamSettingsJson != nullptr) {
            const boost::json::value* streamSettings =
                outboundObj.if_contains("streamSettings");
            if (streamSettings != nullptr && streamSettings->is_object()) {
                *streamSettingsJson = bj::serialize(streamSettings->as_object());
            }
        }
        if (muxJson != nullptr) {
            const boost::json::value* mux = outboundObj.if_contains("mux");
            if (mux != nullptr && mux->is_object()) {
                *muxJson = bj::serialize(mux->as_object());
            }
        }

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// ----- Stream settings encoders (SenderConfig for AddOutbound) -----
// Wire formats mirror Xray-core protos under Xray-core/transport/internet/*.
// JSON streamSettings members (network/security/wsSettings/...) are converted
// to SenderConfig.stream_settings (field 2) and .multiplex_settings (field 4)
// of OutboundHandlerOutbound.

std::string XrayApi::encodeWebSocketConfig(const boost::json::object& ws) {
    // WebSocketConfig: host=1 path=2 header(map)=3 accept_proxy_protocol=4 ed=5
    std::string result;
    const boost::json::value* host = ws.if_contains("host");
    if (host != nullptr && host->is_string()) {
        result += encodeString(1, host->as_string().c_str());
    }
    const boost::json::value* path = ws.if_contains("path");
    if (path != nullptr && path->is_string()) {
        result += encodeString(2, path->as_string().c_str());
    }
    const boost::json::value* headers = ws.if_contains("headers");
    if (headers != nullptr && headers->is_object()) {
        const boost::json::object& headerObj = headers->as_object();
        for (const boost::json::key_value_pair& kv : headerObj) {
            if (!kv.value().is_string()) {
                continue;
            }
            // map entry: key=1 value=2, wrapped in field 3
            std::string entry;
            entry += encodeString(1, std::string(kv.key()));
            entry += encodeString(2, kv.value().as_string().c_str());
            result += encodeLengthDelimited(3, entry);
        }
    }
    const boost::json::value* acceptProxy = ws.if_contains("acceptProxyProtocol");
    if (acceptProxy != nullptr && acceptProxy->is_bool()) {
        result += encodeVarintField(4, acceptProxy->as_bool() ? 1 : 0);
    }
    const boost::json::value* ed = ws.if_contains("ed");
    if (ed != nullptr && ed->is_uint64()) {
        result += encodeVarintField(5, ed->as_uint64());
    }
    return result;
}

std::string XrayApi::encodeTLSSettings(const boost::json::object& tls) {
    // Config: allow_insecure=1 server_name=3 next_protocol=4 min_version=7
    //         max_version=8 cipher_suites=9 fingerprint=11 reject_unknown_sni=12
    std::string result;
    const boost::json::value* allowInsecure = tls.if_contains("allowInsecure");
    if (allowInsecure != nullptr && allowInsecure->is_bool()) {
        result += encodeVarintField(1, allowInsecure->as_bool() ? 1 : 0);
    }
    const boost::json::value* serverName = tls.if_contains("serverName");
    if (serverName == nullptr) {
        serverName = tls.if_contains("sni");
    }
    if (serverName != nullptr && serverName->is_string()) {
        result += encodeString(3, serverName->as_string().c_str());
    }
    const boost::json::value* alpn = tls.if_contains("alpn");
    if (alpn != nullptr && alpn->is_array()) {
        const boost::json::array& alpnArr = alpn->as_array();
        for (const boost::json::value& item : alpnArr) {
            if (item.is_string()) {
                result += encodeString(4, item.as_string().c_str());
            }
        }
    }
    const boost::json::value* minVersion = tls.if_contains("minVersion");
    if (minVersion != nullptr && minVersion->is_string()) {
        result += encodeString(7, minVersion->as_string().c_str());
    }
    const boost::json::value* maxVersion = tls.if_contains("maxVersion");
    if (maxVersion != nullptr && maxVersion->is_string()) {
        result += encodeString(8, maxVersion->as_string().c_str());
    }
    const boost::json::value* cipher = tls.if_contains("cipher");
    if (cipher != nullptr && cipher->is_string()) {
        result += encodeString(9, cipher->as_string().c_str());
    }
    const boost::json::value* fingerprint = tls.if_contains("fingerprint");
    if (fingerprint != nullptr && fingerprint->is_string()) {
        result += encodeString(11, fingerprint->as_string().c_str());
    }
    const boost::json::value* rejectUnknownSni = tls.if_contains("rejectUnknownSni");
    if (rejectUnknownSni != nullptr && rejectUnknownSni->is_bool()) {
        result += encodeVarintField(12, rejectUnknownSni->as_bool() ? 1 : 0);
    }
    return result;
}

std::string XrayApi::encodeRealitySettings(const boost::json::object& reality) {
    // Config: show=1 dest=2 xver=4 server_names=5 private_key=6
    //         fingerprint=21 server_name=22 public_key=23 short_id=24 spider_x=26
    std::string result;
    const boost::json::value* show = reality.if_contains("show");
    if (show != nullptr && show->is_bool()) {
        result += encodeVarintField(1, show->as_bool() ? 1 : 0);
    }
    const boost::json::value* dest = reality.if_contains("dest");
    if (dest != nullptr && dest->is_string()) {
        result += encodeString(2, dest->as_string().c_str());
    }
    const boost::json::value* xver = reality.if_contains("xver");
    if (xver != nullptr && xver->is_uint64()) {
        result += encodeVarintField(4, xver->as_uint64());
    }
    const boost::json::value* serverNames = reality.if_contains("serverNames");
    if (serverNames != nullptr && serverNames->is_array()) {
        const boost::json::array& namesArr = serverNames->as_array();
        for (const boost::json::value& item : namesArr) {
            if (item.is_string()) {
                result += encodeString(5, item.as_string().c_str());
            }
        }
    }
    const boost::json::value* privateKey = reality.if_contains("privateKey");
    if (privateKey != nullptr && privateKey->is_string()) {
        result += encodeString(6, privateKey->as_string().c_str());
    }
    const boost::json::value* fingerprint = reality.if_contains("fingerprint");
    if (fingerprint != nullptr && fingerprint->is_string()) {
        result += encodeString(21, fingerprint->as_string().c_str());
    }
    const boost::json::value* serverName = reality.if_contains("serverName");
    if (serverName != nullptr && serverName->is_string()) {
        result += encodeString(22, serverName->as_string().c_str());
    }
    const boost::json::value* publicKey = reality.if_contains("publicKey");
    if (publicKey != nullptr && publicKey->is_string()) {
        result += encodeString(23, publicKey->as_string().c_str());
    }
    const boost::json::value* shortId = reality.if_contains("shortId");
    if (shortId != nullptr && shortId->is_string()) {
        result += encodeString(24, shortId->as_string().c_str());
    }
    const boost::json::value* spiderX = reality.if_contains("spiderX");
    if (spiderX != nullptr && spiderX->is_string()) {
        result += encodeString(26, spiderX->as_string().c_str());
    }
    return result;
}

std::string XrayApi::encodeGRPCSettings(const boost::json::object& grpc) {
    // Config (package xray.transport.internet.grpc.encoding):
    //   authority=1 service_name=2 multi_mode=3 idle_timeout=4
    //   health_check_timeout=5 permit_without_stream=6 initial_windows_size=7 user_agent=8
    std::string result;
    const boost::json::value* authority = grpc.if_contains("authority");
    if (authority != nullptr && authority->is_string()) {
        result += encodeString(1, authority->as_string().c_str());
    }
    const boost::json::value* serviceName = grpc.if_contains("serviceName");
    if (serviceName != nullptr && serviceName->is_string()) {
        result += encodeString(2, serviceName->as_string().c_str());
    }
    const boost::json::value* multiMode = grpc.if_contains("multiMode");
    if (multiMode != nullptr && multiMode->is_bool()) {
        result += encodeVarintField(3, multiMode->as_bool() ? 1 : 0);
    }
    const boost::json::value* permitWithoutStream = grpc.if_contains("permitWithoutStream");
    if (permitWithoutStream != nullptr && permitWithoutStream->is_bool()) {
        result += encodeVarintField(6, permitWithoutStream->as_bool() ? 1 : 0);
    }
    const boost::json::value* userAgent = grpc.if_contains("userAgent");
    if (userAgent != nullptr && userAgent->is_string()) {
        result += encodeString(8, userAgent->as_string().c_str());
    }
    return result;
}

std::string XrayApi::encodeKCPSettings(const boost::json::object& kcp) {
    // Config: mtu=1(MTU) tti=2(TTI) uplink_capacity=3 downlink_capacity=4
    //         congestion=5(bool) write_buffer=6 read_buffer=7 header_config=8 seed=10
    // MTU/TTI/Capacity wrap the value in their own field 1.
    std::string result;
    const boost::json::value* mtu = kcp.if_contains("mtu");
    if (mtu != nullptr && mtu->is_uint64()) {
        result += encodeLengthDelimited(1, encodeVarintField(1, mtu->as_uint64()));
    }
    const boost::json::value* tti = kcp.if_contains("tti");
    if (tti != nullptr && tti->is_uint64()) {
        result += encodeLengthDelimited(2, encodeVarintField(1, tti->as_uint64()));
    }
    const boost::json::value* uplinkCapacity = kcp.if_contains("uplinkCapacity");
    if (uplinkCapacity != nullptr && uplinkCapacity->is_uint64()) {
        result += encodeLengthDelimited(3, encodeVarintField(1, uplinkCapacity->as_uint64()));
    }
    const boost::json::value* downlinkCapacity = kcp.if_contains("downlinkCapacity");
    if (downlinkCapacity != nullptr && downlinkCapacity->is_uint64()) {
        result += encodeLengthDelimited(4, encodeVarintField(1, downlinkCapacity->as_uint64()));
    }
    const boost::json::value* congestion = kcp.if_contains("congestion");
    if (congestion != nullptr && congestion->is_bool()) {
        result += encodeVarintField(5, congestion->as_bool() ? 1 : 0);
    }
    const boost::json::value* writeBuffer = kcp.if_contains("writeBuffer");
    if (writeBuffer != nullptr && writeBuffer->is_uint64()) {
        result += encodeLengthDelimited(6, encodeVarintField(1, writeBuffer->as_uint64()));
    }
    const boost::json::value* readBuffer = kcp.if_contains("readBuffer");
    if (readBuffer != nullptr && readBuffer->is_uint64()) {
        result += encodeLengthDelimited(7, encodeVarintField(1, readBuffer->as_uint64()));
    }
    const boost::json::value* seed = kcp.if_contains("seed");
    if (seed != nullptr && seed->is_string()) {
        // EncryptionSeed { string seed = 1; }
        std::string seedMsg = encodeString(1, seed->as_string().c_str());
        result += encodeLengthDelimited(10, seedMsg);
    }
    // header_config (field 8) omitted: covers "none" (default) only.
    return result;
}

std::string XrayApi::encodeHTTPSettings(const boost::json::object& http) {
    // Modern "http" transport maps to httpupgrade.Config:
    //   host=1 path=2 header(map)=3 ed=5
    std::string result;
    const boost::json::value* host = http.if_contains("host");
    if (host != nullptr && host->is_string()) {
        result += encodeString(1, host->as_string().c_str());
    }
    const boost::json::value* path = http.if_contains("path");
    if (path != nullptr && path->is_string()) {
        result += encodeString(2, path->as_string().c_str());
    }
    const boost::json::value* headers = http.if_contains("headers");
    if (headers != nullptr && headers->is_object()) {
        const boost::json::object& headerObj = headers->as_object();
        for (const boost::json::key_value_pair& kv : headerObj) {
            if (!kv.value().is_string()) {
                continue;
            }
            std::string entry;
            entry += encodeString(1, std::string(kv.key()));
            entry += encodeString(2, kv.value().as_string().c_str());
            result += encodeLengthDelimited(3, entry);
        }
    }
    const boost::json::value* ed = http.if_contains("ed");
    if (ed != nullptr && ed->is_uint64()) {
        result += encodeVarintField(5, ed->as_uint64());
    }
    return result;
}

std::string XrayApi::encodeStreamConfig(const boost::json::object& stream) {
    // StreamConfig: protocol_name=5 transport_settings=2(repeated TransportConfig)
    //               security_type=3 security_settings=4(repeated TypedMessage)
    //               address=8 port=9
    std::string result;

    // JSON network string vs Xray registered protocol name.
    std::string network;
    const boost::json::value* networkValue = stream.if_contains("network");
    if (networkValue != nullptr && networkValue->is_string()) {
        network = std::string(networkValue->as_string().c_str());
    }
    std::string protocolName;
    if (network == "ws") {
        protocolName = "websocket";
    } else if (network == "kcp") {
        protocolName = "mkcp";
    } else if (network == "grpc") {
        protocolName = "grpc";
    } else if (network == "http") {
        protocolName = "httpupgrade";
    } else if (network == "tcp" || network == "httpupgrade"
               || network == "splithttp" || network == "hysteria") {
        protocolName = network;
    }
    if (!protocolName.empty()) {
        result += encodeString(5, protocolName);
    }

    // transport_settings: only for non-plain transports with a settings object.
    const boost::json::value* settingsValue = nullptr;
    std::string transportTypeUrl;
    if (network == "ws") {
        settingsValue = stream.if_contains("wsSettings");
        transportTypeUrl = "xray.transport.internet.websocket.Config";
    } else if (network == "grpc") {
        settingsValue = stream.if_contains("grpcSettings");
        transportTypeUrl = "xray.transport.internet.grpc.encoding.Config";
    } else if (network == "kcp") {
        settingsValue = stream.if_contains("kcpSettings");
        transportTypeUrl = "xray.transport.internet.kcp.Config";
    } else if (network == "http") {
        settingsValue = stream.if_contains("httpSettings");
        transportTypeUrl = "xray.transport.internet.httpupgrade.Config";
    }

    if (settingsValue != nullptr && settingsValue->is_object()
        && !protocolName.empty()) {
        const boost::json::object& settingsObj = settingsValue->as_object();
        std::string encodedSettings;
        if (network == "ws") {
            encodedSettings = encodeWebSocketConfig(settingsObj);
        } else if (network == "grpc") {
            encodedSettings = encodeGRPCSettings(settingsObj);
        } else if (network == "kcp") {
            encodedSettings = encodeKCPSettings(settingsObj);
        } else if (network == "http") {
            encodedSettings = encodeHTTPSettings(settingsObj);
        }
        if (!encodedSettings.empty()) {
            std::string typedMsg;
            typedMsg += encodeString(1, transportTypeUrl);
            typedMsg += encodeString(2, encodedSettings);
            std::string transportConfig;
            transportConfig += encodeLengthDelimited(2, typedMsg);
            transportConfig += encodeString(3, protocolName);
            result += encodeLengthDelimited(2, transportConfig);
        }
    }

    // security_type + security_settings.
    std::string security;
    const boost::json::value* securityValue = stream.if_contains("security");
    if (securityValue != nullptr && securityValue->is_string()) {
        security = std::string(securityValue->as_string().c_str());
    }
    if (security == "tls" || security == "reality") {
        result += encodeString(3, security);
        const boost::json::value* secSettingsValue = nullptr;
        std::string secTypeUrl;
        std::string encodedSecSettings;
        if (security == "tls") {
            secSettingsValue = stream.if_contains("tlsSettings");
            secTypeUrl = "xray.transport.internet.tls.Config";
            if (secSettingsValue != nullptr && secSettingsValue->is_object()) {
                encodedSecSettings = encodeTLSSettings(secSettingsValue->as_object());
            }
        } else {
            secSettingsValue = stream.if_contains("realitySettings");
            secTypeUrl = "xray.transport.internet.reality.Config";
            if (secSettingsValue != nullptr && secSettingsValue->is_object()) {
                encodedSecSettings = encodeRealitySettings(secSettingsValue->as_object());
            }
        }
        std::string secTypedMsg;
        secTypedMsg += encodeString(1, secTypeUrl);
        secTypedMsg += encodeString(2, encodedSecSettings);
        result += encodeLengthDelimited(4, secTypedMsg);
    }

    const boost::json::value* address = stream.if_contains("address");
    if (address != nullptr && address->is_string()) {
        result += encodeLengthDelimited(8, encodeIPOrDomain(address->as_string().c_str()));
    }
    const boost::json::value* port = stream.if_contains("port");
    if (port != nullptr) {
        if (port->is_uint64()) {
            result += encodeVarintField(9, port->as_uint64());
        } else if (port->is_int64()) {
            const int64_t portValue = port->as_int64();
            if (portValue > 0) {
                result += encodeVarintField(9, static_cast<uint64_t>(portValue));
            }
        }
        // non-numeric / non-positive port omitted: defaults apply on the Xray side.
    }
    return result;
}

std::string XrayApi::encodeMultiplexConfig(const boost::json::object& mux) {
    // MultiplexingConfig: enabled=1(bool) concurrency=2(int32)
    std::string result;
    const boost::json::value* enabled = mux.if_contains("enabled");
    if (enabled != nullptr && enabled->is_bool()) {
        result += encodeVarintField(1, enabled->as_bool() ? 1 : 0);
    }
    const boost::json::value* concurrency = mux.if_contains("concurrency");
    if (concurrency != nullptr && concurrency->is_int64()) {
        const int64_t concurrencyValue = concurrency->as_int64();
        if (concurrencyValue >= 0) {
            result += encodeVarintField(2, static_cast<uint64_t>(concurrencyValue));
        }
        // negative (-1 = auto) omitted: defaults apply on the Xray side.
    }
    return result;
}

std::string XrayApi::encodeSenderSettings(const boost::json::object* streamSettings,
                                          const boost::json::object* mux) {
    // SenderConfig: stream_settings=2 multiplex_settings=4
    std::string sender;
    if (streamSettings != nullptr) {
        sender += encodeLengthDelimited(2, encodeStreamConfig(*streamSettings));
    }
    if (mux != nullptr) {
        sender += encodeLengthDelimited(4, encodeMultiplexConfig(*mux));
    }
    return sender;
}

// ----- HPACK encoder (no Huffman, no dynamic table) -----

std::string XrayApi::encodeHpack(
    const std::vector<std::pair<std::string, std::string>>& headers) {

    static const std::vector<std::pair<std::string, int>> NAME_INDEX = {
        {":authority", 1}, {":method", 2}, {":path", 4},
        {":scheme", 6},    {"content-type", 31}, {"te", 57},
        {"grpc-timeout", 62}, {"grpc-encoding", 63},
    };

    std::string result;
    for (const auto& [name, value] : headers) {
        // Known name+value indexed entries
        if (name == ":method" && value == "POST") {
            result += static_cast<char>(0x80 | 3); continue;
        }
        if (name == ":method" && value == "GET") {
            result += static_cast<char>(0x80 | 2); continue;
        }
        if (name == ":scheme" && value == "http") {
            result += static_cast<char>(0x80 | 6); continue;
        }
        if (name == ":scheme" && value == "https") {
            result += static_cast<char>(0x80 | 7); continue;
        }

        // Literal without indexing — name in table or full literal
        int ni = -1;
        for (const auto& [n, idx] : NAME_INDEX) {
            if (n == name) { ni = idx; break; }
        }

        if (ni >= 0) {
            // Literal without indexing, name referenced by index
            if (ni < 15) {
                result += static_cast<char>(ni);
            } else {
                result += static_cast<char>(0x0F);
                result += encodeVarint(static_cast<uint64_t>(ni - 15));
            }
        } else {
            // Literal without indexing, full name sent
            result += static_cast<char>(0x00);
            result += encodeVarint(static_cast<uint64_t>(name.size()));
            result += name;
        }
        // Value
        result += encodeVarint(static_cast<uint64_t>(value.size()));
        result += value;
    }
    return result;
}

// ----- HTTP/2 frame helpers (private to this TU) -----
namespace {

void writeUint24(unsigned char* buf, uint32_t val) {
    buf[0] = static_cast<unsigned char>((val >> 16) & 0xFF);
    buf[1] = static_cast<unsigned char>((val >> 8) & 0xFF);
    buf[2] = static_cast<unsigned char>(val & 0xFF);
}
void writeUint32BE(unsigned char* buf, uint32_t val) {
    buf[0] = static_cast<unsigned char>((val >> 24) & 0xFF);
    buf[1] = static_cast<unsigned char>((val >> 16) & 0xFF);
    buf[2] = static_cast<unsigned char>((val >> 8) & 0xFF);
    buf[3] = static_cast<unsigned char>(val & 0xFF);
}

// Ensure Winsock is started (ref-counted, safe to call many times)
static bool ensureWinsock() {
    static bool started = false;
    if (!started) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        started = true;
    }
    return true;
}

} // anonymous namespace

// ----- gRPC transport -----

bool XrayApi::parseServerAddr(std::string& host, int& port) const {
    std::string addr = serverAddr_;
    // Strip "tcp://" prefix
    size_t pos = addr.find("://");
    if (pos != std::string::npos) addr = addr.substr(pos + 3);
    // Handle IPv6 addresses: "[::1]:port" or "[::1]"
    if (!addr.empty() && addr[0] == '[') {
        size_t close = addr.find(']');
        if (close == std::string::npos) return false;
        host = addr.substr(0, close + 1);  // includes brackets
        if (close + 1 >= addr.size() || addr[close + 1] != ':') return false;
        try {
            port = std::stoi(addr.substr(close + 2));
        } catch (...) { return false; }
        return true;
    }
    // IPv4 or hostname: "host:port"
    pos = addr.find(':');
    if (pos == std::string::npos) return false;
    host = addr.substr(0, pos);
    try {
        port = std::stoi(addr.substr(pos + 1));
    } catch (...) { return false; }
    return true;
}

int XrayApi::grpcConnect(const std::string& host, int port) {
    if (!ensureWinsock()) {
        lastError_ = "grpcConnect: WSAStartup failed";
        return -1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        lastError_ = "grpcConnect: socket() failed";
        return -1;
    }

    // 5-second receive timeout
    DWORD rcvTimeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, host.c_str(), &sin.sin_addr) != 1) {
        closesocket(sock);
        lastError_ = "grpcConnect: inet_pton failed";
        return -1;
    }

    if (connect(sock, reinterpret_cast<const sockaddr*>(&sin), sizeof(sin)) < 0) {
        int err = WSAGetLastError();
        closesocket(sock);
        lastError_ = "grpcConnect: connect() failed: " + std::to_string(err);
        return -1;
    }

    return static_cast<int>(sock);
}

void XrayApi::grpcClose(int sock) {
    if (sock >= 0) {
        shutdown(static_cast<SOCKET>(sock), SD_BOTH);
        closesocket(static_cast<SOCKET>(sock));
    }
}

bool XrayApi::grpcSendPreface(int sock) {
    SOCKET s = static_cast<SOCKET>(sock);

    // HTTP/2 connection preface
    const char PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    if (::send(s, PREFACE, 24, 0) != 24) {
        lastError_ = "grpcSendPreface: send preface failed";
        return false;
    }

    // Empty SETTINGS frame (size=0)
    unsigned char settings[9] = {
        0x00, 0x00, 0x00,    // length = 0
        0x04,                 // type = SETTINGS
        0x00,                 // flags = 0 (no ACK)
        0x00, 0x00, 0x00, 0x00 // stream_id = 0
    };
    if (::send(s, reinterpret_cast<const char*>(settings), 9, 0) != 9) {
        lastError_ = "grpcSendPreface: send SETTINGS failed";
        return false;
    }
    return true;
}

bool XrayApi::grpcSendReceive(int sock, const std::string& path,
                               const std::string& requestPayload,
                               std::string& response) {
    SOCKET s = static_cast<SOCKET>(sock);
    static const int STREAM_ID = 1;

    // ---- Send HEADERS frame ----
    std::vector<std::pair<std::string, std::string>> hdr = {
        {":method", "POST"},
        {":path", path},
        {":scheme", "http"},
        {"content-type", "application/grpc"},
        {"te", "trailers"}
    };
    std::string hpackData = encodeHpack(hdr);

    unsigned char hdrFrame[9];
    writeUint24(hdrFrame, static_cast<uint32_t>(hpackData.size()));
    hdrFrame[3] = 0x01;              // type = HEADERS
    hdrFrame[4] = 0x04;              // END_HEADERS
    writeUint32BE(&hdrFrame[5], STREAM_ID);

    if (::send(s, reinterpret_cast<const char*>(hdrFrame), 9, 0) != 9 ||
        ::send(s, hpackData.data(), static_cast<int>(hpackData.size()), 0) !=
            static_cast<int>(hpackData.size())) {
        lastError_ = "grpcSendReceive: send HEADERS failed";
        return false;
    }

    // ---- Send DATA frame (with 5-byte gRPC prefix) ----
    // Note: A proper gRPC implementation would also ACK the server's SETTINGS
    // (frame type 0x04, stream 0) before sending DATA, but Xray tolerates
    // the race because it processes HEADERS first and only checks SETTINGS ACK
    // on the next server event loop iteration.
    unsigned char msgPrefix[5] = {0};  // uncompressed, then big-endian length
    writeUint32BE(&msgPrefix[1], static_cast<uint32_t>(requestPayload.size()));

    std::string dataPayload;
    dataPayload.append(reinterpret_cast<const char*>(msgPrefix), 5);
    dataPayload.append(requestPayload);

    unsigned char dataFrame[9];
    writeUint24(dataFrame, static_cast<uint32_t>(dataPayload.size()));
    dataFrame[3] = 0x00;              // type = DATA
    dataFrame[4] = 0x01;              // END_STREAM
    writeUint32BE(&dataFrame[5], STREAM_ID);

    if (::send(s, reinterpret_cast<const char*>(dataFrame), 9, 0) != 9 ||
        ::send(s, dataPayload.data(), static_cast<int>(dataPayload.size()), 0) !=
            static_cast<int>(dataPayload.size())) {
        lastError_ = "grpcSendReceive: send DATA failed";
        return false;
    }

    // ---- Read response frames ----
    response.clear();
    bool done = false;
    int frameCount = 0;
    const int MAX_FRAMES = 200;

    while (!done && frameCount < MAX_FRAMES) {
        ++frameCount;

        // Read 9-byte frame header
        unsigned char frameHdr[9];
        int total = 0;
        while (total < 9) {
            int n = ::recv(s, reinterpret_cast<char*>(frameHdr) + total,
                           9 - total, 0);
            if (n <= 0) {
                // Connection closed by server — if we have data, it's OK
                if (!response.empty()) return true;
                lastError_ = "grpcSendReceive: recv frame header failed";
                return false;
            }
            total += n;
        }

        uint32_t flen = (static_cast<uint32_t>(frameHdr[0]) << 16) |
                        (static_cast<uint32_t>(frameHdr[1]) << 8)  |
                         static_cast<uint32_t>(frameHdr[2]);
        unsigned char ftype   = frameHdr[3];
        unsigned char fflags  = frameHdr[4];
        uint32_t fsid = (static_cast<uint32_t>(frameHdr[5]) << 24) |
                        (static_cast<uint32_t>(frameHdr[6]) << 16) |
                        (static_cast<uint32_t>(frameHdr[7]) << 8)  |
                         static_cast<uint32_t>(frameHdr[8]);

        // Read payload
        std::string payload;
        if (flen > 0) {
            payload.resize(flen);
            total = 0;
            while (total < static_cast<int>(flen)) {
                int n = ::recv(s, &payload[0] + total, flen - total, 0);
                if (n <= 0) {
                    if (!response.empty()) return true;
                    lastError_ = "grpcSendReceive: recv payload failed";
                    return false;
                }
                total += n;
            }
        }

        if (fsid == STREAM_ID) {
            if (ftype == 0x00) {  // DATA
                // Skip 5-byte gRPC prefix
                if (payload.size() >= 5) {
                    bool compressed = (payload[0] & 0x01) != 0;
                    if (compressed) {
                        lastError_ = "grpcSendReceive: compressed response";
                        return false;
                    }
                    uint32_t msgLen =
                        (static_cast<unsigned char>(payload[1]) << 24) |
                        (static_cast<unsigned char>(payload[2]) << 16) |
                        (static_cast<unsigned char>(payload[3]) << 8)  |
                         static_cast<unsigned char>(payload[4]);
                    if (payload.size() >= 5 + msgLen) {
                        response = payload.substr(5, msgLen);
                    }
                }
                if (fflags & 0x01) done = true;  // END_STREAM
            } else if (ftype == 0x01) {  // HEADERS
                if (fflags & 0x04) { /* END_HEADERS — OK */ }
                if (fflags & 0x01) done = true;  // END_STREAM (trailers)
            }
        } else if (fsid == 0 && ftype == 0x04) {
            // SETTINGS on connection stream — ACK it
            if (!(fflags & 0x01)) {
                unsigned char ack[9] = {
                    0x00, 0x00, 0x00, 0x04, 0x01,
                    0x00, 0x00, 0x00, 0x00
                };
                ::send(s, reinterpret_cast<const char*>(ack), 9, 0);
            }
        } else if (ftype == 0x07) {
            // GOAWAY — stop
            break;
        }
    }

    if (!done) {
        lastError_ = "grpcSendReceive: response incomplete after " +
                     std::to_string(frameCount) + " frames";
    }
    return done;
}

// ----- Public gRPC methods -----

bool XrayApi::removeOutboundDirect(const std::string& tag) {
    Logger::write("[XrayApi] removeOutboundDirect: tag=" + tag, LogLevel::DEBUG);

    std::string host;
    int port;
    if (!parseServerAddr(host, port)) {
        lastError_ = "removeOutboundDirect: bad serverAddr=" + serverAddr_;
        return false;
    }

    int sock = grpcConnect(host, port);
    if (sock < 0) return false;

    bool ok = grpcSendPreface(sock);
    if (!ok) { grpcClose(sock); return false; }

    // RemoveOutboundRequest { string tag = 1; }
    std::string proto = encodeString(1, tag);

    std::string response;
    ok = grpcSendReceive(sock,
        "/xray.app.proxyman.command.HandlerService/RemoveOutbound",
         proto, response);

    grpcClose(sock);

    if (!ok) {
        Logger::write("[XrayApi] removeOutboundDirect FAILED: " + lastError_, LogLevel::ERR);
    } else {
        Logger::write("[XrayApi] removeOutboundDirect SUCCESS tag=" + tag, LogLevel::DEBUG);
    }
    return ok;
}

bool XrayApi::addOutboundDirect(const std::string& outboundJson,
                                 const std::string& tag,
                                 std::string& resultOutput) {
    Logger::write("[XrayApi] addOutboundDirect: tag=" + tag, LogLevel::DEBUG);

    std::string tagOut;
    std::string typeUrl;
    std::string valueJsonStr;
    std::string streamSettingsJson;
    std::string muxJson;

    if (!parseOutboundJson(outboundJson, tagOut, typeUrl, valueJsonStr,
                           &streamSettingsJson, &muxJson)) {
        // If JSON parsing fails, treat outboundJson as a raw payload
        Logger::write("[XrayApi] addOutboundDirect: JSON parse fallback",
                      LogLevel::DEBUG);
        tagOut    = tag;
        typeUrl   = "xray.core.proxy.outbound.Config";
        valueJsonStr = outboundJson;
    }

    // ---- Check if protocol has protobuf encoder ----
    // jsonConfigToProtobuf handles freedom, blackhole, and all 6 proxy protocols
    // (vmess, vless, trojan, shadowsocks, socks, http) via direct protobuf wire
    // encoding. Only fall back to subprocess for unsupported protocols.
    const std::string supportedTypeUrls[] = {
        "xray.proxy.vmess.outbound.Config",
        "xray.proxy.vless.outbound.Config",
        "xray.proxy.trojan.ClientConfig",
        "xray.proxy.shadowsocks.ClientConfig",
        "xray.proxy.socks.ClientConfig",
        "xray.proxy.http.ClientConfig",
        "xray.proxy.freedom.Config",
        "xray.proxy.blackhole.Config"
    };
    const bool hasProtobufEncoder =
        (std::find(std::begin(supportedTypeUrls),
                   std::end(supportedTypeUrls),
                   typeUrl) != std::end(supportedTypeUrls));

    if (!hasProtobufEncoder) {
        Logger::write("[XrayApi] addOutboundDirect: fallback to subprocess for "
                      + typeUrl, LogLevel::DEBUG);
        return addOutbound(outboundJson, tag, resultOutput);
    }

    // ---- gRPC path for supported protocols ----
    // Build protobuf: AddOutboundRequest { OutboundHandlerConfig outbound = 1; }
    //   OutboundHandlerConfig { string tag = 1;
    //                           TypedMessage sender_settings = 2;
    //                           TypedMessage proxy_settings = 3; }
    //     TypedMessage { string type = 1; bytes value = 2; }
    std::string typedMsg;
    typedMsg += encodeString(1, typeUrl);
    typedMsg += encodeString(2, jsonConfigToProtobuf(typeUrl, valueJsonStr));

    std::string handler;
    handler += encodeString(1, tagOut);

    // ---- sender_settings (field 2): SenderConfig TypedMessage ----
    // OutboundHandlerConfig { string tag=1; TypedMessage sender_settings=2;
    //                         TypedMessage proxy_settings=3; }
    // SenderConfig { stream_settings=2; multiplex_settings=4; } is wrapped as
    // TypedMessage { type="xray.app.proxyman.SenderConfig"; value=<SenderConfig bin> }.
    const boost::json::object* streamSettingsPtr = nullptr;
    const boost::json::object* muxPtr = nullptr;
    boost::json::value streamSettingsHolder;
    boost::json::value muxHolder;
    if (!streamSettingsJson.empty()) {
        try {
            streamSettingsHolder = boost::json::parse(streamSettingsJson);
            if (streamSettingsHolder.is_object()) {
                streamSettingsPtr = &streamSettingsHolder.as_object();
            }
        } catch (const std::exception& ex) {
            Logger::write(std::string("[XrayApi] addOutboundDirect: bad streamSettings: ")
                          + ex.what(), LogLevel::DEBUG);
        }
    }
    if (!muxJson.empty()) {
        try {
            muxHolder = boost::json::parse(muxJson);
            if (muxHolder.is_object()) {
                muxPtr = &muxHolder.as_object();
            }
        } catch (const std::exception& ex) {
            Logger::write(std::string("[XrayApi] addOutboundDirect: bad mux: ")
                          + ex.what(), LogLevel::DEBUG);
        }
    }
    const std::string senderValue =
        encodeSenderSettings(streamSettingsPtr, muxPtr);
    if (!senderValue.empty()) {
        std::string senderTyped;
        senderTyped += encodeString(1, "xray.app.proxyman.SenderConfig");
        senderTyped += encodeString(2, senderValue);
        handler += encodeLengthDelimited(2, senderTyped);
    }

    handler += encodeLengthDelimited(3, typedMsg);

    std::string proto = encodeLengthDelimited(1, handler);

    std::string host;
    int port;
    if (!parseServerAddr(host, port)) {
        lastError_ = "addOutboundDirect: bad serverAddr=" + serverAddr_;
        return false;
    }

    int sock = grpcConnect(host, port);
    if (sock < 0) return false;

    bool ok = grpcSendPreface(sock);
    if (!ok) { grpcClose(sock); return false; }

    std::string response;
    ok = grpcSendReceive(sock,
        "/xray.app.proxyman.command.HandlerService/AddOutbound",
         proto, response);

    grpcClose(sock);

    resultOutput = response;

    if (!ok) {
        Logger::write("[XrayApi] addOutboundDirect FAILED: " + lastError_,
                      LogLevel::ERR);
    } else {
        Logger::write("[XrayApi] addOutboundDirect SUCCESS tag=" + tag,
                      LogLevel::DEBUG);
    }
    return ok;
}

bool XrayApi::listOutboundsDirect(std::string& output) {
    Logger::write("[XrayApi] listOutboundsDirect", LogLevel::DEBUG);

    std::string host;
    int port;
    if (!parseServerAddr(host, port)) {
        lastError_ = "listOutboundsDirect: bad serverAddr=" + serverAddr_;
        return false;
    }

    int sock = grpcConnect(host, port);
    if (sock < 0) return false;

    bool ok = grpcSendPreface(sock);
    if (!ok) { grpcClose(sock); return false; }

    // ListOutboundsRequest { } — empty protobuf message
    std::string proto;  // empty = no fields

    ok = grpcSendReceive(sock,
        "/xray.app.proxyman.command.HandlerService/ListOutbounds",
         proto, output);

    grpcClose(sock);

    if (!ok) {
        Logger::write("[XrayApi] listOutboundsDirect FAILED: " + lastError_,
                      LogLevel::ERR);
    } else {
        Logger::write("[XrayApi] listOutboundsDirect SUCCESS, response sz=" +
                      std::to_string(output.size()) + " bytes",
                      LogLevel::DEBUG);
    }
    return ok;
}

#endif // USE_GRPC_API

} // namespace xray
