#include <algorithm>
#include <mutex>
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
                      const std::string* stdinData = nullptr, int* pLastError = nullptr) {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

    // Create stdout pipe (child writes, parent reads)
    HANDLE hOutRead = nullptr, hOutWrite = nullptr;
    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) {
        if (pLastError) *pLastError = static_cast<int>(GetLastError());
        return -1;
    }
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);

    // Create stdin pipe only when input is provided
    HANDLE hInRead = nullptr, hInWrite = nullptr;
    if (stdinData) {
        if (!CreatePipe(&hInRead, &hInWrite, &sa, 0)) {
            if (pLastError) *pLastError = static_cast<int>(GetLastError());
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
        if (pLastError) *pLastError = static_cast<int>(GetLastError());
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

    // Read all stdout output with a 5-second deadline.
    // A child that hangs without closing stdout would block forever otherwise.
    char buf[4096];
    DWORD bytesRead = 0;
    output.clear();
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD available = 0;
        if (!PeekNamedPipe(hOutRead, nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        if (available == 0) {
            DWORD waitRet = WaitForSingleObject(pi.hProcess, 50);
            if (waitRet == WAIT_OBJECT_0) break;  // child exited
            continue;
        }
        bytesRead = 0;
        if (!ReadFile(hOutRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) ||
            bytesRead == 0) {
            break;
        }
        buf[bytesRead] = '\0';
        output += buf;
    }

    if (std::chrono::steady_clock::now() >= deadline) {
        TerminateProcess(pi.hProcess, 1);
        return -1;
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
        lastError_ = "Failed to run command: " + cmd + " (GetLastError=" + std::to_string(GetLastError()) + ")";
        return false;
    }
    lastError_.clear();
    return exitCode == 0;
}

bool XrayApi::addOutbound(const std::string& outboundJson, const std::string& tag, std::string& resultOutput) {
    Logger::write("[XrayApi] addOutbound called: tag=" + tag + ", xrayPath=" + xrayPath_ + ", serverAddr=" + serverAddr_, LogLevel::DEBUG);

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
        lastError_ = "Failed to run xray api ado command (GetLastError=" + std::to_string(GetLastError()) + ")";
        return false;
    }

    resultOutput = output;

    bool success = (exitCode == 0);

    if (!success) {
        lastError_ = "xray api ado failed with code: " + std::to_string(exitCode) + " output: " + output;
        // Per-proxy injection failures are already reported at ERR level by the
        // batch-test worker (XRAY_ERROR with lastError_). Keep these two lines
        // at DEBUG to avoid duplicating the flood 3x per proxy (retry loop).
        Logger::write("[XrayApi] addOutbound FAILED: exitCode=" + std::to_string(exitCode) + ", output=" + output, LogLevel::DEBUG);
        Logger::write("[XrayApi] addOutbound FAILED error: " + lastError_, LogLevel::DEBUG);
        return false;
    }

    Logger::write("[XrayApi] addOutbound SUCCESS for tag: " + tag, LogLevel::DEBUG);
    lastError_.clear();
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

    std::string cmd = "\"" + xrayPath_ + "\" api rmo --server " + serverAddr_ +
                      " \"" + cleanTag + "\"";

    // E10: let child exit code be authoritative; special-case benign "already gone"
    std::string output;
    int exitCode = runProcess(cmd, output);
    if (exitCode < 0) {
        lastError_ = "Failed to run xray api rmo command: " + cmd + " (GetLastError=" + std::to_string(GetLastError()) + ")";
        return false;
    }

    // xray rmo exits 0 on success or if tag was already absent (no-op).
    // Treat non-zero as failure unless output signals the tag simply did not exist.
    if (exitCode != 0 && output.find("already") == std::string::npos &&
        output.find("not found") == std::string::npos &&
        output.find("does not exist") == std::string::npos) {
        lastError_ = "xray api rmo failed: code=" + std::to_string(exitCode) +
                     " output=" + output;
        Logger::write("[XrayApi] removeOutbound FAILED: " + lastError_,
                      LogLevel::DEBUG);
        return false;
    }

    Logger::write("[XrayApi] removeOutbound SUCCESS for tag: " + tag,
                  LogLevel::DEBUG);
    lastError_.clear();
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

    lastError_.clear();
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
    lastError_.clear();
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
    boost::json::object::const_iterator sit = obj.find("settings");
    if (sit != obj.end() && sit->value().is_object()) {
        settings = sit->value().as_object();
    }

    std::string result;

    if (isFreedom) {
        // freedom.Config protobuf fields:
        //   1: domain_strategy (DomainStrategy enum, varint)
        //   4: user_level (uint32, varint)
        //   6: proxy_protocol (uint32, varint)

        boost::json::object::iterator it = settings.find("domain_strategy");
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
        boost::json::object::iterator vit = settings.find("vnext");
        if (vit != settings.end() && vit->value().is_array() &&
            !vit->value().as_array().empty()) {
            servers = vit->value().as_array();
        }
    } else {
        boost::json::object::iterator sit2 = settings.find("servers");
        if (sit2 != settings.end() && sit2->value().is_array() &&
            !sit2->value().as_array().empty()) {
            servers = sit2->value().as_array();
        }
    }

    if (servers.empty()) return {};

    if (!servers[0].is_object()) return {};
    const boost::json::object& srv = servers[0].as_object();

    // Extract address and port
    std::string addr;
    if (srv.contains("address") && srv.at("address").is_string())
        addr = srv.at("address").as_string().c_str();
    int port = 0;
    if (srv.contains("port")) {
        const boost::json::value& pv = srv.at("port");
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
        boost::json::object::const_iterator uit = srv.find("users");
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
        boost::json::object::const_iterator uit = srv.find("users");
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
        boost::json::object::const_iterator uit = srv.find("users");
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
    } catch (const std::exception& e) {
        Logger::write("[XrayApi] parseOutboundJson exception: " +
                      std::string(e.what()), LogLevel::WARN);
        return false;
    } catch (...) {
        Logger::write("[XrayApi] parseOutboundJson: unknown exception", LogLevel::WARN);
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
    // Config fields per transport/internet/tls/config.proto:
    //   allow_insecure=1(bool)  server_name=3(string)
    //   next_protocol=4(repeated string, ALPN)  min_version=7(string)
    //   max_version=8(string)  cipher_suites=9(string)  fingerprint=11(string)
    //   reject_unknown_sni=12(bool)
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
    // public_key (23) and short_id (24) are BYTES fields. The database /
    // JSON config carry them as base64 (publicKey) and hex (shortId); the
    // xray JSON adapter decodes both before they reach the proto, so we must
    // decode here as well — encoding the raw strings made every REALITY
    // handshake fail (X25519 NewPublicKey rejected the garbage key).
    const boost::json::value* publicKey = reality.if_contains("publicKey");
    if (publicKey != nullptr && publicKey->is_string()) {
        std::string decoded = base64Decode(publicKey->as_string().c_str());
        if (!decoded.empty()) {
            result += encodeLengthDelimited(23, decoded);
        }
    }
    const boost::json::value* shortId = reality.if_contains("shortId");
    if (shortId != nullptr && shortId->is_string()) {
        std::string sidText = std::string(shortId->as_string().c_str());
        if (!sidText.empty()) {
            std::string decoded = hexDecode(sidText);
            if (!decoded.empty()) {
                result += encodeLengthDelimited(24, decoded);
            }
        }
    }
    const boost::json::value* spiderX = reality.if_contains("spiderX");
    if (spiderX != nullptr && spiderX->is_string()) {
        result += encodeString(26, spiderX->as_string().c_str());
    }
    return result;
}

std::string XrayApi::base64Decode(const std::string& input) {
    // Accepts both URL-safe ("-_") and standard ("+/") alphabets; padding
    // ('=') is optional. Any other character makes the whole input invalid.
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    int reverse[256];
    for (int i = 0; i < 256; ++i) {
        reverse[i] = -1;
    }
    for (int i = 0; alphabet[i] != '\0'; ++i) {
        reverse[static_cast<unsigned char>(alphabet[i])] = i;
    }
    std::string output;
    unsigned int accumulator = 0;
    int bitCount = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '=') {
            break;
        }
        const int value = reverse[static_cast<unsigned char>(c)];
        if (value < 0) {
            return std::string();
        }
        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bitCount += 6;
        if (bitCount >= 8) {
            bitCount -= 8;
            output += static_cast<char>((accumulator >> bitCount) & 0xFFu);
        }
    }
    return output;
}

std::string XrayApi::hexDecode(const std::string& input) {
    // Mirrors Go's hex.Decode: odd-length input is an error.
    if (input.size() % 2 != 0) {
        return std::string();
    }
    std::string output;
    for (size_t i = 0; i + 1 < input.size(); i += 2) {
        const char hi = input[i];
        const char lo = input[i + 1];
        int hiVal = -1;
        if (hi >= '0' && hi <= '9') {
            hiVal = hi - '0';
        } else if (hi >= 'a' && hi <= 'f') {
            hiVal = hi - 'a' + 10;
        } else if (hi >= 'A' && hi <= 'F') {
            hiVal = hi - 'A' + 10;
        }
        int loVal = -1;
        if (lo >= '0' && lo <= '9') {
            loVal = lo - '0';
        } else if (lo >= 'a' && lo <= 'f') {
            loVal = lo - 'a' + 10;
        } else if (lo >= 'A' && lo <= 'F') {
            loVal = lo - 'A' + 10;
        }
        if (hiVal < 0 || loVal < 0) {
            return std::string();
        }
        output += static_cast<char>((hiVal << 4) | loVal);
    }
    return output;
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

std::string XrayApi::encodeXHTTPSettings(const boost::json::object& xhttp) {
    // xhttp is the splithttp family; app normalizes splithttp→xhttp and
    // StreamSettingsBuilder emits xhttpSettings {path, headers}. Encode the
    // common splithttp-family fields (host=1 path=2 mode=3 headers=4 map).
    std::string result;
    const boost::json::value* host = xhttp.if_contains("host");
    if (host != nullptr && host->is_string()) {
        result += encodeString(1, host->as_string().c_str());
    }
    const boost::json::value* path = xhttp.if_contains("path");
    if (path != nullptr && path->is_string()) {
        result += encodeString(2, path->as_string().c_str());
    }
    const boost::json::value* mode = xhttp.if_contains("mode");
    if (mode != nullptr && mode->is_string()) {
        result += encodeString(3, mode->as_string().c_str());
    }
    const boost::json::value* headers = xhttp.if_contains("headers");
    if (headers != nullptr && headers->is_object()) {
        const boost::json::object& headerObj = headers->as_object();
        for (const boost::json::key_value_pair& kv : headerObj) {
            if (!kv.value().is_string()) {
                continue;
            }
            // map entry: key=1 value=2, wrapped in field 4
            std::string entry;
            entry += encodeString(1, std::string(kv.key()));
            entry += encodeString(2, kv.value().as_string().c_str());
            result += encodeLengthDelimited(4, entry);
        }
    }
    return result;
}

std::string XrayApi::encodeSplitHTTPSettings(const boost::json::object& split) {
    // splithttp Config per transport/internet/splithttp/config.proto:
    //   host=1(string) path=2(string) mode=3(string) headers=4(map<string,string>)
    std::string result;
    const boost::json::value* host = split.if_contains("host");
    if (host != nullptr && host->is_string()) {
        result += encodeString(1, host->as_string().c_str());
    }
    const boost::json::value* path = split.if_contains("path");
    if (path != nullptr && path->is_string()) {
        result += encodeString(2, path->as_string().c_str());
    }
    const boost::json::value* mode = split.if_contains("mode");
    if (mode != nullptr && mode->is_string()) {
        result += encodeString(3, mode->as_string().c_str());
    }
    const boost::json::value* headers = split.if_contains("headers");
    if (headers != nullptr && headers->is_object()) {
        const boost::json::object& headerObj = headers->as_object();
        for (const boost::json::key_value_pair& kv : headerObj) {
            if (!kv.value().is_string()) {
                continue;
            }
            // map entry: key=1 value=2, wrapped in field 4
            std::string entry;
            entry += encodeString(1, std::string(kv.key()));
            entry += encodeString(2, kv.value().as_string().c_str());
            result += encodeLengthDelimited(4, entry);
        }
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
    } else if (network == "tcp" || network == "http"
               || network == "splithttp" || network == "hysteria") {
        protocolName = network;
    } else if (network == "xhttp") {
        // Xray v26.2.4+ removed the xhttp protocol name; its JSON adapter
        // remaps network "xhttp" to "splithttp", so do the same here.
        protocolName = "splithttp";
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
    } else if (network == "xhttp" || network == "splithttp") {
        // xhttpSettings and splithttpSettings share one schema; only the
        // splithttp Config message is registered in Xray v26.2.4+.
        settingsValue = (network == "xhttp")
            ? stream.if_contains("xhttpSettings")
            : stream.if_contains("splithttpSettings");
        transportTypeUrl = "xray.transport.internet.splithttp.Config";
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
        } else if (network == "xhttp" || network == "splithttp") {
            // Same wire schema (host=1 path=2 mode=3 headers=4); splithttp
            // is the registered protocol in Xray v26.2.4+.
            encodedSettings = encodeSplitHTTPSettings(settingsObj);
        }
        if (!encodedSettings.empty()) {
            std::string typedMsg;
            typedMsg += encodeString(1, transportTypeUrl);
            typedMsg += encodeString(2, encodedSettings);
            std::string transportConfig;
            transportConfig += encodeLengthDelimited(2, typedMsg);
            // TransportConfig.protocol_name is field 3 (NOT 1) per
            // transport/internet/config.proto. Encoding to field 1 made Xray
            // drop the transport protocol name, losing ws/grpc/xhttp settings.
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
        // security_type (field 3) is a STRING — the message name of the
        // security settings proto (transport/internet/config.proto:
        // "string security_type = 3; // Type of security. Must be a message
        // name of the settings proto."). Encoding it as a varint enum made
        // Xray fail to parse StreamConfig for any TLS/REALITY outbound.
        result += encodeString(3, secTypeUrl);
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

    // RFC 7541 static table has 61 entries. Index 57 is "transfer-encoding",
    // and "te"/"grpc-timeout"/"grpc-encoding" are NOT in the static table, so
    // they must be sent as full literals (handled by the fall-through below).
    static const std::vector<std::pair<std::string, int>> NAME_INDEX = {
        {":authority", 1}, {":method", 2}, {":path", 4},
        {":scheme", 6},    {"content-type", 31},
    };

    std::string result;
    for (std::size_t hidx = 0; hidx < headers.size(); ++hidx) {
        const std::string& name = headers[hidx].first;
        const std::string& value = headers[hidx].second;
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
        for (std::size_t nidx = 0; nidx < NAME_INDEX.size(); ++nidx) {
            const std::string& n = NAME_INDEX[nidx].first;
            const int idx = NAME_INDEX[nidx].second;
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

// ----- RFC 7541 Huffman / HPACK decoding helpers (private to this TU) -----
namespace {

// Canonical RFC 7541 Appendix B Huffman table in MSB-first "wire order"
// (code bits are written from the most significant bit, matching both
// XrayApi::encodeHpack and Go's x/net/http2/hpack encoder used by Xray).
// Indexed by symbol (0..255); entry 256 is EOS and is never decoded.
// Verified against RFC 7541 Appendix C.6 vectors.
struct HuffmanEntry {
    uint32_t code;
    uint8_t  len;
};

const HuffmanEntry HUFFMAN_TABLE[257] = {
    {0x1ff8, 13}, {0x7fffd8, 23}, {0xfffffe2, 28}, {0xfffffe3, 28},
    {0xfffffe4, 28}, {0xfffffe5, 28}, {0xfffffe6, 28}, {0xfffffe7, 28},
    {0xfffffe8, 28}, {0xffffea, 24}, {0x3ffffffc, 30}, {0xfffffe9, 28},
    {0xfffffea, 28}, {0x3ffffffd, 30}, {0xfffffeb, 28}, {0xfffffec, 28},
    {0xfffffed, 28}, {0xfffffee, 28}, {0xfffffef, 28}, {0xffffff0, 28},
    {0xffffff1, 28}, {0xffffff2, 28}, {0x3ffffffe, 30}, {0xffffff3, 28},
    {0xffffff4, 28}, {0xffffff5, 28}, {0xffffff6, 28}, {0xffffff7, 28},
    {0xffffff8, 28}, {0xffffff9, 28}, {0xffffffa, 28}, {0xffffffb, 28},
    {0x14, 6}, {0x3f8, 10}, {0x3f9, 10}, {0xffa, 12}, {0x1ff9, 13},
    {0x15, 6}, {0xf8, 8}, {0x7fa, 11}, {0x3fa, 10}, {0x3fb, 10},
    {0xf9, 8}, {0x7fb, 11}, {0xfa, 8}, {0x16, 6}, {0x17, 6}, {0x18, 6},
    {0x0, 5}, {0x1, 5}, {0x2, 5}, {0x19, 6}, {0x1a, 6}, {0x1b, 6},
    {0x1c, 6}, {0x1d, 6}, {0x1e, 6}, {0x1f, 6}, {0x5c, 7}, {0xfb, 8},
    {0x7ffc, 15}, {0x20, 6}, {0xffb, 12}, {0x3fc, 10}, {0x1ffa, 13},
    {0x21, 6}, {0x5d, 7}, {0x5e, 7}, {0x5f, 7}, {0x60, 7}, {0x61, 7},
    {0x62, 7}, {0x63, 7}, {0x64, 7}, {0x65, 7}, {0x66, 7}, {0x67, 7},
    {0x68, 7}, {0x69, 7}, {0x6a, 7}, {0x6b, 7}, {0x6c, 7}, {0x6d, 7},
    {0x6e, 7}, {0x6f, 7}, {0x70, 7}, {0x71, 7}, {0x72, 7}, {0xfc, 8},
    {0x73, 7}, {0xfd, 8}, {0x1ffb, 13}, {0x7fff0, 19}, {0x1ffc, 13},
    {0x3ffc, 14}, {0x22, 6}, {0x7ffd, 15}, {0x3, 5}, {0x23, 6},
    {0x4, 5}, {0x24, 6}, {0x5, 5}, {0x25, 6}, {0x26, 6}, {0x27, 6},
    {0x6, 5}, {0x74, 7}, {0x75, 7}, {0x28, 6}, {0x29, 6}, {0x2a, 6},
    {0x7, 5}, {0x2b, 6}, {0x76, 7}, {0x2c, 6}, {0x8, 5}, {0x9, 5},
    {0x2d, 6}, {0x77, 7}, {0x78, 7}, {0x79, 7}, {0x7a, 7}, {0x7b, 7},
    {0x7ffe, 15}, {0x7fc, 11}, {0x3ffd, 14}, {0x1ffd, 13}, {0xffffffc, 28},
    {0xfffe6, 20}, {0x3fffd2, 22}, {0xfffe7, 20}, {0xfffe8, 20},
    {0x3fffd3, 22}, {0x3fffd4, 22}, {0x3fffd5, 22}, {0x7fffd9, 23},
    {0x3fffd6, 22}, {0x7fffda, 23}, {0x7fffdb, 23}, {0x7fffdc, 23},
    {0x7fffdd, 23}, {0x7fffde, 23}, {0xffffeb, 24}, {0x7fffdf, 23},
    {0xffffec, 24}, {0xffffed, 24}, {0x3fffd7, 22}, {0x7fffe0, 23},
    {0xffffee, 24}, {0x7fffe1, 23}, {0x7fffe2, 23}, {0x7fffe3, 23},
    {0x7fffe4, 23}, {0x1fffdc, 21}, {0x3fffd8, 22}, {0x7fffe5, 23},
    {0x3fffd9, 22}, {0x7fffe6, 23}, {0x7fffe7, 23}, {0xffffef, 24},
    {0x3fffda, 22}, {0x1fffdd, 21}, {0xfffe9, 20}, {0x3fffdb, 22},
    {0x3fffdc, 22}, {0x7fffe8, 23}, {0x7fffe9, 23}, {0x1fffde, 21},
    {0x7fffea, 23}, {0x3fffdd, 22}, {0x3fffde, 22}, {0xfffff0, 24},
    {0x1fffdf, 21}, {0x3fffdf, 22}, {0x7fffeb, 23}, {0x7fffec, 23},
    {0x1fffe0, 21}, {0x1fffe1, 21}, {0x3fffe0, 22}, {0x1fffe2, 21},
    {0x7fffed, 23}, {0x3fffe1, 22}, {0x7fffee, 23}, {0x7fffef, 23},
    {0xfffea, 20}, {0x3fffe2, 22}, {0x3fffe3, 22}, {0x3fffe4, 22},
    {0x7ffff0, 23}, {0x3fffe5, 22}, {0x3fffe6, 22}, {0x7ffff1, 23},
    {0x3ffffe0, 26}, {0x3ffffe1, 26}, {0xfffeb, 20}, {0x7fff1, 19},
    {0x3fffe7, 22}, {0x7ffff2, 23}, {0x3fffe8, 22}, {0x1ffffec, 25},
    {0x3ffffe2, 26}, {0x3ffffe3, 26}, {0x3ffffe4, 26}, {0x7ffffde, 27},
    {0x7ffffdf, 27}, {0x3ffffe5, 26}, {0xfffff1, 24}, {0x1ffffed, 25},
    {0x7fff2, 19}, {0x1fffe3, 21}, {0x3ffffe6, 26}, {0x7ffffe0, 27},
    {0x7ffffe1, 27}, {0x3ffffe7, 26}, {0x7ffffe2, 27}, {0xfffff2, 24},
    {0x1fffe4, 21}, {0x1fffe5, 21}, {0x3ffffe8, 26}, {0x3ffffe9, 26},
    {0xffffffd, 28}, {0x7ffffe3, 27}, {0x7ffffe4, 27}, {0x7ffffe5, 27},
    {0xfffec, 20}, {0xfffff3, 24}, {0xfffed, 20}, {0x1fffe6, 21},
    {0x3fffe9, 22}, {0x1fffe7, 21}, {0x1fffe8, 21}, {0x7ffff3, 23},
    {0x3fffea, 22}, {0x3fffeb, 22}, {0x1ffffee, 25}, {0x1ffffef, 25},
    {0xfffff4, 24}, {0xfffff5, 24}, {0x3ffffea, 26}, {0x7ffff4, 23},
    {0x3ffffeb, 26}, {0x7ffffe6, 27}, {0x3ffffec, 26}, {0x3ffffed, 26},
    {0x7ffffe7, 27}, {0x7ffffe8, 27}, {0x7ffffe9, 27}, {0x7ffffea, 27},
    {0x7ffffeb, 27}, {0xffffffe, 28}, {0x7ffffec, 27}, {0x7ffffed, 27},
    {0x7ffffee, 27}, {0x7ffffef, 27}, {0x7fffff0, 27}, {0x3ffffee, 26},
    {0x3fffffff, 30}  // symbol 256 = EOS (must never be produced)
};

// RFC 7541 Appendix A static table (61 entries). Entries with an empty
// value are "name-only"; value comes from the wire.
struct HpackStaticEntry {
    const char* name;
    const char* value;
};

const HpackStaticEntry HPACK_STATIC_TABLE[61] = {
    {":authority", ""}, {":method", "GET"}, {":method", "POST"},
    {":path", "/"}, {":path", "/index.html"}, {":scheme", "http"},
    {":scheme", "https"}, {":status", "200"}, {":status", "204"},
    {":status", "206"}, {":status", "304"}, {":status", "400"},
    {":status", "404"}, {":status", "500"}, {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"}, {"accept-language", ""},
    {"accept-ranges", ""}, {"accept", ""},
    {"access-control-allow-origin", ""}, {"age", ""}, {"allow", ""},
    {"authorization", ""}, {"cache-control", ""},
    {"content-disposition", ""}, {"content-encoding", ""},
    {"content-language", ""}, {"content-length", ""},
    {"content-location", ""}, {"content-range", ""},
    {"content-type", ""}, {"cookie", ""}, {"date", ""}, {"etag", ""},
    {"expect", ""}, {"expires", ""}, {"from", ""}, {"host", ""},
    {"if-match", ""}, {"if-modified-since", ""}, {"if-none-match", ""},
    {"if-range", ""}, {"if-unmodified-since", ""},
    {"last-modified", ""}, {"link", ""}, {"location", ""},
    {"max-forwards", ""}, {"proxy-authenticate", ""},
    {"proxy-authorization", ""}, {"range", ""}, {"referer", ""},
    {"refresh", ""}, {"retry-after", ""}, {"server", ""},
    {"set-cookie", ""}, {"strict-transport-security", ""},
    {"transfer-encoding", ""}, {"user-agent", ""}, {"vary", ""},
    {"via", ""}, {"www-authenticate", ""},
};

// Forward declarations for HPACK decoder helpers.
bool huffmanDecode(const std::string& data, std::string& out);
bool hpackReadInt(const std::string& buf, size_t& pos, uint32_t firstValue,
                  uint32_t prefixMask, uint32_t& out);
bool hpackReadString(const std::string& buf, size_t& pos, std::string& out);

// Read an HPACK integer: value is the already-masked prefix field of the
// first byte; prefixMask is the mask for that field (0x7F for 7 bits, etc.).
bool hpackReadInt(const std::string& buf, size_t& pos, uint32_t firstValue,
                  uint32_t prefixMask, uint32_t& out) {
    uint32_t value = firstValue;
    if (value < prefixMask) { out = value; return true; }
    int shift = 0;
    while (true) {
        if (pos >= buf.size()) return false;
        unsigned char b = static_cast<unsigned char>(buf[pos++]);
        value += static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift > 28) return false;
    }
    out = value;
    return true;
}

// Read a header string: one H bit + 7-bit length (with continuation).
bool hpackReadString(const std::string& buf, size_t& pos, std::string& out) {
    if (pos >= buf.size()) return false;
    unsigned char first = static_cast<unsigned char>(buf[pos++]);
    bool huffman = (first & 0x80) != 0;
    uint32_t len;
    if (!hpackReadInt(buf, pos, first & 0x7F, 0x7F, len)) return false;
    if (pos + len > buf.size()) return false;
    std::string raw = buf.substr(pos, len);
    pos += len;
    if (huffman) {
        if (!huffmanDecode(raw, out)) return false;
    } else {
        out = raw;
    }
    return true;
}

// Decode an RFC 7541 Huffman-coded string (MSB-first wire order).
bool huffmanDecode(const std::string& data, std::string& out) {
    out.clear();
    uint32_t code = 0;
    int bits = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        unsigned char byte = static_cast<unsigned char>(data[i]);
        for (int bitPos = 7; bitPos >= 0; --bitPos) {
            int bit = (byte >> bitPos) & 1;
            code = (code << 1) | static_cast<uint32_t>(bit);
            ++bits;
            // Longest real code is 30 bits; anything longer (or EOS) fails.
            if (bits > 30) return false;
            // Huffman codes are prefix-free, so a match at the exact code
            // length is unambiguous.
            int matched = -1;
            for (int sym = 0; sym < 256; ++sym) {
                if (HUFFMAN_TABLE[sym].len == bits &&
                    (code & ((1u << bits) - 1)) == HUFFMAN_TABLE[sym].code) {
                    matched = sym;
                    break;
                }
            }
            if (matched >= 0) {
                out += static_cast<char>(matched);
                code = 0;
                bits = 0;
            }
        }
    }
    // RFC 7541 §5.2: padding must be 1..7 bits of all-ones.
    if (bits > 7) return false;
    if (bits > 0 && (code & ((1u << bits) - 1)) != ((1u << bits) - 1)) {
        return false;
    }
    return true;
}

} // anonymous namespace

bool XrayApi::decodeHuffman(const std::string& data, std::string& out) {
    return huffmanDecode(data, out);
}

bool XrayApi::grpcDecodeHpackHeaders(
    const std::string& block,
    std::vector<std::pair<std::string, std::string>>& headers) {
    headers.clear();
    size_t pos = 0;
    // Dynamic table: newest entry first, so dynamic index 62 == entries[0].
    // A fresh table per block is sufficient for Xray traffic (grpc-go only
    // adds one entry per response block and never references across blocks).
    std::vector<std::pair<std::string, std::string>> dynTable;
    dynTable.reserve(16);

    while (pos < block.size()) {
        unsigned char b = static_cast<unsigned char>(block[pos++]);

        if (b & 0x80) {
            // 1xxxxxxx: Indexed Header Field
            uint32_t index;
            if (!hpackReadInt(block, pos, b & 0x7F, 0x7F, index)) return false;
            if (index == 0) return false;
            if (index <= 61) {
                headers.emplace_back(HPACK_STATIC_TABLE[index - 1].name,
                                     HPACK_STATIC_TABLE[index - 1].value);
            } else {
                uint32_t dyn = index - 62;
                if (dyn >= dynTable.size()) return false;
                headers.push_back(dynTable[dyn]);
            }
            continue;
        }

        if (b & 0x40) {
            // 01xxxxxx: Literal Header Field with Incremental Indexing
            uint32_t nameIndex;
            if (!hpackReadInt(block, pos, b & 0x3F, 0x3F, nameIndex))
                return false;
            std::string name;
            if (nameIndex == 0) {
                if (!hpackReadString(block, pos, name)) return false;
            } else if (nameIndex <= 61) {
                name = HPACK_STATIC_TABLE[nameIndex - 1].name;
            } else {
                uint32_t dyn = nameIndex - 62;
                if (dyn >= dynTable.size()) return false;
                name = dynTable[dyn].first;
            }
            std::string value;
            if (!hpackReadString(block, pos, value)) return false;
            dynTable.insert(dynTable.begin(), {name, value});
            headers.emplace_back(name, value);
            continue;
        }

        if (b & 0x20) {
            // 001xxxxx: Dynamic Table Size Update
            uint32_t size;
            if (!hpackReadInt(block, pos, b & 0x1F, 0x1F, size)) return false;
            // Table size > 4096 is a protocol error. Smaller updates are
            // accepted but have no effect (no eviction is implemented).
            if (size > 4096) return false;
            continue;
        }

        // 0001xxxx: Literal Never Indexed / 0000xxxx: Literal without
        // Indexing — decoded identically (only the prefix bits differ).
        uint32_t nameIndex;
        if (!hpackReadInt(block, pos, b & 0x0F, 0x0F, nameIndex)) return false;
        std::string name;
        if (nameIndex == 0) {
            if (!hpackReadString(block, pos, name)) return false;
        } else if (nameIndex <= 61) {
            name = HPACK_STATIC_TABLE[nameIndex - 1].name;
        } else {
            uint32_t dyn = nameIndex - 62;
            if (dyn >= dynTable.size()) return false;
            name = dynTable[dyn].first;
        }
        std::string value;
        if (!hpackReadString(block, pos, value)) return false;
        headers.emplace_back(name, value);
    }
    return true;
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

// Ensure Winsock is started (ref-counted, safe to call many times).
// Mutex-guarded: 4 worker threads may call this concurrently on first use;
// a lock-free static bool had a startup race, and std::call_once would cache
// a failed WSAStartup forever (making every later gRPC call fail). Only
// SUCCESS is cached; a failure is retried on the next call and the OS error
// code is logged for diagnosis.
static bool ensureWinsock() {
    static std::mutex mtx;
    static bool started = false;
    std::lock_guard<std::mutex> lock(mtx);
    if (started) return true;
    WSADATA wsa;
    const int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        Logger::write("[XrayApi] ensureWinsock: WSAStartup failed, error=" +
                          std::to_string(rc),
                      LogLevel::ERR);
        return false;
    }
    started = true;
    return true;
}

} // anonymous namespace

// ----- gRPC transport -----

bool XrayApi::parseServerAddr(std::string& host, int& port) const {
    std::string addr = serverAddr_;
    // Strip "tcp://" prefix
    size_t pos = addr.find("://");
    if (pos != std::string::npos) addr = addr.substr(pos + 3);
    // Handle IPv6 addresses: "[::1]:port"
    if (!addr.empty() && addr[0] == '[') {
        size_t close = addr.find(']');
        if (close == std::string::npos) return false;
        host = addr.substr(1, close - 1);  // strip surrounding brackets
        if (close + 1 >= addr.size() || addr[close + 1] != ':') return false;
        const std::string portStr = addr.substr(close + 2);
        size_t parsed = 0;
        try {
            const int parsedPort = std::stoi(portStr, &parsed);
            if (parsed != portStr.size()) return false;
            if (parsedPort < 1 || parsedPort > 65535) return false;
            port = parsedPort;
        } catch (...) { return false; }
        return true;
    }
    // IPv4 or hostname: "host:port"
    pos = addr.find(':');
    if (pos == std::string::npos) return false;
    host = addr.substr(0, pos);
    const std::string portStr = addr.substr(pos + 1);
    size_t parsed = 0;
    try {
        const int parsedPort = std::stoi(portStr, &parsed);
        if (parsed != portStr.size()) return false;
        if (parsedPort < 1 || parsedPort > 65535) return false;
        port = parsedPort;
    } catch (...) { return false; }
    return true;
}

int XrayApi::grpcConnect(const std::string& host, int port) {
    if (!ensureWinsock()) {
        lastError_ = "grpcConnect: WSAStartup failed";
        return -1;
    }

    // Strip IPv6 brackets for getaddrinfo and resolve both IPv4 and IPv6
    std::string resolvedHost = host;
    if (!resolvedHost.empty() && resolvedHost[0] == '[') {
        size_t closeBracket = resolvedHost.find(']');
        if (closeBracket != std::string::npos) {
            resolvedHost = resolvedHost.substr(1, closeBracket - 1);
        }
    }

    struct addrinfo hints, *result = nullptr;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);
    int gaiRet = getaddrinfo(resolvedHost.empty() ? nullptr : resolvedHost.c_str(),
                             portStr.c_str(), &hints, &result);
    if (gaiRet != 0 || result == nullptr) {
        lastError_ = "grpcConnect: getaddrinfo failed: " + std::to_string(gaiRet);
        if (result) freeaddrinfo(result);
        return -1;
    }

    SOCKET sock = INVALID_SOCKET;
    int connectErr = 0;
    for (struct addrinfo* ap = result; ap != nullptr; ap = ap->ai_next) {
        sock = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
        if (sock == INVALID_SOCKET) continue;

        DWORD rcvTimeout = 5000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));

        DWORD sndTimeout = 5000;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&sndTimeout), sizeof(sndTimeout));

        if (connect(sock, ap->ai_addr, static_cast<int>(ap->ai_addrlen)) == 0) {
            break;
        }
        connectErr = WSAGetLastError();
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    freeaddrinfo(result);

    if (sock == INVALID_SOCKET) {
        if (connectErr == WSAETIMEDOUT) {
            lastError_ = "grpcConnect: connect() timed out (WSA" + std::to_string(connectErr) + ")";
        } else if (connectErr == WSAECONNREFUSED) {
            lastError_ = "grpcConnect: connect() refused (WSA" + std::to_string(connectErr) + ")";
        } else if (connectErr == WSAENETUNREACH) {
            lastError_ = "grpcConnect: network unreachable (WSA" + std::to_string(connectErr) + ")";
        } else if (connectErr == WSAEHOSTUNREACH) {
            lastError_ = "grpcConnect: host unreachable (WSA" + std::to_string(connectErr) + ")";
        } else {
            lastError_ = "grpcConnect: connect() failed (WSA" + std::to_string(connectErr) + ")";
        }
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

    std::string statusCode;
    std::string grpcStatusCode;
    std::string grpcMessage;
    bool sawGrpcStatus = false;

    const std::chrono::steady_clock::time_point absoluteDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);

    while (!done && frameCount < MAX_FRAMES) {
        if (std::chrono::steady_clock::now() >= absoluteDeadline) {
            lastError_ = "grpcSendReceive: overall receive timeout (30s) exceeded";
            return false;
        }
        ++frameCount;

        // Read 9-byte frame header
        unsigned char frameHdr[9];
        int total = 0;
        while (total < 9) {
            int n = ::recv(s, reinterpret_cast<char*>(frameHdr) + total,
                           9 - total, 0);
            if (n <= 0) {
                int err = WSAGetLastError();
                // Distinguish connection-close (ok if we have data) from timeout
                if (err == WSAETIMEDOUT) {
                    lastError_ = "grpcSendReceive: recv timeout (GetLastError=" + std::to_string(err) + ")";
                } else if (err == WSAECONNRESET ||
                    err == WSAECONNABORTED || err == WSAENETDOWN) {
                    // Server closed the connection mid-read
                    if (!response.empty()) return true;
                    lastError_ = "grpcSendReceive: connection reset by peer (GetLastError=" + std::to_string(err) + ")";
                } else if (err == WSAEWOULDBLOCK) {
                    // Server closed the connection cleanly
                    if (!response.empty()) return true;
                    lastError_ = "grpcSendReceive: connection closed by peer";
                } else {
                    lastError_ = "grpcSendReceive: recv failed: WSAGetLastError=" + std::to_string(err);
                }
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
                    int err = WSAGetLastError();
                    if (err == WSAETIMEDOUT || err == WSAECONNRESET ||
                        err == WSAECONNABORTED || err == WSAENETDOWN) {
                         lastError_ = "grpcSendReceive: payload recv timeout (GetLastError=" + std::to_string(err) + ")";
                    } else if (err == WSAEWOULDBLOCK) {
                        if (!response.empty()) return true;
                        lastError_ = "grpcSendReceive: payload recv would block";
                    } else {
                        lastError_ = "grpcSendReceive: payload recv failed: WSAGetLastError=" + std::to_string(err);
                    }
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
                // Decode the HPACK block. Xray serves gRPC via Go's
                // x/net/http2/hpack, which Huffman-encodes most names and
                // values (e.g. "grpc-status", "content-type").
                size_t hstart = 0;
                size_t hend = payload.size();
                if (fflags & 0x08) {  // PADDED
                    if (payload.empty()) {
                        lastError_ = "grpcSendReceive: empty padded HEADERS";
                        return false;
                    }
                    unsigned char padLen =
                        static_cast<unsigned char>(payload[0]);
                    hstart = 1;
                    if (padLen > payload.size() - hstart) {
                        lastError_ = "grpcSendReceive: bad HEADERS padding";
                        return false;
                    }
                    hend = payload.size() - padLen;
                }
                if (fflags & 0x20) {  // PRIORITY (1-bit exclusive + 4 bytes)
                    if (payload.size() < hstart + 5) {
                        lastError_ = "grpcSendReceive: truncated HEADERS priority";
                        return false;
                    }
                    hstart += 5;
                }
                if (hstart > hend) {
                    lastError_ = "grpcSendReceive: malformed HEADERS frame";
                    return false;
                }
                if (fflags & 0x04) {  // END_HEADERS
                    std::string block =
                        payload.substr(hstart, hend - hstart);
                    std::vector<std::pair<std::string, std::string>> hdrs;
                    if (!grpcDecodeHpackHeaders(block, hdrs)) {
                        lastError_ = "grpcSendReceive: invalid HPACK block";
                        return false;
                    }
                    for (const std::pair<std::string, std::string>& h : hdrs) {
                        if (h.first == ":status") {
                            statusCode = h.second;
                        } else if (h.first == "grpc-status") {
                            grpcStatusCode = h.second;
                            sawGrpcStatus = true;
                        } else if (h.first == "grpc-message") {
                            grpcMessage = h.second;
                        }
                    }
                }
                if (fflags & 0x01) done = true;  // END_STREAM (trailers)
            }
        } else if (fsid == 0 && ftype == 0x04) {
            // SETTINGS on connection stream — ACK it
            if (!(fflags & 0x01)) {
                unsigned char ack[9] = {
                    0x00, 0x00, 0x00, 0x04, 0x01,
                    0x00, 0x00, 0x00, 0x00
                };
                if (::send(s, reinterpret_cast<const char*>(ack), 9, 0) != 9) {
                    lastError_ = "grpcSendReceive: send SETTINGS ACK failed";
                    return false;
                }
            }
        } else if (ftype == 0x03) {
            // RST_STREAM — immediate connection error
            lastError_ = "grpcSendReceive: RST_STREAM received";
            return false;
        } else if (ftype == 0x07) {
            // GOAWAY — stop
            break;
        }
    }

    if (!done) {
        lastError_ = "grpcSendReceive: response incomplete after " +
                     std::to_string(frameCount) + " frames";
        return false;
    }

    // Validate gRPC trailers: a non-zero grpc-status means the RPC failed.
    if (sawGrpcStatus && grpcStatusCode != "0") {
        lastError_ = "grpc call failed: status=" + grpcStatusCode +
                     (grpcMessage.empty() ? std::string()
                                          : ", message=" + grpcMessage);
        return false;
    }
    return true;
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
        Logger::write("[XrayApi] removeOutboundDirect FAILED: " + lastError_, LogLevel::DEBUG);
    } else {
        Logger::write("[XrayApi] removeOutboundDirect SUCCESS tag=" + tag, LogLevel::DEBUG);
        lastError_.clear();
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
        typeUrl   = "xray.proxy.outbound.Config";
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
    const std::string encodedValue = jsonConfigToProtobuf(typeUrl, valueJsonStr);
    // B5: server-class protocols (vmess/vless/trojan/shadowsocks/socks/http)
    // must yield a usable outbound — an empty result means the JSON had no
    // valid server (missing/empty servers array, non-object server entry).
    // Fall back to the subprocess path instead of injecting a broken empty
    // config. freedom/blackhole legitimately encode to empty (defaults).
    if (encodedValue.empty() &&
        (typeUrl == "xray.proxy.vmess.outbound.Config" ||
         typeUrl == "xray.proxy.vless.outbound.Config" ||
         typeUrl == "xray.proxy.trojan.ClientConfig" ||
         typeUrl == "xray.proxy.shadowsocks.ClientConfig" ||
         typeUrl == "xray.proxy.socks.ClientConfig" ||
         typeUrl == "xray.proxy.http.ClientConfig")) {
        Logger::write("[XrayApi] addOutboundDirect: empty config for server-class "
                      + typeUrl + ", fallback to subprocess", LogLevel::DEBUG);
        return addOutbound(outboundJson, tag, resultOutput);
    }
    typedMsg += encodeString(2, encodedValue);

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
                          + ex.what(), LogLevel::WARN);
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
                          + ex.what(), LogLevel::WARN);
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
                      LogLevel::DEBUG);
    } else {
        Logger::write("[XrayApi] addOutboundDirect SUCCESS tag=" + tag,
                      LogLevel::DEBUG);
        lastError_.clear();
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
                      LogLevel::DEBUG);
    } else {
        Logger::write("[XrayApi] listOutboundsDirect SUCCESS, response sz=" +
                      std::to_string(output.size()) + " bytes",
                      LogLevel::DEBUG);
        lastError_.clear();
    }
    return ok;
}

#endif // USE_GRPC_API

} // namespace xray
